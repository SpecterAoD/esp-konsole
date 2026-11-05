// ESP-Konsole: serielle Konsole des piholepi (ttyAMA0, 115200 8N1) per Telnet, Ersatz für esp-link.
// Hardware: Wemos D1 mini im Gehäuse des piholepi, UART0 (TX/RX) an GPIO14/15 des Pi.
//
// Verhalten (Zahlen = Standardwerte, ab v3 per "~config" änderbar, siehe README):
//   - Heim-WLAN: Telnet auf Port 23 nur von ERLAUBT[] (siehe unten, nicht per Telnet änderbar).
//   - Heim-WLAN länger als 3 min weg: Notfall-WLAN (WPA2, 1 Gerät), Telnet unter 192.168.4.1:23.
//     Ist das Heim-WLAN wieder da und kein Gerät im Notfall-WLAN, geht das Notfall-WLAN wieder aus.
//   - Eine Telnet-Sitzung gleichzeitig; eine neue (erlaubte) Verbindung übernimmt und trennt die alte.
//     Trennung nach 30 min ohne Eingabe.
//   - Keine Weboberfläche. Update über das WLAN (ArduinoOTA, Port 8266) nur mit OTA-Passwort UND
//     gültiger Signatur (RSA, öffentlicher Schlüssel in include/signatur_public.h). Erstes Flashen per USB.
//   - Zugangsdaten und Einstellungen im EEPROM (include/konfig.h); eine lokal mit include/secrets.h gebaute
//     Firmware schreibt die Zugangsdaten beim ersten Start dorthin. Firmware von GitHub enthält keine Passwörter.
//   - Zeilen, die mit "~config" beginnen, gehen nicht an den Pi, sondern ändern die Einstellungen
//     (include/config_cmd.h); gespeichert wird erst mit "~config commit".
//   - Info-Port 8023: eine Zeile JSON (Version, Laufzeit, WLAN, Speicher) für ERLAUBT[] und INFO_ERLAUBT[].
//   - Auf die serielle Leitung wird NUR weitergeleitet, nie selbst etwas ausgegeben (der Pi würde es
//     als Anmeldeversuch lesen).
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <Updater.h>

#include "config_cmd.h"
#include "konfig.h"
#include "signatur_public.h"
#include "telnet_filter.h"

// secrets.h ist nur bei lokalen Builds vorhanden (nie im Repository, nie in Releases)
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WLAN_NAME
#define WLAN_NAME ""
#define WLAN_PASSWORT ""
#define NOTFALL_NAME ""
#define NOTFALL_PASSWORT ""
#define OTA_PASSWORT ""
#endif
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// ---- Feste Einstellungen (die übrigen stehen im EEPROM) ----
// Bewusst nicht per Telnet änderbar: ein Tippfehler würde den Zugang sperren.
static const IPAddress ERLAUBT[] = {
    IPAddress(192, 168, 2, 121),  // Laptop (linuxmint)
    IPAddress(192, 168, 2, 100),  // iPhone
};
static const IPAddress INFO_ERLAUBT[] = {
    IPAddress(192, 168, 2, 6),  // piholepi (Update-Prüfung, Überwachung)
};
static const uint32_t BAUD = 115200;
static const uint32_t HEIM_VERSUCH_MS = 30UL * 1000;  // Neuversuch Heim-WLAN im Notfallbetrieb
static const uint32_t NEUSTART_NACH_COMMIT_MS = 2000;  // Zeit, damit die Antwort noch ankommt
// -----------------------

static WiFiServer server(23);       // Port wird in setup() aus den Einstellungen gesetzt
static WiFiServer info_server(8023);
static konfig::Daten kfg;      // laufende Einstellungen
static konfig::Daten entwurf;  // Arbeitskopie für "~config set" (je Sitzung)
static bool kfg_ok = false;
static const char* kfg_quelle = "keine";
static BearSSL::PublicKey signatur_schluessel(SIGNATUR_PUBLIC);
static BearSSL::HashSHA256 signatur_hash;
static BearSSL::SigningVerifier signatur_pruefer(&signatur_schluessel);
static WiFiClient sitzung;
static telnet::Filter filter;
static config_cmd::Weiche weiche;
static bool notfall_an = false;
static uint32_t heim_zuletzt_ok = 0, heim_seit = 0, letzter_versuch = 0, letzte_eingabe = 0;
static bool neustart_geplant = false;
static uint32_t neustart_ab = 0;
static uint64_t laufzeit_ms = 0;  // ohne Überlauf nach 49 Tagen (millis)

static IPAddress ip_aus(const uint8_t p[4]) { return IPAddress(p[0], p[1], p[2], p[3]); }

static bool im_netz(const IPAddress& ip, const uint8_t netz[4], const uint8_t maske[4]) {
  uint32_t m = konfig::ip_wert(maske);
  uint32_t a = (uint32_t(ip[0]) << 24) | (uint32_t(ip[1]) << 16) | (uint32_t(ip[2]) << 8) | ip[3];
  return (a & m) == (konfig::ip_wert(netz) & m);
}

static bool im_notfallnetz(const IPAddress& ip) {
  return notfall_an && im_netz(ip, kfg.notfall_ip, kfg.notfall_maske);
}

static bool erlaubt(const IPAddress& ip) {
  if (im_notfallnetz(ip)) return true;  // wer im Notfall-WLAN ist, kennt dessen Passwort
  for (const IPAddress& e : ERLAUBT)
    if (e == ip) return true;
  return false;
}

static bool info_erlaubt(const IPAddress& ip) {
  if (erlaubt(ip)) return true;
  for (const IPAddress& e : INFO_ERLAUBT)
    if (e == ip) return true;
  return false;
}

// Das Notfall-Netz darf das Heimnetz nicht überdecken, sonst hätte im Notfallbetrieb jedes Gerät im
// Heimnetz Zugang (erlaubt() lässt das ganze Notfall-Netz zu).
static bool notfallnetz_ueberschneidet(const konfig::Daten& d) {
  for (const IPAddress& e : ERLAUBT)
    if (im_netz(e, d.notfall_ip, d.notfall_maske)) return true;
  for (const IPAddress& e : INFO_ERLAUBT)
    if (im_netz(e, d.notfall_ip, d.notfall_maske)) return true;
  if (WiFi.status() == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP(), m = WiFi.subnetMask();
    uint8_t heim_m[4] = {m[0], m[1], m[2], m[3]};
    // Netze überschneiden sich, wenn sie unter der kürzeren Maske gleich sind
    const uint8_t* kurz = konfig::ip_wert(heim_m) < konfig::ip_wert(d.notfall_maske) ? heim_m : d.notfall_maske;
    if (im_netz(ip, d.notfall_ip, kurz)) return true;
  }
  return false;
}

// Schreibt d ins EEPROM (Format 2 + Format-1-Kopie der Zugangsdaten für v2) und liest zur Kontrolle zurück.
// Immer den ganzen Bereich: EEPROM.commit() löscht den Sektor und schreibt nur die angegebene Größe.
static bool konfig_schreiben(const konfig::Daten& d) {
  static uint8_t roh[konfig::EEPROM_GROESSE];
  EEPROM.begin(sizeof roh);
  for (size_t i = 0; i < sizeof roh; i++) roh[i] = EEPROM.read(i);
  konfig::schreiben(d, roh);
  for (size_t i = 0; i < sizeof roh; i++) EEPROM.write(i, roh[i]);
  bool ok = EEPROM.commit();
  for (size_t i = 0; ok && i < sizeof roh; i++) ok = EEPROM.read(i) == roh[i];
  EEPROM.end();
  konfig::Daten zurueck;
  return ok && konfig::laden(roh, sizeof roh, zurueck) == konfig::Quelle::FORMAT2 &&
         memcmp(&zurueck, &d, sizeof d) == 0;
}

// Einstellungen laden: EEPROM gültig -> verwenden (Format 1 von v2 wird übernommen); sonst (oder mit
// KONFIG_ERZWINGEN) Zugangsdaten aus secrets.h schreiben, falls plausibel – vorhandene Einstellungen bleiben.
// Ohne beides bleibt das WLAN aus (Notzugang dann nur per USB).
static void konfig_laden() {
  static uint8_t roh[konfig::EEPROM_GROESSE];
  EEPROM.begin(sizeof roh);
  for (size_t i = 0; i < sizeof roh; i++) roh[i] = EEPROM.read(i);
  EEPROM.end();
  konfig::Quelle quelle = konfig::laden(roh, sizeof roh, kfg);
  bool im_eeprom = quelle != konfig::Quelle::KEINE;
#ifdef KONFIG_ERZWINGEN
  bool schreiben = true;
#else
  bool schreiben = !im_eeprom;
#endif
  konfig::Daten neu;
  if (im_eeprom) {
    neu = kfg;
  } else {
    konfig::grundlage(neu);
  }
  if (schreiben && konfig::zugangsdaten_setzen(neu, WLAN_NAME, WLAN_PASSWORT, NOTFALL_NAME, NOTFALL_PASSWORT,
                                               OTA_PASSWORT)) {
    kfg = neu;
    kfg_ok = true;
    kfg_quelle = "secrets.h";
  } else if (im_eeprom) {
    kfg_ok = true;
    kfg_quelle = quelle == konfig::Quelle::FORMAT2 ? "eeprom" : "eeprom-v1";
  } else {
    konfig::grundlage(kfg);  // keine Zugangsdaten: wenigstens Hostname/Ports definiert
    return;
  }
  // Beide Blöcke auf den Stand von kfg bringen (Übernahme aus Format 1, secrets.h, fehlende Format-1-Kopie)
  static uint8_t soll[konfig::EEPROM_GROESSE];
  memcpy(soll, roh, sizeof soll);
  konfig::schreiben(kfg, soll);
  if (memcmp(soll, roh, sizeof soll) != 0) konfig_schreiben(kfg);  // bei Schreibfehler gilt kfg trotzdem
}

static void info_beantworten() {
  if (!info_server.hasClient()) return;
  WiFiClient c = info_server.accept();
  if (!info_erlaubt(c.remoteIP())) {
    c.stop();
    return;
  }
  char zeile[320];
  snprintf(zeile, sizeof zeile,
           "{\"name\":\"%s\",\"version\":\"%s\",\"laufzeit_s\":%lu,\"rssi\":%d,\"heap\":%u,"
           "\"neustart\":\"%s\",\"notfall\":%s,\"sitzung\":%s,\"konfig\":\"%s\",\"signatur\":true}\n",
           kfg.hostname, FW_VERSION, (unsigned long)(laufzeit_ms / 1000), WiFi.RSSI(), ESP.getFreeHeap(),
           ESP.getResetReason().c_str(), notfall_an ? "true" : "false", sitzung.connected() ? "true" : "false",
           kfg_quelle);
  c.write(zeile, strlen(zeile));
  c.flush();
  c.stop();
}

static void heim_verbinden() {
  if (!kfg_ok) return;
  WiFi.begin(kfg.wlan_name, kfg.wlan_passwort);
  letzter_versuch = millis();
}

static void notfall_ein() {
  // Automatisches Wiederverbinden aus: sonst springt der ESP beim Suchen über alle Kanäle und
  // das Notfall-WLAN bricht ständig ab. Neuversuche steuert wlan_pflegen().
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_AP_STA);
  IPAddress ip = ip_aus(kfg.notfall_ip);
  WiFi.softAPConfig(ip, ip, ip_aus(kfg.notfall_maske));
  WiFi.softAP(kfg.notfall_name, kfg.notfall_passwort, kfg.notfall_kanal, false, 1);
  notfall_an = true;
}

static void notfall_aus() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  notfall_an = false;
}

static void wlan_pflegen() {
  uint32_t jetzt = millis();
  bool heim = WiFi.status() == WL_CONNECTED;
  if (heim) {
    if (heim_seit == 0) heim_seit = jetzt;
    heim_zuletzt_ok = jetzt;
  } else {
    heim_seit = 0;
  }

  if (!kfg_ok) return;
  if (!notfall_an) {
    if (!heim && jetzt - heim_zuletzt_ok > kfg.notfall_nach_ms) notfall_ein();
    return;
  }
  uint8_t geraete = WiFi.softAPgetStationNum();
  if (heim && jetzt - heim_seit > kfg.heim_stabil_ms && geraete == 0 && !sitzung.connected()) {
    notfall_aus();
    return;
  }
  // Heim-WLAN nur suchen, solange niemand im Notfall-WLAN ist (Suche stört das Notfall-WLAN)
  if (!heim && geraete == 0 && jetzt - letzter_versuch > HEIM_VERSUCH_MS) heim_verbinden();
}

static void sitzung_pflegen() {
  if (server.hasClient()) {
    WiFiClient neu = server.accept();
    if (!erlaubt(neu.remoteIP())) {
      neu.stop();  // nicht erlaubt: ohne Meldung trennen
    } else {
      // Eine neue Verbindung übernimmt: eine hängende alte Sitzung (z. B. App im Hintergrund)
      // darf den Notzugang nicht blockieren.
      if (sitzung.connected()) sitzung.stop();
      sitzung = neu;
      sitzung.setNoDelay(true);
      filter.zuruecksetzen();
      weiche.zuruecksetzen();
      entwurf = kfg;  // ein nicht gespeicherter Entwurf der alten Sitzung verfällt
      sitzung.write(telnet::BEGRUESSUNG, sizeof(telnet::BEGRUESSUNG));
      letzte_eingabe = millis();
    }
  }
  if (sitzung && !sitzung.connected()) sitzung.stop();
  if (sitzung.connected() && millis() - letzte_eingabe > kfg.sitzung_leerlauf_ms) sitzung.stop();
}

// Antwort auf ein "~config"-Kommando an den Client (nie an den Pi)
static void antworten(const char* text) {
  sitzung.write("\r\n", 2);
  size_t n = strlen(text);
  if (n) {
    sitzung.write(text, n);
    sitzung.write("\r\n", 2);
  }
}

static void kommando_ausfuehren(const char* zeile) {
  static char text[1024];
  using config_cmd::Ergebnis;
  if (neustart_geplant) {
    antworten("Neustart läuft, keine Änderungen mehr");
    return;
  }
  switch (config_cmd::ausfuehren(zeile, entwurf, kfg, text, sizeof text)) {
    case Ergebnis::COMMIT: {
      bool geaendert = false, neustart = false;
      if (!config_cmd::commit_pruefen(entwurf, kfg, geaendert, neustart, text, sizeof text)) break;
      if (!geaendert) {
        snprintf(text, sizeof text, "Keine Änderungen");
      } else if (notfallnetz_ueberschneidet(entwurf)) {
        snprintf(text, sizeof text, "Fehler: Notfall-Netz überschneidet das Heimnetz, nichts gespeichert");
      } else if (!konfig_schreiben(entwurf)) {
        snprintf(text, sizeof text, "Fehler: EEPROM schreiben fehlgeschlagen, alte Einstellungen bleiben");
      } else if (neustart) {
        snprintf(text, sizeof text, "Gespeichert. Neustart in %lu s", (unsigned long)(NEUSTART_NACH_COMMIT_MS / 1000));
        neustart_geplant = true;
        neustart_ab = millis() + NEUSTART_NACH_COMMIT_MS;
      } else {
        kfg = entwurf;  // nur Zeiten/reboot_tage geändert: wirkt sofort
        kfg_quelle = "eeprom";
        snprintf(text, sizeof text, "Gespeichert, wirkt sofort");
      }
      break;
    }
    case Ergebnis::VERWORFEN:
      entwurf = kfg;
      break;
    default:
      break;
  }
  antworten(text);
}

static void weiterleiten() {
  static uint8_t ein[256], aus[512], gefiltert[256];
  static uint8_t an_pi[sizeof gefiltert + config_cmd::PRAEFIX_LAENGE], echo[3 * sizeof gefiltert + 32];
  // Telnet -> Pi (Zeilen mit "~config" gehen an den ESP)
  if (sitzung.connected()) {
    size_t n = sitzung.available();
    if (n) {
      if (n > sizeof(ein)) n = sizeof(ein);
      n = sitzung.read(ein, n);
      size_t k = filter.eingang(ein, n, gefiltert);
      size_t pos = 0;
      while (pos < k) {
        size_t n_pi, n_echo;
        config_cmd::Weiche::Ereignis ereignis;
        pos += weiche.verarbeiten(gefiltert + pos, k - pos, an_pi, n_pi, echo, n_echo, ereignis);
        if (n_pi) Serial.write(an_pi, n_pi);
        if (n_echo) sitzung.write(echo, n_echo);
        if (ereignis == config_cmd::Weiche::Ereignis::KOMMANDO) kommando_ausfuehren(weiche.zeile());
      }
      letzte_eingabe = millis();
    }
  }
  // Pi -> Telnet (ohne Sitzung werden die Zeichen verworfen)
  size_t n = Serial.available();
  if (n) {
    if (n > sizeof(ein)) n = sizeof(ein);
    n = Serial.readBytes(ein, n);
    if (sitzung.connected()) {
      size_t k = telnet::ausgang(ein, n, aus);
      sitzung.write(aus, k);
    }
  }
}

static void neustart_pflegen() {
  static uint32_t zuletzt = millis();
  uint32_t jetzt = millis();
  laufzeit_ms += uint32_t(jetzt - zuletzt);
  zuletzt = jetzt;

  // nach "~config commit" mit Änderungen, die einen Neustart brauchen (schon gespeichert)
  if (neustart_geplant && int32_t(jetzt - neustart_ab) >= 0) {
    sitzung.stop();
    delay(100);
    ESP.restart();
  }
  // regelmäßiger Neustart (reboot_tage), aber nicht mitten in einer Sitzung
  if (kfg.reboot_tage && laufzeit_ms >= uint64_t(kfg.reboot_tage) * 86400000ULL && !sitzung.connected())
    ESP.restart();
}

void setup() {
  Serial.setRxBufferSize(1024);
  Serial.begin(BAUD);
  Serial.setDebugOutput(false);
  konfig_laden();
  entwurf = kfg;
  WiFi.persistent(false);  // WLAN-Daten nicht vom SDK speichern lassen, sie stehen in unserem EEPROM-Bereich
  WiFi.mode(WIFI_STA);
  WiFi.hostname(kfg.hostname);
  WiFi.setAutoReconnect(true);
  heim_verbinden();
  heim_zuletzt_ok = millis();  // Frist für das Notfall-WLAN läuft ab dem Start
  server.begin(kfg.telnet_port);
  server.setNoDelay(true);
  info_server.begin(kfg.info_port);

  // Nur signierte Firmware annehmen (gilt für jedes Update über das Netz)
  Update.installSignature(&signatur_hash, &signatur_pruefer);
  ArduinoOTA.setHostname(kfg.hostname);
  ArduinoOTA.setPort(kfg.ota_port);
  if (kfg_ok) ArduinoOTA.setPassword(kfg.ota_passwort);  // wirkt nur vor begin() -> Änderung braucht Neustart
  ArduinoOTA.onStart([]() { sitzung.stop(); });         // Konsole während des Updates trennen
  if (kfg_ok) ArduinoOTA.begin(false);                   // ohne mDNS-Werbung; ohne Passwort kein OTA
}

void loop() {
  neustart_pflegen();
  ArduinoOTA.handle();
  wlan_pflegen();
  sitzung_pflegen();
  info_beantworten();
  weiterleiten();
  delay(1);
}
