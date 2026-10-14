# Secure-Firmware: Einrichtung, Schlüssel und Updates

Stand: 14. Oktober 2026. ESP-IDF 6.1, ESP32-S3-Zero mit ESP32-S3FH4R2,
**4 MiB Flash und 2 MiB Quad-PSRAM**. Die Software und Paketprüfungen sind
implementiert. Die Abnahme auf einem echten ESP steht noch aus; der Betreiber
führt den Erstinstallationstest über die Flash-Seite durch.

## Zwei Varianten

| Funktion | Standard | Secure |
| --- | --- | --- |
| Signierte OTA-Updates | Bisheriger Standardschlüssel | Eigene Schlüssel K0/K1/K2 |
| Hardware-Secure-Boot | Aus | RSA-3072, Secure Boot v2 |
| Flash-/NVS-Verschlüsselung | Aus | An, eigene Schlüssel je Ampel |
| USB-Neuinstallation | Möglich | Nach Aktivierung dauerhaft gesperrt |
| JTAG | Keine neue Sperre | Dauerhaft gesperrt |
| Erstinstallation | Löschen optional | Gesamtes Flash löschen |
| Zurück zu Standard | Möglich | Nach Aktivierung unmöglich |

Secure Boot prüft Bootloader und Anwendung. Verschlüsselung schützt die
gespeicherten Inhalte; der öffentliche Firmware-Download bleibt öffentlich.
Der Installer liest vor jedem Schreib-/Löschvorgang Sicherheits-eFuses und
Schlüsselblöcke. Gesicherte, teilweise konfigurierte, inkompatible oder nicht
vollständig prüfbare Geräte werden abgewiesen. Er programmiert selbst keine
eFuses: Der bewusst installierte Secure-Bootloader aktiviert die Hardware beim
ersten Start. Die Website prüft alle Downloads vor dem Löschen, schreibt den
Bootloader zuletzt und setzt erst nach vollständiger Übertragung zurück.

Stabile Stromversorgung während Erstinstallation und erstem Start angeschlossen
lassen. Anschließend in der Ampel-Verwaltung **Sicherheitsstatus** öffnen.
Der Status befindet sich innerhalb des Panels **Firmware-Updates**.
`/api/security` liefert den öffentlichen Bericht unter der bestehenden
Administrator-Anmeldung. `ready`, `secure_boot`, `flash_encryption`,
`nvs_encryption` und `rom_download_disabled` müssen wahr sein. Ein erfolgreicher
Dateitransfer oder ein verschwundener USB-Port allein bestätigt die Aktivierung
nicht. Wenn keine Firmware-Partition mehr startet, gibt es keine USB-Rettung.

## Welche Schlüssel wofür?

| Schlüssel | Zweck | Ablage |
| --- | --- | --- |
| Standard-OTA-Schlüssel | Standard-Firmware signieren | `secrets/ota_signing_key.pem`, Environment `firmware-signing` |
| K0 | Anfangs aktive Secure-Signaturen | `secrets/K0.pem.dpapi`, Environment `secure-firmware-signing` |
| K1/K2 | Secure-Reserven, späterer Signierschlüsselwechsel | `secrets/K1.pem.dpapi`, `secrets/K2.pem.dpapi`, zusätzlich geschütztes GitHub-Recovery-Environment |
| Flash-Schlüssel | Flash mit XTS-AES-256 verschlüsseln | Auf jeder Ampel erzeugt, lesegeschützte eFuses |
| NVS-Schlüssel | WLAN, Token und Einstellungen verschlüsseln | Auf jeder Ampel erzeugt: zwei 256-Bit-Schlüssel in `nvs_keys`; diese Partition ist durch Flash Encryption geschützt |

K0/K1/K2 sind RSA-Signierschlüssel, keine Datenverschlüsselungsschlüssel. Ein
OTA-Signierschlüsselwechsel ändert weder Flash- noch NVS-Schlüssel. Sie werden
nicht vom Browser übertragen oder auf GitHub gesichert. NVS-Schlüssel werden
nur für einen leeren Einstellungsbereich erzeugt; beschädigte/verlorene
Schlüssel bei vorhandenen Daten führen zum Abbruch. Ein Einstellungen-Reset
löscht die benötigten Schlüssel und eFuse-Sperren nicht.

Die drei öffentlichen Secure-Boot-Digests werden auf der Ampel festgelegt.
`security/K*.pub.pem`, `secure-keyring.json` und der mit allen drei Schlüsseln
signierte `secure-bootloader.bin` sind öffentlich. Es werden fünf der sechs
Schlüsselblöcke für drei Digests und den zweiteiligen Flash-XTS-Schlüssel
benötigt; Flash-Schlüssel dürfen nicht in `BLOCK_KEY5` liegen. Die tatsächliche
Belegung bestimmt ESP-IDF beim Erststart und die Firmware prüft sie anschließend.
Hardware-Anti-Rollback und aggressive Schlüsselwiderrufung sind ausgeschaltet.

## Lokale Sicherung und Wiederherstellung

`secrets/` wird von Git ignoriert. Die `.dpapi`-Dateien sind unter dem aktuellen
Windows-Benutzerprofil verschlüsselt und durch Dateirechte geschützt. Sie sind
allein **kein portables Backup** für einen neuen Computer oder ein verlorenes
Windows-Profil. Vor der dauerhaften Geräteaktivierung zusätzlich
passwortverschlüsselte PKCS8-Backups auf einem getrennten Datenträger erstellen:

```powershell
python tools/prepare_secure_keys.py export --index 0 --output E:\CI-Lights-Backup\K0.encrypted.pem
python tools/prepare_secure_keys.py export --index 1 --output E:\CI-Lights-Backup\K1.encrypted.pem
python tools/prepare_secure_keys.py export --index 2 --output E:\CI-Lights-Backup\K2.encrypted.pem
```

Jeder Export fragt das Passwort verdeckt zweimal ab, verlangt mindestens 16
Zeichen und prüft die Wiederherstellung sowie die öffentliche Schlüsselidentität.
Das Passwort getrennt vom Backup verwahren. Den Standardschlüssel ebenfalls in
einem verschlüsselten Backup sichern. Vorhandene Schlüssel niemals neu erzeugen.

Auf einem neuen Windows-Profil, wenn die entsprechende lokale Datei fehlt:

```powershell
python tools/prepare_secure_keys.py restore --index 1 --input E:\CI-Lights-Backup\K1.encrypted.pem
```

Die Wiederherstellung prüft den registrierten öffentlichen Schlüssel, erzeugt
eine neue DPAPI-Datei für das aktuelle Profil und überschreibt keine vorhandene
Datei. Private Inhalte erscheinen weder im Terminal noch in Argumenten.

## GitHub-Environments

| Environment | Secrets | Zugriff |
| --- | --- | --- |
| `firmware-signing` | `OTA_SIGNING_KEY_PEM` | Nur Branch `main`, Standard-Build |
| `secure-firmware-signing` | `SECURE_SIGNING_KEY_PEM` | Nur Branch `main`, aktive Secure-Signatur |
| `secure-key-recovery` | `SECURE_RESERVE_K1_PEM`, `SECURE_RESERVE_K2_PEM` | Nur Branch `main`, manuelle Freigabe durch den Eigentümer, kein Administrator-Bypass |

K1/K2 wurden auf ausdrücklichen Wunsch des Betreibers zusätzlich bei GitHub
hinterlegt. **Kein regulärer Workflow referenziert die Recovery-Secrets.**
Die reservierten Schlüssel bleiben lokal erhalten. Zur Einrichtung:

```powershell
python tools/configure_secure_github.py --apply
# Nur wenn zusätzlich die GitHub-Reserve-Sicherung ausdrücklich gewünscht ist:
python tools/configure_secure_github.py --apply --store-reserves
```

Die GitHub-Oberfläche/API zeigt Secret-Werte nicht wieder an. Ein bewusst
eingerichteter und freigegebener Wiederherstellungsworkflow könnte sie lesen
und verschlüsselt exportieren; ein solcher Workflow ist aktuell nicht vorhanden.
Die Freigabe schützt vor automatischer Nutzung, nicht vor einer vollständigen
Kompromittierung des Eigentümerkontos. Deshalb Offline-Backups behalten.
[GitHub: Secrets](https://docs.github.com/en/actions/how-tos/write-workflows/choose-what-workflows-do/use-secrets),
[GitHub: Environment-Schutz](https://docs.github.com/en/actions/reference/workflows-and-actions/deployments-and-environments).

## Bauen und veröffentlichen

Im ESP-IDF-6.1-Terminal:

```powershell
python tools/build_profiles.py --profile both
python tools/package_firmware.py --build-dir build-standard --secure-build-dir build-secure
```

`build-standard/` und `build-secure/` haben getrennte Konfigurationen und dieselbe
Release-Version. Reguläre Secure-Builds öffnen nur den aktiven Schlüssel in einem
kurzzeitig geschützten Signierordner. Der eingefrorene Bootloader wird öffentlich
auf Hash, Konfiguration und alle drei Signaturen geprüft; die Reserven werden
dafür nicht geöffnet. Seine erste Offline-Freigabe verwendet
`--approve-bootloader`; eine bestehende Freigabe wird erhalten.

`publish.bat` prüft und baut beide Profile, veröffentlicht über GitHub Actions
und wartet auf Release/Pages. `-SkipLocalBuild` verwendet ausschließlich CI.
Produktionspakete enthalten Standard- und Secure-Dateien mit getrennten
Manifesten. Der Verpackungsjob benötigt keine privaten Schlüssel. Andere
Branches und Pull Requests bauen nur mit Wegwerf-Schlüsseln und veröffentlichen
keine produktiven Pakete. Entwicklungsartefakte sind nicht zum dauerhaften
Provisionieren produktiver Geräte vorgesehen.

## K0 → K1 → K2 oder direkt K0 → K2

Nach einem Leak zunächst Ursache und Zugriffe bereinigen. Dann beispielsweise:

```powershell
python tools/activate_secure_key.py --index 1 --revoke-mask 1
python tools/configure_secure_github.py --apply
```

Die öffentliche Policy wird in die signierte Anwendung eingebaut. `1` widerruft
K0, `3` widerruft K0 und K1. Der aktive Schlüssel darf nie in der Maske stehen;
die Policy lässt keine Rücknahme eines Widerrufs oder Wechsel zu einem früheren
Index zu. Für direkten Wechsel auf K2: `--index 2 --revoke-mask 3`.

**Zwei aufeinanderfolgende erfolgreiche Secure-OTA-Releases mit dem neuen
Schlüssel sind erforderlich.** Nach dem ersten Start zeigt die Ampel
`waiting_for_signed_fallback`. Erst wenn die laufende und die andere OTA-App
beide als gültig bestätigt sind und beide sowie der Bootloader mit dem neuen
Schlüssel geprüft wurden, schreibt die Firmware den Widerruf in eFuses.
Ein Reset vorher lässt das bisherige Vertrauen erhalten; nachher erkennt die
Firmware den bereits erledigten Widerruf. Kein HTTP-Endpunkt löst einen
beliebigen Widerruf aus. Offline gebliebene Geräte vertrauen alten Schlüsseln
bis zu ihrem eigenen erfolgreichen Wechsel weiter.

## Eigenen Fork einrichten

Ein Fork übernimmt keine privaten GitHub-Secrets. In einem **frischen Windows-
Checkout ohne lokale Schlüssel**, mit `origin` auf den eigenen Fork:

```powershell
python tools/initialize_fork.py --repository MEIN-KONTO/CI-Lights --initialize-new-device-trust
python tools/build_profiles.py --profile both --approve-bootloader
python tools/configure_github.py --repository MEIN-KONTO/CI-Lights --owner MEIN-KONTO --apply
python tools/configure_secure_github.py --apply
```

Der Initialisierer erzeugt vier unabhängige Schlüssel: den Standardschlüssel
und K0/K1/K2. Er ersetzt das geerbte öffentliche Vertrauen ausschließlich im
frischen Fork und verlangt dessen eigenen Bootloader. Er verweigert das
Original-Repository, einen fremden Remote und vorhandene private Schlüssel.
`firmware_repository.txt` und GitHub CI lenken die Updates auf den eigenen Fork;
der Installer übernimmt dessen Release-Links. Bestehende gesicherte
Original-Geräte akzeptieren diese Fork-Firmware nicht. Bei einem eigenen Fork
auch Impressum, Datenschutz und Betreiberangaben anpassen.

## Ausstehende Hardware-Abnahme

Auf dem vorgesehenen Testgerät prüfen: Erststart und eFuse-Status, Fortbestand
von NVS/Branding nach Neustart und OTA, abgewiesene falsche Signaturen,
OTA-Rollback, gesperrte lokale Updatewege sowie alle Schlüsselwechsel inklusive
Rückfall und Unterbrechungen. Reale Schlüsselwiderrufe sind dauerhaft; dafür
eigene Testschlüssel und entbehrliche Geräte verwenden. Automatische Signatur-
und Browser-Tests ersetzen diese Hardware-Abnahme nicht.

[Espressif: Secure Boot v2](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/security/secure-boot-v2.html),
[Flash Encryption](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/security/flash-encryption.html),
[NVS Encryption](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/storage/nvs_encryption.html).
