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
const uint16_t FORMAT = 2;

struct Daten {
  uint32_t magie;
  uint16_t format;
  uint16_t reserviert;
  char wlan_name[33];
  char wlan_passwort[65];
  char notfall_name[33];
  char notfall_passwort[65];
  char ota_passwort[65];
  // Neue Felder (v2)
  char dns_server[40];          // z.B. "8.8.8.8"
  char ntp_server[65];          // z.B. "pool.ntp.org"
  uint16_t telnet_port;
  uint16_t info_port;
  uint16_t ota_port;
  char hostname[33];            // z.B. "esp-konsole"
  uint8_t notfall_kanal;
  uint32_t notfall_nach_ms;     // Verzögerung bis Notfall-WLAN
  uint32_t heim_stabil_ms;      // Stabilitäts-Schwelle
  uint32_t sitzung_leerlauf_ms; // Session-Timeout
  uint16_t reboot_tage;         // 0 = nie, sonst Tage
  uint8_t debug;
  uint8_t reserviert2[11];
  // AP-Einstellungen im Notfallmodus
  uint8_t notfall_ip[4];        // z.B. 192.168.4.1
  uint8_t notfall_maske[4];     // z.B. 255.255.255.0
  uint8_t reserviert3[8];
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

// Standard-Werte für neue Felder
inline void defaults_setzen(Daten& d) {
  strncpy(d.dns_server, "8.8.8.8", sizeof d.dns_server - 1);
  strncpy(d.ntp_server, "pool.ntp.org", sizeof d.ntp_server - 1);
  d.telnet_port = 23;
  d.info_port = 8023;
  d.ota_port = 8266;
  strncpy(d.hostname, "esp-konsole", sizeof d.hostname - 1);
  d.notfall_kanal = 6;
  d.notfall_nach_ms = 3UL * 60 * 1000;
  d.heim_stabil_ms = 60UL * 1000;
  d.sitzung_leerlauf_ms = 30UL * 60 * 1000;
  d.reboot_tage = 0;
  d.debug = 0;
  d.notfall_ip[0] = 192; d.notfall_ip[1] = 168; d.notfall_ip[2] = 4; d.notfall_ip[3] = 1;
  d.notfall_maske[0] = 255; d.notfall_maske[1] = 255; d.notfall_maske[2] = 255; d.notfall_maske[3] = 0;
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
      !memchr(d.ota_passwort, 0, sizeof d.ota_passwort) || !memchr(d.dns_server, 0, sizeof d.dns_server) ||
      !memchr(d.ntp_server, 0, sizeof d.ntp_server) || !memchr(d.hostname, 0, sizeof d.hostname))
    return false;
  return werte_ok(d.wlan_name, d.wlan_passwort, d.notfall_name, d.notfall_passwort, d.ota_passwort);
}

// Migriert alte Struktur (v1) zur neuen (v2)
inline bool migrieren_v1_zu_v2(const uint8_t* alte_daten, Daten& neu) {
  // Alte Struktur: magie(4), format(2), reserviert(2), wlan_name(33), wlan_pw(65), 
  //               notfall_name(33), notfall_pw(65), ota_pw(65), crc(4)
  const size_t ALTE_GROESSE = 4 + 2 + 2 + 33 + 65 + 33 + 65 + 65 + 4;  // 273
  
  if (*(const uint32_t*)(alte_daten) != MAGIE || *(const uint16_t*)(alte_daten + 4) != 1) return false;
  
  memset(&neu, 0, sizeof neu);
  neu.magie = MAGIE;
  neu.format = FORMAT;
  
  memcpy(neu.wlan_name, alte_daten + 8, 33);
  memcpy(neu.wlan_passwort, alte_daten + 41, 65);
  memcpy(neu.notfall_name, alte_daten + 106, 33);
  memcpy(neu.notfall_passwort, alte_daten + 139, 65);
  memcpy(neu.ota_passwort, alte_daten + 204, 65);
  
  defaults_setzen(neu);
  neu.crc = pruefsumme(neu);
  return true;
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
  defaults_setzen(d);
  d.crc = pruefsumme(d);
  return true;
}

}  // namespace konfig
