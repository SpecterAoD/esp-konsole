// Test der Einstellungen im EEPROM:  g++ -std=c++17 -Iinclude test/test_konfig.cpp -o /tmp/k && /tmp/k
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

  pruefe("CRC32 Referenzwert", crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xCBF43926u);
  printf("%d Fehler\n", fehler);
  return fehler ? 1 : 0;
}
