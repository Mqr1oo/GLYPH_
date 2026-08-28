// FIX: versiunea veche folosea un cache fara versiune si strategie "cache-first"
// pentru TOT. Odata instalata, aplicatia servea la infinit din cache si
// utilizatorii nu mai primeau niciodata o actualizare.
// Acum: numele cache-ului e versionat, cache-urile vechi se sterg la activate,
// HTML-ul merge network-first (cu fallback pe cache offline), restul cache-first.

const CACHE_VERSION = 'v2';
const CACHE_NAME = 'GLYPH-' + CACHE_VERSION;

const ASSETS = [
  './',
  './glyph.html',
  './manifest.json',
  './icon-192.png',
  './icon-512.png'   // FIX: lipsea din lista, iconul mare nu era disponibil offline
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
        keys.filter((k) => k !== CACHE_NAME).map((k) => caches.delete(k))
      ))
      .then(() => self.clients.claim())
  );
});

// Permite paginii sa forteze activarea imediata a unei versiuni noi.
self.addEventListener('message', (e) => {
  if (e.data && e.data.type === 'SKIP_WAITING') self.skipWaiting();
});

self.addEventListener('fetch', (e) => {
  const req = e.request;

  if (req.method !== 'GET') return;

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
