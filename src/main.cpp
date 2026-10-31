// ESP-Konsole: serielle Konsole des piholepi (ttyAMA0, 115200 8N1) per Telnet, Ersatz für esp-link.
// Hardware: Wemos D1 mini im Gehäuse des piholepi, UART0 (TX/RX) an GPIO14/15 des Pi.
//
// Verhalten:
//   - Heim-WLAN: Telnet auf Port 23 nur von ERLAUBT[] (siehe unten).
//   - Heim-WLAN länger als NOTFALL_NACH_MS weg: Notfall-WLAN (WPA2, 1 Gerät), Telnet unter 192.168.4.1:23.
//     Ist das Heim-WLAN wieder da und kein Gerät im Notfall-WLAN, geht das Notfall-WLAN wieder aus.
//   - Eine Telnet-Sitzung gleichzeitig; eine neue (erlaubte) Verbindung übernimmt und trennt die alte.
//     Trennung nach SITZUNG_LEERLAUF_MS ohne Eingabe.
//   - Keine Weboberfläche. Update über das WLAN (ArduinoOTA, Port 8266) nur mit OTA-Passwort UND
//     gültiger Signatur (RSA, öffentlicher Schlüssel in include/signatur_public.h). Erstes Flashen per USB.
//   - Zugangsdaten im EEPROM (include/konfig.h); eine lokal mit include/secrets.h gebaute Firmware schreibt
//     sie beim ersten Start dorthin. Firmware von GitHub enthält keine Passwörter.
//   - Info-Port 8023: eine Zeile JSON (Version, Laufzeit, WLAN, Speicher) für ERLAUBT[] und INFO_ERLAUBT[].
//   - Auf die serielle Leitung wird NUR weitergeleitet, nie selbst etwas ausgegeben (der Pi würde es
//     als Anmeldeversuch lesen).
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <Updater.h>

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

// ---- Einstellungen ----
static const IPAddress ERLAUBT[] = {
    IPAddress(192, 168, 2, 121),  // Laptop (linuxmint)
    IPAddress(192, 168, 2, 100),  // iPhone
};
static const IPAddress INFO_ERLAUBT[] = {
    IPAddress(192, 168, 2, 6),  // piholepi (Update-Prüfung, Überwachung)
};
static const char* HOSTNAME = "esp-konsole";
static const uint16_t TELNET_PORT = 23;
static const uint16_t INFO_PORT = 8023;
static const uint32_t BAUD = 115200;
static const uint32_t NOTFALL_NACH_MS = 3UL * 60 * 1000;       // Heim-WLAN so lange weg -> Notfall-WLAN
static const uint32_t HEIM_STABIL_MS = 60UL * 1000;            // so lange wieder da -> Notfall-WLAN aus
static const uint32_t HEIM_VERSUCH_MS = 30UL * 1000;           // Neuversuch Heim-WLAN im Notfallbetrieb
static const uint32_t SITZUNG_LEERLAUF_MS = 30UL * 60 * 1000;  // Telnet-Sitzung ohne Eingabe trennen
static const uint8_t NOTFALL_KANAL = 6;
static const IPAddress NOTFALL_IP(192, 168, 4, 1), NOTFALL_MASKE(255, 255, 255, 0);
// -----------------------

static WiFiServer server(TELNET_PORT);
static WiFiServer info_server(INFO_PORT);
static konfig::Daten kfg;
static bool kfg_ok = false;
static const char* kfg_quelle = "keine";
static BearSSL::PublicKey signatur_schluessel(SIGNATUR_PUBLIC);
static BearSSL::HashSHA256 signatur_hash;
static BearSSL::SigningVerifier signatur_pruefer(&signatur_schluessel);
static WiFiClient sitzung;
static telnet::Filter filter;
static bool notfall_an = false;
static uint32_t heim_zuletzt_ok = 0, heim_seit = 0, letzter_versuch = 0, letzte_eingabe = 0;

static bool im_notfallnetz(const IPAddress& ip) {
  return notfall_an && (uint32_t(ip) & uint32_t(NOTFALL_MASKE)) == (uint32_t(NOTFALL_IP) & uint32_t(NOTFALL_MASKE));
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

// Einstellungen laden: EEPROM gültig -> verwenden; sonst (oder mit KONFIG_ERZWINGEN) Werte aus secrets.h
// schreiben, falls plausibel. Ohne beides bleibt das WLAN aus (Notzugang dann nur per USB).
static void konfig_laden() {
  EEPROM.begin(sizeof(konfig::Daten));
  EEPROM.get(0, kfg);
  bool im_eeprom = konfig::gueltig(kfg);
#ifdef KONFIG_ERZWINGEN
  bool schreiben = true;
#else
  bool schreiben = !im_eeprom;
#endif
  konfig::Daten neu;
  if (schreiben && konfig::fuellen(neu, WLAN_NAME, WLAN_PASSWORT, NOTFALL_NAME, NOTFALL_PASSWORT, OTA_PASSWORT)) {
    if (!im_eeprom || memcmp(&neu, &kfg, sizeof neu) != 0) {
      EEPROM.put(0, neu);
      EEPROM.commit();
    }
    kfg = neu;
    kfg_ok = true;
    kfg_quelle = "secrets.h";
  } else if (im_eeprom) {
    kfg_ok = true;
    kfg_quelle = "eeprom";
  }
  EEPROM.end();
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
           HOSTNAME, FW_VERSION, (unsigned long)(millis() / 1000), WiFi.RSSI(), ESP.getFreeHeap(),
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
  WiFi.softAPConfig(NOTFALL_IP, NOTFALL_IP, NOTFALL_MASKE);
  WiFi.softAP(kfg.notfall_name, kfg.notfall_passwort, NOTFALL_KANAL, false, 1);
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
    if (!heim && jetzt - heim_zuletzt_ok > NOTFALL_NACH_MS) notfall_ein();
    return;
  }
  uint8_t geraete = WiFi.softAPgetStationNum();
  if (heim && jetzt - heim_seit > HEIM_STABIL_MS && geraete == 0 && !sitzung.connected()) {
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
      sitzung.write(telnet::BEGRUESSUNG, sizeof(telnet::BEGRUESSUNG));
      letzte_eingabe = millis();
    }
  }
  if (sitzung && !sitzung.connected()) sitzung.stop();
  if (sitzung.connected() && millis() - letzte_eingabe > SITZUNG_LEERLAUF_MS) sitzung.stop();
}

static void weiterleiten() {
  static uint8_t ein[256], aus[512];
  // Telnet -> Pi
  if (sitzung.connected()) {
    size_t n = sitzung.available();
    if (n) {
      if (n > sizeof(ein)) n = sizeof(ein);
      n = sitzung.read(ein, n);
      size_t k = filter.eingang(ein, n, aus);
      if (k) Serial.write(aus, k);
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

void setup() {
  Serial.setRxBufferSize(1024);
  Serial.begin(BAUD);
  Serial.setDebugOutput(false);
  konfig_laden();
  WiFi.persistent(false);  // WLAN-Daten nicht vom SDK speichern lassen, sie stehen in unserem EEPROM-Bereich
  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  heim_verbinden();
  heim_zuletzt_ok = millis();  // Frist für das Notfall-WLAN läuft ab dem Start
  server.begin();
  server.setNoDelay(true);
  info_server.begin();

  // Nur signierte Firmware annehmen (gilt für jedes Update über das Netz)
  Update.installSignature(&signatur_hash, &signatur_pruefer);
  ArduinoOTA.setHostname(HOSTNAME);
  if (kfg_ok) ArduinoOTA.setPassword(kfg.ota_passwort);
  ArduinoOTA.onStart([]() { sitzung.stop(); });  // Konsole während des Updates trennen
  if (kfg_ok) ArduinoOTA.begin(false);            // ohne mDNS-Werbung; ohne Passwort kein OTA
}

void loop() {
  ArduinoOTA.handle();
  wlan_pflegen();
  sitzung_pflegen();
  info_beantworten();
  weiterleiten();
  delay(1);
}
