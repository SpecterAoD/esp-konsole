// Einstellungen der ESP-Konsole im EEPROM-Bereich des ESP (ohne Arduino-Abhängigkeiten, auf dem Laptop testbar).
//
// Ab Version 2 stehen die Zugangsdaten nicht mehr fest in der Firmware, sondern im EEPROM:
//   - Eine lokal mit include/secrets.h gebaute Firmware schreibt die Werte beim ersten Start dorthin
//     (oder immer, wenn mit KONFIG_ERZWINGEN gebaut).
//   - Firmware von GitHub (ohne secrets.h) liest sie nur. Deshalb enthalten Releases keine Passwörter.
// Ab Version 3 (Format 2) kommen Ports, Hostname, Notfall-WLAN und Zeiten dazu; änderbar per Telnet
// (include/config_cmd.h).
//
// Ablage im EEPROM (abwärtskompatibel, eine v2-Firmware läuft jederzeit weiter):
//   ADR_V1 (0):   Format 1 wie bei v2 – nur Zugangsdaten. v3 hält ihn immer auf demselben Stand.
//   ADR_V2 (512): Format 2 – alles.
// Laden (laden): Format 2 gültig -> verwenden; hat eine v2-Firmware inzwischen den Format-1-Block mit anderen
// Zugangsdaten geschrieben, gelten diese. Nur Format 1 gültig -> übernehmen, Einstellungen = Standard.
// Schreiben (schreiben): immer beide Blöcke. Achtung: EEPROM.commit() löscht den ganzen Sektor und schreibt
// nur die bei EEPROM.begin() angegebene Größe; eine v2-Firmware (276 Bytes) löscht beim Schreiben also den
// Format-2-Block – dann greift die Übernahme aus Format 1.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace konfig {

const uint32_t MAGIE = 0x4E4F4B45;  // "EKON"
const uint16_t FORMAT = 2;
const size_t ADR_V1 = 0, ADR_V2 = 512, EEPROM_GROESSE = 1024;

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
static_assert(ADR_V1 + sizeof(DatenV1) <= ADR_V2 && ADR_V2 + sizeof(Daten) <= EEPROM_GROESSE, "Ablage");

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

// Format-1-Block prüfen (Kopf, Prüfsumme inkl. Füllbytes, abgeschlossene und plausible Werte)
inline bool v1_gueltig(const DatenV1& a) {
  if (a.magie != MAGIE || a.format != 1) return false;
  if (a.crc != crc32(reinterpret_cast<const uint8_t*>(&a), offsetof(DatenV1, crc))) return false;
  if (!memchr(a.wlan_name, 0, sizeof a.wlan_name) || !memchr(a.wlan_passwort, 0, sizeof a.wlan_passwort) ||
      !memchr(a.notfall_name, 0, sizeof a.notfall_name) || !memchr(a.notfall_passwort, 0, sizeof a.notfall_passwort) ||
      !memchr(a.ota_passwort, 0, sizeof a.ota_passwort))
    return false;
  return werte_ok(a.wlan_name, a.wlan_passwort, a.notfall_name, a.notfall_passwort, a.ota_passwort);
}

// Format-1-Block mit den Zugangsdaten aus d (so, wie ihn eine v2-Firmware selbst schreiben würde)
inline void v1_aus(const Daten& d, DatenV1& a) {
  memset(&a, 0, sizeof a);
  a.magie = MAGIE;
  a.format = 1;
  memcpy(a.wlan_name, d.wlan_name, sizeof a.wlan_name);
  memcpy(a.wlan_passwort, d.wlan_passwort, sizeof a.wlan_passwort);
  memcpy(a.notfall_name, d.notfall_name, sizeof a.notfall_name);
  memcpy(a.notfall_passwort, d.notfall_passwort, sizeof a.notfall_passwort);
  memcpy(a.ota_passwort, d.ota_passwort, sizeof a.ota_passwort);
  a.crc = crc32(reinterpret_cast<const uint8_t*>(&a), offsetof(DatenV1, crc));
}

inline bool gleiche_zugangsdaten(const Daten& d, const DatenV1& a) {
  return strcmp(d.wlan_name, a.wlan_name) == 0 && strcmp(d.wlan_passwort, a.wlan_passwort) == 0 &&
         strcmp(d.notfall_name, a.notfall_name) == 0 && strcmp(d.notfall_passwort, a.notfall_passwort) == 0 &&
         strcmp(d.ota_passwort, a.ota_passwort) == 0;
}

enum class Quelle {
  KEINE,             // nichts Gültiges
  FORMAT2,           // Format-2-Block
  FORMAT2_ZUGANG_V1, // Format 2, Zugangsdaten aber aus dem neueren Format-1-Block (von einer v2-Firmware)
  FORMAT1,           // nur Format 1: übernommen, Einstellungen = Standard
};

// Liest die rohen EEPROM-Bytes (mindestens EEPROM_GROESSE). d ist nur bei Quelle != KEINE gültig.
// Danach sollte der Aufrufer schreiben(d, …) ausführen und bei Unterschied ins EEPROM zurückschreiben.
inline Quelle laden(const uint8_t* roh, size_t n, Daten& d) {
  if (n < EEPROM_GROESSE) return Quelle::KEINE;
  DatenV1 a;
  memcpy(&a, roh + ADR_V1, sizeof a);  // memcpy statt Zeiger-Cast: Ausrichtung egal
  bool v1 = v1_gueltig(a);
  Daten b;
  memcpy(&b, roh + ADR_V2, sizeof b);
  if (gueltig(b)) {
    d = b;
    if (v1 && !gleiche_zugangsdaten(b, a)) {
      zugangsdaten_setzen(d, a.wlan_name, a.wlan_passwort, a.notfall_name, a.notfall_passwort, a.ota_passwort);
      return Quelle::FORMAT2_ZUGANG_V1;
    }
    return Quelle::FORMAT2;
  }
  if (v1) {
    grundlage(d);
    zugangsdaten_setzen(d, a.wlan_name, a.wlan_passwort, a.notfall_name, a.notfall_passwort, a.ota_passwort);
    return Quelle::FORMAT1;
  }
  return Quelle::KEINE;
}

// Schreibt d (gültig, mit Prüfsumme) als Format 2 und die Zugangsdaten als Format 1 in die rohen EEPROM-Bytes
// (mindestens EEPROM_GROESSE). Bytes zwischen den Blöcken bleiben unverändert.
inline void schreiben(const Daten& d, uint8_t* roh) {
  DatenV1 a;
  v1_aus(d, a);
  memcpy(roh + ADR_V1, &a, sizeof a);
  memcpy(roh + ADR_V2, &d, sizeof d);
}

// Stehen beide Blöcke schon genau so in den rohen EEPROM-Bytes, wie schreiben(d, …) sie ablegen würde?
inline bool abgelegt(const Daten& d, const uint8_t* roh) {
  DatenV1 a;
  v1_aus(d, a);
  return memcmp(roh + ADR_V1, &a, sizeof a) == 0 && memcmp(roh + ADR_V2, &d, sizeof d) == 0;
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
