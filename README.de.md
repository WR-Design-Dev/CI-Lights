# CI-Lights (ESP32-S3)

**Sprachen:** [English](README.md) | Deutsch

**Projektinformation:** Der Code dieses Projekts wurde durch KI erstellt.

> **Experimentelles Projekt - nicht produktionsreif.** Die Server-Zertifikatspruefung fuer WPA2-Enterprise ist zu Testzwecken deaktiviert, die lokale Verwaltung verwendet unverschluesseltes HTTP. Nur in einer vertrauenswuerdigen Testumgebung verwenden.

## Entwicklungsumgebung mit Visual Studio Code einrichten

1. [Visual Studio Code](https://code.visualstudio.com/) und die offizielle
   [Espressif-IDF-Erweiterung](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension)
   installieren. In VS Code laesst sich die Erweiterung auch ueber
   `Strg+Umschalt+X` und die Suche nach `ESP-IDF` finden.
2. Mit `F1` die Befehlspalette oeffnen und
   `ESP-IDF: Open ESP-IDF Installation Manager` ausfuehren. ESP-IDF **6.1.0**
   samt Werkzeugen installieren. Danach
   `ESP-IDF: Select Current ESP-IDF Version` ausfuehren und diese Installation
   auswaehlen. Eine bereits vorhandene Installation kann direkt ausgewaehlt
   werden, wenn die Erweiterung sie erkennt; sonst hilft die verlinkte
   Anleitung zur manuellen Konfiguration. Bei Problemen prueft
   `ESP-IDF: Doctor Command` die Einrichtung.
   [Espressif-Installationsanleitung](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/installation.html)
3. Dieses Repository mit Git klonen oder den bereits vorhandenen Projektordner
   verwenden. In VS Code ueber `Datei > Ordner oeffnen` den Ordner
   **CI-Lights** mit der obersten `CMakeLists.txt` oeffnen:

   ```sh
   git clone https://github.com/WR-Design-Dev/CI-Lights.git
   ```

   Es ist ein bestehendes Projekt; der Befehl `ESP-IDF: New Project` wird nicht
   benoetigt. Fuer C/C++-IntelliSense kann
   `ESP-IDF: Add VS Code Configuration Folder` die lokale
   `.vscode`-Konfiguration erzeugen.
4. Das Ziel muss `esp32s3` sein. Es ist in `sdkconfig` bereits gesetzt; falls
   VS Code ein anderes Ziel anzeigt, `ESP-IDF: Set Espressif Device Target`
   ausfuehren und `esp32s3` waehlen. Die vorhandene Konfiguration verwendet
   **2 MB Flash**, eine eigene Partitionstabelle (`partitions.csv`) und
   **2 MB Quad-PSRAM**. Diese Einstellungen beim Konfigurieren beibehalten.
5. `ESP-IDF: Build your Project` ausfuehren. Beim ersten Build muss der
   ESP-IDF Component Manager die Abhaengigkeiten cJSON und mDNS aus dem
   Internet laden. Ein erfolgreicher Build erstellt `build/CI-Lights.bin`.
6. Das ESP32-S3-Zero zum Flashen mit einem Daten-USB-Kabel im Download-Modus
   verbinden. Das Board besitzt keinen USB-zu-UART-Chip: Dazu laut
   [Waveshare-Dokumentation](https://docs.waveshare.com/ESP32-S3-Zero)
   **BOOT** beim Anschliessen gedrueckt halten oder **BOOT** gedrueckt halten
   und **RESET** betaetigen. Dann in VS Code `ESP-IDF: Select Port to Use`
   ausfuehren und den seriellen Port (unter Windows z. B. `COMx`) waehlen.
   `ESP-IDF: Flash your Project` starten und **UART** als Flash-Methode
   auswaehlen. Nach dem Flashen **RESET** zum Starten der Firmware druecken.
   [Espressif-Flash-Anleitung](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/flashdevice.html)
7. Falls der Port nach dem Reset wechselt, ihn erneut mit
   `ESP-IDF: Select Port to Use` waehlen. `ESP-IDF: Monitor Device` zeigt die Start- und
   Diagnosemeldungen. Anschliessend die unten beschriebene WLAN-Ersteinrichtung
   durchfuehren. [Espressif-Monitor-Anleitung](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/monitoroutput.html)

Beim ersten Start ohne gespeicherte Konfiguration erstellt der ESP das
WPA3-geschuetzte WLAN `ci-lights-xxxx-setup`. `xxxx` sind die letzten zwei
Bytes der WLAN-MAC-Adresse, beispielsweise `ci-lights-7a3f-setup`. Das
WLAN-Passwort wird als acht LED-Signale angezeigt: Pro Signal leuchtet
genau eine Position (oben `T`, Mitte `M`, unten `B`) in einer Farbe
(Red `R`, Orange `O`, Yellow `Y`, Green `G`, Blue `B`, Violet `V`).
Position und Farbe ergeben jeweils zwei Buchstaben, zum Beispiel `TR`
fuer oben rot und `BV` fuer unten violett. Die acht Paare werden ohne
Leerzeichen als 16-stelliges WLAN-Passwort eingegeben. Jedes Signal leuchtet 0,9 Sekunden. Zwischen den ersten sieben Signalen
bleibt die Ampel 1,5 Sekunden schwarz. Nach dem achten folgt eine
3,5 Sekunden lange schwarze Pause, dann beginnt die Folge erneut. Nach jedem Neustart entsteht ein neuer Code. Das
Einrichtungs-WLAN erfordert WPA3; ein WPA2-Modus ist nicht vorgesehen.
Nach dem Verbinden
sollte das Captive Portal
automatisch die Einrichtungsseite anzeigen. Falls iOS oder Android sie nicht
oeffnet, rufe `http://192.168.4.1` manuell auf. Dort werden WLAN-Name,
optional WLAN-Benutzername und WLAN-Passwort gespeichert; bei offenen WLANs bleibt das Passwortfeld leer. Ist ein Benutzername eingetragen, versucht der ESP eine WPA2-Enterprise-Anmeldung per PEAP oder TTLS mit MSCHAPv2. Bei offenen und PSK-WLANs bleibt der Benutzername leer. Die Server-Zertifikatspruefung ist fuer diesen Enterprise-Test auf ausdruecklichen Wunsch deaktiviert. Dadurch kann sich ein Angreifer als das Firmen-WLAN ausgeben; vor einem produktiven Einsatz muss stattdessen die Firmen-CA eingebunden und die Pruefung wieder aktiviert werden. Die Seite zeigt auch den kuenftigen lokalen URL-Namen
und die WLAN-MAC-Adresse an. Beides bitte notieren. Nach dem Neustart verbindet
sich der ESP mit einem gespeicherten WLAN. Bei der Einrichtung muessen ein
Verwaltungsbenutzername (3 bis 32 Zeichen) und ein Verwaltungspasswort (8 bis
64 Zeichen) gesetzt werden; das Passwort wird zur Kontrolle wiederholt. Wenn
kein WLAN erreichbar ist, startet das Einrichtungs-WLAN und Captive Portal
erneut; die bestehenden Verwaltungszugangsdaten bleiben dann gueltig. Nach
einem Firmware-Update von einer Version ohne Verwaltungspasswort wird die
Einrichtung erneut geoeffnet. Geraete mit dem bisherigen festen Benutzer
`admin` behalten diesen Namen bis zum Zuruecksetzen der Einstellungen.

## Job im Browser auswaehlen

Nach der WLAN-Verbindung ist die Ampel ueber eine lokale mDNS-Adresse
erreichbar:

`http://ci-lights-ID.local`

`ID` sind die letzten zwei Bytes der WLAN-MAC-Adresse als vier
hexadezimale Zeichen. Beispiel: Hat die MAC-Adresse die Endung `7a:3f`, lautet
die Adresse `http://ci-lights-7a3f.local`.

Die genaue Adresse wird beim Start auch in der seriellen Konsole ausgegeben.
Beim Oeffnen der Verwaltungsseite erscheint ein Anmeldedialog fuer den bei der
Einrichtung festgelegten Benutzernamen und das Passwort. Auch die Verwaltungs-API
erfordert diese Anmeldung. Das Passwort wird als gesalzener Hash gespeichert. Die lokale
Webseite nutzt HTTP; Anmeldung und andere Zugangsdaten werden im Netzwerk
unverschluesselt uebertragen.
Auf dieser Verwaltungsseite werden zuerst Jenkins-URL, Jenkins-Benutzer und
Jenkins-API-Token eingetragen. Anschliessend erscheint ein Dropdown mit den
Jenkins-Jobs aus allen Ordnern und Unterordnern. Die Auswahl zeigt den
Ordnerpfad vor dem Jobnamen, damit gleichnamige Jobs erkennbar bleiben.
Nach dem Speichern der Jenkins-Zugangsdaten wird die Jobliste direkt geladen.
Danach kann sie ueber den kleinen Aktualisieren-Knopf neben dem Dropdown erneut
von Jenkins geladen werden. Die geladene Liste bleibt im Browser gespeichert
und ist auch nach einem erneuten Oeffnen der Seite ohne neue Abfrage verfuegbar. Ein
Reiterwechsel oder eine Anmeldung loest keine erneute Joblisten-Abfrage aus.
Nach einer Aenderung der Jenkins-Zugangsdaten wird die Liste automatisch neu geladen.
Der ausgewaehlte Job wird dauerhaft gespeichert
und sofort abgefragt. `blue` leuchtet gruen, `yellow` gelb, `red` rot; bei einem
laufenden Build (`*_anime`) pulsiert oder blinkt die jeweilige Jenkins-Statusfarbe
an den LEDs und in der Web-Ampel. Bei `grey`, `aborted` und `notbuilt` leuchtet nur die
mittlere LED gedimmt grau; die obere und untere bleiben aus.
`disabled` schaltet die Ampel vollstaendig aus (Schwarz). Noch ohne Jobauswahl leuchtet die
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
Unter `Konfiguration` koennen bis zu acht WLANs gespeichert werden. Die
Netzwerksuche hilft bei der Auswahl. Ein bereits gespeicherter WLAN-Name wird
ohne doppelten Eintrag mit den neuen Zugangsdaten aktualisiert und gilt danach
als zuletzt gespeichert. Neue Namen werden hinzugefuegt. Sind alle acht Plaetze
belegt, ersetzt ein neues WLAN das am laengsten nicht neu gespeicherte Profil.
Passwoerter werden nicht in der Webseite angezeigt. Nach dem Speichern startet
der ESP neu und versucht zuerst das neue WLAN. Bei jedem weiteren Start versucht
er zuerst das zuletzt erfolgreich verwendete WLAN und danach die anderen
gespeicherten WLANs. Bei mehreren Access Points mit demselben Namen bevorzugt
er den mit dem staerksten Signal. Nach schnellen Fehlversuchen pausiert er kurz
und versucht es innerhalb des Zeitfensters erneut, bevor er zum naechsten
gespeicherten WLAN oder zur Einrichtung wechselt. Das letzte gespeicherte WLAN
kann in der Verwaltung nicht entfernt werden. Einzeln gespeicherte WLANs aus
aelteren Firmware-Versionen
werden automatisch als erstes Profil uebernommen.
Der Bereich `Mikrocontroller` im Footer ist anfangs eingeklappt. Nach dem Aufklappen zeigt
er das erkannte Chipmodell mit Revision, Kernzahl, aktuellem CPU-Takt und maximalem
CPU-Takt, Flash, Firmware- und ESP-IDF-Version, Build-Zeit, Laufzeit,
WLAN-MAC-Adresse, IPv4-Adresse, globale und lokale IPv6-Adresse, Kanal und
Signal sowie die aktuellen Speicherwerte. Der aktuelle CPU-Takt der Firmware
ist 160 MHz; der ESP32-S3 unterstuetzt laut
[Espressif-Datenblatt](https://documentation.espressif.com/esp32-s3-mini-1_mini-1u_datasheet_en.pdf)
maximal 240 MHz. Die Werte werden beim
Aufklappen und danach jede Minute ueber `GET /api/device-info` aktualisiert.
Eine globale IPv6-Adresse erscheint nur, wenn das WLAN sie bereitstellt.
Freier und gesamter
interner Heap, bisheriges Minimum, groesster freier Block sowie freier und gesamter
PSRAM werden in Bytes angezeigt.

Oben rechts schalten die Flaggen zwischen Deutsch und Englisch um. Deutsch ist
die Standardsprache. Die Auswahl wird im NVS gespeichert und nach einem Neustart
auf der Einrichtungs- und Verwaltungsseite wieder geladen.

Die Ampelgrafik wird in die Firmware eingebettet. Banner und Favicon sind nicht
enthalten: Das Banner wird durch Klick auf den Platzhalter oberhalb der Seite
hochgeladen, das Favicon unter `Konfiguration`. Der sichtbare Titel
`CI-Lights` laesst sich direkt im Titel bearbeiten. Beim Verlassen des
Textfelds wird er im NVS gespeichert; Escape verwirft die Eingabe.
Erlaubt sind SVG oder PNG fuer das Banner und ICO oder PNG fuer das Favicon,
jeweils bis knapp 32 KiB. Beide liegen in einer eigenen 128-KiB-Flash-Partition
und bleiben bei Neustarts sowie beim normalen Firmware-Flashen erhalten.
Nach dem ersten Flashen erscheinen Banner und Favicon erst nach einem Upload.

## Steuerungsmodi und REST-API

Die Betriebsart wird nicht dauerhaft gespeichert und startet nach jedem Neustart
mit `Auto (Jenkins)`.

- `Auto (Jenkins)`: Jenkins steuert die Ampel und wird beim Wechsel sofort erneut abgefragt.
- `Manuell`: Rot, Gelb und Gruen koennen auf der Verwaltungsseite einzeln geschaltet werden.
- `Disco (WS2812)`: Die WS2812-Ampel spielt die ausgewaehlte Animation; Jenkins wird ignoriert.
- `REST API`: Jenkins wird ignoriert und ein Client setzt genau einen Ampelstatus.

Die REST-API ist ueber dieselbe lokale Adresse wie die Verwaltungsseite erreichbar.
Jeder Aufruf benoetigt HTTP Basic Auth mit dem Benutzernamen und Passwort aus
der Einrichtung. Mit `curl -u BENUTZERNAME` fragt `curl` das
Passwort interaktiv ab; es muss dann nicht im Befehl stehen. `ID` im Hostnamen
durch die vier Zeichen des eigenen Geraets ersetzen. Ohne Anmeldung antwortet
das Geraet mit HTTP `401 Unauthorized`.

Zuerst muss der API-Modus aktiviert werden:

```sh
curl -u BENUTZERNAME -X POST http://ci-lights-ID.local/api/mode \
  -H "Content-Type: application/json" \
  -d '{"mode":"api"}'
```

Danach setzt dieser Aufruf die Ampel auf Gruen. Gueltige Werte fuer `status`
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

Ein Statusaufruf ausserhalb des API-Modus wird mit HTTP `409 Conflict` abgewiesen.
Der Modus-Endpunkt bleibt jedoch in jeder Betriebsart erreichbar. Damit kann ein
API-Client nach einem Wechsel zu `auto` oder `manual` jederzeit wieder den
API-Modus aktivieren und wird nicht ausgesperrt.

## Manuelle Ampelsteuerung

Auf der Verwaltungsseite koennen Rot, Gelb und Gruen jeweils einzeln ein- oder
ausgeschaltet werden, wenn die Betriebsart `Manuell` aktiv ist. Jede LED hat dafuer einen Umschaltknopf, der ihren
aktuellen Zustand `An` oder `Aus` zeigt. Die manuelle Steuerung aendert nur die
angeklickte LED; die beiden anderen LEDs behalten ihren Zustand. Beim Wechsel
zur Betriebsart `Auto (Jenkins)` aktualisiert Jenkins die Ampel wieder.

Die Ampelgrafik neben den Schaltern zeigt den aktuellen Zustand mit einem
deutlichen Leuchteffekt an. Sie wird nach einer Jobauswahl sofort und sonst
jede Sekunde aktualisiert.

## Disco-Modus und LED-Helligkeit

Im Reiter `Steuerung` laesst sich fuer laufende Jenkins-Builds zwischen
`Pulsieren` und `Blinken (an/aus)` waehlen. Die Auswahl wird dauerhaft
gespeichert und gilt auch nach einem Neustart. Ohne gespeicherten Wert ist
`Pulsieren` voreingestellt. Per REST API kann der Effekt ebenfalls gesetzt werden:

```sh
curl -u BENUTZERNAME -X POST http://ci-lights-ID.local/api/build-effect \
  -H "Content-Type: application/json" \
  -d '{"effect":"blink"}'
```

Die Helligkeit steht im Reiter `Steuerung` und gilt fuer die externe
WS2812-Ampel, die eingebaute RGB-LED und alle Betriebsarten. Sie ist von 1 bis
100 Prozent einstellbar und wird dauerhaft gespeichert. Ohne zuvor gespeicherten
Wert startet sie mit 50 Prozent.

Im Disco-Modus stehen zehn an WLED angelehnte WS2812-Animationen zur Auswahl:
`Rainbow`, `Colorloop`, `Chase`, `Rainbow Chase`, `Blink`, `Breathe`,
`Twinkle`, `Scan`, `Theater Chase` und `Fireworks`.

Die Auswahl ist auch per REST API moeglich, nachdem der Disco-Modus aktiviert
wurde:

```sh
curl -u BENUTZERNAME -X POST http://ci-lights-ID.local/api/disco-effect \
  -H "Content-Type: application/json" \
  -d '{"effect":"rainbow-chase"}'
```

Die Helligkeit kann unabhaengig von der Betriebsart gesetzt werden:

```sh
curl -u BENUTZERNAME -X POST http://ci-lights-ID.local/api/brightness \
  -H "Content-Type: application/json" \
  -d '{"brightness":35}'
```

Die Jobliste und jede Statusabfrage laufen vom ESP direkt zu Jenkins. Die
Webseite zeigt gespeicherte Zugangsdaten nicht an; beim Speichern uebertraegt
sie den Jenkins-Token jedoch an den ESP.

Die lokale Webseite und REST-API verlangen HTTP Basic Auth. Weil die Verbindung
nur HTTP nutzt, koennen Anmeldedaten und der Jenkins-Token beim Uebertragen
im WLAN mitgelesen werden. Wer gueltige Verwaltungszugangsdaten besitzt,
kann den Jenkins-Zugang, den angezeigten Job und den Ampelstatus aendern.
mDNS kann in manchen Firmen-WLANs durch Client-Isolation oder Multicast-Filter
blockiert sein; dann ist die Seite nur ueber die vom Router vergebene
IP-Adresse erreichbar.

## Diagnose ueber den USB-Monitor

Nach dem Flashen zeigt `idf.py -p COMx monitor` die seriellen Meldungen an
(`COMx` durch den Port des ESP ersetzen). Beim Start werden der Reset-Grund und
die Webserver-Aktivierung ausgegeben. Danach erscheint etwa einmal pro Minute
eine `jenkins: Diagnose`-Zeile mit WLAN-Signalstaerke (RSSI), freiem internem
Heap, bisherigem Minimum, groesstem freiem Block und freiem PSRAM.

Der Tag `network` meldet WLAN-Trennungen mit Grund und RSSI sowie die
Wiederverbindung. `lights_http` meldet angenommene/geschlossene Verbindungen,
HTTP-Parserfehler und die Dauer von `GET /`, `GET /api/jobs` und `POST /api/mode`.
Die drei POST-Aufrufe zur manuellen LED-Steuerung werden ebenfalls protokolliert.
`GET /api/jobs` wird nach dem Speichern des Jenkins-Zugangs und durch den
Aktualisieren-Knopf der Jobliste ausgeloest.

Wenn bei `lights_http` zwar eine Verbindung angenommen wird, aber kein
`GET /` oder API-Aufruf folgt, hilft der HTTP-Fehlercode beim Eingrenzen der
Anfrage. Lange `GET /api/jobs`-Zeiten deuten auf die Jenkins-Verbindung. Viele
`network: WLAN getrennt`-Zeilen oder schwankender RSSI deuten auf die
WLAN-Verbindung. Auf dem ESP32-S3-Zero sind 2 MB PSRAM aktiviert. Groessere
`malloc`-Allokationen, Jenkins-Antwortpuffer und JSON-Daten koennen diesen Speicher
verwenden; ein Teil des internen Speichers bleibt fuer Tasks und Hardware
reserviert. `PSRAM frei 0 B` nach dem Flashen dieser Firmware deutet auf ein
Problem bei der PSRAM-Initialisierung hin. Die Heap-Werte zeigen, ob
Speicherdruck besteht.

## Einstellungen zuruecksetzen

Falls der URL-Name vergessen wurde oder neue Zugangsdaten benoetigt werden:
Bei **laufendem** ESP die Taste **BOOT** fuenf Sekunden gedrueckt halten. Der
ESP loescht dann alle gespeicherten Einstellungen einschliesslich WLAN,
Jenkins und Verwaltungspasswort und startet wieder mit
`ci-lights-xxxx-setup`. Firmware und hochgeladenes
Branding bleiben erhalten. Ein vollstaendiges Loeschen des Flashs entfernt
auch Logo und Favicon; sie muessen danach erneut hochgeladen werden.

BOOT ist beim ESP32-S3-Zero GPIO 0. Die Taste nicht beim Einschalten oder
gleichzeitig mit RESET gedrueckt halten, denn dann startet der ESP den
Flash-Download-Modus statt der Firmware.

## Verdrahtung

### ESP32-S3-Zero

Die Mikrocontroller-Anzeige liest das Chipmodell zur Laufzeit aus. Der Name des
Platinenherstellers ist nicht im Chip hinterlegt. Die folgenden Anschlussangaben
dokumentieren die in der Firmware verwendete ESP32-S3-Zero-GPIO-Belegung.

Die Firmware ist fuer den ESP32-S3-Zero ausgelegt. Die Ampel besteht aus einer
Kette von drei WS2812-kompatiblen LEDs. Der Dateneingang der ersten LED liegt an
GPIO 1; die LEDs werden mit 5 V versorgt und teilen sich die Masse mit dem Board.
In Datenflussrichtung ist die erste LED Gruen (oben), die zweite Gelb und die
dritte Rot (unten). Nach dem Einschalten pulsieren alle drei LEDs weiss, bis die Firmware
erstmals einen Ampelzustand oder ein Blinkmuster ausgibt.
Die eingebaute WS2812B des Boards an GPIO 21 wird weiterhin parallel als
Statusanzeige angesteuert und zeigt denselben Zustand sowie dieselben Blinkmuster.

Die Hardwaredetails des Boards sind in der
[ESP32-S3-Zero-Dokumentation von Waveshare](https://docs.waveshare.com/ESP32-S3-Zero)
beschrieben.

### WS2812-Ampel

Das Board so halten, dass der USB-Anschluss oben ist und die Pins zum Betrachter
zeigen. Die Pins auf der rechten Seite von oben zaehlen:

| WS2812-Kette | ESP32-S3-Zero | Position rechts oben |
| --- | --- | --- |
| Pluspol (5 V) | 5 V | 1. Pin |
| Minuspol (GND) | GND | 2. Pin |
| Datenleitung (DIN der ersten LED) | GPIO 1 | 4. Pin |

Die LED-Streifen so zuschneiden, dass jedes Streifenstueck genau drei LEDs
enthaelt. Ausschliesslich an den markierten Schnittstellen schneiden, damit
die Streifen passgenau in die Ampel passen.

Die Jumperkabel, an die der LED-Streifen angeloetet wird, muessen jeweils
inklusive Dupont-Stecker 12 cm lang sein.
Die abisolierten Drahtenden der Jumperkabel auf 2 mm Laenge kuerzen und
verzinnen. Vor dem Zusammenloeten auch die Anschlusskontakte der LED-Streifen
verzinnen.

```text
     WS2812-Kette                         ESP32-S3-Zero
  +------------------+                 +-----------------+
  | 5 V              |-----------------| 5 V             |
  | GND              |-----------------| GND             |
  | DIN (LED 1: Gruen) |---------------| GPIO 1          |
  | DOUT -> LED 2: Gelb -> LED 3: Rot                    |
  +------------------+                 +-----------------+
```

GPIO 1 liefert 3,3-V-Datensignale, die WS2812-Kette wird jedoch mit 5 V
versorgt. Fuer einen verlaesslichen Betrieb ist deshalb ein 3,3-V-auf-5-V-
Pegelwandler (zum Beispiel 74AHCT125) zwischen GPIO 1 und DIN empfehlenswert;
mindestens einen gemeinsamen GND braucht die Schaltung zwingend. Ein
Widerstand von etwa 330 Ohm in der Datenleitung und ein Elko (etwa 1000 uF)
zwischen 5 V und GND am Eingang der Kette sind ebenfalls empfehlenswert.

## 3D-Druck-Gehaeuse

Das passende Gehaeuse fuer die Ampel gibt es hier:
[CI-Lights auf MakerWorld](https://makerworld.com/de/models/3352315).

## Firmware-Version

Jeder erfolgreiche Aufruf von `idf.py build` erhoeht die Firmware-Version im
Format `1.0.N`, auch wenn keine Quelldatei geaendert wurde. `version.txt`
speichert die zuletzt gebaute Nummer und bleibt bei `idf.py fullclean`
erhalten. Die Versionsnummer steht in den Firmware-Metadaten und im Footer
der Verwaltungsseite.

## Abhaengigkeit und Sicherheit

Die Datei `main/idf_component.yml` fuegt die cJSON- und mDNS-Komponenten von
Espressif hinzu. Beim ersten `idf.py build` muss der ESP-IDF Component Manager
sie aus dem Internet laden.

WLAN- und Jenkins-Daten liegen nicht im Sourcecode, werden aber unverschluesselt
im Flash des ESP gespeichert.

Es gibt bewusst keine OTA-Updates. Die einzige Firmware-Partition umfasst
1,75 MiB des 2-MiB-Flashs und wird per USB geflasht. Die letzten 128 KiB
sind fuer hochgeladenes Branding reserviert.

Das Einrichtungs-WLAN ist mit WPA3-SAE und einem nach jedem Neustart neu
erzeugten LED-Code geschuetzt. Das Captive Portal nutzt dennoch HTTP; wer
Zugang zum Einrichtungsnetz erlangt, kann die dort eingegebenen WLAN-Daten bei
einem aktiven Angriff mitlesen oder veraendern. Jenkins-Zugangsdaten werden erst
danach auf der Verwaltungsseite im lokalen WLAN eingegeben.

Verwendet Jenkins eine interne CA, muss deren Root-Zertifikat zusaetzlich in
das ESP-IDF-Zertifikats-Bundle aufgenommen werden. Die TLS-Pruefung darf nicht
deaktiviert werden.

## Lizenz

Der eigene Quellcode, die Dokumentation und die Ampelgrafik dieses Projekts
stehen unter der [Unlicense](LICENSE). Banner und Favicon werden vom Benutzer
hochgeladen und sind nicht Teil des Projekts; ihre Nutzungsrechte liegen bei
den jeweiligen Rechteinhabern. Eingebundene Komponenten wie ESP-IDF, cJSON
und mDNS behalten ihre jeweiligen Lizenzen. Bei der Weitergabe fertiger
Firmware muessen die anwendbaren Lizenztexte und Copyright-Hinweise dieser
Komponenten beiliegen.
