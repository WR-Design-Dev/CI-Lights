# Konfiguration sichern und auf eine Ampel übernehmen

In der Verwaltung unter **Konfiguration → Konfiguration exportieren** ein eigenes
Sicherungspasswort eingeben und wiederholen. **Verschlüsselte Sicherung
herunterladen** speichert `CI-Lights-Konfiguration.json` im Browser. Das Passwort
hat mindestens 12 und höchstens 128 UTF-8-Bytes und wird nicht auf der Ampel
gespeichert. Ohne dieses Passwort kann die Sicherung nicht wiederhergestellt
werden. Datei und Passwort getrennt aufbewahren.

Die Sicherung enthält alle gespeicherten WLAN-Profile mit Zugangsdaten und
Reihenfolge, Jenkins-URL, Benutzer, Token, ausgewählten Job, Abfrageintervall und
Timeouts, Verwaltungskonto einschließlich gespeichertem Passwortprüfwert,
Seitentitel, Gerätenamen, Sprache, CPU-Modus, LED-Helligkeit, Farben, manuelle
Schaltzustände, Betriebsart, Disco-/Build-Effekt, OTA-Automatik mit Zeitzone und
Zeitplan sowie Banner und Favicon. Noch nicht gespeicherte Eingaben gehören
nicht zur Sicherung. Der aktuelle Jenkins-/REST-API-Status und der
Browser-Cache der Jobliste sind flüchtige Zustände.

**Konfiguration importieren** ist in demselben Verwaltungstab und im ersten
Einrichtungsdialog vorhanden. Neue Ampel wie gewohnt über ihr Einrichtungs-WLAN
verbinden, dann die Datei auswählen, das Sicherungspasswort eingeben und
**Datei prüfen** wählen. Die Entschlüsselung kann etwas dauern. Die
Zusammenfassung zeigt Gerätenamen, WLANs, Jenkins-Job und Verwaltungsbenutzer.
Erst nach der Bestätigung **Aktuelle Einstellungen ersetzen …** und einem Klick
auf **Konfiguration übernehmen und neu starten** wird etwas gespeichert.
Ein falsches Passwort oder eine ungültige Datei verändert die Konfiguration
nicht. Bestehende WLAN-Profile und ein vorhandenes Branding werden vollständig
durch die Sicherung ersetzt, auch wenn dort kein Banner/Favicon hinterlegt ist.

Nach dem Neustart verbindet sich die Ampel mit dem ersten erreichbaren WLAN der
importierten Prioritätenliste. Zur Anmeldung gilt das ursprüngliche
Verwaltungspasswort der Quell-Ampel, nicht das Sicherungspasswort. Der gewünschte
Gerätename wird übernommen; bei einem Namenskonflikt greift der vorhandene
MAC-Namensfallback. Bei unbekannten WLAN-Zugangsdaten startet wieder die
Einrichtung, die sich dann mit dem importierten Verwaltungskonto anmelden lässt.

Standard und Secure können dieselbe Konfigurationsdatei verwenden, sofern die
Firmware das Dateischema unterstützt. Firmware, Signierschlüssel,
Flash-/NVS-Schlüssel, eFuses, MAC-Adresse und Sicherheitsprofil bleiben Eigenschaften
des Zielgeräts. Auch dessen OTA-Verlauf und Sperre einer fehlgeschlagenen
Firmware-Version werden beibehalten. Import und Export sind während eines
Firmware-Updates bzw. einer noch offenen OTA-Startprüfung gesperrt.

Die Datei verwendet AES-256-GCM mit zufälliger 16-Byte-Salt, 12-Byte-Nonce und
16-Byte-Authentifizierungstag. Der Schlüssel wird aus dem Sicherungspasswort mit
PBKDF2-HMAC-SHA256 und 600.000 Iterationen abgeleitet. Version 1 enthält die
Felder `format`, `schema`, `kdf`, `iterations`, `cipher`, `salt`, `nonce` und
`data`; binäre Werte sind Base64-codiert. Die GCM-Zusatzdaten sind
`CI-Lights encrypted configuration v1`; `data` enthält Chiffretext gefolgt vom
Authentifizierungstag. Der entschlüsselte Inhalt hat das eigene Schema
`ci-lights-config`, Version 1. Es gibt keinen unverschlüsselten Export.

Die Übertragung zur Ampel nutzt die bestehende lokale Verwaltungsverbindung.
Dateiverschlüsselung ersetzt keine HTTPS-Transportverschlüsselung; nur im
vorgesehenen vertrauenswürdigen IoT-/Einrichtungsnetz verwenden.

Der Import schreibt einen inaktiven Einstellungsbereich und inaktive
Branding-Slots. Erst nach vollständigem Schreiben wird ein einzelner
NVS-Auswahlwert umgeschaltet. Bis zum Neustart bleibt die bisherige Konfiguration
im Speicher aktiv. Ein Werksreset löscht beide Einstellungsbereiche und behält
wie bisher das aktuell gewählte Branding. Die NVS- und Branding-Adressen sowie
der freigegebene Secure-Bootloader bleiben unverändert.

Prüfungen nach einem ESP-IDF-Build:

```sh
node tests/test_backup_ui.js
python -m unittest discover -s tests -p 'test_config_backup.py'
```

Die nativen Tests laufen unter Linux/WSL mit C-Compiler und OpenSSL 3. Sie
verwenden den Produktionscode mit simuliertem NVS/Flash und einer
OpenSSL-Implementierung der PSA-Schnittstelle. Sie prüfen vollständige
Wiederherstellung, Ersteinrichtung, Manipulationen, ungültige Einstellungen,
Schreibfehler, beide Speicherbereiche und maximale Dateigrößen. Den tatsächlichen
PSA-/FreeRTOS-Ablauf und die WLAN-Verbindung auf Hardware zusätzlich prüfen.
