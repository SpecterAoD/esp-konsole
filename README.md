# ESP-Konsole – serielle Konsole des piholepi per Telnet

Ersatz für die esp-link-Firmware auf dem **Wemos D1 mini** im Gehäuse des piholepi
(esp-link v3.2.47 stürzte am 30.10.2026 mehrmals pro Stunde ab, hatte keine Anmeldung und ein schwaches Notfall-WLAN).

| | |
|---|---|
| Heim-WLAN | Telnet `192.168.2.16:23`, nur von den Adressen in `ERLAUBT[]` (`src/main.cpp`: Laptop .121, iPhone .100) |
| Notfall-WLAN | geht an, wenn das Heim-WLAN **3 min** weg ist; WPA2, 1 Gerät, Telnet `192.168.4.1:23`; geht wieder aus, sobald das Heim-WLAN 1 min stabil ist und niemand mehr im Notfall-WLAN ist |
| Sitzung | eine gleichzeitig (eine neue Verbindung übernimmt und trennt die alte), Trennung nach 30 min ohne Eingabe; kein doppeltes Echo (Telnet-Verhandlung: Server übernimmt das Echo) |
| Update | über das WLAN mit ArduinoOTA (Port 8266), nur mit `OTA_PASSWORT`; erstes Flashen per USB |
| Weboberfläche | keine |
| Serielle Leitung | 115200 8N1, UART0; die Firmware schreibt selbst **nichts** auf die Leitung (der Pi würde es als Anmeldeversuch lesen) |

Die Anmeldung am piholepi ist zusätzlich abgesichert: `pam_faillock` (5 Fehlversuche → 15 min) und ntfy bei
Anmeldung/Fehlversuch, siehe `system/etc/pam.d/login`. Beim Neustart des ESP gibt der ESP8266-Bootloader
kurz Zeichen mit 74880 Baud aus; der Pi kann das als (ungültigen) Anmeldeversuch zählen – harmlos.

## Pi-Seite (eingespielt mit host-deploy.sh piholepi)

- `serial-getty@serial0` fest auf **115200** (`feste-baudrate.conf`, ohne `--keep-baud`): Datenmüll auf der Leitung
  (Flashen, Neustart des ESP) schaltete agetty sonst auf eine andere Baudrate → über Telnet nur Nullbytes.
- `serial-getty@ttyAMA0` abgeschaltet (`nicht-doppelt.conf`): sonst zwei agetty auf derselben Leitung.
- `kernel.sysrq = 0` (`60-hardening.conf`): ein Break auf der Leitung löste sonst SysRq-Befehle aus
  (am 31.10.2026 u. a. Konsolen-Meldestufe 8; möglich wären Neustart/Ausschalten gewesen).
- USB-Seriell auf dem Board ist ein **FT232R** (Nachbau): Schreiben geht, **Auslesen nicht** (esptool: Corrupt data).

## Geheimnisse

`include/secrets.h` (Vorlage `include/secrets.h.example`) enthält Heim-WLAN, Notfall-WLAN und OTA-Passwort.
Die Datei steht in `.gitignore` und kommt **nie** ins Repository. Passwörter erzeugen:

```
tr -dc 'A-HJ-NP-Za-km-z2-9' </dev/urandom | head -c 24; echo
```

## Bauen (Laptop)

```
cd piholepi/esp-konsole
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

## Update über das WLAN

Erprobt am 31.10.2026 vom Laptop (ufw nur kurz für den ESP und einen festen Port geöffnet):

```
sudo ufw allow from 192.168.2.16 to any port 48266 proto tcp
.venv/bin/python ~/.platformio/packages/framework-arduinoespressif8266/tools/espota.py \
  -i 192.168.2.16 -p 8266 -P 48266 -a <OTA_PASSWORT> -f .pio/build/d1_mini/firmware.bin
sudo ufw delete allow from 192.168.2.16 to any port 48266 proto tcp
```

Alternativ:

```
.venv/bin/pio run -t upload --upload-port 192.168.2.16 --upload-protocol espota \
  --upload-flags="--auth=<OTA_PASSWORT>"
```

oder mit `espota.py -i 192.168.2.16 -p 8266 -a <OTA_PASSWORT> -f firmware.bin`.
Beim Update baut der **ESP die Verbindung zum hochladenden Rechner auf** (zufälliger Port): auf dem Laptop bzw.
piholepi muss ufw das für die Dauer des Updates von 192.168.2.16 zulassen, sonst bricht es ab.
Die laufende Telnet-Sitzung wird dabei getrennt. Schlägt das Update fehl, läuft die alte Firmware weiter.
