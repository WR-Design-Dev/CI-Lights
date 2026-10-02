@echo off
setlocal
rem CI-Lights bauen, pruefen und ueber GitHub Actions veroeffentlichen.
rem
rem Voraussetzungen (einmalig installieren):
rem   1. Git for Windows: https://git-scm.com/downloads/win
rem      git config --global user.name "DEIN NAME"
rem      git config --global user.email "DEINE GITHUB COMMIT E-MAIL"
rem   2. Node.js 24 oder neuer: https://nodejs.org/
rem   3. GitHub CLI: https://cli.github.com/
rem      gh auth login --hostname github.com --git-protocol https --web
rem      Mit WR-Design-Dev anmelden. Vorhandene Anmeldung wird weiterverwendet.
rem      Alternativ wird build\github-cli\bin\gh.exe verwendet, falls vorhanden.
rem   4. Fuer den lokalen Build: ESP-IDF 6.1 mit ESP32-S3-Tools installieren.
rem      https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/get-started/
rem      Erkannt werden IDF_PATH, idf.currentSetup in .vscode\settings.json
rem      und C:\esp\v6.1\esp-idf. Andere Installation: -IdfPath "C:\Pfad\esp-idf"
rem      EIM-Installationen unter C:\Espressif\tools werden automatisch erkannt.
rem      Andere Werkzeugablage vorher setzen: set "IDF_TOOLS_PATH=C:\Pfad\Tools"
rem      Bei abweichender Python-venv: set "IDF_PYTHON_ENV_PATH=C:\Pfad\venv"
rem   5. Originalen privaten OTA-Schluessel aus dem eigenen Backup nach
rem      secrets\ota_signing_key.pem kopieren. Niemals einen neuen erzeugen!
rem      Zum Bauen NUR auf GitHub genuegt stattdessen -SkipLocalBuild.
rem
rem GitHub muss bereits wie fuer dieses Projekt eingerichtet sein:
rem   - Repository WR-Design-Dev/CI-Lights, Branch main und Workflow firmware.yml.
rem   - Environment firmware-signing mit Secret OTA_SIGNING_KEY_PEM (nur main).
rem   - GitHub Pages: Quelle "GitHub Actions"; Actions darf Releases schreiben.
rem   Der Secret-Wert und Zugangstokens gehoeren NIEMALS in dieses Skript.
rem
rem Verwendung:
rem   publish.bat
rem   publish.bat -Message "Meine Aenderung"
rem   publish.bat -SkipLocalBuild
rem   publish.bat -CheckOnly
rem   publish.bat -Message "Meine Aenderung" -Yes
rem -CheckOnly prueft Voraussetzungen ohne Build, Commit oder Push.
rem -Yes bestaetigt den Commit ohne Rueckfrage; nur bewusst verwenden.
rem Ohne -Yes werden die vorgemerkten Dateien vor dem Commit angezeigt.
rem Projektdateien werden uebernommen, lokale .vscode\settings.json nicht.
rem Git-ignorierte Schluessel, Backups, build und dist werden nicht hochgeladen.
rem Ohne neue Commits wird ein neuer GitHub-Build manuell gestartet.
rem Die veroeffentlichte Versionsnummer wird immer von GitHub vergeben.
rem Das Skript wartet auf Build, Signierung, Release und Pages-Veroeffentlichung.
rem Fuer Automatisierung ohne abschliessende Pause: set CI_LIGHTS_NO_PAUSE=1
rem
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\publish_firmware.ps1" %*
set "taskExit=%ERRORLEVEL%"
echo.
if not "%CI_LIGHTS_NO_PAUSE%"=="1" pause
exit /b %taskExit%
