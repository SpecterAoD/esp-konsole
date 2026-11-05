// Test der Telnet-Kommandos und der Weiche:  g++ -std=c++17 -Wall -Werror -Iinclude test/test_config_cmd.cpp -o /tmp/c && /tmp/c
#include <cstdio>
#include <string>
#include <vector>
#include "config_cmd.h"

using namespace config_cmd;

static int fehler = 0;
static void pruefe(const char* name, bool ok) {
  printf("%s %s\n", ok ? "ok    " : "FEHLER", name);
  if (!ok) fehler++;
}

// Schickt Pakete durch die Weiche; sammelt, was an den Pi, als Echo und als Kommando herauskommt
struct Lauf {
  Weiche w;
  std::string pi, echo;
  std::vector<std::string> kommandos;
  void senden(const std::string& paket) {
    std::vector<uint8_t> a(paket.size() + PRAEFIX_LAENGE), e(3 * paket.size() + 32);
    size_t pos = 0;
    while (pos < paket.size()) {
      size_t n_pi, n_echo;
      Weiche::Ereignis ev;
      pos += w.verarbeiten(reinterpret_cast<const uint8_t*>(paket.data()) + pos, paket.size() - pos, a.data(), n_pi,
                           e.data(), n_echo, ev);
      pi.append(reinterpret_cast<char*>(a.data()), n_pi);
      echo.append(reinterpret_cast<char*>(e.data()), n_echo);
      if (ev == Weiche::Ereignis::KOMMANDO) kommandos.push_back(w.zeile());
    }
  }
  void zeichenweise(const std::string& s) {
    for (char c : s) senden(std::string(1, c));
  }
};

int main() {
  // ---- Weiche ----
  {
    Lauf l;
    l.senden("ls -l\r");
    pruefe("normale Zeile geht an den Pi", l.pi == "ls -l\r" && l.kommandos.empty() && l.echo.empty());
  }
  {
    Lauf l;
    l.zeichenweise("~config set wlan_pass geheimgeheim\r");
    pruefe("zeichenweise: Kommando erkannt", l.kommandos.size() == 1 && l.kommandos[0] == "set wlan_pass geheimgeheim");
    pruefe("zeichenweise: nichts davon an den Pi", l.pi.empty());
    pruefe("zeichenweise: ESP macht das Echo", l.echo == "~config set wlan_pass geheimgeheim");
  }
  {
    Lauf l;
    l.senden("ls\r~config show\rwho\r");
    pruefe("gemischtes Paket: Pi bekommt beide normalen Zeilen", l.pi == "ls\rwho\r");
    pruefe("gemischtes Paket: Kommando erkannt", l.kommandos.size() == 1 && l.kommandos[0] == "show");
  }
  {
    Lauf l;
    l.zeichenweise("~con");
    pruefe("Präfix wird zurückgehalten", l.pi.empty());
    l.senden("x\r");
    pruefe("kein Kommando: Zurückgehaltenes geht an den Pi", l.pi == "~conx\r" && l.kommandos.empty());
  }
  {
    Lauf l;
    l.senden("echo ~config show\r");
    pruefe("~config mitten in der Zeile geht an den Pi", l.pi == "echo ~config show\r" && l.kommandos.empty());
  }
  {
    Lauf l;
    l.senden("~configx\r");
    pruefe("~configx ist kein Kommando", l.pi == "~configx\r" && l.kommandos.empty());
  }
  {
    Lauf l;
    l.senden("~config\r");
    pruefe("~config allein = Hilfe", l.kommandos.size() == 1 && l.kommandos[0].empty());
  }
  {
    Lauf l;
    l.senden("~config set hostnamX\x7f" "e x\r");
    pruefe("Rücktaste im Kommando", l.kommandos.size() == 1 && l.kommandos[0] == "set hostname x");
  }
  {
    Lauf l;
    l.senden("~config set wlan_pass abc\x03");
    l.senden("ls\r");
    pruefe("Strg-C bricht ab, danach normal", l.kommandos.empty() && l.pi == "ls\r");
  }
  {
    Lauf l;
    l.senden("~config set ota_pass " + std::string(300, 'a') + "\r");
    pruefe("zu lange Zeile verworfen", l.kommandos.empty() && l.pi.empty() &&
                                           l.echo.find("zu lang") != std::string::npos);
    l.senden("~config show\r");
    pruefe("danach wieder bereit", l.kommandos.size() == 1);
  }

  // ---- Kommandos ----
  konfig::Daten aktiv;
  konfig::fuellen(aktiv, "MeinWLAN", "geheim-wlan-123", "Notfall", "abcdefghijklmnopqrstuvwx", "ota-passwort-24-zeichen!");
  konfig::Daten e = aktiv;
  char t[1024];

  pruefe("set ändert nur den Entwurf",
         ausfuehren("set wlan_name \"Mein WLAN\"", e, aktiv, t, sizeof t) == Ergebnis::GEAENDERT &&
             strcmp(e.wlan_name, "Mein WLAN") == 0 && strcmp(aktiv.wlan_name, "MeinWLAN") == 0);
  pruefe("Anführungszeichen entfernt ('...')",
         ausfuehren("set wlan_pass 'mit leer zeichen'", e, aktiv, t, sizeof t) == Ergebnis::GEAENDERT &&
             strcmp(e.wlan_passwort, "mit leer zeichen") == 0);
  pruefe("IP in Anführungszeichen",
         ausfuehren("set notfall_ip \"192.168.4.2\"", e, aktiv, t, sizeof t) == Ergebnis::GEAENDERT &&
             e.notfall_ip[3] == 2);
  pruefe("Passwort zu kurz",
         ausfuehren("set notfall_pass kurz", e, aktiv, t, sizeof t) == Ergebnis::FEHLER &&
             strcmp(e.notfall_passwort, aktiv.notfall_passwort) == 0);
  pruefe("Port doppelt abgelehnt", ausfuehren("set info_port 23", e, aktiv, t, sizeof t) == Ergebnis::FEHLER);
  pruefe("Port mit Zusatz abgelehnt", ausfuehren("set telnet_port 23x", e, aktiv, t, sizeof t) == Ergebnis::FEHLER);
  pruefe("negativer Wert abgelehnt", ausfuehren("set reboot_tage -1", e, aktiv, t, sizeof t) == Ergebnis::FEHLER);
  pruefe("IP mit Zusatz abgelehnt",
         ausfuehren("set notfall_ip 192.168.4.1abc", e, aktiv, t, sizeof t) == Ergebnis::FEHLER);
  pruefe("Maske /16 abgelehnt", ausfuehren("set notfall_maske 255.255.0.0", e, aktiv, t, sizeof t) == Ergebnis::FEHLER);
  pruefe("Zeit in Sekunden",
         ausfuehren("set notfall_nach_s 300", e, aktiv, t, sizeof t) == Ergebnis::GEAENDERT && e.notfall_nach_ms == 300000);
  pruefe("Zeit außerhalb", ausfuehren("set heim_stabil_s 5", e, aktiv, t, sizeof t) == Ergebnis::FEHLER);
  pruefe("unbekannter Name", ausfuehren("set dns 1.1.1.1", e, aktiv, t, sizeof t) == Ergebnis::FEHLER);
  pruefe("unbekanntes Kommando", ausfuehren("reset", e, aktiv, t, sizeof t) == Ergebnis::FEHLER);

  ausfuehren("show", e, aktiv, t, sizeof t);
  std::string s = t;
  pruefe("show: Passwörter nur als Länge", s.find("geheim") == std::string::npos &&
                                               s.find("mit leer") == std::string::npos &&
                                               s.find("(16 Zeichen)") != std::string::npos);
  pruefe("show: Änderungen markiert", s.find("* wlan_name      Mein WLAN") != std::string::npos &&
                                          s.find("  hostname       esp-konsole") != std::string::npos);

  // ---- commit ----
  bool geaendert, neustart;
  konfig::Daten c = aktiv;
  c.crc = 0;  // muss neu berechnet werden
  pruefe("commit ohne Änderung", commit_pruefen(c, aktiv, geaendert, neustart, t, sizeof t) && !geaendert && !neustart);
  pruefe("commit mit WLAN-Änderung: gültig, Neustart",
         commit_pruefen(e, aktiv, geaendert, neustart, t, sizeof t) && geaendert && neustart && konfig::gueltig(e));
  c = aktiv;
  ausfuehren("set leerlauf_min 5", c, aktiv, t, sizeof t);
  pruefe("commit nur Leerlauf: ohne Neustart",
         commit_pruefen(c, aktiv, geaendert, neustart, t, sizeof t) && geaendert && !neustart &&
             c.sitzung_leerlauf_ms == 300000);
  c = aktiv;
  ausfuehren("set ota_pass neues-ota-passwort", c, aktiv, t, sizeof t);
  pruefe("commit OTA-Passwort: Neustart", commit_pruefen(c, aktiv, geaendert, neustart, t, sizeof t) && neustart);
  konfig::Daten leer;
  konfig::grundlage(leer);
  konfig::Daten le = leer;
  ausfuehren("set wlan_name Nur-Name", le, leer, t, sizeof t);
  pruefe("commit unvollständig abgelehnt", !commit_pruefen(le, leer, geaendert, neustart, t, sizeof t));
  pruefe("discard", ausfuehren("discard", e, aktiv, t, sizeof t) == Ergebnis::VERWORFEN);

  // ---- Hilfsfunktionen ----
  char b[32];
  wert_bereinigen("  \"a b\"  ", b, sizeof b);
  pruefe("bereinigen: Leerzeichen + Anführungszeichen", strcmp(b, "a b") == 0);
  wert_bereinigen("\"", b, sizeof b);
  pruefe("bereinigen: einzelnes Anführungszeichen bleibt", strcmp(b, "\"") == 0);
  uint8_t ip[4];
  pruefe("ip_lesen gültig", ip_lesen("10.0.0.255", ip) && ip[3] == 255);
  pruefe("ip_lesen 256", !ip_lesen("10.0.0.256", ip));
  pruefe("ip_lesen zu kurz", !ip_lesen("10.0.0", ip));
  char klein[8];
  Ausgabe a(klein, sizeof klein);
  a("%s", "viel zu langer Text");
  pruefe("Ausgabe schneidet sicher ab", strlen(klein) == 7);

  printf("%d Fehler\n", fehler);
  return fehler ? 1 : 0;
}
