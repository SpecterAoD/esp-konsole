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

Bei jedem Push: Test der Telnet-Aufbereitung, Bauen mit Platzhaltern (Warnungen = Fehler) und Suche nach
eingecheckten Passwörtern (`.github/workflows/pruefen.yml`). Die dort gebaute Datei enthält nur Platzhalter
und ist **nicht** zum Flashen gedacht; die echte Firmware wird lokal mit `include/secrets.h` gebaut.

## Versionen und automatische Updates

- Version = `git describe` (z. B. `v2.0.0`). Ein Tag `v*` löst bei GitHub Bau, **Signatur** (Secret `ESP_SIGNIER_SCHLUESSEL`)
  und Release aus (`firmware.signed.bin` + `.sha256`).
- Der piholepi prüft täglich um 14:20 (`esp-konsole-update.timer`, Skript im Heimnetz-Repository): Release neuer als die
  Version am ESP → SHA256 und Signatur prüfen → einspielen → Ergebnis per ntfy. Nicht bei offener Telnet-Sitzung.
- Neue Version veröffentlichen: `git tag v2.0.1 && git push origin v2.0.1`.

## Signaturschlüssel

- Öffentlich: `include/signatur_public.h` (in der Firmware) und im Heimnetz-Repository für den piholepi.
- Privat: Laptop `.schluessel/privat.pem` (600, nie im Repository) und GitHub-Secret `ESP_SIGNIER_SCHLUESSEL`.
  **Sicherungskopie im Passwort-Manager ablegen** – ohne den Schlüssel gehen Updates nur noch per USB.
- Lokal signieren: `python3 ~/.platformio/packages/framework-arduinoespressif8266/tools/signing.py -m sign -b .pio/build/d1_mini/firmware.bin -s .schluessel/privat.pem -o firmware.signed.bin`
- Prüfen: `python3 werkzeuge/signatur_pruefen.py firmware.signed.bin include/signatur_public.h`

## Passwörter ändern

Neue Werte in `include/secrets.h`, dann `pio run -e d1_mini_konfig` (überschreibt die Werte im EEPROM), signieren und
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
g++ -std=c++17 -Iinclude test/test_filter.cpp -o /tmp/t && /tmp/t   # Test der Telnet-Aufbereitung
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
