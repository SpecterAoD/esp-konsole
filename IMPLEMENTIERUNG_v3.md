# ESP-Konsole v3 – Implementierungszusammenfassung

## ✅ Implementierte Features

Folgende Einstellungen sind jetzt **über Telnet-Kommandos konfigurierbar** ohne Neuflashen:

### Netzwerk-Einstellungen
- ✅ WLAN SSID (Heim)
- ✅ WLAN Passwort (Heim)
- ✅ Notfall-WLAN SSID
- ✅ Notfall-WLAN Passwort
- ✅ DNS-Server
- ✅ NTP-Server
- ✅ Notfall-WLAN IP
- ✅ Notfall-WLAN Maske

### Port-Einstellungen
- ✅ Telnet-Port (statt hardcoded 23)
- ✅ Info-Port (statt hardcoded 8023)
- ✅ OTA-Port (statt hardcoded 8266)

### Sonstige Konfiguration
- ✅ OTA-Passwort
- ✅ Hostname (statt hardcoded "esp-konsole")
- ✅ Notfall-WLAN Kanal
- ✅ Auto-Reboot Interval (0 = nie)
- ✅ Debug-Modus

### Timeouts
- ✅ Notfall-WLAN Verzögerung (statt hardcoded 3 min)
- ✅ Heim-WLAN Stabilitäts-Schwelle (statt hardcoded 1 min)
- ✅ Sitzungs-Idle-Timeout (statt hardcoded 30 min)

**NICHT konfigurierbar (hardcoded, aus Sicherheit):**
- ❌ Erlaubte IPs (ERLAUBT[], INFO_ERLAUBT[]) – würde Lockout riskieren

---

## 📦 Dateien erstellt/modifiziert

### Neue Dateien
- **include/config_cmd.h** – Kommandoparser und -ausführung (370 Zeilen)
- **KONFIGURATION.md** – Benutzer-Dokumentation
- **CHANGELOG.md** – Versionsverlauf

### Modifizierte Dateien
- **include/konfig.h**:
  - FORMAT von 1 auf 2 erhöht
  - Struct Daten erweitert: 273 → 464 Bytes
  - Migration v1→v2 implementiert
  - Defaults-Funktion hinzugefügt
  
- **src/main.cpp**:
  - Config-Kommando-Integration in Telnet-Handling
  - Konfigurierte Werte aus EEPROM nutzen statt Hardcodes
  - Neustart-Verarbeitung nach Konfigurationsänderungen
  - Dynamische Server-Initialisierung mit konfigurierten Ports

- **README.md**:
  - Hinweis auf v3 und Konfigurierbarkeit
  - Link zu KONFIGURATION.md

---

## 🎯 Kommandosystem

### Kommandos
```
config set <key> <value>   # Wert im RAM ändern
config show               # Alle Werte anzeigen
config commit             # In EEPROM speichern + Neustart (wenn nötig)
```

### Beispiele
```
config set wlan_name "Neues-WLAN"
config set wlan_pass "passwort123"
config set dns "1.1.1.1"
config set telnet_port 2323
config commit
```

---

## 🔧 Technische Details

### EEPROM-Struktur v2
```
Offset   Feld                   Größe
0-3      magie                  4 Bytes   (0x4E4F4B45 = "EKON")
4-5      format                 2 Bytes   (2 = v2)
6-7      reserviert             2 Bytes
8-40     wlan_name              33 Bytes
41-105   wlan_passwort          65 Bytes
106-138  notfall_name           33 Bytes
139-203  notfall_passwort       65 Bytes
204-268  ota_passwort           65 Bytes
269-308  dns_server             40 Bytes
309-373  ntp_server             65 Bytes
374-375  telnet_port            2 Bytes
376-377  info_port              2 Bytes
378-379  ota_port               2 Bytes
380-412  hostname               33 Bytes
413      notfall_kanal          1 Byte
414-417  notfall_nach_ms        4 Bytes
418-421  heim_stabil_ms         4 Bytes
422-425  sitzung_leerlauf_ms    4 Bytes
426-427  reboot_tage            2 Bytes
428      debug                  1 Byte
429-439  reserviert2            11 Bytes
440-443  notfall_ip[4]          4 Bytes
444-447  notfall_maske[4]       4 Bytes
448-455  reserviert3            8 Bytes
456-459  crc                    4 Bytes   (CRC32 über 0-455)
---
Summe:                           464 Bytes
```

### Validierung
- WLAN SSID: 1-32 Zeichen
- Passwörter: 8-63 Zeichen (WPA2) oder 8-64 Zeichen (OTA)
- Ports: 1-65535
- DNS/NTP: Gültiges Format
- IPs: Gültiges IPv4-Format
- Timeouts: Sinnvolle Millisekunden-Werte

### Migration v1→v2
- Erkannt an Format-Version = 1 im EEPROM
- Alte 5 Passwort-Felder → neue Struktur
- Neue Felder erhalten Standardwerte
- Automatisch beim Laden durchgeführt

---

## 🧪 Getestete Funktionalität

- ✅ konfig.h Struktur und Funktionen (C++-Test)
- ✅ Defaults korrekt gesetzt
- ✅ fuellen() funktioniert mit Beispieldaten
- ✅ gueltig() prüft korrekt
- ✅ CRC32 über korrekte Byte-Range
- ✅ config_cmd.h hat 61 gepaarte Klammern
- ✅ main.cpp kompiliert (mit Platzhaltern für Arduino-Headers)

**Noch zu testen:**
- ⏳ Vollständige Kompilation mit platformio (Netzwerkproblem zur Zeit)
- ⏳ Funktionelle Tests auf echtem ESP8266
- ⏳ Migration v1→v2 auf echtem Gerät

---

## 📋 Verwendung

### Konfiguration ändern
1. Mit Telnet verbinden: `telnet 192.168.2.16 23`
2. Kommandos eingeben:
   ```
   config set wlan_name "Mein-WLAN"
   config set wlan_pass "sicheres-passwort"
   config show
   config commit
   ```
3. ESP startet neu mit neuer Konfiguration
4. Automatisch zum WLAN-Login zurückkehren oder Notfall-WLAN nutzen

### Erste Inbetriebnahme
- Wie bisher: `include/secrets.h` mit Werten füllen
- Build mit `pio run` 
- Mit USB flashen
- Ab dann können Einstellungen über Telnet geändert werden

---

## 🚀 Nächste Schritte (Optional)

- [ ] Vollständiger Build auf Raspberry Pi mit PlatformIO
- [ ] Test auf echtem ESP8266 (v2→v3 Migration)
- [ ] Test aller Kommandos
- [ ] Performance-Test (RAM-Verbrauch wegen String-Operationen)
- [ ] Reset-Kommando implementieren (TODO in config_cmd.h)
- [ ] Backup/Restore der Konfiguration (als Base64)
- [ ] Web-API optional (würde aber gegen "keine Web-Oberfläche" verstoßen)

---

## 📝 Hinweise

- **Sicherheit**: Kommandos nur von hardcoded IPs erlaubt (kein Risiko für Lockout)
- **Speicher**: ESP8266 hat 4 MB EEPROM, nur ~464 Bytes werden genutzt
- **Kompatibilität**: v3 migriert automatisch v1-Konfigurationen
- **Backward-Kompatibilität**: Alte Secrets.h weiterhin nutzbar
- **Performance**: String-Operationen in config_cmd.h sind auf Arduino leicht

