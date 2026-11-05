// Test der Einstellungen im EEPROM:  g++ -std=c++17 -Wall -Werror -Iinclude test/test_konfig.cpp -o /tmp/k && /tmp/k
#include <cstdio>
#include <cstring>
#include "konfig.h"

static int fehler = 0;
static void pruefe(const char* name, bool ok) {
  printf("%s %s\n", ok ? "ok    " : "FEHLER", name);
  if (!ok) fehler++;
}

int main() {
  using namespace konfig;
  const char *w = "MeinWLAN", *wp = "geheim-wlan-123", *n = "Notfall", *np = "abcdefghijklmnopqrstuvwx", *o = "ota-passwort-24-zeichen!";
  Daten d;
  pruefe("gültige Werte werden übernommen", fuellen(d, w, wp, n, np, o) && gueltig(d));
  pruefe("Werte stehen drin", strcmp(d.wlan_name, w) == 0 && strcmp(d.ota_passwort, o) == 0);

  Daten x;
  pruefe("Platzhalter abgelehnt", !fuellen(x, "<heim-wlan>", wp, n, np, o));
  pruefe("leerer Wert abgelehnt", !fuellen(x, w, "", n, np, o));
  pruefe("WPA-Passwort zu kurz abgelehnt", !fuellen(x, w, "kurz", n, np, o));
  pruefe("WPA-Passwort 64 Zeichen abgelehnt", !fuellen(x, w, wp, n, "1234567890123456789012345678901234567890123456789012345678901234", o));
  pruefe("WLAN-Name 33 Zeichen abgelehnt", !fuellen(x, "123456789012345678901234567890123", wp, n, np, o));

  Daten kaputt = d;
  kaputt.wlan_passwort[0] ^= 1;
  pruefe("veränderte Daten -> Prüfsumme falsch", !gueltig(kaputt));

  Daten leer;
  memset(&leer, 0xFF, sizeof leer);  // frisch gelöschtes Flash
  pruefe("gelöschtes Flash ist ungültig", !gueltig(leer));
  memset(&leer, 0, sizeof leer);
  pruefe("Nullen sind ungültig", !gueltig(leer));

  Daten offen = d;
  memset(offen.notfall_name, 'A', sizeof offen.notfall_name);  // ohne Abschluss-Null
  offen.crc = pruefsumme(offen);
  pruefe("nicht abgeschlossene Zeichenkette ungültig", !gueltig(offen));

  // ---- Format 2: Einstellungen ----
  pruefe("Standardwerte wie v2", d.telnet_port == 23 && d.info_port == 8023 && d.ota_port == 8266 &&
                                     d.notfall_kanal == 6 && strcmp(d.hostname, "esp-konsole") == 0 &&
                                     d.notfall_nach_ms == 180000 && d.sitzung_leerlauf_ms == 1800000);
  auto mit = [&](void (*aendern)(Daten&)) {
    Daten t = d;
    aendern(t);
    t.crc = pruefsumme(t);
    return gueltig(t);
  };
  pruefe("Port 0 ungültig", !mit([](Daten& t) { t.info_port = 0; }));
  pruefe("doppelter Port ungültig", !mit([](Daten& t) { t.ota_port = t.telnet_port; }));
  pruefe("Kanal 14 ungültig", !mit([](Daten& t) { t.notfall_kanal = 14; }));
  pruefe("Leerlauf 0 ungültig", !mit([](Daten& t) { t.sitzung_leerlauf_ms = 0; }));
  pruefe("Hostname mit _ ungültig", !mit([](Daten& t) { strcpy(t.hostname, "esp_konsole"); }));
  pruefe("Maske /16 ungültig", !mit([](Daten& t) { t.notfall_maske[2] = 0; }));
  pruefe("Notfall-IP = Netzadresse ungültig", !mit([](Daten& t) { t.notfall_ip[3] = 0; }));
  pruefe("Notfall-IP /30 gültig", mit([](Daten& t) {
           t.notfall_maske[3] = 252;
           t.notfall_ip[3] = 2;
         }));
  uint8_t loch[4] = {255, 255, 0, 255}, ip4[4] = {10, 0, 0, 1};
  pruefe("Maske mit Lücke ungültig", !notfall_netz_ok(ip4, loch));

  // ---- Übernahme Format 1 ----
  DatenV1 v1;
  memset(&v1, 0, sizeof v1);
  v1.magie = MAGIE;
  v1.format = 1;
  strcpy(v1.wlan_name, w);
  strcpy(v1.wlan_passwort, wp);
  strcpy(v1.notfall_name, n);
  strcpy(v1.notfall_passwort, np);
  strcpy(v1.ota_passwort, o);
  v1.crc = crc32(reinterpret_cast<const uint8_t*>(&v1), offsetof(DatenV1, crc));
  uint8_t roh[sizeof(Daten) + 1];
  memset(roh, 0xFF, sizeof roh);  // Rest des EEPROM-Bereichs wie gelöschtes Flash
  memcpy(roh + 1, &v1, sizeof v1);  // absichtlich ungerade Adresse (Ausrichtung)
  Daten m;
  pruefe("v1 übernommen", migrieren_v1_zu_v2(roh + 1, sizeof(Daten), m) && gueltig(m));
  pruefe("v1: Zugangsdaten gleich, Standard-Einstellungen",
         strcmp(m.wlan_passwort, wp) == 0 && strcmp(m.ota_passwort, o) == 0 && m.telnet_port == 23 &&
             m.format == FORMAT);
  roh[1 + offsetof(DatenV1, wlan_passwort)] ^= 1;
  pruefe("v1 mit falscher Prüfsumme abgelehnt", !migrieren_v1_zu_v2(roh + 1, sizeof(Daten), m));
  roh[1 + offsetof(DatenV1, wlan_passwort)] ^= 1;
  pruefe("zu kurzer Puffer abgelehnt", !migrieren_v1_zu_v2(roh + 1, sizeof(DatenV1) - 1, m));
  Daten v2 = d;
  pruefe("v2-Block wird nicht als v1 übernommen",
         !migrieren_v1_zu_v2(reinterpret_cast<const uint8_t*>(&v2), sizeof v2, m));

  // ---- Neustart nötig? ----
  Daten b = d;
  b.sitzung_leerlauf_ms = 60000;
  b.reboot_tage = 7;
  pruefe("Zeiten/reboot_tage ohne Neustart", !neustart_noetig(d, b));
  b.ota_port = 8267;
  pruefe("Port mit Neustart", neustart_noetig(d, b));
  b = d;
  b.ota_passwort[0] ^= 1;
  pruefe("OTA-Passwort mit Neustart", neustart_noetig(d, b));

  // ---- Zugangsdaten neu, Einstellungen bleiben (KONFIG_ERZWINGEN) ----
  Daten z = d;
  z.telnet_port = 2323;
  pruefe("zugangsdaten_setzen behält Ports",
         zugangsdaten_setzen(z, "Anderes", wp, n, np, o) && gueltig(z) && z.telnet_port == 2323);

  pruefe("CRC32 Referenzwert", crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xCBF43926u);
  printf("%d Fehler\n", fehler);
  return fehler ? 1 : 0;
}
