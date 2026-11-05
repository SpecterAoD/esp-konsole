# ESP-Konsole: Konfiguration ohne Neuflashen

Ab Version 3 können alle Einstellungen direkt über Telnet geändert werden, ohne die Firmware neu zu flashen.

## Kommandos

Alle Kommandos müssen von einer erlaubten IP-Adresse (`ERLAUBT[]` in `main.cpp`) gesendet werden.

### config set <key> <value>

Setzt einen Konfigurationswert im RAM. Der Wert wird **nicht automatisch** gespeichert!

**Beispiele:**

```
config set wlan_name "Mein-WLAN"
config set wlan_pass "mein-passwort"
config set notfall_name "ESP-Notfall"
config set notfall_pass "notfall-passwort"
config set ota_pass "ota-passwort"
config set dns "1.1.1.1"
config set ntp "time.cloudflare.com"
config set telnet_port 23
config set info_port 8023
config set ota_port 8266
config set hostname "esp-konsole"
config set notfall_kanal 6
config set notfall_ip "192.168.4.1"
config set notfall_maske "255.255.255.0"
config set notfall_nach_ms 180000
config set heim_stabil_ms 60000
config set sitzung_leerlauf_ms 1800000
config set reboot_tage 30
config set debug 0
```

**Validierung:**
- WLAN SSID: 1-32 Zeichen
- WLAN-Passwort: 8-63 Zeichen (WPA2)
- OTA-Passwort: 8-64 Zeichen
- Notfall-WLAN SSID: 1-32 Zeichen
- Notfall-Passwort: 8-63 Zeichen
- DNS/NTP-Server: gültiges Format
- Ports: 1-65535
- Hostname: 1-32 Zeichen
- Notfall-WLAN Kanal: 1-13
- IPs und Masken: gültiges IPv4-Format
- Timeouts: sinnvolle Millisekunden-Werte

### config show

Zeigt alle aktuellen Konfigurationswerte an.

```
config show
```

**Ausgabe:**
```
=== ESP-Konsole Konfiguration ===
WLAN SSID:              Mein-WLAN
WLAN Passwort:          8 Zeichen
DNS-Server:             8.8.8.8
NTP-Server:             pool.ntp.org
Hostname:               esp-konsole
Telnet-Port:            23
Info-Port:              8023
OTA-Port:               8266
...
=====================================
```

### config commit

Speichert alle mit `config set` gemachten Änderungen ins EEPROM und führt einen Neustart durch (falls erforderlich).

```
config commit
```

Der Neustart wird **2 Sekunden nach** dem Kommando ausgelöst, sodass der Nutzer noch eine Bestätigung sieht.

## Änderungsverhalten

| Einstellung | RAM | Neustart? | Speicherung |
|-------------|-----|-----------|-------------|
| WLAN SSID | ✓ | ✓ | EEPROM |
| WLAN-Passwort | ✓ | ✓ | EEPROM |
| Notfall-WLAN SSID | ✓ | ✓ | EEPROM |
| Notfall-Passwort | ✓ | ✓ | EEPROM |
| OTA-Passwort | ✓ | ✗ | EEPROM |
| DNS-Server | ✓ | ✓ | EEPROM |
| NTP-Server | ✓ | ✓ | EEPROM |
| Hostname | ✓ | ✓ | EEPROM |
| Telnet-Port | ✓ | ✓ | EEPROM |
| Info-Port | ✓ | ✓ | EEPROM |
| OTA-Port | ✓ | ✓ | EEPROM |
| Notfall-WLAN Kanal | ✓ | ✓ | EEPROM |
| Notfall-IP | ✓ | ✓ | EEPROM |
| Notfall-Maske | ✓ | ✓ | EEPROM |
| Notfall-Verzögerung | ✓ | ✗ | EEPROM |
| Heim-Stabilitäts-Zeit | ✓ | ✗ | EEPROM |
| Sitzungs-Timeout | ✓ | ✗ | EEPROM |
| Auto-Reboot-Interval | ✓ | ✗ | EEPROM |
| Debug-Modus | ✓ | ✗ | EEPROM |

## Workflow: Passwörter ändern

```telnet
telnet 192.168.2.16 23
> config set wlan_pass "neues-passwort"
> config set ota_pass "neues-ota-passwort"
> config commit
```

Der ESP startet neu und verbindet sich mit dem neuen WLAN-Passwort.

## Fehlerbehandlung

Ungültige Werte werden mit einer Fehlermeldung quittiert:
```
Fehler: WLAN SSID: 1-32 Zeichen
```

Der Wert wird **nicht** gespeichert. `config show` zeigt den alten Wert.

## Backward-Kompatibilität

Version 3 migriert automatisch Konfigurationen von Version 1 (Format im EEPROM) zur neuen Struktur (Format 2).

- Alte Zugangsdaten bleiben erhalten
- Neue Felder erhalten Standardwerte
- Migration findet beim ersten Start statt

## Sicherheit

- Nur von `ERLAUBT[]`-Adressen möglich (siehe `src/main.cpp`)
- Passwörter werden **nicht** im Klartext über Telnet übertragen (Passwortlänge wird in `config show` angezeigt)
- EEPROM-Werte sind mit CRC32-Prüfsumme versehen
- Kein Web-Interface, keine weiteren Angriffsvektoren

## Troubleshooting

**Frage:** Das Telnet-Kommando wird an der Pi-Konsole weitergeleitet statt als Konfigurationskommando behandelt.

**Antwort:** `config`-Kommandos müssen **exakt** mit `config ` beginnen. Sie werden vor der Weiterleitung an den Pi abgefangen.

**Frage:** Änderungen werden nicht übernommen.

**Antwort:** `config commit` erforderlich, um in den EEPROM zu schreiben. Ohne `commit` sind Änderungen nur im RAM.

**Frage:** Neustart dauert länger als 2 Sekunden.

**Antwort:** Verzögerung ist beabsichtigt, damit der Nutzer in der Telnet-Sitzung die Bestätigung sieht, bevor die Verbindung getrennt wird.
