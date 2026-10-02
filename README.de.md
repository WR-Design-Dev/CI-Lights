# CI-Lights (ESP32-S3)

**Sprachen:** [English](README.md) | Deutsch

**Projektinformation:** Der Code dieses Projekts wurde durch KI erstellt.

> Nur in einem separatem IoT-Netzwerk einsetzen.

## 3D-Druckmodell

Das [3D-Druckmodell für die CI-Lights-Ampel findest du auf MakerWorld](https://makerworld.com/de/models/3352315-ci-lights).

## Installation im Browser und Firmware-Updates

Die Installationsseite wird nach dem ersten erfolgreichen GitHub-Pages-Deployment
unter [wr-design-dev.github.io/CI-Lights](https://wr-design-dev.github.io/CI-Lights/)
bereitgestellt. Mit Chrome oder Edge auf dem Computer und einem USB-Datenkabel
kannst du dort die komplette Firmware installieren, ohne ESP-IDF zu installieren.
Bei einem erneuten USB-Flash die Löschoption deaktiviert lassen, um Einstellungen
und Branding zu behalten. Die Erstinstallation des OTA-Partitionslayouts erfolgt
einmal per USB; die bisherige Firmware kann diese Umstellung nicht per WLAN vornehmen.
Die eigene Flash-Seite nutzt esptool-js 0.6.1 von UNPKG erst nach dem Verbinden.
Englisch ist die Standardsprache; die Flaggen **EN** und **DE** im Seitenkopf
schalten die gesamte Flash-Seite inklusive Meldungen und Rechtstexten um.
Mit **Gerät löschen** kannst du nach Bestätigung den gesamten Flash auch ohne
Installation leeren. Impressum und Datenschutz sind im Seitenfuß verlinkt.

Danach in der Ampel-Verwaltung **Nach Updates suchen** und bei einer neueren
Version **Update installieren** wählen. Die Ampel lädt das öffentliche GitHub
Release über HTTPS, prüft Größe, SHA-256 und RSA-Signatur und startet neu.
Bei einem unvollständigen Download bleibt die bisherige Firmware aktiv.
Die neue Firmware bestätigt einen erfolgreichen lokalen Start nach zehn Sekunden;
ein Neustart vor dieser Bestätigung führt zur vorherigen Firmware zurück.
Einrichtung und gespeicherte Daten bleiben bei OTA-Updates erhalten.

Die GitHub-Einrichtung, die Schlüsselsicherung und der automatische Build sind
in [Firmware-Updates einrichten](docs/firmware-updates.md) beschrieben.

## Entwicklungsumgebung mit Visual Studio Code einrichten

1. [Visual Studio Code](https://code.visualstudio.com/) und die offizielle
   [Espressif-IDF-Erweiterung](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension)
   installieren. In VS Code lässt sich die Erweiterung auch über
   `Strg+Umschalt+X` und die Suche nach `ESP-IDF` finden.
2. Mit `F1` die Befehlspalette öffnen und
   `ESP-IDF: Open ESP-IDF Installation Manager` ausführen. ESP-IDF **6.1.0**
   samt Werkzeugen installieren. Danach
   `ESP-IDF: Select Current ESP-IDF Version` ausführen und diese Installation
   auswählen. Eine bereits vorhandene Installation kann direkt ausgewählt
   werden, wenn die Erweiterung sie erkennt; sonst hilft die verlinkte
   Anleitung zur manuellen Konfiguration. Bei Problemen prüft
   `ESP-IDF: Doctor Command` die Einrichtung.
   [Espressif-Installationsanleitung](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/installation.html)
3. Dieses Repository mit Git klonen oder den bereits vorhandenen Projektordner
   verwenden. In VS Code über `Datei > Ordner öffnen` den Ordner
   **CI-Lights** mit der obersten `CMakeLists.txt` öffnen:

   ```sh
   git clone https://github.com/WR-Design-Dev/CI-Lights.git
   ```

   Es ist ein bestehendes Projekt; der Befehl `ESP-IDF: New Project` wird nicht
   benötigt. Für C/C++-IntelliSense kann
   `ESP-IDF: Add VS Code Configuration Folder` die lokale
   `.vscode`-Konfiguration erzeugen.
4. Das Ziel muss `esp32s3` sein. Es ist in `sdkconfig` bereits gesetzt; falls
   VS Code ein anderes Ziel anzeigt, `ESP-IDF: Set Espressif Device Target`
   ausführen und `esp32s3` wählen. Die vorhandene Konfiguration verwendet
   **4 MB Flash**, eine eigene Partitionstabelle (`partitions.csv`) und
   **2 MB Quad-PSRAM**. Diese Einstellungen beim Konfigurieren beibehalten.
5. Vor dem ersten Build muss `secrets/ota_signing_key.pem` vorhanden sein.
   Der Projektinhaber verwendet den gesicherten Originalschlüssel. Für einen
   eigenen Entwicklungs-Build im ESP-IDF-Terminal einmal
   `python tools/prepare_ota_key.py --development` ausführen. Dieser Schlüssel
   erlaubt lokale USB-Installationen; offizielle OTA-Releases werden von damit
   installierten Entwicklungsgeräten nicht akzeptiert.
   Dann `ESP-IDF: Build your Project` ausführen. Beim ersten Build muss der
   ESP-IDF Component Manager die Abhängigkeiten cJSON und mDNS aus dem
   Internet laden. Ein erfolgreicher Build erstellt `build/CI-Lights.bin`.
6. Das ESP32-S3-Zero zum Flashen mit einem Daten-USB-Kabel im Download-Modus
   verbinden. Das Board besitzt keinen USB-zu-UART-Chip: Dazu laut
   [Waveshare-Dokumentation](https://docs.waveshare.com/ESP32-S3-Zero)
   **BOOT** beim Anschließen gedrückt halten oder **BOOT** gedrückt halten
   und **RESET** betätigen. Dann in VS Code `ESP-IDF: Select Port to Use`
   ausführen und den seriellen Port (unter Windows z. B. `COMx`) wählen.
   `ESP-IDF: Flash your Project` starten und **UART** als Flash-Methode
   auswählen. Nach dem Flashen **RESET** zum Starten der Firmware drücken.
   [Espressif-Flash-Anleitung](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/flashdevice.html)
7. Falls der Port nach dem Reset wechselt, ihn erneut mit
   `ESP-IDF: Select Port to Use` wählen. `ESP-IDF: Monitor Device` zeigt Warnungen,
   Fehler, die Webadresse und Hinweise zur WLAN-Einrichtung. Anschließend die WLAN-Ersteinrichtung
   durchführen. [Espressif-Monitor-Anleitung](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/monitoroutput.html)

Beim ersten Start ohne gespeicherte Konfiguration erstellt der ESP das
WPA3-geschützte WLAN `ci-lights-xxxx-setup`. `xxxx` sind die letzten zwei
Bytes der WLAN-MAC-Adresse, beispielsweise `ci-lights-7a3f-setup`. Das
WLAN-Passwort wird als acht LED-Signale angezeigt: Pro Signal leuchtet
genau eine Position (oben `T`, Mitte `M`, unten `B`) in einer Farbe
(Red `R`, Orange `O`, Yellow `Y`, Green `G`, Blue `B`, Violet `V`).
Position und Farbe ergeben jeweils zwei Buchstaben, zum Beispiel `TR`
für oben rot und `BV` für unten violett. Die acht Paare werden ohne
Leerzeichen als 16-stelliges WLAN-Passwort eingegeben. Jedes Signal leuchtet 0,9 Sekunden. Zwischen den ersten sieben Signalen
bleibt die Ampel 1,5 Sekunden schwarz. Nach dem achten folgt eine
3,5 Sekunden lange schwarze Pause, dann beginnt die Folge erneut. Nach jedem Neustart entsteht ein neuer Code. Das
Einrichtungs-WLAN erfordert WPA3; ein WPA2-Modus ist nicht vorgesehen.
Nach dem Verbinden
sollte das Captive Portal
automatisch die Einrichtungsseite anzeigen. Falls iOS oder Android sie nicht
öffnet, rufe `http://192.168.4.1` manuell auf. Dort werden WLAN-Name,
optional WLAN-Benutzername und WLAN-Passwort gespeichert; bei offenen WLANs bleibt das Passwortfeld leer. Ist ein Benutzername eingetragen, versucht der ESP eine WPA2-Enterprise-Anmeldung per PEAP oder TTLS mit MSCHAPv2. Bei offenen und PSK-WLANs bleibt der Benutzername leer. Die Server-Zertifikatsprüfung ist für diesen Enterprise-Test auf ausdrücklichen Wunsch deaktiviert. Dadurch kann sich ein Angreifer als das Firmen-WLAN ausgeben; vor einem produktiven Einsatz muss stattdessen die Firmen-CA eingebunden und die Prüfung wieder aktiviert werden. Die Seite zeigt auch den künftigen lokalen URL-Namen
und die WLAN-MAC-Adresse an. Beides bitte notieren. Nach dem Neustart verbindet
sich der ESP mit einem gespeicherten WLAN. Bei der Einrichtung müssen ein
Verwaltungsbenutzername (3 bis 32 Zeichen) und ein Verwaltungspasswort (8 bis
64 Zeichen) gesetzt werden; das Passwort wird zur Kontrolle wiederholt. Wenn
kein WLAN erreichbar ist, startet das Einrichtungs-WLAN und Captive Portal
erneut; die bestehenden Verwaltungszugangsdaten bleiben dann gültig. Nach
einem Firmware-Update von einer Version ohne Verwaltungspasswort wird die
Einrichtung erneut geöffnet. Geräte mit dem bisherigen festen Benutzer
`admin` behalten diesen Namen bis zum Zurücksetzen der Einstellungen.

## Job im Browser auswählen

Nach der WLAN-Verbindung ist die Ampel über eine lokale mDNS-Adresse
erreichbar:

`http://ci-lights-ID.local`

`ID` sind die letzten zwei Bytes der WLAN-MAC-Adresse als vier
hexadezimale Zeichen. Beispiel: Hat die MAC-Adresse die Endung `7a:3f`, lautet
die Adresse `http://ci-lights-7a3f.local`.

Die genaue Adresse wird beim Start auch in der seriellen Konsole ausgegeben.
Beim Öffnen der Verwaltungsseite erscheint ein Anmeldedialog für den bei der
Einrichtung festgelegten Benutzernamen und das Passwort. Auch die Verwaltungs-API
erfordert diese Anmeldung. Das Passwort wird als gesalzener Hash gespeichert. Die lokale
Webseite nutzt HTTP; Anmeldung und andere Zugangsdaten werden im Netzwerk
unverschlüsselt übertragen.
Auf dieser Verwaltungsseite werden zuerst Jenkins-URL, Jenkins-Benutzer und
Jenkins-API-Token eingetragen. Anschließend erscheint ein Dropdown mit den
Jenkins-Jobs aus allen Ordnern und Unterordnern. Die Auswahl zeigt den
Ordnerpfad vor dem Jobnamen, damit gleichnamige Jobs erkennbar bleiben.
Nach dem Speichern der Jenkins-Zugangsdaten wird die Jobliste direkt geladen.
Danach kann sie über den kleinen Aktualisieren-Knopf neben dem Dropdown erneut
von Jenkins geladen werden. Die geladene Liste bleibt im Browser gespeichert
und ist auch nach einem erneuten Öffnen der Seite ohne neue Abfrage verfügbar. Ein
Reiterwechsel oder eine Anmeldung löst keine erneute Joblisten-Abfrage aus.
Nach einer Änderung der Jenkins-Zugangsdaten wird die Liste automatisch neu geladen.
Der ausgewählte Job wird dauerhaft gespeichert
und sofort abgefragt. `blue` leuchtet grün, `yellow` gelb, `red` rot; bei einem
laufenden Build (`*_anime`) pulsiert oder blinkt die jeweilige Jenkins-Statusfarbe
an den LEDs und in der Web-Ampel. Bei `grey`, `aborted` und `notbuilt` leuchtet nur die
mittlere LED gedimmt grau; die obere und untere bleiben aus.
`disabled` schaltet die Ampel vollständig aus (Schwarz). Noch ohne Jobauswahl leuchtet die
Ampel gelb.

Der Jobstatus wird beim Start sofort abgefragt. Bei einem vorübergehenden
Fehler folgen zwei weitere Versuche im Abstand von fünf Sekunden. Anschließend
wird er standardmäßig alle fünf Minuten abgefragt. Im Reiter `Konfiguration`
kann das Intervall als ganze Zahl in Minuten eingestellt werden; erlaubt sind
Werte ab einer Minute. Eine Änderung
wird dauerhaft gespeichert und spätestens bei der nächsten Minutentakt-Prüfung
wirksam.

Die Verwaltungsseite ist in die Reiter `Job`, `Konfiguration` und `Steuerung`
aufgeteilt.
Unter `Konfiguration` können bis zu acht WLANs gespeichert werden. Die
Netzwerksuche hilft bei der Auswahl. Ein bereits gespeicherter WLAN-Name wird
ohne doppelten Eintrag mit den neuen Zugangsdaten aktualisiert und behält
seine Position. Neue Namen werden am Ende eingefügt. Sind alle acht Plätze
belegt, ersetzt ein neues WLAN das letzte Profil. Mit den Pfeilen in der
Verwaltung lässt sich die Reihenfolge dauerhaft ändern. Passwörter werden
nicht in der Webseite angezeigt. Nach dem Speichern startet der ESP neu und
versucht die WLANs von oben nach unten. Eine geänderte Reihenfolge gilt ab
dem nächsten Neustart. Bei mehreren Access Points mit demselben Namen bevorzugt
er den mit dem stärksten Signal. Nach schnellen Fehlversuchen pausiert er kurz
und versucht es innerhalb des Zeitfensters erneut, bevor er zum nächsten
gespeicherten WLAN oder zur Einrichtung wechselt. Das einzige gespeicherte WLAN
kann in der Verwaltung nicht entfernt werden. Einzeln gespeicherte WLANs aus
älteren Firmware-Versionen
werden automatisch als erstes Profil übernommen.
Der Bereich `Mikrocontroller` im Footer ist anfangs eingeklappt. Nach dem Aufklappen zeigt
er das erkannte Chipmodell mit Revision, Kernzahl, aktuellem CPU-Takt und maximalem
CPU-Takt, Flash, Firmware- und ESP-IDF-Version, Build-Zeit, Laufzeit,
WLAN-MAC-Adresse, IPv4-Adresse, globale und lokale IPv6-Adresse, Kanal und
Signal sowie die aktuellen Speicherwerte. Unter `Konfiguration` kann der
CPU-Modus auf feste 160 MHz, feste 240 MHz oder automatisch 40–160 MHz bzw.
40–240 MHz gestellt werden. Der Wechsel wirkt ohne Neustart und wird im NVS
gespeichert. Im Automatikmodus senkt ESP-IDF den Takt bei Leerlauf und hebt ihn
bei WLAN-Arbeit, Jenkins-Abfragen und Verwaltungsanfragen bis zur gewählten
Obergrenze an. 40 MHz sind eine Untergrenze, kein dauerhaft garantierter
Leerlauftakt. Der angezeigte Takt wird während der Geräteabfrage gemessen und
kann daher die Obergrenze zeigen. Der ESP32-S3 unterstützt laut
[Espressif-Datenblatt](https://documentation.espressif.com/esp32-s3-mini-1_mini-1u_datasheet_en.pdf)
maximal 240 MHz. Der Modus kann auch über `GET /api/cpu-mode` gelesen und mit
`POST /api/cpu-mode` und `{"mode":"auto240"}` geändert werden; weitere Werte
sind `fixed160`, `auto160` und `fixed240`. Die Werte werden beim
Aufklappen und danach jede Minute über `GET /api/device-info` aktualisiert.
Eine globale IPv6-Adresse erscheint nur, wenn das WLAN sie bereitstellt.
Freier und gesamter
interner Heap, bisheriges Minimum, größter freier Block sowie freier und gesamter
PSRAM werden in Bytes angezeigt.

Oben rechts schalten die Flaggen zwischen Deutsch und Englisch um. Deutsch ist
die Standardsprache. Die Auswahl wird im NVS gespeichert und nach einem Neustart
auf der Einrichtungs- und Verwaltungsseite wieder geladen.

Die Ampelgrafik wird in die Firmware eingebettet. Banner und Favicon sind nicht
enthalten: Das Banner wird durch Klick auf den Platzhalter oberhalb der Seite
hochgeladen, das Favicon unter `Konfiguration`. Der sichtbare Titel
`CI-Lights` lässt sich direkt im Titel bearbeiten. Beim Verlassen des
Textfelds wird er im NVS gespeichert; Escape verwirft die Eingabe.
Erlaubt sind SVG oder PNG für das Banner und ICO oder PNG für das Favicon,
jeweils bis knapp 32 KiB. Beide liegen in einer eigenen 128-KiB-Flash-Partition
und bleiben bei Neustarts sowie beim normalen Firmware-Flashen erhalten.
Nach dem ersten Flashen erscheinen Banner und Favicon erst nach einem Upload.

## Steuerungsmodi und REST-API

Die Betriebsart wird nicht dauerhaft gespeichert und startet nach jedem Neustart
mit `Auto (Jenkins)`.

- `Auto (Jenkins)`: Jenkins steuert die Ampel und wird beim Wechsel sofort erneut abgefragt.
- `Manuell`: Rot, Gelb und Grün können auf der Verwaltungsseite einzeln geschaltet werden.
- `Disco (WS2812)`: Die WS2812-Ampel spielt die ausgewählte Animation; Jenkins wird ignoriert.
- `REST API`: Jenkins wird ignoriert und ein Client setzt genau einen Ampelstatus.

Die REST-API ist über dieselbe lokale Adresse wie die Verwaltungsseite erreichbar.
Jeder Aufruf benötigt HTTP Basic Auth mit dem Benutzernamen und Passwort aus
der Einrichtung. Mit `curl -u BENUTZERNAME` fragt `curl` das
Passwort interaktiv ab; es muss dann nicht im Befehl stehen. `ID` im Hostnamen
durch die vier Zeichen des eigenen Geräts ersetzen. Ohne Anmeldung antwortet
das Gerät mit HTTP `401 Unauthorized`.

Zuerst muss der API-Modus aktiviert werden:

```sh
curl -u BENUTZERNAME -X POST http://ci-lights-ID.local/api/mode \
  -H "Content-Type: application/json" \
  -d '{"mode":"api"}'
```

Danach setzt dieser Aufruf die Ampel auf Grün. Gültige Werte für `status`
sind `off`, `red`, `yellow` und `green`.

```sh
curl -u BENUTZERNAME -X POST http://ci-lights-ID.local/api/status \
  -H "Content-Type: application/json" \
  -d '{"status":"green"}'
```

Mit `POST /api/mode` lassen sich die Werte `auto`, `manual`, `disco` und `api` setzen.
`GET /api/lights` liefert den aktuellen LED-Zustand sowie die aktive Betriebsart.
Auch dieser Leseaufruf braucht das Passwort:

```sh
curl -u BENUTZERNAME http://ci-lights-ID.local/api/lights
```

Ein Statusaufruf außerhalb des API-Modus wird mit HTTP `409 Conflict` abgewiesen.
Der Modus-Endpunkt bleibt jedoch in jeder Betriebsart erreichbar. Damit kann ein
API-Client nach einem Wechsel zu `auto` oder `manual` jederzeit wieder den
API-Modus aktivieren und wird nicht ausgesperrt.

## Manuelle Ampelsteuerung

Auf der Verwaltungsseite können Rot, Gelb und Grün jeweils einzeln ein- oder
ausgeschaltet werden, wenn die Betriebsart `Manuell` aktiv ist. Jede LED hat dafür einen Umschaltknopf, der ihren
aktuellen Zustand `An` oder `Aus` zeigt. Die manuelle Steuerung ändert nur die
angeklickte LED; die beiden anderen LEDs behalten ihren Zustand. Beim Wechsel
zur Betriebsart `Auto (Jenkins)` aktualisiert Jenkins die Ampel wieder.

Die Ampelgrafik neben den Schaltern zeigt den aktuellen Zustand mit einem
deutlichen Leuchteffekt an. Sie wird nach einer Jobauswahl sofort und sonst
jede Sekunde aktualisiert.

## Disco-Modus und LED-Helligkeit

Im Reiter `Steuerung` lässt sich für laufende Jenkins-Builds zwischen
`Pulsieren` und `Blinken (an/aus)` wählen. Die Auswahl wird dauerhaft
gespeichert und gilt auch nach einem Neustart. Ohne gespeicherten Wert ist
`Pulsieren` voreingestellt. Per REST API kann der Effekt ebenfalls gesetzt werden:

```sh
curl -u BENUTZERNAME -X POST http://ci-lights-ID.local/api/build-effect \
  -H "Content-Type: application/json" \
  -d '{"effect":"blink"}'
```

Die Helligkeit steht im Reiter `Steuerung` und gilt für die externe
WS2812-Ampel, die eingebaute RGB-LED und alle Betriebsarten. Sie ist von 1 bis
100 Prozent einstellbar und wird dauerhaft gespeichert. Ohne zuvor gespeicherten
Wert startet sie mit 50 Prozent.

Im Disco-Modus stehen zehn an WLED angelehnte WS2812-Animationen zur Auswahl:
`Rainbow`, `Colorloop`, `Chase`, `Rainbow Chase`, `Blink`, `Breathe`,
`Twinkle`, `Scan`, `Theater Chase` und `Fireworks`.

Die Auswahl ist auch per REST API möglich, nachdem der Disco-Modus aktiviert
wurde:

```sh
curl -u BENUTZERNAME -X POST http://ci-lights-ID.local/api/disco-effect \
  -H "Content-Type: application/json" \
  -d '{"effect":"rainbow-chase"}'
```

Die Helligkeit kann unabhängig von der Betriebsart gesetzt werden:

```sh
curl -u BENUTZERNAME -X POST http://ci-lights-ID.local/api/brightness \
  -H "Content-Type: application/json" \
  -d '{"brightness":35}'
```

Die Jobliste und jede Statusabfrage laufen vom ESP direkt zu Jenkins. Die
Webseite zeigt gespeicherte Zugangsdaten nicht an; beim Speichern überträgt
sie den Jenkins-Token jedoch an den ESP.

Die lokale Webseite und REST-API verlangen HTTP Basic Auth. Weil die Verbindung
nur HTTP nutzt, können Anmeldedaten und der Jenkins-Token beim Übertragen
im WLAN mitgelesen werden. Wer gültige Verwaltungszugangsdaten besitzt,
kann den Jenkins-Zugang, den angezeigten Job und den Ampelstatus ändern.
mDNS kann in manchen Firmen-WLANs durch Client-Isolation oder Multicast-Filter
blockiert sein; dann ist die Seite nur über die vom Router vergebene
IP-Adresse erreichbar.

## Diagnose über den USB-Monitor

Nach dem Flashen zeigt `idf.py -p COMx monitor` die seriellen Meldungen an
(`COMx` durch den Port des ESP ersetzen). Beim Start werden der Reset-Grund und
die Webserver-Aktivierung ausgegeben. Danach erscheint etwa einmal pro Minute
eine `jenkins: Diagnose`-Zeile mit WLAN-Signalstärke (RSSI), freiem internem
Heap, bisherigem Minimum, größtem freiem Block und freiem PSRAM.

Der Tag `network` meldet WLAN-Trennungen mit Grund und RSSI sowie die
Wiederverbindung. `lights_http` meldet angenommene/geschlossene Verbindungen,
HTTP-Parserfehler und die Dauer von `GET /`, `GET /api/jobs` und `POST /api/mode`.
Die drei POST-Aufrufe zur manuellen LED-Steuerung werden ebenfalls protokolliert.
`GET /api/jobs` wird nach dem Speichern des Jenkins-Zugangs und durch den
Aktualisieren-Knopf der Jobliste ausgelöst.

Wenn bei `lights_http` zwar eine Verbindung angenommen wird, aber kein
`GET /` oder API-Aufruf folgt, hilft der HTTP-Fehlercode beim Eingrenzen der
Anfrage. Lange `GET /api/jobs`-Zeiten deuten auf die Jenkins-Verbindung. Viele
`network: WLAN getrennt`-Zeilen oder schwankender RSSI deuten auf die
WLAN-Verbindung. Auf dem ESP32-S3-Zero sind 2 MB PSRAM aktiviert. Größere
`malloc`-Allokationen, Jenkins-Antwortpuffer und JSON-Daten können diesen Speicher
verwenden; ein Teil des internen Speichers bleibt für Tasks und Hardware
reserviert. `PSRAM frei 0 B` nach dem Flashen dieser Firmware deutet auf ein
Problem bei der PSRAM-Initialisierung hin. Die Heap-Werte zeigen, ob
Speicherdruck besteht.

## Einstellungen zurücksetzen

Falls der URL-Name vergessen wurde oder neue Zugangsdaten benötigt werden:
Bei **laufendem** ESP die Taste **BOOT** fünf Sekunden gedrückt halten. Der
ESP löscht dann alle gespeicherten Einstellungen einschließlich WLAN,
Jenkins und Verwaltungspasswort und startet wieder mit
`ci-lights-xxxx-setup`. Firmware und hochgeladenes
Branding bleiben erhalten. Ein vollständiges Löschen des Flashs entfernt
auch Logo und Favicon; sie müssen danach erneut hochgeladen werden.

BOOT ist beim ESP32-S3-Zero GPIO 0. Die Taste nicht beim Einschalten oder
gleichzeitig mit RESET gedrückt halten, denn dann startet der ESP den
Flash-Download-Modus statt der Firmware.

## Verdrahtung

### ESP32-S3-Zero

Die Mikrocontroller-Anzeige liest das Chipmodell zur Laufzeit aus. Der Name des
Platinenherstellers ist nicht im Chip hinterlegt. Die folgenden Anschlussangaben
dokumentieren die in der Firmware verwendete ESP32-S3-Zero-GPIO-Belegung.

Die Firmware ist für den ESP32-S3-Zero ausgelegt. Die Ampel besteht aus einer
Kette von drei WS2812-kompatiblen LEDs. Der Dateneingang der ersten LED liegt an
GPIO 1; die LEDs werden mit 5 V versorgt und teilen sich die Masse mit dem Board.
In Datenflussrichtung ist die erste LED Grün (oben), die zweite Gelb und die
dritte Rot (unten). Nach dem Einschalten pulsieren alle drei LEDs weiß, bis die Firmware
erstmals einen Ampelzustand oder ein Blinkmuster ausgibt.
Die eingebaute WS2812B des Boards an GPIO 21 wird weiterhin parallel als
Statusanzeige angesteuert und zeigt denselben Zustand sowie dieselben Blinkmuster.

Die Hardwaredetails des Boards sind in der
[ESP32-S3-Zero-Dokumentation von Waveshare](https://docs.waveshare.com/ESP32-S3-Zero)
beschrieben.

### WS2812-Ampel

Das Board so halten, dass der USB-Anschluss oben ist und die Pins zum Betrachter
zeigen. Die Pins auf der rechten Seite von oben zählen:

| WS2812-Kette | ESP32-S3-Zero | Position rechts oben |
| --- | --- | --- |
| Pluspol (5 V) | 5 V | 1. Pin |
| Minuspol (GND) | GND | 2. Pin |
| Datenleitung (DIN der ersten LED) | GPIO 1 | 4. Pin |

Die LED-Streifen so zuschneiden, dass jedes Streifenstück genau drei LEDs
enthält. Außchließlich an den markierten Schnittstellen schneiden, damit
die Streifen passgenau in die Ampel passen.

Die Jumperkabel, an die der LED-Streifen angelötet wird, müssen jeweils
inklusive Dupont-Stecker 12 cm lang sein.
Die abisolierten Drahtenden der Jumperkabel auf 2 mm Länge kürzen und
verzinnen. Vor dem Zusammenlöten auch die Anschlusskontakte der LED-Streifen
verzinnen.

```text
     WS2812-Kette                         ESP32-S3-Zero
  +------------------+                 +-----------------+
  | 5 V              |-----------------| 5 V             |
  | GND              |-----------------| GND             |
  | DIN (LED 1: Grün) |---------------| GPIO 1          |
  | DOUT -> LED 2: Gelb -> LED 3: Rot                    |
  +------------------+                 +-----------------+
```

GPIO 1 liefert 3,3-V-Datensignale, die WS2812-Kette wird jedoch mit 5 V
versorgt. Für einen verlässlichen Betrieb ist deshalb ein 3,3-V-auf-5-V-
Pegelwandler (zum Beispiel 74AHCT125) zwischen GPIO 1 und DIN empfehlenswert;
mindestens einen gemeinsamen GND braucht die Schaltung zwingend. Ein
Widerstand von etwa 330 Ohm in der Datenleitung und ein Elko (etwa 1000 uF)
zwischen 5 V und GND am Eingang der Kette sind ebenfalls empfehlenswert.

## 3D-Druck-Gehäuse

Das passende Gehäuse für die Ampel gibt es hier:
[CI-Lights auf MakerWorld](https://makerworld.com/de/models/3352315).

## Firmware-Version

Jeder erfolgreiche Aufruf von `idf.py build` erhöht die Firmware-Version im
Format `1.0.N`, auch wenn keine Quelldatei geändert wurde. `version.txt`
speichert die zuletzt gebaute Nummer und bleibt bei `idf.py fullclean`
erhalten. Die Versionsnummer steht in den Firmware-Metadaten und im Footer
der Verwaltungsseite.

GitHub Actions baut bei jedem Push und Pull Request. Pushes auf `main` erzeugen
signierte Releases und aktualisieren die Installationsseite. Andere Branches
und Pull Requests verwenden Wegwerf-Schlüssel und veröffentlichen keine Updates.
Die CI-Version lautet `1.0.(1000 + Workflow-Laufnummer)`; sie wird über
`CI_LIGHTS_RELEASE_VERSION` festgelegt, ohne `version.txt` zu verändern.

### Unter Windows bauen und veröffentlichen

Starte `publish.bat` im Projektordner oder per Doppelklick. Die Kommentare
am Anfang des Skripts enthalten die Voraussetzungen und Einrichtungsbefehle
für Git, Node.js, GitHub CLI und ESP-IDF 6.1.

Das Skript prüft die Browser-Oberflächen, baut und signiert die Firmware
lokal mit dem vorhandenen Originalschlüssel und prüft Signatur sowie
Installationspakete. Danach zeigt es die Projektdateien vor dem Commit an;
mit `JA` bestätigst du Commit und Push nach `main`. GitHub baut erneut,
signiert mit seinem Environment-Secret und veröffentlicht Release und
Flash-Seite. Das Skript wartet bis zum Abschluss und zeigt die Links.

```bat
publish.bat -Message "Meine Änderung"
publish.bat -CheckOnly
publish.bat -SkipLocalBuild
```

`-CheckOnly` prüft Voraussetzungen ohne Build oder Veröffentlichung.
`-SkipLocalBuild` lässt den lokalen SDK-Build aus; GitHub baut und signiert
weiterhin. Dafür sind lokal weder ESP-IDF noch der private OTA-Schlüssel
nötig. Ohne neue Commits wird ein neuer GitHub-Build manuell gestartet.
`-Yes` überspringt die Commit-Bestätigung für bewusst gewollte Automatisierung.

Der Git-Index muss vor dem Start leer sein. Der Branch muss `main` sein und
den aktuellen `origin/main` enthalten. Das Skript nimmt alle geänderten
Projektdateien auf; `.vscode/settings.json`, Git-ignorierte Schlüssel,
Backups und Build-Dateien bleiben lokal. Passwörter und Tokens gehören
nicht in das Skript. Ein vorhandener OTA-Schlüssel wird wiederverwendet.

Die Firmware wird mit `-Os` auf geringe Flash-Größe optimiert. SDK-Info-Logs
werden beim Übersetzen entfernt; die benötigten WLAN- und Einrichtungshinweise
bleiben erhalten. Für den Platzbedarf im Flash zählt `build/CI-Lights.bin`;
die deutlich größeren ELF- und Map-Dateien enthalten auch Debug-Informationen.

## Abhängigkeit und Sicherheit

Die Datei `main/idf_component.yml` fügt die cJSON- und mDNS-Komponenten von
Espressif hinzu. Beim ersten `idf.py build` muss der ESP-IDF Component Manager
sie aus dem Internet laden.

WLAN- und Jenkins-Daten liegen nicht im Sourcecode, werden aber unverschlüsselt
im Flash des ESP gespeichert.

Die 4-MiB-Partitionierung enthält zwei Firmware-Slots mit je 1,75 MiB und
eine 128-KiB-Branding-Partition. Signierte OTA-Updates wechseln zwischen den
Slots. Die Signatur wird in Software gegen den öffentlichen Schlüssel der
laufenden Firmware geprüft. Hardware-Secure-Boot und Flash-Verschlüsselung
sind nicht aktiviert; eFuses werden durch diese Einrichtung nicht verändert.
Der private RSA-Schlüssel ist weder in der Firmware noch im Repository
enthalten. Die lokale PEM-Datei ist unverschlüsselt und muss geschützt sowie
separat verschlüsselt gesichert werden. GitHub verwendet ein Environment-Secret
mit Zugriff nur für `main`. Wer vertraünswürdigen Build-Code auf `main`
verändern kann, kann auch diesen Schlüssel auslesen.

Das Einrichtungs-WLAN ist mit WPA3-SAE und einem nach jedem Neustart neu
erzeugten LED-Code geschützt. Das Captive Portal nutzt dennoch HTTP; wer
Zugang zum Einrichtungsnetz erlangt, kann die dort eingegebenen WLAN-Daten bei
einem aktiven Angriff mitlesen oder verändern. Jenkins-Zugangsdaten werden erst
danach auf der Verwaltungsseite im lokalen WLAN eingegeben.

Verwendet Jenkins eine interne CA, muss deren Root-Zertifikat zusätzlich in
das ESP-IDF-Zertifikats-Bundle aufgenommen werden. Die TLS-Prüfung darf nicht
deaktiviert werden.

## Lizenz

Der eigene Quellcode, die Dokumentation und die Ampelgrafik dieses Projekts
stehen unter der [Unlicense](LICENSE). Banner und Favicon werden vom Benutzer
hochgeladen und sind nicht Teil des Projekts; ihre Nutzungsrechte liegen bei
den jeweiligen Rechteinhabern. Eingebundene Komponenten wie ESP-IDF, cJSON
und mDNS behalten ihre jeweiligen Lizenzen. Bei der Weitergabe fertiger
Firmware müssen die anwendbaren Lizenztexte und Copyright-Hinweise dieser
Komponenten beiliegen.
