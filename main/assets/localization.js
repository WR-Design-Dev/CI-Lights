// German source strings are also the fallback when the language API is unavailable.
const uiTranslations = new Map([
  ['Firmware-Updates', 'Firmware updates'],
  ['Die Update-Anfrage hat zu lange gedauert.', 'The update request timed out.'],
  ['Installiere eine neue Firmware direkt ueber WLAN. Die Ampel startet danach neu.', 'Install new firmware over Wi-Fi. The traffic light restarts afterwards.'],
  ['Installiert:', 'Installed:'],
  ['| Verfuegbar:', '| Available:'],
  ['Nach Updates suchen', 'Check for updates'],
  ['Update installieren', 'Install update'],
  ['Update-Fortschritt', 'Update progress'],
  ['Noch nicht nach Updates gesucht.', 'Updates have not been checked yet.'],
  ['Firmware-Signatur wird geprueft.', 'Verifying the firmware signature.'],
  ['Noch kein oeffentliches Firmware-Release gefunden.', 'No public firmware release found yet.'],
  ['Update-Pruefung fehlgeschlagen. Internet und Release-Dateien pruefen.', 'Update check failed. Check your internet connection and release files.'],
  ['Die Firmware ist aktuell.', 'The firmware is up to date.'],
  ['Neue Firmware ist verfuegbar.', 'New firmware is available.'],
  ['Das Release hat sich geaendert. Bitte erneut nach Updates suchen.', 'The release has changed. Please check for updates again.'],
  ['Firmware wird heruntergeladen. Stromversorgung angeschlossen lassen.', 'Downloading firmware. Keep the power connected.'],
  ['Update fehlgeschlagen. Die bisherige Firmware bleibt aktiv.', 'Update failed. The previous firmware remains active.'],
  ['Update geprueft. Die Ampel startet neu.', 'Update verified. The traffic light is restarting.'],
  ['OTA-Partitionierung fehlt. Einmal vollstaendig per USB flashen.', 'OTA partitions are missing. Perform a complete USB installation once.'],
  ['Neue Firmware wird beim Start geprueft.', 'Checking the new firmware at startup.'],
  ['Startpruefung erfolgreich. Firmware bestaetigt.', 'Startup check passed. Firmware confirmed.'],
  ['JSON-Anfrage erforderlich.', 'A JSON request is required.'],
  ['Anfrage unvollstaendig.', 'Incomplete request.'],
  ['Ungueltige Firmware-Version.', 'Invalid firmware version.'],
  ['GitHub-Release wird geprueft.', 'Checking the GitHub release.'],
  ['Update beschaeftigt, noch nicht geprueft oder nicht eingerichtet.', 'Update busy, not checked yet, or not configured.'],
  ['Nicht genug Speicher fuer das Update.', 'Not enough memory for the update.'],
  ['Die neue Firmware wurde nicht gestartet oder zurueckgerollt.', 'The new firmware did not start or was rolled back.'],
  ['Die Ampel startet neu. Verbindung wird wiederhergestellt.', 'The traffic light is restarting. Reconnecting.'],
  ['Die Ampel ist noch nicht erreichbar. Verbindung pruefen und die Seite neu laden.', 'The traffic light is still unreachable. Check the connection and reload this page.'],
  ['Neue Firmware installieren? Die Ampel startet danach neu.', 'Install the new firmware? The traffic light will restart.'],
  ['WLAN einrichten', 'Set up Wi-Fi'],
  ['Verwaltungszugang einrichten', 'Set up management access'],
  ['Sprache', 'Language'],
  ['Deutsch', 'German'],
  ['Englisch', 'English'],
  ['Fehler: ', 'Error: '],
  ['Bitte notieren:', 'Please note:'],
  ['Verwaltungsseite nach dem Neustart', 'Management page after restart'],
  ['WLAN-MAC', 'Wi-Fi MAC'],
  ['Zurücksetzen: Bei laufendem ESP BOOT fünf Sekunden gedrückt halten.', 'Reset: Hold BOOT for five seconds while the ESP is running.'],
  ['WLAN-Name', 'Wi-Fi name'],
  ['WLAN auswählen oder eingeben', 'Select or enter Wi-Fi'],
  ['WLANs aktualisieren', 'Refresh Wi-Fi networks'],
  ['WLAN-Benutzername (nur WPA2-Enterprise)', 'Wi-Fi username (WPA2 Enterprise only)'],
  ['WLAN-Passwort (bei offenen WLANs leer lassen)', 'Wi-Fi password (leave blank for open networks)'],
  ['Lege einen Benutzernamen und ein Verwaltungspasswort fest.', 'Choose a username and management password.'],
  ['Verwaltungsbenutzername', 'Management username'],
  ['3 bis 32 Zeichen: Buchstaben, Ziffern, Punkt, Unterstrich und Bindestrich.', '3 to 32 characters: letters, digits, dot, underscore, and hyphen.'],
  ['Verwaltungspasswort', 'Management password'],
  ['Verwaltungspasswort wiederholen', 'Repeat management password'],
  ['Der Verwaltungszugang ist bereits eingerichtet und bleibt erhalten.', 'Management access is already configured and will be kept.'],
  ['Gespeicherte WLANs bleiben erhalten.', 'Saved Wi-Fi networks will be kept.'],
  ['Die Verwaltungspasswörter stimmen nicht überein.', 'The management passwords do not match.'],
  ['Benutzername und Verwaltungspasswort korrekt eingeben und Passwort wiederholen.', 'Enter a valid username and management password, and repeat the password.'],
  ['Benutzername oder Verwaltungspasswort ist ungültig.', 'Invalid username or management password.'],
  ['Verwaltungszugang konnte nicht gespeichert werden.', 'Management access could not be saved.'],
  ['Verwaltung anmelden', 'Sign in to management'],
  ['Melde dich mit dem Benutzernamen und Passwort aus der Einrichtung an.', 'Sign in with the username and password from setup.'],
  ['Benutzername', 'Username'],
  ['Passwort', 'Password'],
  ['Anmelden', 'Sign in'],
  ['Benutzername oder Passwort falsch.', 'Incorrect username or password.'],
  ['WPA2-Enterprise: Benutzername und Passwort eintragen. Es wird PEAP oder TTLS mit MSCHAPv2 versucht. Achtung: Die Server-Zertifikatsprüfung ist für diesen Test deaktiviert.', 'WPA2 Enterprise: Enter username and password. PEAP or TTLS with MSCHAPv2 is used. Warning: Server certificate verification is disabled for this test.'],
  ['Speichern und neu starten', 'Save and restart'],
  ['Job', 'Job'],
  ['Konfiguration', 'Configuration'],
  ['Steuerung', 'Controls'],
  ['Verwaltung', 'Management'],
  ['Aktueller Ampelzustand', 'Current traffic light state'],
  ['Ampel', 'Traffic light'],
  ['Jenkins-Zugang', 'Jenkins credentials'],
  ['Zum Ändern alle drei Werte erneut eingeben. Der Token wird nicht angezeigt.', 'To make changes, enter all three values again. The token is not displayed.'],
  ['Jenkins-URL', 'Jenkins URL'],
  ['Jenkins-Benutzer', 'Jenkins user'],
  ['Jenkins-API-Token', 'Jenkins API token'],
  ['Jenkins-Zugang speichern', 'Save Jenkins credentials'],
  ['Automatische Abfrage', 'Automatic polling'],
  ['Gespeicherte WLANs', 'Saved Wi-Fi networks'],
  ['Beim Start versucht die Ampel die WLANs von oben nach unten und bleibt beim ersten erreichbaren. Mit den Pfeilen änderst du die Reihenfolge; sie gilt ab dem nächsten Neustart. Neue WLANs kommen ans Ende. Bei voller Liste ersetzt ein neues WLAN das letzte Profil. Ist keines erreichbar, startet die WLAN-Einrichtung.', 'At startup, the traffic light tries Wi-Fi networks from top to bottom and stays on the first available one. Use the arrows to change the order; it takes effect after the next restart. New networks are added at the end. When the list is full, a new network replaces the last profile. If none works, Wi-Fi setup starts.'],
  ['Handy-Hotspot und IoT-WLAN: Hotspot nach oben setzen. Zum Verwalten Hotspot einschalten und die Ampel neu starten. Danach Hotspot ausschalten und die Ampel erneut starten, damit sie ins IoT-WLAN wechselt.', 'Phone hotspot and IoT Wi-Fi: move the hotspot to the top. Turn on the hotspot and restart the traffic light to manage it. Then turn off the hotspot and restart the traffic light again to switch to the IoT Wi-Fi.'],
  ['Bei geschützten WLANs das Passwort erneut eingeben. Ein bereits gespeicherter WLAN-Name wird aktualisiert.', 'Enter the password again for protected networks. An existing Wi-Fi network with the same name will be updated.'],
  ['WLAN speichern und neu starten', 'Save Wi-Fi and restart'],
  ['Logo', 'Logo'],
  ['Banner', 'Banner'],
  ['Banner hochladen', 'Upload banner'],
  ['Banner hochladen (SVG oder PNG, maximal 32 KiB)', 'Upload banner (SVG or PNG, up to 32 KiB)'],
  ['Titel bearbeiten', 'Edit title'],
  ['Titel', 'Title'],
  ['Speichern', 'Save'],
  ['Abbrechen', 'Cancel'],
  ['Titel gespeichert.', 'Title saved.'],
  ['Titel konnte nicht geladen werden.', 'The title could not be loaded.'],
  ['Titel konnte nicht gelesen werden.', 'The title could not be read.'],
  ['Titel konnte nicht gespeichert werden.', 'The title could not be saved.'],
  ['Ungueltiger Titel.', 'Invalid title.'],
  ['Branding', 'Branding'],
  ['Das Banner oben kannst du direkt anklicken und hochladen. Das Favicon wird hier hochgeladen. Beide Dateien bleiben nach einem Neustart im Flash gespeichert.', 'Click the banner above to upload it. Upload the favicon here. Both files remain in flash after a restart.'],
  ['Logo und Favicon sind nicht in der Firmware enthalten. Lade eigene Dateien hoch; sie bleiben nach einem Neustart im Flash gespeichert.', 'Logo and favicon are not included in the firmware. Upload your own files; they remain in flash after a restart.'],
  ['Logo (SVG oder PNG, maximal 32 KiB)', 'Logo (SVG or PNG, up to 32 KiB)'],
  ['Favicon (ICO oder PNG, maximal 32 KiB)', 'Favicon (ICO or PNG, up to 32 KiB)'],
  ['Noch nicht hochgeladen', 'Not uploaded yet'],
  ['Gespeichert', 'Saved'],
  ['Logo hochladen', 'Upload logo'],
  ['Favicon hochladen', 'Upload favicon'],
  ['Nicht unterstuetzter Dateityp.', 'Unsupported file type.'],
  ['Datei ist zu gross (maximal 32 KiB).', 'File is too large (maximum 32 KiB).'],
  ['Branding gespeichert.', 'Branding saved.'],
  ['Ungueltiger Branding-Typ.', 'Invalid branding type.'],
  ['Dateityp fehlt.', 'Missing file type.'],
  ['Logo: SVG/PNG; Favicon: ICO/PNG.', 'Logo: SVG/PNG; favicon: ICO/PNG.'],
  ['Nicht genug Speicher fuer den Upload.', 'Not enough memory for the upload.'],
  ['Upload konnte nicht gelesen werden.', 'The upload could not be read.'],
  ['Dateiinhalt passt nicht zum Format.', 'File contents do not match the format.'],
  ['Branding konnte nicht gespeichert werden.', 'Branding could not be saved.'],
  ['Verbunden', 'Connected'],
  ['Zuletzt verwendet', 'Last used'],
  ['Nach oben', 'Move up'],
  ['Nach unten', 'Move down'],
  ['Entfernen', 'Remove'],
  ['Ungueltige WLAN-Reihenfolge.', 'Invalid Wi-Fi order.'],
  ['WLAN-Reihenfolge konnte nicht gelesen werden.', 'Could not read Wi-Fi order.'],
  ['WLAN kann nicht weiter verschoben werden.', 'Wi-Fi network cannot be moved further.'],
  ['WLAN-Reihenfolge konnte nicht gespeichert werden.', 'Could not save Wi-Fi order.'],
  ['WLAN-Reihenfolge gespeichert. Gilt ab dem naechsten Neustart.', 'Wi-Fi order saved. It takes effect after the next restart.'],
  ['Der Jenkins-Status wird in diesem Abstand aktualisiert. Erlaubt sind ganze Minuten ab 1.', 'The Jenkins status is refreshed at this interval. Enter a whole number of minutes, at least 1.'],
  ['Abfrageintervall in Minuten', 'Polling interval in minutes'],
  ['Intervall speichern', 'Save interval'],
  ['CPU-Takt', 'CPU clock'],
  ['CPU-Modus', 'CPU mode'],
  ['Fest 160 MHz', 'Fixed 160 MHz'],
  ['Fest 240 MHz', 'Fixed 240 MHz'],
  ['Automatisch 40–160 MHz', 'Automatic 40–160 MHz'],
  ['Automatisch 40–240 MHz', 'Automatic 40–240 MHz'],
  ['Automatisch: im Leerlauf bis 40 MHz, bei WLAN-, Jenkins- und Verwaltungsanfragen bis zur gewählten Obergrenze. Ein Wechsel wirkt sofort und bleibt gespeichert.', 'Automatic: down to 40 MHz when idle, up to the selected limit for Wi-Fi, Jenkins, and management requests. Changes take effect immediately and are saved.'],
  ['Ungueltiger CPU-Modus.', 'Invalid CPU mode.'],
  ['CPU-Modus konnte nicht gelesen werden.', 'CPU mode could not be read.'],
  ['CPU-Modus konnte nicht aktiviert werden.', 'CPU mode could not be activated.'],
  ['CPU-Modus konnte nicht gespeichert werden.', 'CPU mode could not be saved.'],
  ['CPU-Modus gespeichert.', 'CPU mode saved.'],
  ['Ampelsteuerung', 'Traffic light controls'],
  ['Wähle, wer die Ampel steuert. Manuell und API setzen Jenkins vollständig aus.', 'Choose what controls the traffic light. Manual and API modes disable Jenkins polling.'],
  ['LED-Helligkeit', 'LED brightness'],
  ['Betriebsart', 'Operating mode'],
  ['Laufender Jenkins-Build', 'Running Jenkins build'],
  ['Pulsieren', 'Pulse'],
  ['Blinken (an/aus)', 'Blink (on/off)'],
  ['Gilt für die Statusfarbe eines laufenden Jenkins-Builds.', 'Applies to the status color of a running Jenkins build.'],
  ['Auto (Jenkins)', 'Auto (Jenkins)'],
  ['Manuell', 'Manual'],
  ['Disco', 'Disco'],
  ['Disco-Animation', 'Disco animation'],
  ['Zehn kompakte Effekte, angelehnt an WLED.', 'Ten compact effects inspired by WLED.'],
  ['Rot: Aus', 'Red: Off'],
  ['Gelb: Aus', 'Yellow: Off'],
  ['Grün: Aus', 'Green: Off'],
  ['LED-Farben', 'LED colors'],
  ['Rot', 'Red'],
  ['Gelb', 'Yellow'],
  ['Grün', 'Green'],
  ['Jenkins: Grau', 'Jenkins: Grey'],
  ['Im API-Modus:', 'In API mode:'],
  ['mit', 'with'],
  ['Jenkins-Job', 'Jenkins job'],
  ['Wähle den Job, dessen Status die Ampel anzeigen soll.', 'Choose the job whose status the traffic light should show.'],
  ['Jobliste aktualisieren', 'Refresh job list'],
  ['Liste noch nicht geladen.', 'Job list not loaded yet.'],
  ['Gespeicherte Jobliste.', 'Saved job list.'],
  ['Jobliste aktualisiert.', 'Job list refreshed.'],
  ['Jenkins-Zugang geändert. Jobliste aktualisieren.', 'Jenkins credentials changed. Refresh the job list.'],
  ['Ungültige Jobliste.', 'Invalid job list.'],
  ['Job anzeigen', 'Show job'],
  ['Gerätedaten werden geladen ...', 'Loading device information ...'],
  ['Jobliste wird geladen', 'Loading job list'],
  ['Jobliste wird geladen ...', 'Loading job list ...'],
  ['Jenkins-Status wird abgefragt', 'Checking Jenkins status'],
  ['Jenkins-Status wird abgefragt ...', 'Checking Jenkins status ...'],
  ['Chip', 'Chip'],
  ['Mikrocontroller', 'Microcontroller'],
  ['Modell', 'Model'],
  ['Revision', 'Revision'],
  ['CPU-Kerne', 'CPU cores'],
  ['Aktueller Takt', 'Current clock'],
  ['Takt bei Abfrage', 'Clock during request'],
  ['Maximaler Takt', 'Maximum clock'],
  ['Bus-Takt', 'Bus clock'],
  ['Quarz', 'Crystal'],
  ['Speicher', 'Memory'],
  ['Flash', 'Flash'],
  ['Heap intern frei', 'Free internal heap'],
  ['Heap intern gesamt', 'Total internal heap'],
  ['Heap Minimum', 'Minimum free heap'],
  ['Größter Block', 'Largest free block'],
  ['PSRAM frei', 'Free PSRAM'],
  ['PSRAM gesamt', 'Total PSRAM'],
  ['Firmware', 'Firmware'],
  ['Version', 'Version'],
  ['Build', 'Build'],
  ['ESP-IDF', 'ESP-IDF'],
  ['Laufzeit', 'Uptime'],
  ['WLAN', 'Wi-Fi'],
  ['Name', 'Name'],
  ['MAC-Adresse', 'MAC address'],
  ['IPv4', 'IPv4'],
  ['IPv6 global', 'Global IPv6'],
  ['IPv6 lokal', 'Link-local IPv6'],
  ['Signal', 'Signal'],
  ['Kanal', 'Channel'],
  ['Unbekannt', 'Unknown'],
  ['Nicht vorhanden', 'Unavailable'],
  ['Nicht verbunden', 'Not connected'],
  ['Keine Jobs gefunden.', 'No jobs found.'],
  ['Gerätedaten konnten nicht geladen werden.', 'Device information could not be loaded.'],
  ['Jenkins steuert die Ampel.', 'Jenkins controls the traffic light.'],
  ['Die LEDs können einzeln geschaltet werden.', 'The LEDs can be controlled individually.'],
  ['Die WS2812-Ampel spielt die gewählte Disco-Animation.', 'The WS2812 traffic light plays the selected disco animation.'],
  ['Der Status wird ausschließlich per REST API gesetzt.', 'The status is set only through the REST API.'],
  ['An', 'On'],
  ['Aus', 'Off'],
  ['Bitte mindestens 1 ganze Minute angeben.', 'Please enter at least 1 whole minute.'],
  ['Suche nach WLANs ...', 'Searching for Wi-Fi networks ...'],
  ['Keine WLANs gefunden. Du kannst den Namen auch manuell eingeben.', 'No Wi-Fi networks found. You can also enter the name manually.'],
  ['Neustart wird bereits vorbereitet.', 'A restart is already being prepared.'],
  ['Ungültige Konfigurationsdaten.', 'Invalid configuration data.'],
  ['Daten konnten nicht gelesen werden.', 'Data could not be read.'],
  ['Gespeichert. Der ESP startet in zwei Sekunden neu.', 'Saved. The ESP will restart in two seconds.'],
  ['Weiterleitung zur CI-Lights-Einrichtung.', 'Redirecting to CI-Lights setup.'],
  ['Einrichtungsseite konnte nicht erstellt werden.', 'The setup page could not be created.'],
  ['Nicht genug Speicher fuer die Einrichtungsseite.', 'Not enough memory for the setup page.'],
  ['Bitte WLAN-Name und WLAN-Passwort korrekt ausfüllen.', 'Please enter a valid Wi-Fi name and password.'],
  ['Speichern fehlgeschlagen.', 'Saving failed.'],
  ['Eine WLAN-Suche läuft bereits.', 'A Wi-Fi scan is already running.'],
  ['WLAN-Suche konnte nicht gestartet werden.', 'The Wi-Fi scan could not be started.'],
  ['Gefundene WLANs konnten nicht gelesen werden.', 'Discovered Wi-Fi networks could not be read.'],
  ['Nicht genug Speicher für die WLAN-Liste.', 'Not enough memory for the Wi-Fi list.'],
  ['WLAN-Liste konnte nicht erstellt werden.', 'The Wi-Fi list could not be created.'],
  ['Gespeicherte WLANs konnten nicht gelesen werden.', 'Saved Wi-Fi networks could not be read.'],
  ['Ungueltiger WLAN-Name.', 'Invalid Wi-Fi name.'],
  ['WLAN-Name konnte nicht gelesen werden.', 'The Wi-Fi name could not be read.'],
  ['Das letzte WLAN kann nicht entfernt werden.', 'The last Wi-Fi network cannot be removed.'],
  ['WLAN nicht gefunden.', 'Wi-Fi network not found.'],
  ['WLAN konnte nicht entfernt werden.', 'The Wi-Fi network could not be removed.'],
  ['WLAN entfernt. Die Aenderung gilt nach dem naechsten Neustart.', 'Wi-Fi network removed. The change takes effect after the next restart.'],
  ['Bitte zuerst Jenkins-URL, Benutzer und API-Token speichern.', 'Please save the Jenkins URL, user, and API token first.'],
  ['Nicht genug Speicher fuer die Jobliste.', 'Not enough memory for the job list.'],
  ['Ausgewaehlter Job konnte nicht gelesen werden.', 'The selected job could not be read.'],
  ['Jenkins-Jobliste konnte nicht vollstaendig geladen werden.', 'The Jenkins job list could not be loaded completely.'],
  ['Jobliste konnte nicht vollstaendig erstellt werden.', 'The job list could not be created completely.'],
  ['Jobliste konnte nicht erstellt werden.', 'The job list could not be created.'],
  ['Ungültige Jenkins-Zugangsdaten.', 'Invalid Jenkins credentials.'],
  ['Jenkins-Zugangsdaten konnten nicht gelesen werden.', 'Jenkins credentials could not be read.'],
  ['Bitte eine HTTPS-Jenkins-URL, Benutzer und API-Token angeben.', 'Please enter an HTTPS Jenkins URL, user, and API token.'],
  ['Jenkins-Zugang konnte nicht gespeichert werden.', 'Jenkins credentials could not be saved.'],
  ['Jenkins-Zugang gespeichert. Wähle jetzt einen Jenkins-Job aus.', 'Jenkins credentials saved. Choose a Jenkins job now.'],
  ['Abfrageintervall konnte nicht gelesen werden.', 'The polling interval could not be read.'],
  ['Ungültiges Abfrageintervall.', 'Invalid polling interval.'],
  ['Bitte ein ganzzahliges Intervall ab 1 Minute angeben.', 'Please enter a whole number of minutes, at least 1.'],
  ['Abfrageintervall konnte nicht gespeichert werden.', 'The polling interval could not be saved.'],
  ['Abfrageintervall wurde gespeichert.', 'The polling interval was saved.'],
  ['Ungültige Jobauswahl.', 'Invalid job selection.'],
  ['Jobauswahl konnte nicht gelesen werden.', 'The job selection could not be read.'],
  ['Jobauswahl konnte nicht gespeichert werden.', 'The job selection could not be saved.'],
  ['Job gespeichert. Die Ampel wurde aktualisiert.', 'Job saved. The traffic light was updated.'],
  ['Manuelle Ampelsteuerung ist nicht verfügbar.', 'Manual traffic light control is unavailable.'],
  ['Bitte zuerst den manuellen Modus aktivieren.', 'Please enable manual mode first.'],
  ['Ungültige LED-Steuerung.', 'Invalid LED control.'],
  ['LED-Steuerung konnte nicht gelesen werden.', 'LED control could not be read.'],
  ['Unbekannte LED.', 'Unknown LED.'],
  ['Ungültige LED-Farbe.', 'Invalid LED color.'],
  ['LED-Farbe konnte nicht gelesen werden.', 'LED color could not be read.'],
  ['LED-Zustand konnte nicht gelesen werden.', 'LED state could not be read.'],
  ['Disco-Steuerung ist nicht verfuegbar.', 'Disco control is unavailable.'],
  ['Bitte zuerst den Disco-Modus aktivieren.', 'Please enable disco mode first.'],
  ['Ungueltige Disco-Animation.', 'Invalid disco animation.'],
  ['Disco-Animation konnte nicht gelesen werden.', 'Disco animation could not be read.'],
  ['Unbekannte Disco-Animation.', 'Unknown disco animation.'],
  ['Disco-Animation aktiviert.', 'Disco animation enabled.'],
  ['Ungueltiger Build-Effekt.', 'Invalid build effect.'],
  ['Build-Effekt konnte nicht gelesen werden.', 'Build effect could not be read.'],
  ['Unbekannter Build-Effekt.', 'Unknown build effect.'],
  ['Build-Effekt konnte nicht gespeichert werden.', 'Build effect could not be saved.'],
  ['Build-Effekt gespeichert.', 'Build effect saved.'],
  ['Helligkeitssteuerung ist nicht verfuegbar.', 'Brightness control is unavailable.'],
  ['Ungueltige Helligkeit.', 'Invalid brightness.'],
  ['Helligkeit konnte nicht gelesen werden.', 'Brightness could not be read.'],
  ['Helligkeit muss eine ganze Zahl von 1 bis 100 sein.', 'Brightness must be a whole number from 1 to 100.'],
  ['Helligkeit konnte nicht gespeichert werden.', 'Brightness could not be saved.'],
  ['Betriebsart kann nicht gesetzt werden.', 'The operating mode cannot be set.'],
  ['Ungültige Betriebsart.', 'Invalid operating mode.'],
  ['Betriebsart konnte nicht gelesen werden.', 'The operating mode could not be read.'],
  ['Betriebsart muss auto, manual, disco oder api sein.', 'The mode must be auto, manual, disco, or api.'],
  ['Jenkins-Betriebsart ist aktiv.', 'Jenkins mode is active.'],
  ['Disco-Modus ist aktiv. Jenkins wird ignoriert.', 'Disco mode is active. Jenkins is ignored.'],
  ['Betriebsart ist aktiv. Jenkins wird ignoriert.', 'The mode is active. Jenkins is ignored.'],
  ['REST-API-Steuerung ist nicht verfügbar.', 'REST API control is unavailable.'],
  ['Bitte zuerst den API-Modus aktivieren.', 'Please enable API mode first.'],
  ['Ungültiger API-Status.', 'Invalid API status.'],
  ['API-Status konnte nicht gelesen werden.', 'The API status could not be read.'],
  ['Status muss off, red, yellow oder green sein.', 'Status must be off, red, yellow, or green.'],
  ['Geraetedaten konnten nicht erstellt werden.', 'Device information could not be created.'],
  ['Ungueltige Sprache.', 'Invalid language.'],
  ['Sprache konnte nicht gelesen werden.', 'Language could not be read.'],
  ['Sprache konnte nicht gespeichert werden.', 'Language could not be saved.'],
]);

const uiI18n = (() => {
  let language = 'de';
  const originalTitle = document.title;
  const textNodes = [];
  const attributes = [];
  const knownNodes = new WeakSet();
  const knownAttributes = new WeakMap();
  const listeners = [];

  function t(source) {
    return language === 'en' ? uiTranslations.get(source) || source : source;
  }

  function response(source) {
    if (language !== 'en') return source;
    const poll = /^Jenkins wird alle (\d+) Minute(?:n)? abgefragt\.$/.exec(source);
    if (poll) return `Jenkins is polled every ${poll[1]} minute${poll[1] === '1' ? '' : 's'}.`;
    const brightness = /^Helligkeit auf (\d+) % gesetzt\.$/.exec(source);
    if (brightness) return `Brightness set to ${brightness[1]}%.`;
    const color = /^Farbe für (Rot|Gelb|Grün) gesetzt\.$/.exec(source);
    if (color) return `Color set for ${t(color[1])}.`;
    const light = /^(Rot|Gelb|Grün) ist jetzt (eingeschaltet|ausgeschaltet)\.$/.exec(source);
    if (light) return `${t(light[1])} is now ${light[2] === 'eingeschaltet' ? 'on' : 'off'}.`;
    return t(source);
  }

  function capture() {
    const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
    for (let node = walker.nextNode(); node; node = walker.nextNode()) {
      if (knownNodes.has(node) || /^(SCRIPT|STYLE)$/.test(node.parentElement?.tagName || '')) continue;
      const original = node.nodeValue.trim();
      if (!uiTranslations.has(original)) continue;
      knownNodes.add(node);
      textNodes.push({node, original, prefix: node.nodeValue.match(/^\s*/)[0], suffix: node.nodeValue.match(/\s*$/)[0]});
    }
    for (const element of document.querySelectorAll('[aria-label],[title],[placeholder]')) {
      let seen = knownAttributes.get(element);
      if (!seen) { seen = new Set(); knownAttributes.set(element, seen); }
      for (const name of ['aria-label', 'title', 'placeholder']) {
        if (seen.has(name)) continue;
        const original = element.getAttribute(name);
        if (!uiTranslations.has(original)) continue;
        seen.add(name);
        attributes.push({element, name, original});
      }
    }
  }

  function apply() {
    document.documentElement.lang = language;
    document.title = t(originalTitle);
    for (const {node, original, prefix, suffix} of textNodes) {
      node.nodeValue = prefix + t(original) + suffix;
    }
    for (const {element, name, original} of attributes) element.setAttribute(name, t(original));
    document.querySelectorAll('[data-language]').forEach(button => {
      button.setAttribute('aria-pressed', String(button.dataset.language === language));
    });
    for (const listener of listeners) listener();
  }

  async function change(next) {
    if (next === language) return;
    const result = await fetch('/api/language', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({language:next})});
    if (!result.ok) throw new Error(response(await result.text()));
    language = next;
    apply();
  }

  async function start() {
    capture();
    document.querySelectorAll('[data-language]').forEach(button => button.addEventListener('click', () => {
      change(button.dataset.language).catch(error => alert(error.message));
    }));
    try {
      const result = await fetch('/api/language', {cache:'no-store'});
      if (result.ok) {
        const saved = (await result.json()).language;
        if (saved === 'de' || saved === 'en') language = saved;
      }
    } catch (_) {}
    apply();
  }

  return {t, response, start, onChange: listener => listeners.push(listener), get language() { return language; }};
})();
