# Firmware-Updates und GitHub-Einrichtung

Repository: `WR-Design-Dev/CI-Lights`, persönliches Konto `WR-Design-Dev`.

## Einmalige GitHub-Einrichtung

1. Den vorhandenen privaten Schlüssel `secrets/ota_signing_key.pem` separat
   verschlüsselt sichern, zum Beispiel in einem verschlüsselten Backup auf
   einem anderen Datenträger. Der Schlüssel ist für weitere OTA-Updates
   derselben Geräte erforderlich. Er wird von Git ignoriert. Die Datei
   `ota_signing_public_key.sha256` enthält nur den öffentlichen Fingerabdruck.
2. [GitHub CLI](https://cli.github.com/) installieren und als `WR-Design-Dev`
   anmelden: `gh auth login --scopes workflow`. Anmeldung und Zwei-Faktor-Code selbst eingeben;
   weder Token noch privaten Schlüssel in Chats oder Issues einfügen.
3. Im ESP-IDF-Terminal aus dem Projektordner zuerst prüfen:

   ```sh
   python tools/configure_github.py
   ```

4. Die Einrichtung anwenden:

   ```sh
   python tools/configure_github.py --apply
   ```

   Das Skript prüft Konto, Eigentum und Administratorrechte. Es entfernt
   fremde schreibberechtigte Mitarbeiter, ausstehende Mitarbeiter-Einladungen
   und schreibberechtigte Deploy-Keys aus diesem Repository. Öffentlicher
   Lesezugriff und Pull Requests aus Forks bleiben möglich. GitHub Actions
   erhält nur im Veröffentlichungsjob Schreibrechte für Releases.

   Anschließend richtet es das Environment `firmware-signing` mit einer
   Branch-Regel außchließlich für `main` ein. Der Schlüssel wird über stdin
   als **Environment-Secret `OTA_SIGNING_KEY_PEM`** hochgeladen, ohne ihn in
   Kommandozeilenargumenten, Shell-Verlauf oder Ausgaben darzustellen. Ein
   gleichnamiges Repository-Secret wird entfernt. GitHub Pages wird auf
   **GitHub Actions** als Quelle gesetzt.

   Existiert das Signier-Environment bereits mit einer breiteren Freigabe,
   bricht das Skript vor dem Schlüssel-Upload ab. Unter **Settings > Environments
   > firmware-signing > Deployment branches and tags** dann **Selected branches
   and tags** und außchließlich **Branch: main** einstellen und das Skript
   erneut ausführen. Tags und Wildcards dürfen dort nicht erlaubt sein.

5. Die geprüften Quelldateien inklusive `.github/workflows/firmware.yml`,
   `tools/`, `web-flasher/`, `tests/` und des öffentlichen Fingerabdrucks nach
   `main` pushen. `secrets/`, `build/`, `dist/` und lokale Backups bleiben lokal.
   Ein alter Git-Remote muss auf das richtige Repository zeigen:

   ```sh
   git remote -v
   git remote set-url origin https://github.com/WR-Design-Dev/CI-Lights.git
   ```

   Der erfolgreiche Workflow erstellt ein GitHub Release und veröffentlicht
   die Installationsseite unter `https://wr-design-dev.github.io/CI-Lights/`.
   Vor dem ersten erfolgreichen Deployment ist diese Adresse noch nicht nutzbar.

Die Einstellungen lassen sich auch manuell in GitHub vornehmen: **Settings >
Collaborators** für Zugriff, **Settings > Environments** für das Secret und
**Settings > Pages > Source: GitHub Actions** für die Installationsseite.

Impressum und Datenschutz sind über den Seitenfuß erreichbar. Die Betreiberangaben
stammen von der eigenen Seite `lupiflash.ddns.net`; die Datenschutzhinweise
beschreiben GitHub Pages, UNPKG und die lokale USB-Kommunikation dieser Seite.
Die Flash-Seite startet auf Englisch. Mit den Flaggen **EN** und **DE** im
Seitenkopf wechselst du die Sprache, einschließlich Meldungen, Bestätigungen
und Rechtstexten. Die Auswahl bleibt über `?lang=en` beziehungsweise `?lang=de`
beim Wechsel zwischen den Seiten erhalten; Cookies oder Browser-Speicher sind
dafür nicht erforderlich.
Die Ampelgrafik stammt direkt aus `main/assets/TrafficLight.svg`, derselben
Quelldatei wie die Grafik in der Verwaltungsoberfläche.
Farben, Karten, Buttons und Flaggen-Umschalter entsprechen dem Stil der
Ampel-Verwaltung; die SVG-Flaggen wurden aus deren Umschalter übernommen.

## Automatischer Build

- Jeder Push, jeder Pull Request und manuell gestartete Workflows bauen mit
  ESP-IDF 6.1 für ESP32-S3.
- `main` nutzt das vorhandene RSA-3072-Secret. Der Build vergleicht dessen
  öffentlichen Fingerabdruck mit dem Repository und verweigert einen anderen
  Schlüssel. Nach Build, Tests und Verpackung wird die temporäre PEM-Datei
  im Runner gelöscht.
- Andere Branches und Pull Requests verwenden Wegwerf-Schlüssel und haben
  keinen Zugriff auf das Produktions-Environment. Ihre Artefakte dienen für
  Entwicklungsgeräte mit USB-Flash und passen nicht zu produktiven OTA-Geräten.
- Ein erfolgreicher `main`-Build veröffentlicht `v1.0.N`, wobei `N` die
  Workflow-Laufnummer plus 1000 ist. Neuere Releases werden als Latest markiert.
  Ein älterer parallel fertiggestellter Build ersetzt Latest nicht.
- Veröffentlicht wird der Build des aktuellen `main`-Commits. Hat ein weiterer
  Push `main` inzwischen geändert, endet der ältere Build erfolgreich und
  bleibt als Artefakt verfügbar; der neuere Build übernimmt die Veröffentlichung.
  Damit werden auch GitHubs Einschränkungen bei Release-Tags für ältere
  Commits berücksichtigt ([GitHub-Hinweis](https://github.blog/changelog/2023-11-02-github-actions-enforcing-workflow-scope-when-creating-a-release/)).
- Scheitert nur die Veröffentlichung, **Re-run failed jobs** verwenden. Dadurch
  werden dieselben signierten Dateien erneut für Pages verwendet. Ein kompletter
  Neubuild derselben Versionsnummer kann wegen neuer Build-Zeit andere Dateien
  erzeugen; bestehende Release-Dateien werden dann nicht überschrieben. Für
  eine neue Firmware einen neuen Push oder einen neuen manuellen Lauf starten.

## Was wird veröffentlicht?

| Datei | Verwendung |
| --- | --- |
| `CI-Lights.bin` | Signierte Anwendung für OTA und USB |
| `ota-manifest.json` | Version, Größe, SHA-256, Ziel-Chip, Layout und Release-URL |
| `bootloader.bin`, `partition-table.bin`, `ota_data_initial.bin` | Einmalige vollständige USB-Installation |
| `SHA256SUMS` | Prüfsummen der Release-Dateien |
| `install-manifest.json` und Installationsseite | Browser-USB-Installation auf GitHub Pages |

Die Browser-Firmware liegt neben der Webseite auf Pages, damit Downloads keine
Freigaben für andere Domains benötigen. OTA-Dateien liegen in öffentlichen
GitHub Releases; die Ampel braucht keine GitHub-Zugangsdaten.
Espressifs esptool-js 0.6.1 wird erst nach Klick auf **Ampel per USB verbinden**
und Port-Auswahl von `https://unpkg.com/esptool-js@0.6.1/bundle.js` geladen.
Das Öffnen der Seite lädt keine externen CDN-Skripte. Die Löschoption ist
standardmäßig deaktiviert. Vor dem Flashen werden Größe und SHA-256 aller
vier Dateien geprüft. Die Datenübertragung erfolgt lokal per Web Serial.
Der separate Button **Gerät löschen** leert nach Bestätigung den gesamten
Flash ohne Neuinstallation. Er funktioniert auch ohne verfügbaren Firmware-Download.

## Flash-Aufteilung und Wiederherstellung

| Bereich | Adresse | Größe |
| --- | --- | --- |
| NVS (Einstellungen) | `0x9000` | 24 KiB |
| PHY | `0x10000` | 4 KiB |
| OTA-Auswahl | `0x11000` | 8 KiB |
| Firmware-Slot 0 | `0x20000` | 1,75 MiB |
| Branding | `0x1e0000` | 128 KiB |
| Firmware-Slot 1 | `0x200000` | 1,75 MiB |

Die Datenadressen der bisherigen Firmware bleiben erhalten. Die erste Migration
benötigt Bootloader, Partitionstabelle, initiale OTA-Auswahl und Anwendung per
USB; danach reicht die Anwendung per OTA. Bei einer erneuten Browser-Installation
**Alle Gerätedaten löschen** deaktiviert lassen, um bestehende Daten zu erhalten.

Die neue Anwendung bestätigt den Start erst nach erfolgreicher Initialisierung
von Konfiguration, PSRAM und Webserver sowie zehn Sekunden Laufzeit. Ein Absturz
oder Reset vor der Bestätigung löst beim nächsten Start Rollback aus, wenn
eine gültige vorherige Firmware vorhanden ist. Ein erfolgreicher lokaler Start
prüft nicht die Erreichbarkeit von Jenkins oder GitHub.

GitHubs Oberfläche und Secret-API zeigen einen gespeicherten Schlüssel nicht
wieder an; die Ampel enthält nur den öffentlichen Schlüssel. Deshalb eine
separate Sicherung des privaten Schlüssels behalten. Mit einem neuen Schlüssel ist weiterhin eine komplette
USB-Neuinstallation möglich; bestehende Geräte akzeptieren ihn für OTA nicht.
Hardware-Secure-Boot, Flash-Verschlüsselung und Anti-Rollback-eFuses werden hier
nicht aktiviert. Für diese Software-Signierung gibt es kein eFuse-Limit für
erneutes USB-Flashen; jeder Schreibvorgang unterliegt der normalen Flash-Lebensdauer.

## Wie bleibt der Schlüssel geheim?

GitHub speichert das Secret verschlüsselt und stellt es nur Jobs bereit, deren
Environment-Regeln erfüllt sind. Der Workflow gibt nur den öffentlichen
Fingerabdruck aus und lädt außchließlich die festgelegten Firmware-Dateien
und die statische Webseite als Artefakte hoch. In der Firmware steckt der
öffentliche Schlüssel, mit dem die Ampel die Signatur prüft.

Während des Signierens muss vertraünswürdiger Build-Code den privaten
Schlüssel lesen können. Alleinige Schreibrechte für `WR-Design-Dev` reduzieren
den Personenkreis, der diesen Code ändern kann. Prüfe deshalb Änderungen aus
Pull Requests vor dem Merge, insbesondere Workflows, Build-Skripte und Tests.
GitHub-Apps mit Schreibrechten und die Zugangsdaten deines Kontos gehören
ebenfalls zu diesem Vertrauensbereich. Die lokale PEM-Datei selbst ist nicht
verschlüsselt; Zugriffsrechte und ein verschlüsseltes Backup schützen sie.

## Lokal prüfen

Nach einem signierten Build im ESP-IDF-Terminal:

```sh
node tests/test_ota_ui.js
node tests/test_web_flasher.mjs
python -m unittest discover -s tests -p 'test_*.py'
python tools/package_firmware.py
```

Die Paketprüfung weist falsche Chip-Ziele, falsche Versionsnummern, zu große
oder unsignierte Images, veränderte Flash-Adressen und beschädigte Signaturen
ab. `dist/release/` enthält die Release-Dateien, `dist/site/` die komplette Seite.

## Quellen

- [ESP-IDF OTA und Rollback](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/system/ota.html)
- [ESP-IDF signierte Updates ohne Hardware-Secure-Boot](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/security/secure-boot-v2.html)
- [GitHub Environment-Regeln](https://docs.github.com/en/actions/how-tos/deploy/configure-and-manage-deployments/manage-environments)
- [GitHub Secrets](https://docs.github.com/en/actions/how-tos/write-workflows/choose-what-workflows-do/use-secrets)
- [GitHub Pages mit Actions](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages)
- [Espressif esptool-js](https://espressif.github.io/esptool-js/)
- [UNPKG](https://unpkg.com/)
