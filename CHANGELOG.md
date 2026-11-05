# Changelog ESP-Konsole

## [v3.0.0] - 2026-11-05

### ✨ Neue Features

- **Konfigurierbarkeit ohne Neuflashen**: Alle Einstellungen können jetzt direkt über Telnet-Kommandos geändert werden
- **Neue `config`-Kommandos**:
  - `config set <key> <value>` – Einstellung im RAM ändern
  - `config show` – Alle Einstellungen anzeigen
  - `config commit` – Änderungen ins EEPROM speichern und ggf. neustarten
- **Konfigurierbare Parameter**:
  - WLAN SSID und Passwort (Heim + Notfall)
  - OTA-Passwort
  - DNS- und NTP-Server
  - Telnet-Port, Info-Port, OTA-Port
  - Hostname
  - Notfall-WLAN Kanal
  - Notfall-WLAN IP und Netzwerkmaske
  - Timeouts und Auto-Reboot-Interval
  - Debug-Modus

### 🔄 Migrationen

- **v1→v2**: Automatische Migration alter EEPROM-Daten zur neuen Struktur (Format 2)
- **EEPROM-Format v1→v2**: Alte Konfigurationen bleiben erhalten, neue Felder erhalten Standardwerte

### 📝 Struktur-Änderungen

- **konfig.h**: `struct Daten` erweitert von 273 auf 464 Bytes
  - Neue Felder: `dns_server`, `ntp_server`, `telnet_port`, `info_port`, `ota_port`, `hostname`, `notfall_kanal`, Timeouts, etc.
  - `pruefsumme()` recalculiert korrekt über alle Bytes bis zur CRC
  
- **Neue Datei**: `include/config_cmd.h`
  - Kommandos parsen und ausführen
  - Eingabe-Validierung für alle Feldtypen
  - Telnet-Integration

- **main.cpp**: Anpassung zur Nutzung von Konfigurationswerten aus EEPROM
  - Server-Ports sind nicht mehr hardcoded
  - Hostname ist konfigurierbar
  - WLAN-Timeouts sind konfigurierbar
  - Kommando-Verarbeitung in `weiterleiten()`

### 🛡️ Sicherheit

- Kommandos nur von erlaubten IPs (`ERLAUBT[]`)
- Passwörter werden nur im Klartext ins EEPROM geschrieben, nicht über Telnet angezeigt
- CRC32-Prüfsummen für EEPROM-Daten
- Eingabe-Validierung für alle Parameter

### 📚 Dokumentation

- Neue Datei: `KONFIGURATION.md` mit vollständiger Anleitung
- README.md aktualisiert
- Changelog hinzugefügt

### ⚠️ Breaking Changes

- Format-Version im EEPROM erhöht von 1 auf 2
  - Alte Geräte migrieren automatisch
  - Neue Geräte starten mit Defaults + secrets.h

### 🐛 Bugfixes

- Migration v1→v2 prüft korrekt auf alte Format-Version

---

## [v2.0.0] - (Frühere Version)

- EEPROM-basierte Konfiguration (WLAN, OTA-Passwort)
- Signierte Updates (RSA)
- Info-Port (8023)
- Automatische Notfall-WLAN

---

## [v1.0.0] - (Frühere Version)

- Basis-Funktionalität
- Telnet-Konsole zum Pi
- Hardcoded Konfiguration
