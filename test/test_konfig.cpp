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

  // ---- Ablage: Format 1 (v2) und Format 2 nebeneinander ----
  // So schreibt eine v2-Firmware: nur Format 1 an Adresse 0, Sektor sonst gelöscht (0xFF)
  auto v2_firmware_schreibt = [&](uint8_t* roh, const char* wlan_pw) {
    DatenV1 v1;
    memset(&v1, 0, sizeof v1);
    v1.magie = MAGIE;
    v1.format = 1;
    strcpy(v1.wlan_name, w);
    strcpy(v1.wlan_passwort, wlan_pw);
    strcpy(v1.notfall_name, n);
    strcpy(v1.notfall_passwort, np);
    strcpy(v1.ota_passwort, o);
    v1.crc = crc32(reinterpret_cast<const uint8_t*>(&v1), offsetof(DatenV1, crc));
    memset(roh, 0xFF, EEPROM_GROESSE);
    memcpy(roh + ADR_V1, &v1, sizeof v1);
  };
  // So liest eine v2-Firmware: Format 1 an Adresse 0
  auto v2_firmware_liest = [&](const uint8_t* roh, DatenV1& a) {
    memcpy(&a, roh + ADR_V1, sizeof a);
    return v1_gueltig(a);
  };

  static uint8_t roh[EEPROM_GROESSE + 1];
  uint8_t* r = roh + 1;  // absichtlich ungerade Adresse (Ausrichtung)
  Daten m;
  v2_firmware_schreibt(r, wp);
  pruefe("nur Format 1: übernommen", laden(r, EEPROM_GROESSE, m) == Quelle::FORMAT1 && gueltig(m));
  pruefe("nur Format 1: Zugangsdaten gleich, Standard-Einstellungen",
         strcmp(m.wlan_passwort, wp) == 0 && strcmp(m.ota_passwort, o) == 0 && m.telnet_port == 23 &&
             m.format == FORMAT);

  m.telnet_port = 2323;
  m.crc = pruefsumme(m);
  schreiben(m, r);
  Daten m2;
  pruefe("nach schreiben: Format 2", laden(r, EEPROM_GROESSE, m2) == Quelle::FORMAT2 && m2.telnet_port == 2323);
  DatenV1 alt;
  pruefe("nach schreiben: v2-Firmware findet gültige Zugangsdaten",
         v2_firmware_liest(r, alt) && strcmp(alt.wlan_passwort, wp) == 0);

  Daten neu_pw = m2;
  zugangsdaten_setzen(neu_pw, w, "neues-wlan-pw-1", n, np, o);
  schreiben(neu_pw, r);
  pruefe("Passwort per v3 geändert: v2-Firmware sieht das neue",
         v2_firmware_liest(r, alt) && strcmp(alt.wlan_passwort, "neues-wlan-pw-1") == 0);

  // v2-Firmware ändert Format 1 nur im Puffer (ohne Sektor zu löschen, theoretischer Fall)
  {
    static uint8_t kopie[EEPROM_GROESSE];
    memcpy(kopie, r, sizeof kopie);
    v2_firmware_schreibt(kopie, "von-v2-geaendert");
    memcpy(kopie + ADR_V2, r + ADR_V2, sizeof(Daten));  // Format 2 noch da
    pruefe("Format 1 neuer: Zugangsdaten daraus, Einstellungen aus Format 2",
           laden(kopie, sizeof kopie, m2) == Quelle::FORMAT2_ZUGANG_V1 &&
               strcmp(m2.wlan_passwort, "von-v2-geaendert") == 0 && m2.telnet_port == 2323 && gueltig(m2));
  }
  v2_firmware_schreibt(r, "von-v2-geaendert");  // echte v2-Firmware: Sektor gelöscht
  pruefe("v2 hat Sektor neu geschrieben: Übernahme aus Format 1",
         laden(r, EEPROM_GROESSE, m2) == Quelle::FORMAT1 && strcmp(m2.wlan_passwort, "von-v2-geaendert") == 0);

  r[ADR_V1 + offsetof(DatenV1, wlan_passwort)] ^= 1;
  pruefe("Format 1 mit falscher Prüfsumme abgelehnt", laden(r, EEPROM_GROESSE, m2) == Quelle::KEINE);
  pruefe("zu kurzer Puffer abgelehnt", laden(r, EEPROM_GROESSE - 1, m2) == Quelle::KEINE);
  memset(r, 0xFF, EEPROM_GROESSE);
  pruefe("gelöschter Sektor: nichts", laden(r, EEPROM_GROESSE, m2) == Quelle::KEINE);

  // Format 2 kaputt, Format 1 gültig -> Übernahme aus Format 1
  schreiben(d, r);
  r[ADR_V2 + offsetof(Daten, telnet_port)] ^= 1;
  pruefe("Format 2 kaputt: Format 1 greift", laden(r, EEPROM_GROESSE, m2) == Quelle::FORMAT1 && gueltig(m2));

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
