# ESP-Konsole v3.0.0 – Git Anleitung

## Vorbereitung: Änderungen reviewen

```bash
cd esp-konsole
git status
git diff include/konfig.h include/config_cmd.h src/main.cpp
```

## Dateien zum Staging hinzufügen

```bash
# Neue Dateien
git add include/config_cmd.h
git add KONFIGURATION.md
git add QUICKSTART.md
git add IMPLEMENTIERUNG_v3.md
git add CHANGELOG.md

# Modifizierte Dateien
git add include/konfig.h
git add src/main.cpp
git add README.md

# Oder alle zusammen:
git add -A
```

## Commit mit aussagekräftiger Nachricht

```bash
git commit -m "feat: Konfigurierbarkeit ohne Neuflashen (v3)

- Telnet-Kommandos für alle Einstellungen: config set/show/commit
- EEPROM-Format erweitert (v1 → v2): 273 → 464 Bytes
- 17 neue konfigurierbare Parameter (WLAN, DNS, NTP, Ports, etc.)
- Automatische Migration von v1 zu v2 Konfigurationen
- CRC32-Validierung für Datensicherheit
- Keine Web-Oberfläche (wie gewünscht)
- Umfangreiche Dokumentation (KONFIGURATION.md, QUICKSTART.md, etc.)

Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

## Release erstellen (Tag v3.0.0)

```bash
git tag -a v3.0.0 -m "Release: ESP-Konsole v3.0.0

Konfigurierbarkeit ohne Neuflashen via Telnet-Kommandos"

git push origin main
git push origin v3.0.0
```

## GitHub Release-Notes (optional)

Auf GitHub: https://github.com/SpecterAoD/esp-konsole/releases

```markdown
# ESP-Konsole v3.0.0

## 🎉 Neue Features

- **Konfigurierbarkeit ohne Neuflashen**: Alle Einstellungen über Telnet-Kommandos änderbar
- **Telnet-Kommandos**: `config set key value`, `config show`, `config commit`
- **17 neue Parameter**: WLAN, DNS, NTP, Ports, Hostname, Timeouts, etc.
- **EEPROM-Migration**: Alte v1-Konfigurationen werden automatisch migriert

## 📋 Konfigurierbare Einstellungen

- WLAN SSID + Passwort (Heim + Notfall)
- OTA-Passwort
- DNS- + NTP-Server
- Telnet/Info/OTA Ports
- Hostname
- Notfall-WLAN IP + Maske + Kanal
- Timeouts (3 verschiedene)
- Auto-Reboot Interval
- Debug-Modus

## 📚 Dokumentation

- `KONFIGURATION.md` – Vollständige Referenz
- `QUICKSTART.md` – Schnelleinstieg
- `IMPLEMENTIERUNG_v3.md` – Technische Details
- `CHANGELOG.md` – Versionsverlauf

## 🔒 Sicherheit

- Kommandos nur von erlaubten IPs
- CRC32-Prüfsummen für EEPROM
- Eingabe-Validierung
- Keine Lockout-Risiken

## 📦 Download

Die signierte Firmware wird nach der Veröffentlichung dieses Tags automatisch erstellt.

---

**Details**: Siehe `IMPLEMENTIERUNG_v3.md` für technische Informationen.
```

## Lokale Vorbereitung vor GitHub Push

```bash
# Sicherstellen, dass Git sauber ist
git status
# (sollte: "On branch main, nothing to commit, working tree clean")

# Lokale Tags prüfen
git tag | grep v3

# Remote prüfen
git remote -v
# (sollte: SpecterAoD/esp-konsole zeigen)
```

## Nach dem Push: Konfigurationsverbreitung

Falls du auch das piholepi-Repo (Heimnetz) hast:

```bash
cd ../Heimnetz
# Update der esp-konsole Integration, falls nötig
git pull
# Update der Signatur-Public-Keys / Release-URL
```

---

## ⚠️ Wichtige Hinweise

### Vor dem Committen:
- ✅ Alle Dateien sind wohlgeformt (Tests bestanden)
- ✅ Keine hardcoded Passwörter in den neuen Dateien
- ✅ README.md wurde aktualisiert
- ✅ Dokumentation ist vollständig

### Nach dem Tag-Push:
- 🤖 GitHub Actions sollten automatisch bauen (siehe `.github/workflows/`)
- 📦 Release mit signierter Firmware sollte erstellt werden
- 📧 Optional: Release auf GitHub Discussions/Releases ankündigen

### Für Piholepi:
- Das Update-Script sollte die neue Firmware automatisch erkennen
- Migration v2→v3 findet automatisch beim Start statt
- Keine Änderungen am piholepi-Update-Prozess nötig

---

**Letzte Kontrolle vor Release:**

```bash
# Struktur-Größe prüfen
g++ -std=c++17 -I. -DSIZE_CHECK << 'EOF'
#include "include/konfig.h"
#include <iostream>
int main() {
  std::cout << "sizeof(konfig::Daten) = " << sizeof(konfig::Daten) << std::endl;
  return 0;
}
EOF
# → Sollte: 464 (nicht 273!)
```

---

**Viel Erfolg bei der Veröffentlichung!** 🚀
