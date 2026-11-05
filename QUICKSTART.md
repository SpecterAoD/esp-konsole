# ESP-Konsole v3 – Quick Start

## 🚀 Schnelleinstieg: Konfiguration ändern ohne Neuflashen

### 1. Mit Telnet verbinden
```bash
telnet 192.168.2.16 23
```

**oder von einem anderen Gerät im Netz:**
```bash
telnet 192.168.2.16 23  # Muss in ERLAUBT[] sein (siehe src/main.cpp)
```

### 2. Konfiguration anschauen
```telnet
config show
```

**Ausgabe:**
```
=== ESP-Konsole Konfiguration ===
WLAN SSID:              MyWiFi
WLAN Passwort:          8 Zeichen
Notfall-WLAN SSID:      ESP-Notfall
...
```

### 3. Einstellungen ändern

**WLAN-Daten aktualisieren:**
```telnet
config set wlan_name "Neues-WLAN"
config set wlan_pass "neus-passwort"
```

**DNS-Server ändern:**
```telnet
config set dns "1.1.1.1"
```

**NTP-Server ändern:**
```telnet
config set ntp "time.nist.gov"
```

**Ports ändern:**
```telnet
config set telnet_port 2323
config set ota_port 8268
```

**Hostname anpassen:**
```telnet
config set hostname "pi-console"
```

### 4. Änderungen speichern
```telnet
config commit
```

Der ESP wird **automatisch neugestartet** (nach 2 Sekunden), wenn Änderungen einen Reboot erfordern.

---

## ⚙️ Alle möglichen Einstellungen

| Befehl | Beispiel | Beschreibung |
|--------|----------|-------------|
| `config set wlan_name` | `config set wlan_name "Mein-WLAN"` | Heim-WLAN SSID (1-32 Zeichen) |
| `config set wlan_pass` | `config set wlan_pass "passwort123"` | Heim-WLAN Passwort (8-63 Zeichen) |
| `config set notfall_name` | `config set notfall_name "ESP-Notfall"` | Notfall-WLAN SSID |
| `config set notfall_pass` | `config set notfall_pass "notfallpass123"` | Notfall-WLAN Passwort |
| `config set ota_pass` | `config set ota_pass "otapass123"` | OTA-Update Passwort |
| `config set dns` | `config set dns "8.8.8.8"` | DNS-Server |
| `config set ntp` | `config set ntp "pool.ntp.org"` | NTP-Server |
| `config set telnet_port` | `config set telnet_port 23` | Telnet-Port |
| `config set info_port` | `config set info_port 8023` | Info-Port |
| `config set ota_port` | `config set ota_port 8266` | OTA-Update Port |
| `config set hostname` | `config set hostname "esp-device"` | Netzwerk-Hostname |
| `config set notfall_kanal` | `config set notfall_kanal 6` | WLAN-Kanal (1-13) |
| `config set notfall_ip` | `config set notfall_ip "192.168.4.1"` | Notfall-AP IP-Adresse |
| `config set notfall_maske` | `config set notfall_maske "255.255.255.0"` | Notfall-AP Netzmaske |
| `config set notfall_nach_ms` | `config set notfall_nach_ms 180000` | Verzögerung bis Notfall-WLAN (ms) |
| `config set heim_stabil_ms` | `config set heim_stabil_ms 60000` | Heim-WLAN Stabilitäts-Zeit (ms) |
| `config set sitzung_leerlauf_ms` | `config set sitzung_leerlauf_ms 1800000` | Session-Timeout (ms) |
| `config set reboot_tage` | `config set reboot_tage 30` | Auto-Reboot nach X Tagen (0=nie) |
| `config set debug` | `config set debug 1` | Debug-Modus (0/1) |

---

## 🔄 Typische Workflows

### Workflow 1: Neues WLAN verbinden
```telnet
config set wlan_name "Neues-WLAN-Name"
config set wlan_pass "neues-passwort"
config commit
```
→ ESP startet neu, verbindet sich mit neuem WLAN

### Workflow 2: Notfall-WLAN Einstellungen anpassen
```telnet
config set notfall_name "Laptop-Notfall"
config set notfall_pass "lange-zufallszeichenfolge"
config commit
```
→ Neuer Name und Passwort für Notfall-WLAN

### Workflow 3: DNS und Zeit-Server ändern
```telnet
config set dns "1.1.1.1"
config set ntp "de.pool.ntp.org"
config commit
```
→ Bessere Netzwerk- und Zeitsynchronisation

### Workflow 4: Telnet auf anderen Port verschieben
```telnet
config set telnet_port 2323
config commit
```
→ Ab jetzt: `telnet 192.168.2.16 2323`

### Workflow 5: Mehrere ESPs im Netz (unterschiedliche Hostnamen)
```telnet
# ESP 1
config set hostname "esp-pi1"
config commit

# ESP 2
config set hostname "esp-pi2"
config commit
```

---

## ⚠️ Wichtige Hinweise

### Validierung
- **Ungültige Eingaben** werden mit Fehlermeldung quittiert:
  ```
  Fehler: WLAN SSID: 1-32 Zeichen
  ```
  Der Wert wird **nicht** gespeichert.

### Neustart erforderlich?
- Einstellungen wie WLAN, Ports, DNS → **Neustart erforderlich**
- Einstellungen wie OTA-Passwort, Debug → **Kein Neustart**
- Wird automatisch erkannt und durchgeführt

### Sicherheit
- ✅ Nur von erlaubten IPs möglich (ERLAUBT[] in `src/main.cpp`)
- ✅ Keine Passwort-Echos in `config show` (nur Zeichenzahl)
- ✅ CRC32-Prüfsummen schützen vor Datenverschädigung

### RAM vs. EEPROM
- `config set` ändert nur den **RAM** (nicht persistent)
- `config commit` speichert ins **EEPROM** (permanent)
- Ohne `commit` gehen Änderungen bei Neustart verloren!

---

## 🛠️ Troubleshooting

### Problem: "config set" Kommando wird an Pi weitergeleitet
**Lösung:** Kommando muss exakt mit `config ` beginnen (mit Space). Keine Typos!

### Problem: Änderung wird nicht übernommen
**Lösung:** `config commit` nicht vergessen!

### Problem: "Fehler: Ungültiger Schlüssel"
**Lösung:** Schlüsselnamen prüfen (z.B. `wlan_pass` nicht `wlan_password`)

### Problem: IP-Adresse wird nicht akzeptiert
**Lösung:** Gültiges IPv4-Format: `192.168.4.1` (Oktette 0-255)

### Problem: Port-Nummer nicht änderbar
**Lösung:** Port muss zwischen 1 und 65535 liegen

### Problem: Verliere Verbindung nach Neustart
**Lösung:** Neue Telnet-Verbindung starten. Die alte wird bei Neustart getrennt.

---

## ℹ️ Weitere Ressourcen

- Vollständige Dokumentation: siehe `KONFIGURATION.md`
- Technische Details: siehe `IMPLEMENTIERUNG_v3.md`
- Changelog: siehe `CHANGELOG.md`
- Quellcode: `include/config_cmd.h`, `include/konfig.h`, `src/main.cpp`

---

**Version**: v3.0.0  
**Datum**: 2026-11-05  
**Firmware**: ESP8266 (Wemos D1 mini)
