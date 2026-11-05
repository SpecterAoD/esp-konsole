// Einstellungen der ESP-Konsole im EEPROM-Bereich des ESP (ohne Arduino-Abhängigkeiten, auf dem Laptop testbar).
//
// Ab Version 2 stehen die Zugangsdaten nicht mehr fest in der Firmware, sondern im EEPROM:
//   - Eine lokal mit include/secrets.h gebaute Firmware schreibt die Werte beim ersten Start dorthin
//     (oder immer, wenn mit KONFIG_ERZWINGEN gebaut).
//   - Firmware von GitHub (ohne secrets.h) liest sie nur. Deshalb enthalten Releases keine Passwörter.
// Ab Version 3 (Format 2) kommen Ports, Hostname, Notfall-WLAN und Zeiten dazu; änderbar per Telnet
// (include/config_cmd.h). Ein Format-1-Block wird beim Start übernommen (migrieren_v1_zu_v2).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace konfig {

const uint32_t MAGIE = 0x4E4F4B45;  // "EKON"
const uint16_t FORMAT = 2;

// Format 1 (Firmware v2.x), nur noch zum Übernehmen
struct DatenV1 {
  uint32_t magie;
  uint16_t format;
  uint16_t reserviert;
  char wlan_name[33];
  char wlan_passwort[65];
  char notfall_name[33];
  char notfall_passwort[65];
  char ota_passwort[65];
  uint32_t crc;  // über alle Bytes davor (inkl. 3 Füllbytes)
};
static_assert(sizeof(DatenV1) == 276 && offsetof(DatenV1, crc) == 272, "Layout Format 1");

struct Daten {
  uint32_t magie;
  uint16_t format;
  uint16_t reserviert;
  char wlan_name[33];
  char wlan_passwort[65];
  char notfall_name[33];
  char notfall_passwort[65];
  char ota_passwort[65];
  // ab Format 2
  char hostname[33];
  uint8_t notfall_kanal;
  uint8_t notfall_ip[4];
  uint8_t notfall_maske[4];
  uint8_t reserviert2;
  uint16_t telnet_port;
  uint16_t info_port;
  uint16_t ota_port;
  uint16_t reboot_tage;  // 0 = nie, sonst Neustart nach so vielen Tagen Laufzeit (nicht bei offener Sitzung)
  uint32_t notfall_nach_ms;      // Heim-WLAN so lange weg -> Notfall-WLAN
  uint32_t heim_stabil_ms;       // so lange wieder da -> Notfall-WLAN aus
  uint32_t sitzung_leerlauf_ms;  // Telnet-Sitzung ohne Eingabe trennen
  uint8_t reserviert3[16];
  uint32_t crc;  // über alle Bytes davor
};
static_assert(offsetof(Daten, hostname) == 269 && offsetof(Daten, telnet_port) == 312 &&
                  offsetof(Daten, crc) == 348 && sizeof(Daten) == 352,
              "Layout Format 2");

// Wertebereiche (auch für die Telnet-Kommandos)
const uint32_t NOTFALL_NACH_MIN_S = 60, NOTFALL_NACH_MAX_S = 3600;
const uint32_t HEIM_STABIL_MIN_S = 10, HEIM_STABIL_MAX_S = 600;
const uint32_t LEERLAUF_MIN_MIN = 1, LEERLAUF_MAX_MIN = 120;
const uint32_t REBOOT_MAX_TAGE = 365;

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

// Hostname: 1–32 Zeichen, Buchstaben, Ziffern, '-', nicht am Anfang/Ende
inline bool hostname_ok(const char* s) {
  if (!laenge(s, 1, 32)) return false;
  size_t n = strlen(s);
  if (s[0] == '-' || s[n - 1] == '-') return false;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return false;
  }
  return true;
}

inline uint32_t ip_wert(const uint8_t ip[4]) {
  return (uint32_t(ip[0]) << 24) | (uint32_t(ip[1]) << 16) | (uint32_t(ip[2]) << 8) | ip[3];
}

// Notfall-WLAN: Maske /24 bis /30, Adresse weder Netz- noch Broadcast-Adresse
inline bool notfall_netz_ok(const uint8_t ip[4], const uint8_t maske[4]) {
  uint32_t m = ip_wert(maske), a = ip_wert(ip);
  if (m < 0xFFFFFF00u || m > 0xFFFFFFFCu) return false;
  if (((~m + 1) & ~m) != 0) return false;  // Einsen nicht zusammenhängend
  uint32_t host = a & ~m;
  return a != 0 && host != 0 && host != ~m;
}

// Alle Einstellungen ab Format 2 in ihren Bereichen
inline bool einstellungen_ok(const Daten& d) {
  if (!memchr(d.hostname, 0, sizeof d.hostname) || !hostname_ok(d.hostname)) return false;
  if (d.notfall_kanal < 1 || d.notfall_kanal > 13) return false;
  if (!notfall_netz_ok(d.notfall_ip, d.notfall_maske)) return false;
  if (d.telnet_port == 0 || d.info_port == 0 || d.ota_port == 0 || d.telnet_port == d.info_port ||
      d.telnet_port == d.ota_port || d.info_port == d.ota_port)
    return false;
  if (d.reboot_tage > REBOOT_MAX_TAGE) return false;
  if (d.notfall_nach_ms < NOTFALL_NACH_MIN_S * 1000 || d.notfall_nach_ms > NOTFALL_NACH_MAX_S * 1000) return false;
  if (d.heim_stabil_ms < HEIM_STABIL_MIN_S * 1000 || d.heim_stabil_ms > HEIM_STABIL_MAX_S * 1000) return false;
  if (d.sitzung_leerlauf_ms < LEERLAUF_MIN_MIN * 60000 || d.sitzung_leerlauf_ms > LEERLAUF_MAX_MIN * 60000)
    return false;
  return true;
}

// Alles außer Kopf und Prüfsumme plausibel (Zugangsdaten + Einstellungen)
inline bool inhalt_ok(const Daten& d) {
  // Zeichenketten müssen abgeschlossen sein
  if (!memchr(d.wlan_name, 0, sizeof d.wlan_name) || !memchr(d.wlan_passwort, 0, sizeof d.wlan_passwort) ||
      !memchr(d.notfall_name, 0, sizeof d.notfall_name) || !memchr(d.notfall_passwort, 0, sizeof d.notfall_passwort) ||
      !memchr(d.ota_passwort, 0, sizeof d.ota_passwort))
    return false;
  return werte_ok(d.wlan_name, d.wlan_passwort, d.notfall_name, d.notfall_passwort, d.ota_passwort) &&
         einstellungen_ok(d);
}

inline bool gueltig(const Daten& d) {
  if (d.magie != MAGIE || d.format != FORMAT || d.crc != pruefsumme(d)) return false;
  return inhalt_ok(d);
}

// Bisherige feste Werte (v2) als Standard für die Einstellungen ab Format 2
inline void standard_setzen(Daten& d) {
  memset(d.hostname, 0, sizeof d.hostname);
  strncpy(d.hostname, "esp-konsole", sizeof d.hostname - 1);
  d.notfall_kanal = 6;
  const uint8_t ip[4] = {192, 168, 4, 1}, maske[4] = {255, 255, 255, 0};
  memcpy(d.notfall_ip, ip, 4);
  memcpy(d.notfall_maske, maske, 4);
  d.telnet_port = 23;
  d.info_port = 8023;
  d.ota_port = 8266;
  d.reboot_tage = 0;
  d.notfall_nach_ms = 3UL * 60 * 1000;
  d.heim_stabil_ms = 60UL * 1000;
  d.sitzung_leerlauf_ms = 30UL * 60 * 1000;
}

// Leerer Block mit Kopf und Standard-Einstellungen, ohne Zugangsdaten (noch ohne Prüfsumme)
inline void grundlage(Daten& d) {
  memset(&d, 0, sizeof d);
  d.magie = MAGIE;
  d.format = FORMAT;
  standard_setzen(d);
}

// Setzt nur die Zugangsdaten und die Prüfsumme; false (d unverändert), wenn nicht plausibel
inline bool zugangsdaten_setzen(Daten& d, const char* wlan, const char* wlan_pw, const char* nf, const char* nf_pw,
                                const char* ota) {
  if (!werte_ok(wlan, wlan_pw, nf, nf_pw, ota)) return false;
  memset(d.wlan_name, 0, sizeof d.wlan_name);
  memset(d.wlan_passwort, 0, sizeof d.wlan_passwort);
  memset(d.notfall_name, 0, sizeof d.notfall_name);
  memset(d.notfall_passwort, 0, sizeof d.notfall_passwort);
  memset(d.ota_passwort, 0, sizeof d.ota_passwort);
  strncpy(d.wlan_name, wlan, sizeof d.wlan_name - 1);
  strncpy(d.wlan_passwort, wlan_pw, sizeof d.wlan_passwort - 1);
  strncpy(d.notfall_name, nf, sizeof d.notfall_name - 1);
  strncpy(d.notfall_passwort, nf_pw, sizeof d.notfall_passwort - 1);
  strncpy(d.ota_passwort, ota, sizeof d.ota_passwort - 1);
  d.crc = pruefsumme(d);
  return true;
}

// Füllt d komplett neu (Standard-Einstellungen, inkl. Prüfsumme); false, wenn die Werte nicht plausibel sind
inline bool fuellen(Daten& d, const char* wlan, const char* wlan_pw, const char* nf, const char* nf_pw, const char* ota) {
  if (!werte_ok(wlan, wlan_pw, nf, nf_pw, ota)) return false;
  grundlage(d);
  return zugangsdaten_setzen(d, wlan, wlan_pw, nf, nf_pw, ota);
}

// Übernimmt einen gültigen Format-1-Block (rohe EEPROM-Bytes, mindestens sizeof(DatenV1)).
// Nur wenn Kopf, Prüfsumme und Werte stimmen; die neuen Einstellungen bekommen die bisherigen festen Werte.
inline bool migrieren_v1_zu_v2(const uint8_t* roh, size_t n, Daten& neu) {
  if (n < sizeof(DatenV1)) return false;
  DatenV1 alt;
  memcpy(&alt, roh, sizeof alt);  // kein Zeiger-Cast: Ausrichtung egal
  if (alt.magie != MAGIE || alt.format != 1) return false;
  if (alt.crc != crc32(roh, offsetof(DatenV1, crc))) return false;
  if (!memchr(alt.wlan_name, 0, sizeof alt.wlan_name) || !memchr(alt.wlan_passwort, 0, sizeof alt.wlan_passwort) ||
      !memchr(alt.notfall_name, 0, sizeof alt.notfall_name) ||
      !memchr(alt.notfall_passwort, 0, sizeof alt.notfall_passwort) ||
      !memchr(alt.ota_passwort, 0, sizeof alt.ota_passwort))
    return false;
  Daten d;
  grundlage(d);
  if (!zugangsdaten_setzen(d, alt.wlan_name, alt.wlan_passwort, alt.notfall_name, alt.notfall_passwort,
                           alt.ota_passwort))
    return false;
  neu = d;
  return true;
}

// Braucht der Wechsel von alt nach neu einen Neustart? Nur die Zeiten und reboot_tage wirken sofort.
inline bool neustart_noetig(const Daten& alt, const Daten& neu) {
  Daten a = alt, b = neu;
  a.notfall_nach_ms = b.notfall_nach_ms;
  a.heim_stabil_ms = b.heim_stabil_ms;
  a.sitzung_leerlauf_ms = b.sitzung_leerlauf_ms;
  a.reboot_tage = b.reboot_tage;
  return memcmp(&a, &b, offsetof(Daten, crc)) != 0;
}

}  // namespace konfig
