# ESP-Konsole – serielle Konsole des piholepi per Telnet

Ersatz für die esp-link-Firmware auf dem **Wemos D1 mini** im Gehäuse des piholepi
Bis 31.10.2026 lag der Code im Heimnetz-Repository unter `piholepi/esp-konsole` (Historie übernommen).
(esp-link v3.2.47 – eine Alpha-Version – stürzte am 30.10.2026 mehrmals pro Stunde ab, hatte keine Anmeldung und ein schwaches Notfall-WLAN).

| | |
|---|---|
| Heim-WLAN | Telnet `192.168.2.16:23`, nur von den Adressen in `ERLAUBT[]` (`src/main.cpp`: Laptop .121, iPhone .100) |
| Notfall-WLAN | geht an, wenn das Heim-WLAN **3 min** weg ist; WPA2, 1 Gerät, Telnet `192.168.4.1:23`; geht wieder aus, sobald das Heim-WLAN 1 min stabil ist und niemand mehr im Notfall-WLAN ist |
| Sitzung | eine gleichzeitig (eine neue Verbindung übernimmt und trennt die alte), Trennung nach 30 min ohne Eingabe; kein doppeltes Echo (Telnet-Verhandlung: Server übernimmt das Echo) |
| Update | über das WLAN mit ArduinoOTA (Port 8266), nur mit OTA-Passwort **und gültiger RSA-Signatur** (ab v2); erstes Flashen per USB |
| Einstellungen | ab v3 per Telnet änderbar (`~esp-config`, siehe unten): Zugangsdaten, Hostname, Ports, Notfall-WLAN, Zeiten; die Zahlen in dieser Tabelle sind die Standardwerte. `ERLAUBT[]` bleibt fest in der Firmware |
| Zugangsdaten | ab v2 im EEPROM des ESP; eine lokal mit `include/secrets.h` gebaute Firmware schreibt sie beim ersten Start, Releases enthalten keine |
| Info-Port | `8023`: eine Zeile JSON (Version, Laufzeit, RSSI, Speicher, Neustartgrund, Notfall, Sitzung, Herkunft der Einstellungen) für Laptop, iPhone und piholepi |
| Weboberfläche | keine |
| Serielle Leitung | 115200 8N1, UART0; die Firmware schreibt selbst **nichts** auf die Leitung (der Pi würde es als Anmeldeversuch lesen) |

Die Anmeldung am piholepi ist zusätzlich abgesichert: `pam_faillock` (5 Fehlversuche → 15 min) und ntfy bei
Anmeldung/Fehlversuch, siehe `system/etc/pam.d/login`. Beim Neustart des ESP gibt der ESP8266-Bootloader
kurz Zeichen mit 74880 Baud aus; der Pi kann das als (ungültigen) Anmeldeversuch zählen – harmlos.

## Pi-Seite (im Repository SpecterAoD/Heimnetz, eingespielt mit host-deploy.sh piholepi)

- `serial-getty@serial0` fest auf **115200** (`feste-baudrate.conf`, ohne `--keep-baud`): Datenmüll auf der Leitung
  (Flashen, Neustart des ESP) schaltete agetty sonst auf eine andere Baudrate → über Telnet nur Nullbytes.
- `serial-getty@ttyAMA0` abgeschaltet (`nicht-doppelt.conf`): sonst zwei agetty auf derselben Leitung.
- `kernel.sysrq = 0` (`60-hardening.conf`): ein Break auf der Leitung löste sonst SysRq-Befehle aus
  (am 31.10.2026 u. a. Konsolen-Meldestufe 8; möglich wären Neustart/Ausschalten gewesen).
- USB-Seriell auf dem Board ist ein **FT232R** (Nachbau): Schreiben geht, **Auslesen nicht** (esptool: Corrupt data).

## Prüfungen (GitHub Actions)

Bei jedem Push: Tests (Telnet-Aufbereitung, Einstellungen im EEPROM, `~esp-config`-Kommandos), Bauen mit Platzhaltern (Warnungen = Fehler) und Suche nach
eingecheckten Passwörtern (`.github/workflows/pruefen.yml`). Die dort gebaute Datei enthält nur Platzhalter
und ist **nicht** zum Flashen gedacht; die echte Firmware wird lokal mit `include/secrets.h` gebaut.

## Versionen und automatische Updates

- Version = `git describe` (z. B. `v3.0.0`). Ein Tag `v*` löst bei GitHub Bau, **Signatur** (Secret `ESP_SIGNIER_SCHLUESSEL`)
  und Release aus (`firmware.signed.bin` + `.sha256`).
- Der piholepi prüft täglich um 14:20 (`esp-konsole-update.timer`, Skript im Heimnetz-Repository): Release neuer als die
  Version am ESP → SHA256 und Signatur prüfen → einspielen → Ergebnis per ntfy. Nicht bei offener Telnet-Sitzung.
- Neue Version veröffentlichen: `git tag v3.0.1 && git push origin v3.0.1`.
- **v2 ↔ v3:** v3 legt die Einstellungen neben dem alten Block ab (Format 2 ab Adresse 512) und hält den alten Block
  (Format 1, Adresse 0) mit den Zugangsdaten immer aktuell. Beim ersten Start übernimmt v3 die Zugangsdaten von v2
  (Info-Port meldet dann einmal `"konfig":"eeprom-v1"`), die neuen Einstellungen bekommen die bisherigen festen Werte.
  **Zurück auf v2 geht jederzeit**, auch per Release: v2 findet seine Zugangsdaten (mit per `~esp-config` geänderten Passwörtern).
  Schreibt v2 dabei ins EEPROM, gehen die v3-Einstellungen verloren (Ports usw. wieder Standard), die Zugangsdaten nicht.

## Signaturschlüssel

- Öffentlich: `include/signatur_public.h` (in der Firmware) und im Heimnetz-Repository für den piholepi.
- Privat: Laptop `.schluessel/privat.pem` (600, nie im Repository) und GitHub-Secret `ESP_SIGNIER_SCHLUESSEL`.
  **Sicherungskopie im Passwort-Manager ablegen** – ohne den Schlüssel gehen Updates nur noch per USB.
- Lokal signieren: `python3 ~/.platformio/packages/framework-arduinoespressif8266/tools/signing.py -m sign -b .pio/build/d1_mini/firmware.bin -s .schluessel/privat.pem -o firmware.signed.bin`
- Prüfen: `python3 werkzeuge/signatur_pruefen.py firmware.signed.bin include/signatur_public.h`

## Einstellungen per Telnet (ab v3)

In der Telnet-Sitzung eine Zeile mit `~esp-config` beginnen (wie die `~.`-Befehle von ssh). Solche Zeilen gehen **nicht**
an den Pi; das Echo macht dann der ESP. `~esp-config` gilt nur am Zeilenanfang, sonst geht alles unverändert an den Pi.

| Kommando | Wirkung |
|---|---|
| `~esp-config show` | Entwurf anzeigen; `*` = geändert, noch nicht gespeichert; Passwörter nur als Länge |
| `~esp-config set <name> <wert>` | nur im Entwurf ändern; Werte mit Leerzeichen in `"…"` oder `'…'` |
| `~esp-config commit` | Entwurf prüfen, mit Prüfsumme ins EEPROM schreiben, zurücklesen; bei Bedarf Neustart nach 2 s |
| `~esp-config discard` | Entwurf verwerfen (auch beim Trennen der Sitzung) |
| `~esp-config help` | Kurzhilfe |

Ohne `commit` ändert sich nichts. Rücktaste geht, Strg-C bricht die Zeile ab, Zeilen über 160 Zeichen werden verworfen.

| Name | Standard | Bereich | Wirkt |
|---|---|---|---|
| `wlan_name`, `notfall_name` | – | 1–32 Zeichen | Neustart |
| `wlan_pass`, `notfall_pass` | – | 8–63 Zeichen | Neustart |
| `ota_pass` | – | 8–64 Zeichen | Neustart (ArduinoOTA übernimmt es nur beim Start) |
| `hostname` | `esp-konsole` | 1–32, `a-z A-Z 0-9 -` | Neustart |
| `telnet_port` / `info_port` / `ota_port` | 23 / 8023 / 8266 | 1–65535, verschieden | Neustart |
| `notfall_kanal` | 6 | 1–13 | Neustart |
| `notfall_ip` / `notfall_maske` | 192.168.4.1 / 255.255.255.0 | Maske /24–/30, darf das Heimnetz nicht überdecken | Neustart |
| `notfall_nach_s` | 180 | 60–3600 | sofort |
| `heim_stabil_s` | 60 | 10–600 | sofort |
| `leerlauf_min` | 30 | 1–120 | sofort |
| `reboot_tage` | 0 (nie) | 0–365; Neustart nur ohne offene Sitzung | sofort |

**Vorsicht:**
- Falsches Heim-WLAN → nach `notfall_nach_s` kommt das Notfall-WLAN. Heim- **und** Notfall-WLAN falsch → nur noch USB.
  Deshalb immer nur eins davon auf einmal ändern und testen.
- Der piholepi (`esp-konsole-update`, Überwachung) erwartet Info-Port 8023 und OTA-Port 8266; wer sie ändert,
  muss das Heimnetz-Repository anpassen. Neues OTA-Passwort ebenso in `/etc/heimnetz/esp-konsole.env` eintragen.
- Eine Firmware mit `KONFIG_ERZWINGEN` (`pio run -e d1_mini_konfig`) überschreibt nur die Zugangsdaten, die übrigen
  Einstellungen bleiben.

## Passwörter ändern

Am einfachsten per Telnet (siehe oben), z. B. `~esp-config set ota_pass "…"` und `~esp-config commit`.
Oder: neue Werte in `include/secrets.h`, dann `pio run -e d1_mini_konfig` (überschreibt die Werte im EEPROM), signieren und
einspielen. Danach das neue OTA-Passwort auch in `/etc/heimnetz/esp-konsole.env` auf dem piholepi eintragen.

## Geheimnisse

`include/secrets.h` (Vorlage `include/secrets.h.example`) enthält Heim-WLAN, Notfall-WLAN und OTA-Passwort.
Die Datei steht in `.gitignore` und kommt **nie** ins Repository. Passwörter erzeugen:

```
tr -dc 'A-HJ-NP-Za-km-z2-9' </dev/urandom | head -c 24; echo
```

## Bauen (Laptop)

```
cd esp-konsole                                                 # Laptop: /srv/claude/Projekte/esp-konsole
python3 -m venv .venv && .venv/bin/pip install platformio     # einmalig
cp include/secrets.h.example include/secrets.h                 # einmalig, Werte eintragen
.venv/bin/pio run                                              # -> .pio/build/d1_mini/firmware.bin
for t in filter konfig config_cmd; do g++ -std=c++17 -Wall -Werror -Iinclude test/test_$t.cpp -o /tmp/$t && /tmp/$t; done   # Tests
```

## Erstes Flashen per USB am piholepi (als piroot)

Der UART des Pi (GPIO14 = TX) hängt am RX des D1 mini und würde beim Flashen dazwischenfunken.
Deshalb wird GPIO14 vorübergehend auf Eingang gestellt. DNS und alle anderen Dienste laufen weiter.
Die Konsole läuft als `serial-getty@serial0` (serial0 = ttyAMA0) – **nicht** `serial-getty@ttyAMA0` anhalten/starten,
sonst liest die weiterlaufende Konsole die Flash-Daten mit (esptool: „Corrupt data“, „Invalid head of packet (0x0D)“)
bzw. es laufen zwei Konsolen auf einer Leitung. ModemManager wird angehalten, weil er neue serielle Geräte abfragt.

1. `firmware.bin` auf den piholepi kopieren (z. B. nach `/tmp/esp-konsole.bin`), esptool bereitstellen:
   `python3 -m venv /tmp/esptool && /tmp/esptool/bin/pip install esptool`
2. `sudo systemctl stop serial-getty@serial0 ModemManager` und `sudo pinctrl set 14 ip`
3. USB-Kabel vom D1 mini an den piholepi → erscheint als `/dev/ttyUSB0` (`dmesg | tail`)
4. **Sicherung der alten Firmware** (enthält die esp-link-Einstellungen inkl. WLAN-Passwort – nur auf dem Pi lassen):
   `sudo /tmp/esptool/bin/esptool --port /dev/ttyUSB0 read-flash 0 0x400000 /root/esp-link-sicherung_JJJJ-MM-TT.bin`
5. Flash löschen (alte WLAN-Einstellungen von esp-link entfernen) und flashen:
   `sudo /tmp/esptool/bin/esptool --port /dev/ttyUSB0 erase-flash`
   `sudo /tmp/esptool/bin/esptool --port /dev/ttyUSB0 write-flash 0 /tmp/esp-konsole.bin`
6. USB-Kabel abziehen, dann `sudo pinctrl set 14 a0` und `sudo systemctl start serial-getty@serial0 ModemManager`
7. Test: `telnet 192.168.2.16 23` vom Laptop → Enter → `piholepi login:`; von einer nicht erlaubten Adresse wird sofort getrennt.

**Rückweg:** wie oben, aber `write-flash 0 /root/esp-link-sicherung_JJJJ-MM-TT.bin` (stellt esp-link samt Einstellungen wieder her).

## Update über das WLAN (von Hand)

Mit dem Skript aus dem Heimnetz-Repository (Passwort aus einer Datei, nicht auf der Kommandozeile; Zeitgrenzen):

```
sudo ufw allow from 192.168.2.16 to any port 48266 proto tcp
python3 ../heimnetz/piholepi/system/usr/local/sbin/esp-konsole-update.py --datei firmware.signed.bin \
  --env .schluessel/ota.env --schluessel include/signatur_public.h      # erst mit --pruefen
sudo ufw delete allow from 192.168.2.16 to any port 48266 proto tcp
```

`.schluessel/ota.env` enthält `ESP_OTA_PASSWORT=…` (600). Nicht `espota.py -a …` verwenden: das Passwort steht dann in der
Prozessliste, und bei abgelehnter Signatur wartet espota endlos.
