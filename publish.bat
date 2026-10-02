@echo off
setlocal
chcp 65001 >nul
rem UTF-8 aktivieren, damit Umlaute in Skriptausgaben richtig angezeigt werden.
rem CI-Lights bauen, prüfen und über GitHub Actions veröffentlichen.
rem
rem Voraussetzungen (einmalig installieren):
rem   1. Git for Windows: https://git-scm.com/downloads/win
rem      git config --global user.name "DEIN NAME"
rem      git config --global user.email "DEINE GITHUB COMMIT E-MAIL"
rem   2. Node.js 24 oder neuer: https://nodejs.org/
rem   3. GitHub CLI: https://cli.github.com/
rem      gh auth login --hostname github.com --git-protocol https --web --scopes repo,workflow
rem      Bei bestehender Anmeldung ohne Workflow-Recht:
rem      gh auth refresh --hostname github.com --scopes workflow
rem      Mit WR-Design-Dev anmelden. Vorhandene Anmeldung wird weiterverwendet.
rem      Alternativ wird build\github-cli\bin\gh.exe verwendet, falls vorhanden.
rem   4. Für den lokalen Build: ESP-IDF 6.1 mit ESP32-S3-Tools installieren.
rem      https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/get-started/
rem      Erkannt werden IDF_PATH, idf.currentSetup in .vscode\settings.json
rem      und C:\esp\v6.1\esp-idf. Andere Installation: -IdfPath "C:\Pfad\esp-idf"
rem      EIM-Installationen unter C:\Espressif\tools werden automatisch erkannt.
rem      Andere Werkzeugablage vorher setzen: set "IDF_TOOLS_PATH=C:\Pfad\Tools"
rem      Bei abweichender Python-venv: set "IDF_PYTHON_ENV_PATH=C:\Pfad\venv"
rem   5. Originalen privaten OTA-Schlüssel aus dem eigenen Backup nach
rem      secrets\ota_signing_key.pem kopieren. Niemals einen neuen erzeugen!
rem      Zum Bauen NUR auf GitHub genügt stattdessen -SkipLocalBuild.
rem
rem GitHub muss bereits wie für dieses Projekt eingerichtet sein:
rem   - Repository WR-Design-Dev/CI-Lights, Branch main und Workflow firmware.yml.
rem   - Environment firmware-signing mit Secret OTA_SIGNING_KEY_PEM (nur main).
rem   - GitHub Pages: Quelle "GitHub Actions"; Actions darf Releases schreiben.
rem   Der Secret-Wert und Zugangstokens gehören NIEMALS in dieses Skript.
rem
rem Verwendung:
rem   publish.bat
rem   publish.bat -Message "Meine Änderung"
rem   publish.bat -SkipLocalBuild
rem   publish.bat -CheckOnly
rem   publish.bat -Message "Meine Änderung" -Yes
rem -CheckOnly prüft Voraussetzungen ohne Build, Commit oder Push.
rem -Yes bestätigt den Commit ohne Rückfrage; nur bewusst verwenden.
rem Ohne -Yes werden die vorgemerkten Dateien vor dem Commit angezeigt.
rem Projektdateien werden übernommen, lokale .vscode\settings.json nicht.
rem Git-ignorierte Schlüssel, Backups, build und dist werden nicht hochgeladen.
rem Ohne neue Commits wird ein neuer GitHub-Build manuell gestartet.
rem Die veröffentlichte Versionsnummer wird immer von GitHub vergeben.
rem Das Skript wartet auf Build, Signierung, Release und Pages-Veröffentlichung.
rem Für Automatisierung ohne abschließende Pause: set CI_LIGHTS_NO_PAUSE=1
rem
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\publish_firmware.ps1" %*
set "taskExit=%ERRORLEVEL%"
echo.
if not "%CI_LIGHTS_NO_PAUSE%"=="1" pause
exit /b %taskExit%
