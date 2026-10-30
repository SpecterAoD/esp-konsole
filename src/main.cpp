// ESP-Konsole: serielle Konsole des piholepi (ttyAMA0, 115200 8N1) per Telnet, Ersatz für esp-link.
// Hardware: Wemos D1 mini im Gehäuse des piholepi, UART0 (TX/RX) an GPIO14/15 des Pi.
//
// Verhalten:
//   - Heim-WLAN: Telnet auf Port 23 nur von ERLAUBT[] (siehe unten).
//   - Heim-WLAN länger als NOTFALL_NACH_MS weg: Notfall-WLAN (WPA2, 1 Gerät), Telnet unter 192.168.4.1:23.
//     Ist das Heim-WLAN wieder da und kein Gerät im Notfall-WLAN, geht das Notfall-WLAN wieder aus.
//   - Eine Telnet-Sitzung gleichzeitig, Trennung nach SITZUNG_LEERLAUF_MS ohne Eingabe.
//   - Keine Weboberfläche. Update über das WLAN (ArduinoOTA, Port 8266) nur mit OTA_PASSWORT,
//     erstes Flashen per USB (siehe README.md).
//   - Auf die serielle Leitung wird NUR weitergeleitet, nie selbst etwas ausgegeben (der Pi würde es
//     als Anmeldeversuch lesen).
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>

#include "secrets.h"
#include "telnet_filter.h"

// ---- Einstellungen ----
static const IPAddress ERLAUBT[] = {
    IPAddress(192, 168, 2, 121),  // Laptop (linuxmint)
    IPAddress(192, 168, 2, 100),  // iPhone
};
static const char* HOSTNAME = "esp-konsole";
static const uint16_t TELNET_PORT = 23;
static const uint32_t BAUD = 115200;
static const uint32_t NOTFALL_NACH_MS = 3UL * 60 * 1000;       // Heim-WLAN so lange weg -> Notfall-WLAN
static const uint32_t HEIM_STABIL_MS = 60UL * 1000;            // so lange wieder da -> Notfall-WLAN aus
static const uint32_t HEIM_VERSUCH_MS = 30UL * 1000;           // Neuversuch Heim-WLAN im Notfallbetrieb
static const uint32_t SITZUNG_LEERLAUF_MS = 30UL * 60 * 1000;  // Telnet-Sitzung ohne Eingabe trennen
static const uint8_t NOTFALL_KANAL = 6;
static const IPAddress NOTFALL_IP(192, 168, 4, 1), NOTFALL_MASKE(255, 255, 255, 0);
// -----------------------

static WiFiServer server(TELNET_PORT);
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

static void heim_verbinden() {
  WiFi.begin(WLAN_NAME, WLAN_PASSWORT);
  letzter_versuch = millis();
}

static void notfall_ein() {
  // Automatisches Wiederverbinden aus: sonst springt der ESP beim Suchen über alle Kanäle und
  // das Notfall-WLAN bricht ständig ab. Neuversuche steuert wlan_pflegen().
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(NOTFALL_IP, NOTFALL_IP, NOTFALL_MASKE);
  WiFi.softAP(NOTFALL_NAME, NOTFALL_PASSWORT, NOTFALL_KANAL, false, 1);
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
    if (sitzung.connected() || !erlaubt(neu.remoteIP())) {
      neu.stop();  // belegt oder nicht erlaubt: ohne Meldung trennen
    } else {
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
  WiFi.persistent(false);  // nichts im Flash speichern, alles kommt aus secrets.h
  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  heim_verbinden();
  heim_zuletzt_ok = millis();  // Frist für das Notfall-WLAN läuft ab dem Start
  server.begin();
  server.setNoDelay(true);

  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORT);
  ArduinoOTA.onStart([]() { sitzung.stop(); });  // Konsole während des Updates trennen
  ArduinoOTA.begin(false);                        // ohne mDNS-Werbung im Netz
}

void loop() {
  ArduinoOTA.handle();
  wlan_pflegen();
  sitzung_pflegen();
  weiterleiten();
  delay(1);
}
