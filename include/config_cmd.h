// Telnet-Kommandos zur Konfiguration (nur von erlaubten IPs)
#pragma once
#include <Arduino.h>
#include "konfig.h"

namespace config_cmd {

// Validierung für verschiedene Feldtypen
inline bool validiere_string(const String& wert, size_t min_len, size_t max_len, const char* feldname, String& fehler) {
  size_t len = wert.length();
  if (len < min_len || len > max_len) {
    fehler = String(feldname) + ": " + String(min_len) + "-" + String(max_len) + " Zeichen";
    return false;
  }
  return true;
}

inline bool validiere_ip(const String& wert, String& fehler) {
  int teile[4];
  if (sscanf(wert.c_str(), "%d.%d.%d.%d", &teile[0], &teile[1], &teile[2], &teile[3]) != 4) {
    fehler = "Ungültige IP";
    return false;
  }
  for (int i = 0; i < 4; i++) {
    if (teile[i] < 0 || teile[i] > 255) {
      fehler = "IP-Oktett außerhalb 0-255";
      return false;
    }
  }
  return true;
}

inline bool validiere_port(const String& wert, String& fehler) {
  int p = wert.toInt();
  if (p <= 0 || p > 65535) {
    fehler = "Port muss 1-65535 sein";
    return false;
  }
  return true;
}

inline bool validiere_uint32(const String& wert, uint32_t min_val, uint32_t max_val, String& fehler) {
  char* endptr;
  uint32_t val = strtoul(wert.c_str(), &endptr, 10);
  if (*endptr != '\0' || val < min_val || val > max_val) {
    fehler = String(min_val) + "-" + String(max_val);
    return false;
  }
  return true;
}

// Parsed ein Kommando und gibt zurück: Erfolgreich?
// Formate:
//   config set <key> <value>
//   config show
//   config commit
inline bool parse_und_execute(const String& input, konfig::Daten& kfg, String& ausgabe, bool& neustart_noetig) {
  ausgabe = "";
  neustart_noetig = false;
  
  String line = input;
  line.trim();
  
  // Einfaches Parsing: auf Spaces prüfen
  int space1 = line.indexOf(' ');
  if (space1 < 0) return false;
  
  String cmd0 = line.substring(0, space1);
  if (cmd0 != "config") return false;
  
  String rest = line.substring(space1 + 1);
  rest.trim();
  
  int space2 = rest.indexOf(' ');
  String cmd = (space2 >= 0) ? rest.substring(0, space2) : rest;
  String args = (space2 >= 0) ? rest.substring(space2 + 1) : "";
  args.trim();
  
  String fehler;
  
  // ---- config set key value ----
  if (cmd == "set") {
    int space = args.indexOf(' ');
    if (space <= 0) {
      ausgabe = "Syntax: config set <key> <value>";
      return true;
    }
    
    String key = args.substring(0, space);
    String value = args.substring(space + 1);
    value.trim();
    
    // WLAN-Einstellungen
    if (key == "wlan_name") {
      if (!validiere_string(value, 1, 32, "WLAN SSID", fehler)) {
        ausgabe = "Fehler: " + fehler;
        return true;
      }
      strncpy(kfg.wlan_name, value.c_str(), sizeof kfg.wlan_name - 1);
      neustart_noetig = true;
      ausgabe = "WLAN SSID gesetzt (Neustart erforderlich)";
      return true;
    }
    
    if (key == "wlan_pass") {
      if (!validiere_string(value, 8, 63, "WLAN-Passwort", fehler)) {
        ausgabe = "Fehler: " + fehler;
        return true;
      }
      strncpy(kfg.wlan_passwort, value.c_str(), sizeof kfg.wlan_passwort - 1);
      neustart_noetig = true;
      ausgabe = "WLAN-Passwort gesetzt (Neustart erforderlich)";
      return true;
    }
    
    // Notfall-WLAN
    if (key == "notfall_name") {
      if (!validiere_string(value, 1, 32, "Notfall-SSID", fehler)) {
        ausgabe = "Fehler: " + fehler;
        return true;
      }
      strncpy(kfg.notfall_name, value.c_str(), sizeof kfg.notfall_name - 1);
      neustart_noetig = true;
      ausgabe = "Notfall-WLAN SSID gesetzt (Neustart erforderlich)";
      return true;
    }
    
    if (key == "notfall_pass") {
      if (!validiere_string(value, 8, 63, "Notfall-Passwort", fehler)) {
        ausgabe = "Fehler: " + fehler;
        return true;
      }
      strncpy(kfg.notfall_passwort, value.c_str(), sizeof kfg.notfall_passwort - 1);
      neustart_noetig = true;
      ausgabe = "Notfall-Passwort gesetzt (Neustart erforderlich)";
      return true;
    }
    
    // OTA-Passwort
    if (key == "ota_pass") {
      if (!validiere_string(value, 8, 64, "OTA-Passwort", fehler)) {
        ausgabe = "Fehler: " + fehler;
        return true;
      }
      strncpy(kfg.ota_passwort, value.c_str(), sizeof kfg.ota_passwort - 1);
      ausgabe = "OTA-Passwort gesetzt";
      return true;
    }
    
    // DNS und NTP
    if (key == "dns") {
      if (!validiere_string(value, 7, 39, "DNS-Server", fehler)) {
        ausgabe = "Fehler: " + fehler;
        return true;
      }
      if (!validiere_ip(value, fehler)) {
        ausgabe = "Fehler DNS: " + fehler;
        return true;
      }
      strncpy(kfg.dns_server, value.c_str(), sizeof kfg.dns_server - 1);
      neustart_noetig = true;
      ausgabe = "DNS-Server gesetzt (Neustart erforderlich)";
      return true;
    }
    
    if (key == "ntp") {
      if (!validiere_string(value, 1, 64, "NTP-Server", fehler)) {
        ausgabe = "Fehler: " + fehler;
        return true;
      }
      strncpy(kfg.ntp_server, value.c_str(), sizeof kfg.ntp_server - 1);
      neustart_noetig = true;
      ausgabe = "NTP-Server gesetzt (Neustart erforderlich)";
      return true;
    }
    
    // Ports
    if (key == "telnet_port") {
      if (!validiere_port(value, fehler)) {
        ausgabe = "Fehler: " + fehler;
        return true;
      }
      kfg.telnet_port = value.toInt();
      neustart_noetig = true;
      ausgabe = "Telnet-Port gesetzt (Neustart erforderlich)";
      return true;
    }
    
    if (key == "info_port") {
      if (!validiere_port(value, fehler)) {
        ausgabe = "Fehler: " + fehler;
        return true;
      }
      kfg.info_port = value.toInt();
      neustart_noetig = true;
      ausgabe = "Info-Port gesetzt (Neustart erforderlich)";
      return true;
    }
    
    if (key == "ota_port") {
      if (!validiere_port(value, fehler)) {
        ausgabe = "Fehler: " + fehler;
        return true;
      }
      kfg.ota_port = value.toInt();
      neustart_noetig = true;
      ausgabe = "OTA-Port gesetzt (Neustart erforderlich)";
      return true;
    }
    
    // Hostname
    if (key == "hostname") {
      if (!validiere_string(value, 1, 32, "Hostname", fehler)) {
        ausgabe = "Fehler: " + fehler;
        return true;
      }
      strncpy(kfg.hostname, value.c_str(), sizeof kfg.hostname - 1);
      neustart_noetig = true;
      ausgabe = "Hostname gesetzt (Neustart erforderlich)";
      return true;
    }
    
    // Notfall-WLAN Kanal
    if (key == "notfall_kanal") {
      int kanal = value.toInt();
      if (kanal < 1 || kanal > 13) {
        ausgabe = "Fehler: Kanal 1-13";
        return true;
      }
      kfg.notfall_kanal = kanal;
      neustart_noetig = true;
      ausgabe = "Notfall-WLAN Kanal gesetzt (Neustart erforderlich)";
      return true;
    }
    
    // Notfall-WLAN IP
    if (key == "notfall_ip") {
      if (!validiere_ip(value, fehler)) {
        ausgabe = "Fehler: " + fehler;
        return true;
      }
      sscanf(value.c_str(), "%hhu.%hhu.%hhu.%hhu", 
             &kfg.notfall_ip[0], &kfg.notfall_ip[1], &kfg.notfall_ip[2], &kfg.notfall_ip[3]);
      neustart_noetig = true;
      ausgabe = "Notfall-WLAN IP gesetzt (Neustart erforderlich)";
      return true;
    }
    
    // Notfall-WLAN Maske
    if (key == "notfall_maske") {
      if (!validiere_ip(value, fehler)) {
        ausgabe = "Fehler: " + fehler;
        return true;
      }
      sscanf(value.c_str(), "%hhu.%hhu.%hhu.%hhu",
             &kfg.notfall_maske[0], &kfg.notfall_maske[1], &kfg.notfall_maske[2], &kfg.notfall_maske[3]);
      neustart_noetig = true;
      ausgabe = "Notfall-WLAN Maske gesetzt (Neustart erforderlich)";
      return true;
    }
    
    // Timeouts
    if (key == "notfall_nach_ms") {
      if (!validiere_uint32(value, 60000, 3600000, fehler)) {  // 1 min - 1 hour
        ausgabe = "Fehler: 60000-3600000 ms";
        return true;
      }
      kfg.notfall_nach_ms = strtoul(value.c_str(), nullptr, 10);
      neustart_noetig = true;
      ausgabe = "Notfall-WLAN Verzögerung gesetzt (Neustart erforderlich)";
      return true;
    }
    
    if (key == "heim_stabil_ms") {
      if (!validiere_uint32(value, 10000, 600000, fehler)) {  // 10 sec - 10 min
        ausgabe = "Fehler: 10000-600000 ms";
        return true;
      }
      kfg.heim_stabil_ms = strtoul(value.c_str(), nullptr, 10);
      neustart_noetig = true;
      ausgabe = "Heim-WLAN Stabilitäts-Schwelle gesetzt (Neustart erforderlich)";
      return true;
    }
    
    if (key == "sitzung_leerlauf_ms") {
      if (!validiere_uint32(value, 60000, 7200000, fehler)) {  // 1 min - 2 hours
        ausgabe = "Fehler: 60000-7200000 ms";
        return true;
      }
      kfg.sitzung_leerlauf_ms = strtoul(value.c_str(), nullptr, 10);
      ausgabe = "Sitzungs-Timeout gesetzt";
      return true;
    }
    
    // Reboot-Intervall
    if (key == "reboot_tage") {
      int tage = value.toInt();
      if (tage < 0 || tage > 365) {
        ausgabe = "Fehler: 0-365 Tage";
        return true;
      }
      kfg.reboot_tage = tage;
      ausgabe = String("Auto-Reboot ") + (tage == 0 ? "deaktiviert" : "auf " + String(tage) + " Tage gesetzt");
      return true;
    }
    
    // Debug
    if (key == "debug") {
      int dbg = value.toInt();
      if (dbg < 0 || dbg > 1) {
        ausgabe = "Fehler: 0 oder 1";
        return true;
      }
      kfg.debug = dbg;
      ausgabe = String("Debug ") + (dbg ? "aktiviert" : "deaktiviert");
      return true;
    }
    
    ausgabe = "Fehler: Unbekannter Schlüssel '" + key + "'";
    return true;
  }
  
  // ---- config show [key] ----
  if (cmd == "show") {
    ausgabe = "\r\n=== ESP-Konsole Konfiguration ===\r\n";
    ausgabe += String("WLAN SSID:              ") + kfg.wlan_name + "\r\n";
    ausgabe += String("WLAN Passwort:          ") + String(strlen(kfg.wlan_passwort)) + " Zeichen\r\n";
    ausgabe += String("Notfall-WLAN SSID:      ") + kfg.notfall_name + "\r\n";
    ausgabe += String("Notfall-Passwort:       ") + String(strlen(kfg.notfall_passwort)) + " Zeichen\r\n";
    ausgabe += String("OTA-Passwort:           ") + String(strlen(kfg.ota_passwort)) + " Zeichen\r\n";
    ausgabe += String("DNS-Server:             ") + kfg.dns_server + "\r\n";
    ausgabe += String("NTP-Server:             ") + kfg.ntp_server + "\r\n";
    ausgabe += String("Hostname:               ") + kfg.hostname + "\r\n";
    ausgabe += String("Telnet-Port:            ") + String(kfg.telnet_port) + "\r\n";
    ausgabe += String("Info-Port:              ") + String(kfg.info_port) + "\r\n";
    ausgabe += String("OTA-Port:               ") + String(kfg.ota_port) + "\r\n";
    ausgabe += String("Notfall-WLAN Kanal:     ") + String(kfg.notfall_kanal) + "\r\n";
    ausgabe += String("Notfall-IP:             ");
    ausgabe += String(kfg.notfall_ip[0]) + "." + String(kfg.notfall_ip[1]) + "." + 
              String(kfg.notfall_ip[2]) + "." + String(kfg.notfall_ip[3]) + "\r\n";
    ausgabe += String("Notfall-Maske:          ");
    ausgabe += String(kfg.notfall_maske[0]) + "." + String(kfg.notfall_maske[1]) + "." + 
              String(kfg.notfall_maske[2]) + "." + String(kfg.notfall_maske[3]) + "\r\n";
    ausgabe += String("Notfall-Verzögerung:    ") + String(kfg.notfall_nach_ms / 1000) + " s\r\n";
    ausgabe += String("Heim-Stabilitäts-Zeit:  ") + String(kfg.heim_stabil_ms / 1000) + " s\r\n";
    ausgabe += String("Sitzungs-Timeout:       ") + String(kfg.sitzung_leerlauf_ms / 1000 / 60) + " min\r\n";
    ausgabe += String("Auto-Reboot:            ") + (kfg.reboot_tage == 0 ? "deaktiviert" : String(kfg.reboot_tage) + " Tage") + "\r\n";
    ausgabe += String("Debug:                  ") + (kfg.debug ? "an" : "aus") + "\r\n";
    ausgabe += "=====================================\r\n";
    return true;
  }
  
  // ---- config commit ----
  if (cmd == "commit") {
    kfg.crc = konfig::pruefsumme(kfg);
    ausgabe = "Konfiguration wurde in EEPROM geschrieben.";
    if (neustart_noetig) {
      ausgabe += " Neustart wird durchgeführt...";
    }
    return true;
  }
  
  // ---- config reset ----
  if (cmd == "reset") {
    ausgabe = "reset nicht implementiert (TODO)";
    return true;
  }
  
  ausgabe = "Unbekanntes Kommando: " + cmd;
  return true;
}

}  // namespace config_cmd
