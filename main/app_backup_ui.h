#pragma once

#define CONFIG_TRANSFER_STYLE \
    ".config-transfer{margin-top:1.3rem;padding-top:1rem;border-top:1px solid #dbe7ef}.config-transfer h2{font-size:1.1rem}.config-transfer .hint{font-size:.88rem;color:#5c7080}.config-transfer [data-error=true]{color:#a11227}.config-transfer [hidden]{display:none!important}.config-summary{padding:1rem;background:#f1f7fc;border:1px solid #dbe7ef;border-radius:.7rem;white-space:pre-wrap;overflow-wrap:anywhere}.config-confirm{display:flex;align-items:flex-start;gap:.6rem;font-weight:500}.config-confirm input{width:auto;flex:0 0 auto;margin:.25rem 0 0}.config-transfer button{display:block;margin-top:1rem}.config-transfer [role=status]{white-space:pre-wrap}"

#define CONFIG_IMPORT_HTML \
    "<section id=\"config-transfer\" class=\"config-transfer\" aria-labelledby=\"config-import-title\"><h2 id=\"config-import-title\">Konfiguration importieren</h2>" \
    "<p class=\"hint\">Eine verschlüsselte Sicherung übernimmt WLANs, Jenkins, Verwaltungszugang, LED-Einstellungen, automatische Updates und Branding. Danach startet die Ampel neu.</p>" \
    "<form id=\"config-import-form\"><label>Konfigurationsdatei<input id=\"config-import-file\" type=\"file\" accept=\".json,application/json\" required></label>" \
    "<label>Sicherungspasswort<input id=\"config-import-password\" type=\"password\" autocomplete=\"off\" required minlength=\"12\" maxlength=\"128\"></label>" \
    "<button id=\"config-validate\" type=\"submit\">Datei prüfen</button></form>" \
    "<div id=\"config-import-review\" hidden><p id=\"config-import-summary\" class=\"config-summary\"></p>" \
    "<label class=\"config-confirm\"><input id=\"config-import-confirm\" type=\"checkbox\"><span>Aktuelle Einstellungen ersetzen und den Verwaltungszugang aus der Sicherung übernehmen.</span></label>" \
    "<p class=\"hint\">Nach dem Import gilt das Verwaltungspasswort der Quell-Ampel. Bei gleichen Gerätenamen verwendet die neue Ampel gegebenenfalls ihre MAC-Adresse als Namen.</p>" \
    "<button id=\"config-import-apply\" type=\"button\" disabled>Konfiguration übernehmen und neu starten</button></div>" \
    "<p id=\"config-import-message\" class=\"hint\" role=\"status\" aria-live=\"polite\"></p></section>"

#define CONFIG_EXPORT_HTML \
    "<section class=\"config-transfer\" aria-labelledby=\"config-export-title\"><h2 id=\"config-export-title\">Konfiguration exportieren</h2>" \
    "<p class=\"hint\">Die Sicherung enthält auch WLAN-Zugangsdaten, Jenkins-Token und das Verwaltungskonto. Sie wird mit deinem Sicherungspasswort verschlüsselt. Dieses Passwort wird nicht auf der Ampel gespeichert.</p>" \
    "<form id=\"config-export-form\"><label>Neues Sicherungspasswort<input id=\"config-export-password\" type=\"password\" autocomplete=\"new-password\" required minlength=\"12\" maxlength=\"128\"></label>" \
    "<label>Sicherungspasswort wiederholen<input id=\"config-export-confirm\" type=\"password\" autocomplete=\"new-password\" required minlength=\"12\" maxlength=\"128\"></label>" \
    "<p class=\"hint\">Mindestens 12 Zeichen. Ohne dieses Passwort lässt sich die Sicherung nicht importieren.</p>" \
    "<button id=\"config-export\" type=\"submit\">Verschlüsselte Sicherung herunterladen</button></form>" \
    "<p id=\"config-export-message\" class=\"hint\" role=\"status\" aria-live=\"polite\"></p></section>"
