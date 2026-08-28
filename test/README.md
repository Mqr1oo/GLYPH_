# Teste native GLYPH

Rulează logica firmware-ului pe PC, fără aparat, fără card SD, fără radio.

```bash
./run.sh
```

Cerințe: `g++` și mbedtls.

- Ubuntu/Debian: `sudo apt install build-essential libmbedtls-dev`
- macOS: `brew install mbedtls`

## Ce se testează

Fișierele din `Arduino/` cu logică pură sunt incluse **direct ca sursă** —
nu sunt copii și nu sunt reimplementări. Dacă strici codul de pe aparat,
testele pică.

| Zonă | Ce verifică |
|---|---|
| Conversia UTC → oră locală | 7128 combinații de dată, oră și fus, contra bibliotecii standard: sfârșit de lună, sfârșit de an, ani bisecți, offset negativ |
| Distanța geografică | puncte cunoscute, simetrie, un grad de latitudine, traversarea antimeridianului |
| Derivarea cheii | determinism, sensibilitate la un singur caracter, numele gol → ALPHA |
| AES-256-GCM | round-trip, nonce diferit la fiecare criptare, respingerea unui singur bit modificat, tag trunchiat, cheie greșită, intrări malformate |
| Formatul pachetului | antet, tip, contor, public vs securizat, pachete stricate, compatibilitate cu formatul vechi |
| Extragerea coordonatelor | valori negative, în afara intervalului, mesaje care conțin `\|`, formate malformate |
| Anti-reluare | contor crescător acceptat, egal sau mai mic respins, repornirea expeditorului nu e tratată ca atac |
| Parserul KML | segmente multiple, text în afara blocului `<coordinates>`, fișier gol, fișier trunchiat, punctul 0,0 |

## De ce sunt de încredere

Testele au fost validate prin mutații: cinci defecte introduse intenționat în
codul real (rollover de lună greșit, anti-reluare permisivă, validare de
coordonate scoasă, verificarea tag-ului GCM ignorată, primul `|` în loc de
ultimul) — **fiecare a fost prins**. O suită care trece indiferent ce strici
nu valorează nimic.

## Ce NU se testează

Tot ce atinge hardware: display, radio, card SD, BLE, task-uri FreeRTOS.
Alea au nevoie de aparat. Testele de aici acoperă logica din spatele lor.
