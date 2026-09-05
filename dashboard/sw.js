// FIX: versiunea veche folosea un cache fara versiune si strategie "cache-first"
// pentru TOT. Odata instalata, aplicatia servea la infinit din cache si
// utilizatorii nu mai primeau niciodata o actualizare.
// Acum: numele cache-ului e versionat, cache-urile vechi se sterg la activate,
// HTML-ul merge network-first (cu fallback pe cache offline), restul cache-first.

const CACHE_VERSION = 'v5';
const CACHE_NAME = 'GLYPH-' + CACHE_VERSION;

const ASSETS = [
  './',
  './glyph.html',
  './manifest.json',
  './icon-192.png',
  './logo.svg',      // logo-ul, scos din pagina intr-un fisier separat
  './icon-512.png',  // FIX: lipsea din lista, iconul mare nu era disponibil offline

  // Biblioteca hartii, de pe CDN. E pusa aici ca sa fie descarcata din start:
  // altfel, prima deschidere a tab-ului Routes fara internet nu ar avea harta
  // deloc. Tile-urile (imaginile hartii) tot au nevoie de conexiune - de aceea
  // tab-ul arata un avertisment cand esti offline.
  'https://unpkg.com/leaflet@1.9.4/dist/leaflet.js',
  'https://unpkg.com/leaflet@1.9.4/dist/leaflet.css'
];

self.addEventListener('install', (e) => {
  e.waitUntil(
    caches.open(CACHE_NAME)
      // addAll pica in intregime daca un singur fisier lipseste -> punem fiecare separat
      .then((cache) => Promise.all(
        ASSETS.map((url) => cache.add(url).catch(() => null))
      ))
  );
});

self.addEventListener('activate', (e) => {
  e.waitUntil(
    caches.keys()
      .then((keys) => Promise.all(
        // TILE_CACHE are versiunea lui si supravietuieste actualizarilor
        // aplicatiei: altfel fiecare update ar arunca harta descarcata.
        keys.filter((k) => k !== CACHE_NAME && k !== TILE_CACHE).map((k) => caches.delete(k))
      ))
      .then(() => self.clients.claim())
  );
});

// Permite paginii sa forteze activarea imediata a unei versiuni noi.
self.addEventListener('message', (e) => {
  if (e.data && e.data.type === 'SKIP_WAITING') self.skipWaiting();
});

// Cache separat pentru imaginile hartii, cu plafon: tile-urile pe care le-ai
// vazut deja se afiseaza si fara semnal. Sunt tinute deoparte de fisierele
// aplicatiei fiindca se strang cu miile si trebuie sa poata fi taiate singure
// fara sa atinga aplicatia.
const TILE_CACHE = 'GLYPH-tiles-v1';
const TILE_CACHE_MAX = 1200;   // ~40-60 MB, cat o zona de oras la zoom mare

function isTileRequest(url) {
  return /(^|\.)(mt\d\.google\.com|tile\.openstreetmap\.org)$/.test(url.hostname);
}

// Taierea se face rar si la coada: primele intrate, primele iesite.
async function trimTileCache() {
  const cache = await caches.open(TILE_CACHE);
  const keys = await cache.keys();
  if (keys.length <= TILE_CACHE_MAX) return;
  const excess = keys.length - TILE_CACHE_MAX;
  for (let i = 0; i < excess; i++) await cache.delete(keys[i]);
}

self.addEventListener('fetch', (e) => {
  const req = e.request;

  if (req.method !== 'GET') return;

  // --- imaginile hartii: din cache daca exista, altfel de pe retea si retinute ---
  let reqUrl = null;
  try { reqUrl = new URL(req.url); } catch (err) { reqUrl = null; }

  if (reqUrl && isTileRequest(reqUrl)) {
    e.respondWith(
      caches.open(TILE_CACHE).then((cache) =>
        cache.match(req).then((hit) => {
          if (hit) return hit;
          return fetch(req).then((res) => {
            // Raspunsul e opac (alt domeniu, fara CORS): nu-l putem citi, dar
            // il putem pastra si reda mai tarziu, ceea ce e tot ce ne trebuie.
            cache.put(req, res.clone()).then(trimTileCache).catch(() => {});
            return res;
          }).catch(() => hit || Response.error());
        })
      )
    );
    return;
  }

  const isDocument =
    req.mode === 'navigate' ||
    (req.headers.get('accept') || '').includes('text/html');

  if (isDocument) {
    // Network-first: userul primeste mereu ultima versiune cand are internet.
    e.respondWith(
      fetch(req)
        .then((res) => {
          const copy = res.clone();
          caches.open(CACHE_NAME).then((c) => c.put(req, copy)).catch(() => {});
          return res;
        })
        .catch(() => caches.match(req).then((r) => r || caches.match('./glyph.html')))
    );
    return;
  }

  // Restul (iconuri, manifest): cache-first, cu completare in fundal.
  e.respondWith(
    caches.match(req).then((cached) => {
      const network = fetch(req)
        .then((res) => {
          const copy = res.clone();
          caches.open(CACHE_NAME).then((c) => c.put(req, copy)).catch(() => {});
          return res;
        })
        .catch(() => cached);
      return cached || network;
    })
  );
});

self.addEventListener('notificationclick', function (event) {
  event.notification.close();
  event.waitUntil(
    clients.matchAll({ type: 'window', includeUncontrolled: true }).then((windowClients) => {
      for (const client of windowClients) {
        if (client.url && 'focus' in client) return client.focus();
      }
      // FIX: deschidea '/', care nu e neaparat aplicatia (ex. GitHub Pages
      // serveste proiectul dintr-un subfolder). Acum deschide pagina corecta.
      if (clients.openWindow) return clients.openWindow('./glyph.html');
    })
  );
});
