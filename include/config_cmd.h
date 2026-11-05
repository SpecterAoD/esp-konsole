// Einstellungen per Telnet ändern (ab v3), ohne Arduino-Abhängigkeiten, auf dem Laptop testbar.
//
// Weiche: Eine Zeile, die mit "~config" beginnt, geht NICHT an den Pi, sondern an den ESP (wie die
// "~."-Befehle von ssh). Am Zeilenanfang hält die Weiche passende Zeichen zurück, bis klar ist, ob es ein
// Kommando wird; sonst gehen sie unverändert an den Pi. Das funktioniert unabhängig davon, wie die Zeichen
// auf TCP-Pakete verteilt sind (zeichenweise oder ganze Zeilen). Zeilenanfang = Sitzungsbeginn oder nach
// CR, LF, Strg-C, Strg-D, Strg-U.
//
// Kommandos (siehe README):  ~config show | set <name> <wert> | commit | discard | help
//   set ändert nur einen Entwurf. commit prüft ihn, schreibt ihn mit neuer Prüfsumme ins EEPROM und
//   startet neu, wenn eine geänderte Einstellung das braucht. Ohne commit ändert sich nichts.
#pragma once
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "konfig.h"

namespace config_cmd {

const char PRAEFIX[] = "~config";
const size_t PRAEFIX_LAENGE = sizeof(PRAEFIX) - 1;
const size_t MAX_ZEILE = 160;  // längere Kommandozeilen werden verworfen

class Weiche {
 public:
  // Puffergrößen für verarbeiten(): pi mindestens n + PRAEFIX_LAENGE, echo mindestens 3 * n + 32
  enum class Ereignis { KEINS, KOMMANDO };

  void zuruecksetzen() {
    zustand_ = Zustand::ZEILENANFANG;
    n_ = 0;
    zeile_[0] = 0;
  }

  // Verarbeitet bis zu n Bytes (schon durch telnet::Filter). Bytes für den Pi -> pi/n_pi, Antwort an den
  // Client (Echo im Kommandomodus) -> echo/n_echo. Bei einer fertigen Kommandozeile hört es dort auf
  // (Ereignis KOMMANDO, zeile() liefert den Text nach "~config"); Rückgabe = Anzahl verbrauchter Bytes.
  size_t verarbeiten(const uint8_t* ein, size_t n, uint8_t* pi, size_t& n_pi, uint8_t* echo, size_t& n_echo,
                     Ereignis& ereignis) {
    n_pi = n_echo = 0;
    ereignis = Ereignis::KEINS;
    for (size_t i = 0; i < n; i++) {
      uint8_t c = ein[i];
      switch (zustand_) {
        case Zustand::DURCH:
          pi[n_pi++] = c;
          if (zeilenende(c)) zustand_ = Zustand::ZEILENANFANG;
          break;

        case Zustand::ZEILENANFANG:
        case Zustand::PRAEFIX:
          if (n_ < PRAEFIX_LAENGE && c == uint8_t(PRAEFIX[n_])) {
            zeile_[n_++] = char(c);  // zurückhalten
            zustand_ = Zustand::PRAEFIX;
          } else if (n_ == PRAEFIX_LAENGE && (c == ' ' || c == '\r' || c == '\n')) {
            // "~config" erkannt: ab jetzt Kommandomodus, Echo macht der ESP
            for (size_t k = 0; k < n_; k++) echo[n_echo++] = uint8_t(zeile_[k]);
            if (c == ' ') echo[n_echo++] = c;
            n_ = 0;
            zustand_ = Zustand::KOMMANDO;
            if (c != ' ') {
              zeile_[0] = 0;
              ereignis = Ereignis::KOMMANDO;
              zustand_ = Zustand::ZEILENANFANG;
              return i + 1;
            }
          } else {
            // doch kein Kommando: Zurückgehaltenes und dieses Zeichen an den Pi
            for (size_t k = 0; k < n_; k++) pi[n_pi++] = uint8_t(zeile_[k]);
            n_ = 0;
            pi[n_pi++] = c;
            zustand_ = zeilenende(c) ? Zustand::ZEILENANFANG : Zustand::DURCH;
          }
          break;

        case Zustand::KOMMANDO:
          if (c == '\r' || c == '\n') {
            zeile_[n_] = 0;
            n_ = 0;
            zustand_ = Zustand::ZEILENANFANG;
            ereignis = Ereignis::KOMMANDO;
            return i + 1;
          } else if (c == 0x03 || c == 0x15) {  // Strg-C / Strg-U: Kommando abbrechen
            const char* t = "^C\r\n";
            for (const char* p = t; *p; p++) echo[n_echo++] = uint8_t(*p);
            n_ = 0;
            zustand_ = Zustand::ZEILENANFANG;
          } else if (c == 0x7F || c == 0x08) {  // Rücktaste
            if (n_ > 0) {
              n_--;
              echo[n_echo++] = '\b';
              echo[n_echo++] = ' ';
              echo[n_echo++] = '\b';
            }
          } else if (c >= 0x20 && c < 0x7F) {
            if (n_ < MAX_ZEILE) {
              zeile_[n_++] = char(c);
              echo[n_echo++] = c;
            } else {
              zustand_ = Zustand::ZU_LANG;
            }
          }
          // andere Steuerzeichen (z. B. Pfeiltasten) im Kommandomodus ignorieren
          break;

        case Zustand::ZU_LANG:  // Rest der Zeile verwerfen, dann Meldung
          if (c == '\r' || c == '\n' || c == 0x03 || c == 0x15) {
            const char* t = "\r\nKommando zu lang, verworfen\r\n";
            for (const char* p = t; *p; p++) echo[n_echo++] = uint8_t(*p);
            n_ = 0;
            zustand_ = Zustand::ZEILENANFANG;
          }
          break;
      }
    }
    return n;
  }

  const char* zeile() const { return zeile_; }

 private:
  enum class Zustand { ZEILENANFANG, PRAEFIX, DURCH, KOMMANDO, ZU_LANG };
  static bool zeilenende(uint8_t c) { return c == '\r' || c == '\n' || c == 0x03 || c == 0x04 || c == 0x15; }

  Zustand zustand_ = Zustand::ZEILENANFANG;
  char zeile_[MAX_ZEILE + 1] = {0};
  size_t n_ = 0;
};

// ---- Kommandos ----

enum class Ergebnis {
  ANZEIGE,     // nur Ausgabe
  GEAENDERT,   // Entwurf geändert
  FEHLER,      // Ausgabe enthält die Fehlermeldung
  COMMIT,      // Aufrufer: Entwurf prüfen, ins EEPROM schreiben, ggf. neu starten (siehe commit_pruefen)
  VERWORFEN,   // Aufrufer: Entwurf = aktive Einstellungen
};

class Ausgabe {
 public:
  Ausgabe(char* p, size_t n) : p_(p), n_(n) {
    if (n_) p_[0] = 0;
  }
  void operator()(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    if (k_ + 1 >= n_) return;
    va_list a;
    va_start(a, fmt);
    int r = vsnprintf(p_ + k_, n_ - k_, fmt, a);
    va_end(a);
    if (r > 0) k_ = (k_ + size_t(r) < n_) ? k_ + size_t(r) : n_ - 1;
  }

 private:
  char* p_;
  size_t n_;
  size_t k_ = 0;
};

// Leerzeichen am Anfang/Ende weg, umschließende "…" oder '…' weg (für Werte mit Leerzeichen)
inline void wert_bereinigen(const char* von, char* nach, size_t n) {
  while (*von == ' ' || *von == '\t') von++;
  size_t l = strlen(von);
  while (l && (von[l - 1] == ' ' || von[l - 1] == '\t')) l--;
  if (l >= 2 && (von[0] == '"' || von[0] == '\'') && von[l - 1] == von[0]) {
    von++;
    l -= 2;
  }
  if (l >= n) l = n - 1;
  memcpy(nach, von, l);
  nach[l] = 0;
}

// Dezimalzahl ohne Vorzeichen, ganz und im Bereich
inline bool zahl_lesen(const char* s, uint32_t min, uint32_t max, uint32_t& wert) {
  if (*s < '0' || *s > '9') return false;
  char* ende;
  unsigned long v = strtoul(s, &ende, 10);
  if (*ende != 0 || v < min || v > max) return false;
  wert = uint32_t(v);
  return true;
}

// IPv4 a.b.c.d, streng (keine Zusätze)
inline bool ip_lesen(const char* s, uint8_t ip[4]) {
  for (int i = 0; i < 4; i++) {
    if (*s < '0' || *s > '9') return false;
    unsigned v = 0;
    int stellen = 0;
    while (*s >= '0' && *s <= '9') {
      v = v * 10 + unsigned(*s++ - '0');
      if (++stellen > 3 || v > 255) return false;
    }
    ip[i] = uint8_t(v);
    if (i < 3 && *s++ != '.') return false;
  }
  return *s == 0;
}

inline bool text_setzen(char* ziel, size_t groesse, const char* wert) {
  memset(ziel, 0, groesse);
  strncpy(ziel, wert, groesse - 1);
  return true;
}

inline void hilfe(Ausgabe& aus) {
  aus("~config show                  Entwurf anzeigen (* = noch nicht gespeichert)\r\n"
      "~config set <name> <wert>     im Entwurf ändern; Werte mit Leerzeichen in \"...\"\r\n"
      "~config commit                prüfen, speichern, bei Bedarf neu starten\r\n"
      "~config discard               Entwurf verwerfen\r\n"
      "Namen: wlan_name wlan_pass notfall_name notfall_pass ota_pass hostname\r\n"
      "       telnet_port info_port ota_port notfall_kanal notfall_ip notfall_maske\r\n"
      "       notfall_nach_s heim_stabil_s leerlauf_min reboot_tage");
}

inline void zeigen(const konfig::Daten& e, const konfig::Daten& a, Ausgabe& aus) {
  auto m = [](bool geaendert) { return geaendert ? '*' : ' '; };
  auto ip = [](const uint8_t* p, char* t) { snprintf(t, 16, "%u.%u.%u.%u", p[0], p[1], p[2], p[3]); };
  char t1[16], t2[16];
  aus("%c wlan_name      %s\r\n", m(strcmp(e.wlan_name, a.wlan_name)), e.wlan_name);
  aus("%c wlan_pass      (%u Zeichen)\r\n", m(strcmp(e.wlan_passwort, a.wlan_passwort)),
      unsigned(strlen(e.wlan_passwort)));
  aus("%c notfall_name   %s\r\n", m(strcmp(e.notfall_name, a.notfall_name)), e.notfall_name);
  aus("%c notfall_pass   (%u Zeichen)\r\n", m(strcmp(e.notfall_passwort, a.notfall_passwort)),
      unsigned(strlen(e.notfall_passwort)));
  aus("%c ota_pass       (%u Zeichen)\r\n", m(strcmp(e.ota_passwort, a.ota_passwort)),
      unsigned(strlen(e.ota_passwort)));
  aus("%c hostname       %s\r\n", m(strcmp(e.hostname, a.hostname)), e.hostname);
  aus("%c telnet_port    %u\r\n", m(e.telnet_port != a.telnet_port), e.telnet_port);
  aus("%c info_port      %u\r\n", m(e.info_port != a.info_port), e.info_port);
  aus("%c ota_port       %u\r\n", m(e.ota_port != a.ota_port), e.ota_port);
  aus("%c notfall_kanal  %u\r\n", m(e.notfall_kanal != a.notfall_kanal), e.notfall_kanal);
  ip(e.notfall_ip, t1);
  ip(e.notfall_maske, t2);
  aus("%c notfall_ip     %s\r\n", m(memcmp(e.notfall_ip, a.notfall_ip, 4)), t1);
  aus("%c notfall_maske  %s\r\n", m(memcmp(e.notfall_maske, a.notfall_maske, 4)), t2);
  aus("%c notfall_nach_s %lu\r\n", m(e.notfall_nach_ms != a.notfall_nach_ms), (unsigned long)(e.notfall_nach_ms / 1000));
  aus("%c heim_stabil_s  %lu\r\n", m(e.heim_stabil_ms != a.heim_stabil_ms), (unsigned long)(e.heim_stabil_ms / 1000));
  aus("%c leerlauf_min   %lu\r\n", m(e.sitzung_leerlauf_ms != a.sitzung_leerlauf_ms),
      (unsigned long)(e.sitzung_leerlauf_ms / 60000));
  aus("%c reboot_tage    %u%s", m(e.reboot_tage != a.reboot_tage), e.reboot_tage, e.reboot_tage ? "" : " (nie)");
}

// Ein Wert in den Entwurf; false + Meldung bei ungültigem Namen oder Wert
inline bool setzen(konfig::Daten& e, const char* name, const char* wert, Ausgabe& aus) {
  using namespace konfig;
  size_t l = strlen(wert);
  uint32_t z;
  struct Text {
    const char* name;
    char* ziel;
    size_t groesse, min, max;
  } texte[] = {
      {"wlan_name", e.wlan_name, sizeof e.wlan_name, 1, 32},
      {"wlan_pass", e.wlan_passwort, sizeof e.wlan_passwort, 8, 63},
      {"notfall_name", e.notfall_name, sizeof e.notfall_name, 1, 32},
      {"notfall_pass", e.notfall_passwort, sizeof e.notfall_passwort, 8, 63},
      {"ota_pass", e.ota_passwort, sizeof e.ota_passwort, 8, 64},
  };
  for (const Text& t : texte) {
    if (strcmp(name, t.name) != 0) continue;
    if (l < t.min || l > t.max || platzhalter(wert)) {
      aus("Fehler: %s braucht %u-%u Zeichen", name, unsigned(t.min), unsigned(t.max));
      return false;
    }
    return text_setzen(t.ziel, t.groesse, wert);
  }
  if (strcmp(name, "hostname") == 0) {
    if (!hostname_ok(wert)) {
      aus("Fehler: hostname 1-32 Zeichen, nur a-z A-Z 0-9 und '-'");
      return false;
    }
    return text_setzen(e.hostname, sizeof e.hostname, wert);
  }
  uint16_t* ports[] = {&e.telnet_port, &e.info_port, &e.ota_port};
  const char* port_namen[] = {"telnet_port", "info_port", "ota_port"};
  for (int i = 0; i < 3; i++) {
    if (strcmp(name, port_namen[i]) != 0) continue;
    if (!zahl_lesen(wert, 1, 65535, z)) {
      aus("Fehler: %s 1-65535", name);
      return false;
    }
    for (int k = 0; k < 3; k++)
      if (k != i && *ports[k] == z) {
        aus("Fehler: Port %lu schon für %s vergeben", (unsigned long)z, port_namen[k]);
        return false;
      }
    *ports[i] = uint16_t(z);
    return true;
  }
  if (strcmp(name, "notfall_kanal") == 0) {
    if (!zahl_lesen(wert, 1, 13, z)) {
      aus("Fehler: notfall_kanal 1-13");
      return false;
    }
    e.notfall_kanal = uint8_t(z);
    return true;
  }
  if (strcmp(name, "notfall_ip") == 0 || strcmp(name, "notfall_maske") == 0) {
    bool ist_ip = name[8] == 'i';
    uint8_t v[4], ip[4], maske[4];
    if (!ip_lesen(wert, v)) {
      aus("Fehler: %s im Format a.b.c.d", name);
      return false;
    }
    memcpy(ip, ist_ip ? v : e.notfall_ip, 4);
    memcpy(maske, ist_ip ? e.notfall_maske : v, 4);
    if (!notfall_netz_ok(ip, maske)) {
      aus("Fehler: Notfall-Netz ungültig (Maske /24 bis /30, Adresse weder Netz- noch Broadcast-Adresse)");
      return false;
    }
    memcpy(ist_ip ? e.notfall_ip : e.notfall_maske, v, 4);
    return true;
  }
  struct Zeit {
    const char* name;
    uint32_t* ziel;
    uint32_t min, max, faktor;
  } zeiten[] = {
      {"notfall_nach_s", &e.notfall_nach_ms, NOTFALL_NACH_MIN_S, NOTFALL_NACH_MAX_S, 1000},
      {"heim_stabil_s", &e.heim_stabil_ms, HEIM_STABIL_MIN_S, HEIM_STABIL_MAX_S, 1000},
      {"leerlauf_min", &e.sitzung_leerlauf_ms, LEERLAUF_MIN_MIN, LEERLAUF_MAX_MIN, 60000},
  };
  for (const Zeit& t : zeiten) {
    if (strcmp(name, t.name) != 0) continue;
    if (!zahl_lesen(wert, t.min, t.max, z)) {
      aus("Fehler: %s %lu-%lu", name, (unsigned long)t.min, (unsigned long)t.max);
      return false;
    }
    *t.ziel = z * t.faktor;
    return true;
  }
  if (strcmp(name, "reboot_tage") == 0) {
    if (!zahl_lesen(wert, 0, REBOOT_MAX_TAGE, z)) {
      aus("Fehler: reboot_tage 0-%lu (0 = nie)", (unsigned long)REBOOT_MAX_TAGE);
      return false;
    }
    e.reboot_tage = uint16_t(z);
    return true;
  }
  aus("Fehler: unbekannter Name '%s' (~config help)", name);
  return false;
}

// Führt die Zeile nach "~config" aus. entwurf = Arbeitskopie der Sitzung, aktiv = laufende Einstellungen.
inline Ergebnis ausfuehren(const char* zeile, konfig::Daten& entwurf, const konfig::Daten& aktiv, char* text,
                           size_t groesse) {
  Ausgabe aus(text, groesse);
  char kopie[MAX_ZEILE + 1];
  wert_bereinigen(zeile, kopie, sizeof kopie);
  char* cmd = kopie;
  char* rest = strchr(kopie, ' ');
  if (rest) {
    *rest++ = 0;
    while (*rest == ' ') rest++;
  } else {
    rest = kopie + strlen(kopie);
  }

  if (*cmd == 0 || strcmp(cmd, "help") == 0) {
    hilfe(aus);
    return Ergebnis::ANZEIGE;
  }
  if (strcmp(cmd, "show") == 0) {
    zeigen(entwurf, aktiv, aus);
    return Ergebnis::ANZEIGE;
  }
  if (strcmp(cmd, "set") == 0) {
    char* wert = strchr(rest, ' ');
    if (*rest == 0 || !wert) {
      aus("Syntax: ~config set <name> <wert>");
      return Ergebnis::FEHLER;
    }
    *wert++ = 0;
    char bereinigt[MAX_ZEILE + 1];
    wert_bereinigen(wert, bereinigt, sizeof bereinigt);
    if (!setzen(entwurf, rest, bereinigt, aus)) return Ergebnis::FEHLER;
    aus("%s geändert (noch nicht gespeichert: ~config commit)", rest);
    return Ergebnis::GEAENDERT;
  }
  if (strcmp(cmd, "commit") == 0) return Ergebnis::COMMIT;
  if (strcmp(cmd, "discard") == 0) {
    aus("Entwurf verworfen");
    return Ergebnis::VERWORFEN;
  }
  aus("Fehler: unbekanntes Kommando '%s' (~config help)", cmd);
  return Ergebnis::FEHLER;
}

// Für commit: Entwurf vollständig prüfen und Prüfsumme setzen. false + Meldung, wenn er nicht gespeichert
// werden darf. Ist nichts geändert, liefert es true mit geaendert = false.
inline bool commit_pruefen(konfig::Daten& entwurf, const konfig::Daten& aktiv, bool& geaendert, bool& neustart,
                           char* text, size_t groesse) {
  Ausgabe aus(text, groesse);
  entwurf.magie = konfig::MAGIE;
  entwurf.format = konfig::FORMAT;
  entwurf.crc = konfig::pruefsumme(entwurf);
  geaendert = memcmp(&entwurf, &aktiv, sizeof entwurf) != 0;
  neustart = geaendert && konfig::neustart_noetig(aktiv, entwurf);
  if (!konfig::gueltig(entwurf)) {
    aus("Fehler: Einstellungen unvollständig oder ungültig, nichts gespeichert");
    return false;
  }
  return true;
}

}  // namespace config_cmd
