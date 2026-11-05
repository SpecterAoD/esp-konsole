// Einstellungen der ESP-Konsole im EEPROM-Bereich des ESP (ohne Arduino-Abhängigkeiten, auf dem Laptop testbar).
//
// Ab Version 2 stehen die Zugangsdaten nicht mehr fest in der Firmware, sondern im EEPROM:
//   - Eine lokal mit include/secrets.h gebaute Firmware schreibt die Werte beim ersten Start dorthin
//     (oder immer, wenn mit KONFIG_ERZWINGEN gebaut).
//   - Firmware von GitHub (ohne secrets.h) liest sie nur. Deshalb enthalten Releases keine Passwörter.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace konfig {

const uint32_t MAGIE = 0x4E4F4B45;  // "EKON"
const uint16_t FORMAT = 1;

struct Daten {
  uint32_t magie;
  uint16_t format;
  uint16_t reserviert;
  char wlan_name[33];
  char wlan_passwort[65];
  char notfall_name[33];
  char notfall_passwort[65];
  char ota_passwort[65];
  uint32_t crc;  // über alle Bytes davor
};

inline uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

inline uint32_t pruefsumme(const Daten& d) {
  return crc32(reinterpret_cast<const uint8_t*>(&d), offsetof(Daten, crc));
}

// Platzhalter aus secrets.h.example ("<…>") oder leer
inline bool platzhalter(const char* s) {
  size_t n = s ? strlen(s) : 0;
  return n == 0 || (s[0] == '<' && s[n - 1] == '>');
}

inline bool laenge(const char* s, size_t min, size_t max) {
  size_t n = strnlen(s, max + 1);
  return n >= min && n <= max;
}

// Plausibel: Namen 1–32 Zeichen, WPA2-Passwörter 8–63, OTA-Passwort 8–64, keine Platzhalter
inline bool werte_ok(const char* wlan, const char* wlan_pw, const char* nf, const char* nf_pw, const char* ota) {
  const char* alle[] = {wlan, wlan_pw, nf, nf_pw, ota};
  for (const char* s : alle)
    if (!s || platzhalter(s)) return false;
  return laenge(wlan, 1, 32) && laenge(wlan_pw, 8, 63) && laenge(nf, 1, 32) && laenge(nf_pw, 8, 63) &&
         laenge(ota, 8, 64);
}

inline bool gueltig(const Daten& d) {
  if (d.magie != MAGIE || d.format != FORMAT || d.crc != pruefsumme(d)) return false;
  // Zeichenketten müssen abgeschlossen sein
  if (!memchr(d.wlan_name, 0, sizeof d.wlan_name) || !memchr(d.wlan_passwort, 0, sizeof d.wlan_passwort) ||
      !memchr(d.notfall_name, 0, sizeof d.notfall_name) || !memchr(d.notfall_passwort, 0, sizeof d.notfall_passwort) ||
      !memchr(d.ota_passwort, 0, sizeof d.ota_passwort))
    return false;
  return werte_ok(d.wlan_name, d.wlan_passwort, d.notfall_name, d.notfall_passwort, d.ota_passwort);
}

// Füllt d (inkl. Prüfsumme); false, wenn die Werte nicht plausibel sind
inline bool fuellen(Daten& d, const char* wlan, const char* wlan_pw, const char* nf, const char* nf_pw, const char* ota) {
  if (!werte_ok(wlan, wlan_pw, nf, nf_pw, ota)) return false;
  memset(&d, 0, sizeof d);
  d.magie = MAGIE;
  d.format = FORMAT;
  strncpy(d.wlan_name, wlan, sizeof d.wlan_name - 1);
  strncpy(d.wlan_passwort, wlan_pw, sizeof d.wlan_passwort - 1);
  strncpy(d.notfall_name, nf, sizeof d.notfall_name - 1);
  strncpy(d.notfall_passwort, nf_pw, sizeof d.notfall_passwort - 1);
  strncpy(d.ota_passwort, ota, sizeof d.ota_passwort - 1);
  d.crc = pruefsumme(d);
  return true;
}

}  // namespace konfig
