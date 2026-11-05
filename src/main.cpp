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

// ---- Einstellungen (Defaults, überschrieben durch EEPROM) ----
static const IPAddress ERLAUBT[] = {
    IPAddress(192, 168, 2, 121),  // Laptop (linuxmint)
    IPAddress(192, 168, 2, 100),  // iPhone
};
static const IPAddress INFO_ERLAUBT[] = {
    IPAddress(192, 168, 2, 6),  // piholepi (Update-Prüfung, Überwachung)
};
static const uint32_t BAUD = 115200;
static const uint32_t HEIM_VERSUCH_MS = 30UL * 1000;  // Neuversuch Heim-WLAN im Notfallbetrieb
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
static bool neustart_noetig = false;
static uint32_t reboot_timer = 0;

static bool im_notfallnetz(const IPAddress& ip) {
  if (!notfall_an) return false;
  IPAddress nf_ip(kfg.notfall_ip[0], kfg.notfall_ip[1], kfg.notfall_ip[2], kfg.notfall_ip[3]);
  IPAddress nf_maske(kfg.notfall_maske[0], kfg.notfall_maske[1], kfg.notfall_maske[2], kfg.notfall_maske[3]);
  return (uint32_t(ip) & uint32_t(nf_maske)) == (uint32_t(nf_ip) & uint32_t(nf_maske));
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

static void konfig_laden() {
  EEPROM.begin(sizeof(konfig::Daten));
  EEPROM.get(0, kfg);
  
  // Versuchen v1-Format zu migrieren
  uint16_t fmt = *(const uint16_t*)((uint8_t*)&kfg + 4);
  if (fmt == 1 && !konfig::gueltig(kfg)) {
    uint8_t alt_buf[273];
    EEPROM.get(0, alt_buf);
    if (konfig::migrieren_v1_zu_v2(alt_buf, kfg)) {
      EEPROM.put(0, kfg);
      EEPROM.commit();
    }
  }
  
  bool im_eeprom = konfig::gueltig(kfg);
#ifdef KONFIG_ERZWINGEN
  bool schreiben = true;
#else
  bool schreiben = !im_eeprom;
#endif
  konfig::Daten neu;
  if (schreiben && konfig::fuellen(neu, WLAN_NAME, WLAN_PASSWORT, NOTFALL_NAME, NOTFALL_PASSWORT, OTA_PASSWORT)) {
    if (!im_eeprom || memcmp(&neu, &kfg, offsetof(konfig::Daten, dns_server)) != 0) {
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
           kfg.hostname, FW_VERSION, (unsigned long)(millis() / 1000), WiFi.RSSI(), ESP.getFreeHeap(),
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
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_AP_STA);
  IPAddress nf_ip(kfg.notfall_ip[0], kfg.notfall_ip[1], kfg.notfall_ip[2], kfg.notfall_ip[3]);
  IPAddress nf_maske(kfg.notfall_maske[0], kfg.notfall_maske[1], kfg.notfall_maske[2], kfg.notfall_maske[3]);
  WiFi.softAPConfig(nf_ip, nf_ip, nf_maske);
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
  if (!heim && geraete == 0 && jetzt - letzter_versuch > HEIM_VERSUCH_MS) heim_verbinden();
}

static void sitzung_pflegen() {
  if (server.hasClient()) {
    WiFiClient neu = server.accept();
    if (!erlaubt(neu.remoteIP())) {
      neu.stop();
    } else {
      if (sitzung.connected()) sitzung.stop();
      sitzung = neu;
      sitzung.setNoDelay(true);
      filter.zuruecksetzen();
      sitzung.write(telnet::BEGRUESSUNG, sizeof(telnet::BEGRUESSUNG));
      letzte_eingabe = millis();
    }
  }
  if (sitzung && !sitzung.connected()) sitzung.stop();
  if (sitzung.connected() && millis() - letzte_eingabe > kfg.sitzung_leerlauf_ms) sitzung.stop();
}

static void weiterleiten() {
  static uint8_t ein[256], aus[512];
  static String cmd_buffer;
  
  // Telnet -> Pi (mit Config-Command-Check)
  if (sitzung.connected()) {
    size_t n = sitzung.available();
    if (n) {
      if (n > sizeof(ein)) n = sizeof(ein);
      n = sitzung.read(ein, n);
      
      // Auf Konfigurationskommandos prüfen (aber nur für erlaubte IPs)
      bool ist_konfigcommand = false;
      for (size_t i = 0; i < n; i++) {
        if (ein[i] == '\r' || ein[i] == '\n') {
          if (cmd_buffer.startsWith("config ")) {
            String ausgabe;
            bool nrestart = false;
            config_cmd::parse_und_execute(cmd_buffer, kfg, ausgabe, nrestart);
            if (!ausgabe.isEmpty()) {
              sitzung.write((const uint8_t*)"\r\n", 2);
              sitzung.write((const uint8_t*)ausgabe.c_str(), ausgabe.length());
              sitzung.write((const uint8_t*)"\r\n", 2);
            }
            if (nrestart) {
              neustart_noetig = true;
              reboot_timer = millis() + 2000;  // 2 Sekunden Verzögerung
            }
            cmd_buffer = "";
            ist_konfigcommand = true;
          } else {
            cmd_buffer = "";
          }
        } else if (ein[i] >= 32 && ein[i] < 127) {
          cmd_buffer += (char)ein[i];
        } else {
          cmd_buffer = "";
        }
      }
      
      // Wenn nicht nur Config-Kommando: an Pi weitergeben
      if (!ist_konfigcommand) {
        size_t k = filter.eingang(ein, n, aus);
        if (k) Serial.write(aus, k);
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

void setup() {
  Serial.setRxBufferSize(1024);
  Serial.begin(BAUD);
  Serial.setDebugOutput(false);
  konfig_laden();
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(kfg.hostname);
  WiFi.setAutoReconnect(true);
  heim_verbinden();
  heim_zuletzt_ok = millis();
  
  // Server mit konfigurierten Ports starten
  if (server) server.stop();
  if (info_server) info_server.stop();
  server = WiFiServer(kfg.telnet_port);
  info_server = WiFiServer(kfg.info_port);
  server.begin();
  server.setNoDelay(true);
  info_server.begin();

  Update.installSignature(&signatur_hash, &signatur_pruefer);
  ArduinoOTA.setHostname(kfg.hostname);
  ArduinoOTA.setPort(kfg.ota_port);
  if (kfg_ok) ArduinoOTA.setPassword(kfg.ota_passwort);
  ArduinoOTA.onStart([]() { sitzung.stop(); });
  if (kfg_ok) ArduinoOTA.begin(false);
}

void loop() {
  // Neustart, wenn Konfiguration das erfordert
  if (neustart_noetig && millis() > reboot_timer) {
    EEPROM.begin(sizeof(konfig::Daten));
    EEPROM.put(0, kfg);
    EEPROM.commit();
    EEPROM.end();
    sitzung.stop();
    delay(500);
    ESP.restart();
  }
  
  ArduinoOTA.handle();
  wlan_pflegen();
  sitzung_pflegen();
  info_beantworten();
  weiterleiten();
  delay(1);
}
