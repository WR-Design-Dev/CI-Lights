# Firmware-Updates und GitHub-Einrichtung

Repository: `WR-Design-Dev/CI-Lights`, persoenliches Konto `WR-Design-Dev`.

## Einmalige GitHub-Einrichtung

1. Den vorhandenen privaten Schluessel `secrets/ota_signing_key.pem` separat
   verschluesselt sichern, zum Beispiel in einem verschluesselten Backup auf
   einem anderen Datentraeger. Der Schluessel ist fuer weitere OTA-Updates
   derselben Geraete erforderlich. Er wird von Git ignoriert. Die Datei
   `ota_signing_public_key.sha256` enthaelt nur den oeffentlichen Fingerabdruck.
2. [GitHub CLI](https://cli.github.com/) installieren und als `WR-Design-Dev`
   anmelden: `gh auth login --scopes workflow`. Anmeldung und Zwei-Faktor-Code selbst eingeben;
   weder Token noch privaten Schluessel in Chats oder Issues einfuegen.
3. Im ESP-IDF-Terminal aus dem Projektordner zuerst pruefen:

   ```sh
   python tools/configure_github.py
   ```

4. Die Einrichtung anwenden:

   ```sh
   python tools/configure_github.py --apply
   ```

   Das Skript prueft Konto, Eigentum und Administratorrechte. Es entfernt
   fremde schreibberechtigte Mitarbeiter, ausstehende Mitarbeiter-Einladungen
   und schreibberechtigte Deploy-Keys aus diesem Repository. Oeffentlicher
   Lesezugriff und Pull Requests aus Forks bleiben moeglich. GitHub Actions
   erhaelt nur im Veroeffentlichungsjob Schreibrechte fuer Releases.

   Anschliessend richtet es das Environment `firmware-signing` mit einer
   Branch-Regel ausschliesslich fuer `main` ein. Der Schluessel wird ueber stdin
   als **Environment-Secret `OTA_SIGNING_KEY_PEM`** hochgeladen, ohne ihn in
   Kommandozeilenargumenten, Shell-Verlauf oder Ausgaben darzustellen. Ein
   gleichnamiges Repository-Secret wird entfernt. GitHub Pages wird auf
   **GitHub Actions** als Quelle gesetzt.

   Existiert das Signier-Environment bereits mit einer breiteren Freigabe,
   bricht das Skript vor dem Schluessel-Upload ab. Unter **Settings > Environments
   > firmware-signing > Deployment branches and tags** dann **Selected branches
   and tags** und ausschliesslich **Branch: main** einstellen und das Skript
   erneut ausfuehren. Tags und Wildcards duerfen dort nicht erlaubt sein.

5. Die geprueften Quelldateien inklusive `.github/workflows/firmware.yml`,
   `tools/`, `web-flasher/`, `tests/` und des oeffentlichen Fingerabdrucks nach
   `main` pushen. `secrets/`, `build/`, `dist/` und lokale Backups bleiben lokal.
   Ein alter Git-Remote muss auf das richtige Repository zeigen:

   ```sh
   git remote -v
   git remote set-url origin https://github.com/WR-Design-Dev/CI-Lights.git
   ```

   Der erfolgreiche Workflow erstellt ein GitHub Release und veroeffentlicht
   die Installationsseite unter `https://wr-design-dev.github.io/CI-Lights/`.
   Vor dem ersten erfolgreichen Deployment ist diese Adresse noch nicht nutzbar.

Die Einstellungen lassen sich auch manuell in GitHub vornehmen: **Settings >
Collaborators** fuer Zugriff, **Settings > Environments** fuer das Secret und
**Settings > Pages > Source: GitHub Actions** fuer die Installationsseite.

Impressum und Datenschutz sind ueber den Seitenfuss erreichbar. Die Betreiberangaben
stammen von der eigenen Seite `lupiflash.ddns.net`; die Datenschutzhinweise
beschreiben GitHub Pages, UNPKG und die lokale USB-Kommunikation dieser Seite.
Die Flash-Seite startet auf Englisch. Mit den Flaggen **EN** und **DE** im
Seitenkopf wechselst du die Sprache, einschliesslich Meldungen, Bestaetigungen
und Rechtstexten. Die Auswahl bleibt ueber `?lang=en` beziehungsweise `?lang=de`
beim Wechsel zwischen den Seiten erhalten; Cookies oder Browser-Speicher sind
dafuer nicht erforderlich.
Die Ampelgrafik stammt direkt aus `main/assets/TrafficLight.svg`, derselben
Quelldatei wie die Grafik in der Verwaltungsoberflaeche.
Farben, Karten, Buttons und Flaggen-Umschalter entsprechen dem Stil der
Ampel-Verwaltung; die SVG-Flaggen wurden aus deren Umschalter uebernommen.

## Automatischer Build

- Jeder Push, jeder Pull Request und manuell gestartete Workflows bauen mit
  ESP-IDF 6.1 fuer ESP32-S3.
- `main` nutzt das vorhandene RSA-3072-Secret. Der Build vergleicht dessen
  oeffentlichen Fingerabdruck mit dem Repository und verweigert einen anderen
  Schluessel. Nach Build, Tests und Verpackung wird die temporaere PEM-Datei
  im Runner geloescht.
- Andere Branches und Pull Requests verwenden Wegwerf-Schluessel und haben
  keinen Zugriff auf das Produktions-Environment. Ihre Artefakte dienen fuer
  Entwicklungsgeraete mit USB-Flash und passen nicht zu produktiven OTA-Geraeten.
- Ein erfolgreicher `main`-Build veroeffentlicht `v1.0.N`, wobei `N` die
  Workflow-Laufnummer plus 1000 ist. Neuere Releases werden als Latest markiert.
  Ein aelterer parallel fertiggestellter Build ersetzt Latest nicht.
- Scheitert nur die Veroeffentlichung, **Re-run failed jobs** verwenden. Dadurch
  werden dieselben signierten Dateien erneut fuer Pages verwendet. Ein kompletter
  Neubuild derselben Versionsnummer kann wegen neuer Build-Zeit andere Dateien
  erzeugen; bestehende Release-Dateien werden dann nicht ueberschrieben. Fuer
  eine neue Firmware einen neuen Push oder einen neuen manuellen Lauf starten.

## Was wird veroeffentlicht?

| Datei | Verwendung |
| --- | --- |
| `CI-Lights.bin` | Signierte Anwendung fuer OTA und USB |
| `ota-manifest.json` | Version, Groesse, SHA-256, Ziel-Chip, Layout und Release-URL |
| `bootloader.bin`, `partition-table.bin`, `ota_data_initial.bin` | Einmalige vollstaendige USB-Installation |
| `SHA256SUMS` | Pruefsummen der Release-Dateien |
| `install-manifest.json` und Installationsseite | Browser-USB-Installation auf GitHub Pages |

Die Browser-Firmware liegt neben der Webseite auf Pages, damit Downloads keine
Freigaben fuer andere Domains benoetigen. OTA-Dateien liegen in oeffentlichen
GitHub Releases; die Ampel braucht keine GitHub-Zugangsdaten.
Espressifs esptool-js 0.6.1 wird erst nach Klick auf **Ampel per USB verbinden**
und Port-Auswahl von `https://unpkg.com/esptool-js@0.6.1/bundle.js` geladen.
Das Oeffnen der Seite laedt keine externen CDN-Skripte. Die Loeschoption ist
standardmaessig deaktiviert. Vor dem Flashen werden Groesse und SHA-256 aller
vier Dateien geprueft. Die Datenuebertragung erfolgt lokal per Web Serial.
Der separate Button **Geraet loeschen** leert nach Bestaetigung den gesamten
Flash ohne Neuinstallation. Er funktioniert auch ohne verfuegbaren Firmware-Download.

## Flash-Aufteilung und Wiederherstellung

| Bereich | Adresse | Groesse |
| --- | --- | --- |
| NVS (Einstellungen) | `0x9000` | 24 KiB |
| PHY | `0x10000` | 4 KiB |
| OTA-Auswahl | `0x11000` | 8 KiB |
| Firmware-Slot 0 | `0x20000` | 1,75 MiB |
| Branding | `0x1e0000` | 128 KiB |
| Firmware-Slot 1 | `0x200000` | 1,75 MiB |

Die Datenadressen der bisherigen Firmware bleiben erhalten. Die erste Migration
benoetigt Bootloader, Partitionstabelle, initiale OTA-Auswahl und Anwendung per
USB; danach reicht die Anwendung per OTA. Bei einer erneuten Browser-Installation
**Alle Geraetedaten loeschen** deaktiviert lassen, um bestehende Daten zu erhalten.

Die neue Anwendung bestaetigt den Start erst nach erfolgreicher Initialisierung
von Konfiguration, PSRAM und Webserver sowie zehn Sekunden Laufzeit. Ein Absturz
oder Reset vor der Bestaetigung loest beim naechsten Start Rollback aus, wenn
eine gueltige vorherige Firmware vorhanden ist. Ein erfolgreicher lokaler Start
prueft nicht die Erreichbarkeit von Jenkins oder GitHub.

GitHubs Oberflaeche und Secret-API zeigen einen gespeicherten Schluessel nicht
wieder an; die Ampel enthaelt nur den oeffentlichen Schluessel. Deshalb eine
separate Sicherung des privaten Schluessels behalten. Mit einem neuen Schluessel ist weiterhin eine komplette
USB-Neuinstallation moeglich; bestehende Geraete akzeptieren ihn fuer OTA nicht.
Hardware-Secure-Boot, Flash-Verschluesselung und Anti-Rollback-eFuses werden hier
nicht aktiviert. Fuer diese Software-Signierung gibt es kein eFuse-Limit fuer
erneutes USB-Flashen; jeder Schreibvorgang unterliegt der normalen Flash-Lebensdauer.

## Wie bleibt der Schluessel geheim?

GitHub speichert das Secret verschluesselt und stellt es nur Jobs bereit, deren
Environment-Regeln erfuellt sind. Der Workflow gibt nur den oeffentlichen
Fingerabdruck aus und laedt ausschliesslich die festgelegten Firmware-Dateien
und die statische Webseite als Artefakte hoch. In der Firmware steckt der
oeffentliche Schluessel, mit dem die Ampel die Signatur prueft.

Waehrend des Signierens muss vertrauenswuerdiger Build-Code den privaten
Schluessel lesen koennen. Alleinige Schreibrechte fuer `WR-Design-Dev` reduzieren
den Personenkreis, der diesen Code aendern kann. Pruefe deshalb Aenderungen aus
Pull Requests vor dem Merge, insbesondere Workflows, Build-Skripte und Tests.
GitHub-Apps mit Schreibrechten und die Zugangsdaten deines Kontos gehoeren
ebenfalls zu diesem Vertrauensbereich. Die lokale PEM-Datei selbst ist nicht
verschluesselt; Zugriffsrechte und ein verschluesseltes Backup schuetzen sie.

## Lokal pruefen

Nach einem signierten Build im ESP-IDF-Terminal:

```sh
node tests/test_ota_ui.js
node tests/test_web_flasher.mjs
python -m unittest discover -s tests -p 'test_*.py'
python tools/package_firmware.py
```

Die Paketpruefung weist falsche Chip-Ziele, falsche Versionsnummern, zu grosse
oder unsignierte Images, veraenderte Flash-Adressen und beschaedigte Signaturen
ab. `dist/release/` enthaelt die Release-Dateien, `dist/site/` die komplette Seite.

## Quellen

- [ESP-IDF OTA und Rollback](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/system/ota.html)
- [ESP-IDF signierte Updates ohne Hardware-Secure-Boot](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/security/secure-boot-v2.html)
- [GitHub Environment-Regeln](https://docs.github.com/en/actions/how-tos/deploy/configure-and-manage-deployments/manage-environments)
- [GitHub Secrets](https://docs.github.com/en/actions/how-tos/write-workflows/choose-what-workflows-do/use-secrets)
- [GitHub Pages mit Actions](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages)
- [Espressif esptool-js](https://espressif.github.io/esptool-js/)
- [UNPKG](https://unpkg.com/)
