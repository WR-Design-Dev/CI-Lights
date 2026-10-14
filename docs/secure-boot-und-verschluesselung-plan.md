# Plan: Zwei Firmware-Varianten mit optionaler eFuse-Aktivierung

Stand: 14. Oktober 2026. Projekt: CI-Lights, ESP32-S3, ESP-IDF 6.1.

Status: **Software umgesetzt; Hardware-Abnahme ausstehend.** Dieser ursprüngliche Plan dokumentiert die Anforderungen. Beide Profile, Signierabläufe, verschlüsselte lokale Schlüssel, GitHub-Secrets, Paketprüfungen und die Browser-Auswahl sind implementiert. Es wurde kein Gerät geflasht und keine eFuse durch den Agenten programmiert. Der Betreiber testet die Secure-Erstinstallation anschließend selbst. Verbindliche Umsetzung, aktuelle Pfade und Abläufe: [Secure-Firmware](secure-firmware.md).

## 1. Gewünschtes Ergebnis

Auf der bestehenden GitHub-Flash-Seite werden zwei vollständige Installationspakete angeboten:

| Eigenschaft | Standard – ohne eFuse-Aktivierung | Gesichert – Schutz beim ersten Start aktivieren |
| --- | --- | --- |
| Zielgruppe | Entwicklung und Geräte mit einfacher USB-Wiederherstellung | Geräte mit Schutz gegen Auslesen gespeicherter Geheimnisse und fremde Firmware |
| OTA-Signaturprüfung | Weiterhin aktiv, bisheriger Signierschlüssel | Aktiv, eigene Signierschlüssel für diese Geräte |
| Hardware-Secure-Boot | Aus | Secure Boot v2 mit RSA-3072 |
| Flash-Verschlüsselung | Aus | Einzigartiger Geräteschlüssel, Release-Modus |
| NVS-Verschlüsselung | Aus | Aktiv, getrennte NVS-Schlüssel |
| Sicherheits-eFuses | Keine Aktivierung durch diese Variante | Einmalige Aktivierung beim ersten Start |
| Spätere Updates | Standard-OTA oder bisheriger USB-Weg | Ausschließlich signierte OTA-Updates; USB-/UART-/ROM-Updates dauerhaft gesperrt |
| Rückkehr zur Standard-Variante | Möglich, solange das Gerät noch ungesichert ist | Nach Aktivierung nicht mehr möglich |

Die Auswahl gilt für **noch nicht gesicherte Geräte**. Ein bereits gesichertes Gerät wird nicht durch die Auswahl „Standard“ oder durch Löschen des Flashs wieder entsichert. Für teilweise provisionierte oder unbekannte Geräte wird kein gewöhnlicher Installationsvorgang freigegeben.

Verbindliche Ergänzung: Nach Aktivierung der Verschlüsselung sind ausschließlich OTA-Updates erlaubt. Bei einem Leak von K0 muss auf K1 oder K2, bei einem späteren Leak von K1 auf K2 gewechselt werden können. Der Wechsel und der endgültige Widerruf kompromittierter Signierschlüssel erfolgen auf dem Gerät über eine signierte OTA-Firmware.

## 2. Was die Schutzfunktionen leisten

Secure Boot prüft die Echtheit von Bootloader und Anwendung. Flash Encryption schützt die gespeicherten Firmware- und ausgewählten Datenbereiche. NVS Encryption schützt insbesondere WLAN-Zugangsdaten und Jenkins-Token. Die Kombination soll das einfache Auslesen und das Einschleusen einer eigenen Auslese-Firmware verhindern. [Espressif: Sicherheitsübersicht](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/security/security.html).

„Von der Hardware kann nichts ausgelesen werden“ wäre zu weitgehend: Rohdaten können gegebenenfalls als verschlüsselter Flash-Inhalt kopiert werden; autorisierte Firmware verarbeitet entschlüsselte Daten. Softwarefehler und aufwendige physische Angriffe bleiben außerhalb einer absoluten Schutzgarantie. Die öffentlich angebotenen Firmware-Dateien bleiben öffentlich; Flash Encryption macht den GitHub-Download nicht geheim.

Die bis zu drei Secure-Boot-Plätze enthalten **Digests öffentlicher Schlüssel**, keine privaten Signierschlüssel und keine drei Flash-Verschlüsselungsschlüssel. Pro Bootloader bzw. Anwendung sind bis zu drei Signaturen möglich. [Espressif: Secure Boot v2](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/security/secure-boot-v2.html).

## 3. Ausgangslage im Repository

| Stelle | Heutiger Zustand | Erforderliche Planung |
| --- | --- | --- |
| [sdkconfig.defaults](../sdkconfig.defaults), [sdkconfig](../sdkconfig) | OTA-Signierung ohne Hardware-Secure-Boot; Flash- und NVS-Verschlüsselung aus | Zwei getrennte, ausdrücklich konfigurierte Build-Profile |
| [partitions.csv](../partitions.csv) | 4 MiB, zwei OTA-Slots, NVS und Branding; keine NVS-Schlüsselpartition | Eigenes geprüftes Layout für die gesicherte Variante |
| [main/app_ota.c](../main/app_ota.c) | Buildprüfung verlangt `CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT`; ein gemeinsames Manifest; feste Slot-Adressen | Prüfung für beide Profile, getrennte Update-Ziele und Profilprüfung |
| [main/app_settings.c](../main/app_settings.c) | `nvs_flash_init()`, NVS-Zugangsdaten, automatische NVS-Löschung bei bestimmten Fehlern | Verschlüsselte Initialisierung und ausdrücklich definierte Fehler-/Resetbehandlung |
| [main/app_branding.c](../main/app_branding.c) | Eigene Partition, Header mit 20 Bytes, beliebige Nutzdatenlängen | Verschlüsselungsfähiges Speicherformat und Schreibausrichtung |
| [tools/package_firmware.py](../tools/package_firmware.py) | Lehnt Hardware-Secure-Boot, Flash Encryption und gesetzte Partitionsflags ausdrücklich ab | Getrennte Prüfvorschriften und Pakete |
| [tools/prepare_ota_key.py](../tools/prepare_ota_key.py) | Ein Schlüsselpfad und ein öffentlicher Fingerabdruck | Profilbezogene Schlüsselzuordnung ohne automatische Rotation |
| [.github/workflows/firmware.yml](../.github/workflows/firmware.yml) | Ein Produktions-Build und ein OTA-/Browser-Paket | Beide Profile bauen und gemeinsam veröffentlichen |
| [web-flasher/flash-core.mjs](../web-flasher/flash-core.mjs), [installer.js](../web-flasher/installer.js) | Ein Manifest, vier Dateien, feste Adressen, Full-Erase möglich, automatischer Reset | Variantenwahl, Gerätezustandsprüfung und separater Erstinstallationsablauf |
| [tools/publish_firmware.ps1](../tools/publish_firmware.ps1) | Ein lokaler Build und bestehender Produktionsschlüssel | Beide Profile bzw. kontrollierten Verzicht auf lokalen Secure-Build berücksichtigen |

Die vorhandene lokale OTA-Schlüsseldatei wurde bereits gegen den öffentlichen Repository-Fingerabdruck geprüft. Sie wird für Standard-Geräte erhalten. Ihre privaten Inhalte gehören nicht in diesen Plan.

## 4. Architektur und Vorgaben

1. Profile `standard` und `secure` aus demselben Quellstand bauen, mit getrennten Build-Verzeichnissen und getrennten erzeugten `sdkconfig`-Dateien. Das vorhandene `sdkconfig` darf das andere Profil nicht unbeabsichtigt übersteuern.
2. Standard-Geräte behalten das heutige Verhalten und den bestehenden OTA-Schlüssel.
3. Secure-Geräte erhalten drei unabhängig erzeugte Secure-Boot-Schlüssel K0, K1 und K2. Anfangs signiert K0 laufende Releases; K1 und K2 sind getrennt verwahrte Reserven. K0 → K1 → K2 und bei Bedarf ein direkter Wechsel K0 → K2 müssen unterstützt werden.
4. Den Secure-Bootloader vor der Erstinstallation mit allen drei Schlüsseln signieren. Reguläre Anwendungen nur mit dem aktuell aktiven Schlüssel signieren. Reserveschlüssel nicht sämtlich in den normalen GitHub-Build aufnehmen.
5. Flash-Verschlüsselungsschlüssel beim ersten Start auf dem Gerät erzeugen lassen. Keine gemeinsamen Geräteschlüssel in Firmware, Browser oder GitHub-Secrets hinterlegen.
6. Für NVS zunächst das Flash-Encryption-basierte Verfahren mit einer `nvs_keys`-Partition verwenden. Das vermeidet einen zusätzlichen HMAC-eFuse-Schlüssel und dessen Provisionierung vor Secure Boot.
7. Nach Verschlüsselungsaktivierung sind ausschließlich signierte OTA-Updates erlaubt. ROM-Download dauerhaft deaktivieren und Debugzugänge sperren; keine USB-/UART-/JTAG-/DFU-Update- oder Serviceausnahme vorsehen. „Secure Download Mode“ allein erfüllt diese Anforderung nicht, weil er weiterhin bestimmte Schreibbefehle erlaubt.
8. Keine automatische eFuse-Absicherung eines Standard-Geräts durch ein normales OTA-Update. Der Wechsel erfolgt bewusst über die Secure-Erstinstallation auf der Flash-Seite.

Getrennte Signierschlüssel sind eine projektspezifische Empfehlung: Secure Boot unterscheidet nach vertrauenswürdigen Schlüsseln, nicht nach der Beschriftung einer Download-Schaltfläche. Würden beide Profile denselben Schlüssel nutzen, wären ihre Signaturen allein keine harte Profiltrennung.

Die Mehrfachsignierung des Bootloaders und die spätere konservative Schlüsselrotation richten sich nach [Espressifs Schlüsselverwaltung](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/security/secure-boot-v2.html#key-management). Ein Bootloader-Update über den heutigen App-OTA-Weg wird nicht eingeplant.

## 5. Schlüssel und eFuse-Ressourcen vorbereiten

| Material | Ablage | Schutz und Nutzung |
| --- | --- | --- |
| Bisheriger privater OTA-Schlüssel | Lokal verschlüsselt gesichert; bisheriges GitHub-Environment | Ausschließlich Standard-Geräte |
| Secure-Signierschlüssel K0 | Gesicherte Ablage; separates geschütztes Signier-Environment | Anfangs aktiver Signierschlüssel; nach einem Leak auf Geräten dauerhaft widerrufen |
| Secure-Reserveschlüssel K1/K2 | Getrennte verschlüsselte Offline-Sicherungen oder geeigneter Signierdienst | Bootloader-Erstsignierung; K1 oder K2 bei Wechsel kontrolliert als aktiven Signierschlüssel übernehmen |
| Drei Secure-Boot-Digests | eFuse-Schlüsselblöcke des Geräts | Schreibgeschützt; müssen für die Prüfung lesbar bleiben |
| Flash-XTS-Schlüssel | Pro Gerät in geeigneten eFuse-Schlüsselblöcken | Gegen Software-Auslesen und nachträgliches Schreiben geschützt |
| NVS-XTS-Schlüssel | Pro Gerät in verschlüsselter `nvs_keys`-Partition | Bei der ersten NVS-Initialisierung erzeugen und danach wiederverwenden |

Für Flash Encryption wird vorläufig XTS-AES-256 vorgesehen: zwei 256-Bit-eFuse-Blöcke, zusätzlich drei Blöcke für Secure-Boot-Digests. AES-128 benötigt einen Block und bleibt eine begründbare Alternative. ESP32-S3 hat sechs Schlüsselblöcke; `BLOCK_KEY5` darf laut Enablement-Dokumentation nicht für XTS-AES verwendet werden. Die tatsächliche Belegung wird anhand von Chiprevision und vorhandenen eFuses vorab geprüft und im Gerätetest festgehalten. [Espressif: Enablement und Schlüsselblöcke](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/security/security-features-enablement-workflows.html).

Wichtig für die spätere Durchführung:

- Der Fingerabdruck in `ota_signing_public_key.sha256` ist SHA-256 über DER-SPKI. Er ist **nicht** unmittelbar der von Secure Boot benötigte eFuse-Digest. Dieser wird mit Espressifs Verfahren aus der Secure-Boot-Schlüsselrepräsentation abgeleitet.
- Secure-Boot-Digests erhalten Schreibschutz, keinen Leseschutz. Flash-Schlüssel erhalten Lese- und Schreibschutz. [Espressif: Key-Digest-Behandlung](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/espefuse/burn-key-digest-cmd.html).
- Alle benötigten geheimen eFuse-Schlüssel müssen vor dem abschließenden Sperren weiterer Leseschutzänderungen berücksichtigt sein. Beim kombinierten Standardablauf schützt der Bootloader den Flash-Schlüssel rechtzeitig; ein später ergänzter HMAC-Schlüssel wäre ein anderer Ablauf.
- Unbenutzte Digest-Plätze nicht offen lassen. Bei drei geplanten Schlüsseln alle drei vor der endgültigen Absicherung hinterlegen. Keine aggressive automatische Schlüsselwiderrufung vorsehen.
- Wiederherstellung der privaten Signierschlüssel aus den Backups prüfen, bevor ein Gerät dauerhaft provisioniert wird.

## 6. Build-Profile und Partitionen planen

Für das Secure-Profil werden folgende ESP-IDF-Einstellungen benötigt. Diese Liste ist eine Planungsgrundlage, keine direkt anzuwendende Konfigurationsdatei:

| Bereich | Geplanter Zustand |
| --- | --- |
| Hardware-Secure-Boot | `CONFIG_SECURE_BOOT` und Secure Boot v2 aktiv, RSA-Schema |
| Signierte Images | Bootloader und Anwendung korrekt signiert; Offline-Mehrfachsignierung des Bootloaders |
| Flash Encryption | `CONFIG_SECURE_FLASH_ENC_ENABLED`, Release-Modus, gewählte XTS-Schlüsselgröße |
| NVS | `CONFIG_NVS_ENCRYPTION` und `CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC` |
| OTA | Zwei Slots, bestehende Startprüfung und Boot-Rollback erhalten |
| Debug/Download | ROM-Download dauerhaft aus (`CONFIG_SECURE_DISABLE_ROM_DL_MODE`); JTAG gesperrt, weitere USB-/DFU-/Service-Updatewege ausgeschlossen |
| Ungenutzte Digest-Plätze | Nicht offenhalten |
| Anti-Rollback | Zunächst gesondert bewerten; nicht mit Boot-Rollback oder Secure Boot gleichsetzen |

Die genauen Symbole und Abhängigkeiten sind gegen ESP-IDF 6.1 zu prüfen. Die Version wird für beide Profile fest gepinnt; die bisherige Devcontainer-Vorgabe `latest` darf für diesen Ablauf keine abweichende IDF-Version auswählen. [Espressif: Kconfig-Referenz](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/kconfig-reference.html).

Das Secure-Layout muss mindestens Folgendes abdecken:

- Zwei ausreichend große, passend ausgerichtete OTA-Slots einschließlich Signatur und Padding.
- NVS-Datenpartition mit NVS-Verschlüsselung; nicht einfach das gewöhnliche Flash-`encrypted`-Flag für NVS setzen.
- Mindestens 4 KiB `data/nvs_keys` mit `encrypted`-Flag. Bei der Erstinstallation muss sie aus Sicht der NVS-Initialisierung leer sein.
- Branding als verschlüsselte Datenpartition.
- Bootloadergröße einschließlich drei Signaturblöcken messen. Der heutige Partitionsoffset `0x8000` wird erst nach der Größenprüfung bestätigt; gegebenenfalls müssen Partitionsadressen verschoben werden.
- Alle betroffenen Adressen, Größen, Flags und Layoutkennungen in Paketprüfung, OTA-Code und Flash-Seite gemeinsam abbilden. Falls das heutige Layout nachweislich passt, kann die physische Belegung teilweise erhalten bleiben.

Für die NVS-Schlüsselerzeugung kann `nvs_flash_init()` im passend konfigurierten Profil die leere Schlüsselpartition initialisieren; vorhandene Schlüssel werden wiederverwendet. Eine bestehende Klartext-NVS wird dadurch nicht automatisch in eine vollständige sichere Migration verwandelt. [Espressif: NVS Encryption](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/storage/nvs_encryption.html).

Bei Branding müssen Header, Nutzdatenbeginn und Schreibblöcke an das verschlüsselte Format angepasst werden. Der heutige 20-Byte-Header und beliebige Nutzdatenlängen erfüllen die erforderliche 16-Byte-Schreibausrichtung nicht. CRC, Originaldatenlänge, Slots und Ausfallverhalten sind dabei beizubehalten bzw. zu versionieren. [Espressif: Partitions API](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/storage/partition.html#_CPPv419esp_partition_writePK15esp_partition_t6size_tPKv6size_t).

## 7. Ablauf der Secure-Erstinstallation

Die Seite schreibt ein vollständiges Secure-Paket auf einen noch ungesicherten ESP32-S3. **Der konfigurierte ESP-IDF-Bootloader aktiviert Secure Boot und Flash Encryption beim ersten Start.** Im Browser werden keine privaten Schlüssel transportiert und keine eigenen eFuse-Brennbefehle ausgeführt.

Geplanter Ablauf:

1. Gerätemodell, Flashgröße, Chiprevision und relevanten Sicherheitszustand auslesen. Bei widersprüchlichen Zuständen abbrechen. Ein fehlgeschlagener Verbindungsversuch gilt nicht als Nachweis eines fabrikneuen Geräts.
2. Paket, Variante, vollständige Dateiliste, Größen, Zieladressen und Hashes prüfen. Eine gemischte Installation aus Standard-Bootloader und Secure-Anwendung ausschließen.
3. Vor dem Schreiben ausdrücklich bestätigen lassen: dauerhafte Absicherung, anschließend ausschließlich OTA-Updates, gesperrte USB-/UART-/ROM-Wiederherstellung und Löschen bisheriger Einstellungen.
4. Für die erste Secure-Version einen vollständigen Flash-Neuaufbau mit Neueinrichtung vorsehen. Dadurch bleiben keine alten Klartext-Zugangsdaten in früheren Partitionen oder freien Bereichen erhalten. Standard-Installationen dürfen weiter ihre vorhandene Option zur Datenerhaltung anbieten.
5. Im Download-Modus bleiben, bis alle benötigten Dateien vollständig geschrieben und geprüft sind. Secure-Bootloader möglichst zuletzt schreiben; Reihenfolge und Resetverhalten am konkreten Board testen. Das ersetzt keine Absicherung gegen Stromausfall.
6. Den ersten normalen Boot auslösen. Die eFuse-Aktivierung ist ab diesem Punkt dauerhaft und geschieht vor der späteren OTA-Startbestätigung.
7. Der Bootloader erzeugt den individuellen Flash-Schlüssel, schützt ihn, verschlüsselt die vorgesehenen Bereiche und aktiviert die Secure-Boot-Vertrauensanker im von ESP-IDF vorgegebenen kombinierten Ablauf. Keine eigene willkürliche Reihenfolge implementieren.
8. Beim Anwendungsstart wird NVS verschlüsselt initialisiert. Die dauerhafte ROM-Download-Sperre wird durch den zugehörigen ESP-IDF-Startablauf umgesetzt; nicht alle Sicherheits-eFuses werden zwingend schon im Bootloader geschrieben. Erst ein Zustand mit vollständig aktivierter Sperre erfüllt die OTA-only-Anforderung.
9. Sicherheitszustand und normalen Start prüfen, anschließend WLAN/Jenkins/Verwaltungszugang neu einrichten. Neue vertrauliche Einstellungen werden erst nach erfolgreicher Speicherabsicherung gespeichert.
10. Weitere Boots erkennen den bereits aktivierten Zustand und erzeugen keine neuen Schlüssel. Ein teilweise gesicherter Zustand wird als Fehler behandelt.

Die Aktivierung benötigt eine stabile Stromversorgung und kann zusätzliche Neustarts sowie längere Wartezeiten verursachen. Die Flash-Seite darf nach dem letzten Schreibblock nur „Dateien übertragen; Absicherung startet“ melden. „Gerät erfolgreich abgesichert“ setzt eine tatsächlich geprüfte Rückmeldung voraus. Ein verschwundener USB-Port genügt dafür nicht. [Espressif: Flash Encryption](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/security/flash-encryption.html).

## 8. GitHub-Flash-Seite: Auswahl und Sperren

Vorgeschlagene Texte:

- **Standard – ohne Hardware-Absicherung:** „USB-Neuinstallation bleibt möglich. Gespeicherte Daten sind nicht gegen physisches Auslesen geschützt.“
- **Gesichert – dauerhaft aktivieren:** „Aktiviert beim ersten Start Secure Boot, Flash- und NVS-Verschlüsselung. Die Aktivierung kann nicht rückgängig gemacht werden. Anschließend sind ausschließlich signierte OTA-Updates möglich; USB-/UART-Neuflashen ist dauerhaft gesperrt.“

Standard wird voreingestellt. Die sichere Variante verlangt eine eigene ausdrückliche Bestätigung vor dem ersten Schreibvorgang; die bisherige generische Installationsbestätigung reicht dafür nicht aus.

| Erkannter Zustand | Standard installieren | Secure-Erstinstallation | Gesamten Flash löschen |
| --- | --- | --- | --- |
| Sicherheitsfunktionen aus, geeignetes Gerät | Möglich | Mit ausdrücklicher Bestätigung möglich | Mit bisheriger Bestätigung möglich |
| Secure Boot/Release-Verschlüsselung aktiv | Gesperrt | Gesperrt; Hinweis auf OTA | Gesperrt |
| Teilweise provisioniert, fremde Schlüssel oder unbekannter Zustand | Gesperrt | Gesperrt | Gesperrt; separater Servicefall |

Bei deaktiviertem ROM-Download kann das Gerät eventuell gar nicht mehr über diesen Weg verbunden werden. Deshalb zusätzlich vor der Verbindung erklären, dass gesicherte Geräte OTA verwenden. Eine Verbindungsfehlermeldung darf nicht zum erneuten Löschen oder erzwungenen Schreiben auffordern.

Die derzeit verwendete Bibliothek `esptool-js@0.6.1` darf nicht unverändert als Sicherheitsprüfung vorausgesetzt werden: Ihr `main()` lädt einen RAM-Flasher-Stub; im geprüften Loader-Quelltext gibt es keine fertige `getSecurityInfo`-Methode. Der Plan benötigt eine getestete ROM-Sicherheitsabfrage vor Stub, Flashschreiben und Löschen oder eine entsprechend überprüfte Bibliotheksversion. [Espressif: Loader-Quelltext v0.6.1](https://github.com/espressif/esptool-js/blob/v0.6.1/src/esploader.ts).

Secure Download Mode unterstützt keinen Flasher-Stub und keinen normalen Speicher-/Registerzugriff, erlaubt aber weiterhin bestimmte Flash-Schreibbefehle. Er wird deshalb nicht als endgültiger Gerätezustand verwendet. Auch USB-OTG-ROM-Funktionen ändern sich mit aktivierten Sicherheitsfunktionen. Die tatsächliche USB-Anbindung des ESP32-S3-Zero muss am Board geprüft werden; sämtliche nicht über OTA laufenden Firmware-Updatewege sind dauerhaft zu sperren. [Espressif: ROM-Download-Einstellungen](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/kconfig-reference.html#config-secure-uart-rom-dl-mode).

Beide Varianten, Bestätigungen, Fehler und Hinweise in Deutsch und Englisch ergänzen. Nach Schreibbeginn darf die Variante nicht mehr gewechselt werden. Nach Erstprovisionierung ist eine Sicherheitsstatusanzeige in der Geräteverwaltung vorzusehen, die nur Status und öffentliche Kennungen zeigt.

## 9. CI, Releases und OTA getrennt veröffentlichen

Vorgeschlagene Struktur:

| Zweck | Standard | Secure |
| --- | --- | --- |
| Build-Verzeichnis | `build-standard/` | `build-secure/` |
| App-Release-Datei | Weiterhin `CI-Lights.bin` | `CI-Lights-secure.bin` |
| OTA-Manifest | Weiterhin `ota-manifest.json` | `ota-manifest-secure.json` |
| Browser-Manifest | Bisherigen Pfad für Kompatibilität erhalten | Zusätzlich `install-manifest-secure.json` |
| CI-Signier-Environment | Bestehendes `firmware-signing` | Separates `firmware-secure-signing`, nur vertrauenswürdiges `main` |
| Paketidentität | Profil `standard`, bisheriges Layout | Profil `secure`, eigenes geprüftes Layout |

Beide Profile erhalten dieselbe Release-Version und denselben Commit als Herkunft. Dateinamen, Manifestfelder und Prüfsummen trennen die Pakete eindeutig. Der vorhandene `/releases/latest/download/ota-manifest.json`-Pfad bleibt für bereits ausgelieferte Standard-Geräte erhalten.

Die Secure-Anwendung verwendet ihren eigenen Manifestpfad und prüft Zielprofil, Chip, Layout, Größe und Signatur vor dem Umschalten. Das erwartete Profil muss mit der signierten Anwendung verbunden sein; eine beliebige Manifest-Beschriftung allein reicht nicht. Die getrennten Schlüssel liefern zusätzlich die Hardware-Signaturtrennung.

Für den mehrfach signierten Secure-Bootloader wird ein kontrollierter separater Freigabeprozess vorgesehen. Ein freigegebenes Bootloader-Artefakt kann mit passenden Secure-Anwendungen paketiert werden; IDF-Version, Bootloader-Konfiguration, Hash und Kompatibilität müssen feststehen. Ein Wechsel dieser Grundlage benötigt erneut die kontrollierte Bootloader-Signierung. Reserveschlüssel bleiben dabei außerhalb regulärer Runner.

Bei einer Rotation werden aktiver Signierschlüssel, erwarteter öffentlicher Fingerabdruck und Schlüsselkennung im Secure-Signierprozess gemeinsam umgestellt. Ein neues Secret allein widerruft K0 oder K1 auf keinem Gerät. Die Freigabe eines Reserveschlüssels erfolgt kontrolliert; der jeweils andere Reserveschlüssel bleibt getrennt verwahrt. Alte und neue Geräte können während des Rollouts unterschiedliche Widerrufszustände haben, die in der Verwaltung sichtbar sein müssen.

Die Paketprüfung muss künftig Bootloader- und App-Signaturen, Profile, Partitionen, Verschlüsselungsflags, Größen und Flash-Header passend prüfen. Sie darf nicht einfach die heutigen Sicherheitsprüfungen entfernen. Beide Release-Pakete und die Seite erst gemeinsam veröffentlichen, wenn alle vorgesehenen Prüfungen bestanden sind.

Pull Requests und andere Branches erhalten keine Produktions-Secure-Schlüssel. Testsignierte Secure-Images sind ausschließlich für gekennzeichnete Testgeräte; sie dürfen nicht auf die öffentliche Produktions-Flash-Seite gelangen.

## 10. OTA, Reset, Wiederherstellung und Schlüsselrotation

- OTA bleibt nutzbar: Die Secure-Anwendung lädt ein signiertes Image und der OTA-Schreibpfad verschlüsselt es mit dem jeweiligen Geräteschlüssel. Es wird kein universeller verschlüsselter Download für alle Geräte erzeugt.
- Erstprovisionierung ist kein gewöhnliches App-OTA. Sie benötigt den passenden Bootloader, das Layout und die bewusst ausgelöste erste Absicherung.
- Werkseinstellungen löschen Konfigurationsdaten, aber weder eFuses noch dauerhaft benötigte Schlüssel. Der BOOT-Taster soll die Einrichtung weiterhin zurücksetzen können, auch wenn ROM-Download gesperrt ist.
- Beschädigte oder fehlende NVS-Schlüssel führen nicht zu automatischer Neuerzeugung mit stillschweigendem Datenverlust. Die heutige NVS-Fehlerbehandlung ist dafür ausdrücklich zu prüfen.
- Ein Gesamtlöschen auf gesicherter Hardware kann das Gerät unbootbar machen; eFuses bleiben gesetzt. Diese Funktion wird auf der Flash-Seite gesperrt.
- Beim auf dem Gerät erzeugten Flash-Schlüssel gibt es keine Host-Kopie. USB-/UART-Recovery ist nach vollständiger Aktivierung ausgeschlossen. Ist auch OTA nicht mehr ausführbar und keine gültige Rückfallebene vorhanden, ist das Gerät unter dieser Vorgabe nicht mehr per normalem Service-Flash reparierbar.
- Schlüsselwechsel und Schlüsselwiderruf erfolgen ausschließlich durch eine korrekt signierte OTA-Firmware. Keine ungeschützte HTTP-Schaltfläche, frei änderbare Manifestangabe oder USB-eFuse-Prozedur zum Widerruf vorsehen.
- Widerruf ist endgültig. Nach K0 → K1 bleibt K2 Reserve; nach K1 → K2 ist K2 der letzte verfügbare Signierschlüssel. Ein verbrauchter Digest-Platz ist kein beliebig wiederbeschreibbarer Schlüsselplatz.

### 10.1 Vorgehen bei einem geleakten Signierschlüssel

K0, K1 und K2 sind **Signierschlüssel**. Dieser Ablauf ersetzt weder den Flash-XTS-Schlüssel noch die NVS-Schlüssel. Ein Leak eines Verschlüsselungsschlüssels wird nicht durch den Wechsel von K0 zu K1 behoben.

Voraussetzungen, die bereits vor Auslieferung erfüllt sein müssen:

- Alle drei passenden öffentlichen Digests sind hinterlegt; K1 und K2 sind nicht widerrufen.
- Der installierte Bootloader besitzt von Anfang an gültige Signaturen für K0, K1 und K2. Er muss bei jedem Schlüsselwechsel ohne Neuflashen weiter vertrauenswürdig bleiben.
- Die Secure-OTA-Prüfung akzeptiert gültige Signaturen aller noch nicht widerrufenen hinterlegten Schlüssel. Sie darf nicht allein den Schlüssel der gerade laufenden Anwendung festschreiben.
- Die signierte Wechsel-Firmware enthält eine eindeutige Vorgabe zu neuem Schlüssel und zu widerrufenden alten Schlüsselindizes. Der Widerruf wird erst nach bestandener Start-/Rollbackprüfung und bestätigter Bootloader-Kompatibilität ausgeführt; Fehler oder Neustarts dürfen nicht versehentlich den letzten gültigen Schlüssel widerrufen.

| Vorfall | Neuer aktiver Schlüssel | Auf betroffenen Geräten zu widerrufen |
| --- | --- | --- |
| K0 geleakt, K1 sicher | K1 | K0 |
| K1 nach erfolgtem Wechsel geleakt, K2 sicher | K2 | K1; K0 bleibt widerrufen |
| K0 und K1 geleakt, K2 sicher | Direkt K2 | K0 und K1 |
| Eine noch ungenutzte Reserve geleakt | Aktuellen sicheren Schlüssel weiter nutzen oder andere sichere Reserve wählen | Die kompromittierte Reserve |
| Alle verbleibenden vertrauenswürdigen Schlüssel kompromittiert oder verloren | Kein geplanter sicherer Ersatz mehr | Kein weiterer Reserveplatz; gesonderte Gerätebewertung erforderlich |

Geplanter Ablauf K0 → K1; für K1 → K2 entsprechend wiederholen:

1. Den betroffenen Signierprozess anhalten und Ursache/Zugriffe bereinigen. Den sicheren Reserveschlüssel erst danach kontrolliert für neue Signaturen freigeben.
2. Eine geprüfte Secure-Firmware ausschließlich mit K1 signieren und über den Secure-OTA-Kanal veröffentlichen. Für diesen Wechsel ist keine neue Signatur mit dem geleakten K0 nötig.
3. Das Gerät lädt und prüft das Update über OTA, startet es und bestätigt die Startprüfung. Bis dahin bleibt K0 für den bisherigen Rückfall erhalten.
4. Vor K0-Widerruf die K1-Signatur des unveränderten Bootloaders prüfen. Soll Boot-Rollback erhalten bleiben, muss zusätzlich eine tatsächlich lauffähige, mit einem verbleibenden gültigen Schlüssel signierte Anwendung im anderen OTA-Slot vorhanden sein. Diese Rückfallebene nötigenfalls durch einen weiteren kontrollierten OTA-Schritt vorbereiten.
5. Die bestätigte Firmware widerruft K0 mit Espressifs API `esp_ota_revoke_secure_boot_public_key()` für den zugehörigen Digest-Index. Danach eFuse-Status prüfen und den erledigten Wechsel anzeigen. Derselbe Vorgang nach einem Neustart erkennt den bereits erfolgten Widerruf.
6. Neue Releases mit K1 signieren; K2 bleibt Reserve. Bei einem späteren K1-Leak denselben Ablauf mit K2 und Widerruf von K1 durchführen. Sind K0 und K1 bereits kompromittiert, beide nach geprüftem direktem K2-Wechsel widerrufen.

Das Verfahren folgt der konservativen Schlüsselwiderrufung. [Espressif: Key Revocation](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/security/secure-boot-v2.html#key-revocation), [Espressif: OTA-Widerruf-API](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/system/ota.html).

Die Firmware erkennt einen privaten Schlüsselleak nicht von selbst. Der Betreiber löst den Wechsel durch das entsprechende signierte Release aus. Ein offline gebliebenes Gerät vertraut dem alten Schlüssel bis zum erfolgreichen Widerruf weiterhin. Ist bereits fremde, mit dem kompromittierten Schlüssel signierte Firmware installiert, ist die Rettung per OTA nicht garantiert; ein bloßes Ersetzen des GitHub-Secrets beseitigt diese Kompromittierung nicht.

Boot-Rollback und ein möglicher Schutz vor älteren, aber korrekt signierten verwundbaren Versionen sind getrennte Themen. Hardware-Anti-Rollback erst mit eigener Versionsstrategie, endlichem eFuse-Budget und geprüfter Wechselwirkung zur Rückfallebene freigeben. [Espressif: OTA und Anti-Rollback](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/system/ota.html#anti-rollback).

## 11. Umsetzung in späteren Arbeitspaketen

Die Software-Arbeitspakete sind umgesetzt. Die folgenden ursprünglichen Abnahmepunkte bleiben als Prüfliste erhalten; insbesondere die Hardware-Prüfungen sind noch offen. Auf Wunsch des Betreibers liegen K1/K2 zusätzlich als ungenutzte Secrets im geschützten GitHub-Environment `secure-key-recovery`; sie werden nicht in den regulären Build aufgenommen.

### A. Entscheidungen und Sicherungen

- [ ] Bedrohungsmodell, ausschließlich OTA nach Aktivierung und Umgang mit verlorenen Einstellungen dokumentieren.
- [ ] Konkretes Board, Chiprevision, USB-Anbindung und bestehenden eFuse-Zustand erfassen.
- [ ] AES-Schlüsselgröße und drei Secure-Schlüssel festlegen; dauerhafte Sperre des ROM-Downloads und weiterer lokaler Updatewege prüfen.
- [ ] Standard-Schlüssel sichern; Secure-Schlüssel kontrolliert erzeugen und Wiederherstellung testen.
- [ ] Bootloader-Freigabeprozess und vollständige eFuse-Sollbelegung dokumentieren.

### B. Firmware und Speicher

- [ ] Zwei reproduzierbare Build-Profile mit getrennten Konfigurationen anlegen.
- [ ] Bootloadergröße messen und Secure-Partitionstabelle verbindlich festlegen.
- [ ] OTA-Buildprüfung, Profilidentität und getrennte Manifestpfade ergänzen.
- [ ] NVS-Initialisierung, Migration durch Neueinrichtung und Reset-/Fehlerbehandlung anpassen.
- [ ] Branding-Format für verschlüsselte Schreibzugriffe anpassen.
- [ ] Sicherheitsstatus und Abbruch bei inkonsistentem Zustand einplanen.
- [ ] Geprüften OTA-Schlüsselwechsel K0 → K1 → K2 und direkten Wechsel auf K2 mit endgültigem Widerruf vorsehen.

### C. Pakete, CI und Flash-Seite

- [ ] Profilbezogene Schlüssel- und Paketprüfungen sowie getrennte Release-Dateien erstellen.
- [ ] GitHub-Workflow und lokalen Publish-Ablauf für beide Profile anpassen.
- [ ] Kontrolliert mehrfach signierten Secure-Bootloader in den Paketablauf einbinden.
- [ ] Zwei Auswahlmöglichkeiten, Bestätigungen und Übersetzungen ergänzen.
- [ ] Sicherheitsprüfung vor Schreiben/Löschen; alte eFuse-Zustände und fremde Geräte ablehnen.
- [ ] Secure-Erstinstallation einschließlich vollständigem Neuaufbau und Resetablauf umsetzen.
- [ ] Standard-OTA-Kompatibilität und vorhandene öffentliche Downloadpfade erhalten.

### D. Prüfung und Freigabe

- [ ] Konfiguration, Signaturen, Paketidentität und Browser-Sperren ohne Hardware prüfen.
- [ ] Virtuelle eFuse-/Emulatortests nutzen, soweit für diesen Ablauf unterstützt; sie ersetzen keine Gerätetests.
- [ ] Erst danach dedizierte entbehrliche Testgeräte dauerhaft provisionieren.
- [ ] Erstinstallation, erneute Boots, OTA, Rollback, Reset und Rotation auf echter Hardware nachweisen.
- [ ] K0-Leak, späteren K1-Leak und gemeinsamen K0/K1-Leak einschließlich Stromausfall-/Neustartverhalten beim Wechsel prüfen.
- [ ] Secure-Option erst nach bestandener Abnahme öffentlich anbieten.

## 12. Abnahmekriterien

| Prüffall | Erwartetes Ergebnis |
| --- | --- |
| Standard-Erstinstallation | Keine Aktivierung von Sicherheits-eFuses; bestehender USB- und OTA-Ablauf funktioniert |
| Secure-Erstinstallation | Gewählter Sicherheitszustand nach erstem Boot nachgewiesen; drei passende Digests und geschützter Geräteschlüssel |
| Zweiter/weiterer Secure-Boot | Keine Schlüsselneuerzeugung und keine wiederholte Erstprovisionierung |
| Speicherprüfung | Test-Zugangsdaten nicht im Klartext im rohen Flash; NVS-Schlüsselpartition und Branding geschützt |
| Debug-/Downloadprüfung | Kein Auslesen entschlüsselter Daten und keine USB-/UART-/JTAG-/DFU-/ROM-Firmwareupdates nach vollständiger Aktivierung |
| Ungültige Signatur | App und Bootloader werden abgelehnt |
| Falsche Variante/fremder Secure-Schlüssel | Vor OTA-Umschaltung bzw. Browser-Schreiben abgelehnt |
| Normales Secure-OTA | Update und Bootbestätigung erfolgreich; NVS und Branding bleiben nutzbar |
| Fehlgeschlagenes Secure-OTA | Rückfall auf eine noch vertrauenswürdige und lauffähige Anwendung |
| Werkseinstellungen | Einstellungen zurückgesetzt, Sicherheitszustand und benötigte Schlüssel erhalten |
| Bereits gesichertes Gerät auf Flash-Seite | Standard, Secure-Erstinstallation und Gesamtlöschen gesperrt bzw. kein Download-Zugang |
| Unterbrochene Installation/inkonsistente eFuses | Kein falscher Erfolg und keine automatische unsichere Fortsetzung; definierter Servicefall |
| Schlüsselwechsel K0 zu K1 | Bootloader, App und notwendige Rückfallebene funktionieren nach endgültigem K0-Widerruf |
| Schlüsselwechsel K1 zu K2 | K0 bleibt gesperrt; Bootloader, App und notwendige Rückfallebene funktionieren nach endgültigem K1-Widerruf |
| K0 und K1 gemeinsam kompromittiert | Direkter OTA-Wechsel auf K2 und geprüfter Widerruf beider alten Schlüssel |
| Update nur mit widerrufenem K0/K1 signiert | Abgelehnt; weder als neues OTA noch als gültige Rückfallebene verwendbar |
| Unterbrochener Schlüsselwechsel | Definierter wiederaufnehmbarer Ablauf; letzter gültiger Schlüssel und benötigte Bootfähigkeit erhalten |
| Veröffentlichte Artefakte | Keine privaten Signier-, Flash- oder NVS-Schlüssel enthalten |

Für Hardwaretests vorher/nachher einen öffentlichen Sicherheitsstatus bzw. eFuse-Bericht sichern, ohne geheime Schlüssel auszugeben. Bei vollständig deaktiviertem ROM-Download ist der nachträgliche `espefuse`-Zugriff nicht mehr verfügbar; deshalb muss die Nachprüfung über einen vorab geplanten zulässigen Statusweg erfolgen.

Stromausfalltests während Provisionierung können Testgeräte dauerhaft unbrauchbar machen. Sie gehören nur auf dafür bestimmte Geräte und werden nicht auf vorhandenen produktiven Ampeln durchgeführt.

## 13. Noch vor Umsetzung verbindlich festzulegen

1. Technischer Nachweis der festgelegten OTA-only-Vorgabe: dauerhaft gesperrter ROM-Download sowie ausgeschlossene weitere lokale Firmware-Updatewege.
2. Endgültige Chip-/Board-Unterstützung, eFuse-Blockzuordnung und Secure-Partitionstabelle nach Bootloader-Größenmessung.
3. Exakte Bibliotheks-/Protokolllösung zur zuverlässigen Sicherheitszustandsprüfung auf der Flash-Seite.
4. Verwaltung und Offline-Signierung der Reserveschlüssel, Rollout von K0 → K1 → K2, direkte K2-Rotation sowie sichere Rückfallebene vor endgültigem Widerruf.
5. Ob Hardware-Anti-Rollback Bestandteil einer späteren Ausbaustufe wird.

Die gewünschte Benutzerführung ist fest eingeplant: **zwei Firmware-Varianten auf der GitHub-Flash-Seite, dauerhafte Aktivierung der Secure-Variante beim ersten Boot, anschließend ausschließlich OTA und Wechsel auf die zwei Reserveschlüssel bei einem Signierschlüsselleak**. Die offenen Punkte betreffen die technische Ausführung und den sicheren OTA-Rollout.
