# Diesel Heater Gateway · v0.5.0

Lokales Gateway für **Seeed Studio XIAO ESP32-S3**: deutsche Weboberfläche, WLAN- und Bluetooth-Suche, MQTT/Home Assistant und Firmware-Updates im Browser.

**Status:** Acht Protokollprofile mit Bluetooth-Verbindung, PIN, Live-Anzeige und gemeinsamer Web-/MQTT-Steuerung sind implementiert. Die konkrete Heizung wurde noch nicht physisch getestet. Eine gespeicherte Auswahl verbindet noch nicht; die Demo sendet keine Heizungsbefehle oder MQTT-Messwerte.

## Funktionen

**Entwicklungsstand der Protokolle:** AA55/AA66 (jeweils Klartext und verschlüsselt), ABBA, CBFF und Hcalory MVP1/MVP2 liegen jetzt als C++-Protokollmodul mit Referenztests vor. Die Verbindungsschicht läuft im Hintergrund. Nach Neustart oder Verbindungsabbruch wird bewusst manuell neu verbunden; Schaltbefehle werden nicht automatisch wiederholt. Umfang und Grenzen: [PROTOCOLS.md](PROTOCOLS.md).

| Bereich | Funktionen und Grenzen |
| --- | --- |
| Übersicht | Verbindungen, Status, responsive Oberfläche und ausdrücklich markierte Demo für Temperatur, Leistung und Lüften; echte Werte erscheinen erst nach gültiger Heizungsantwort |
| Bluetooth | Etwa acht Sekunden Suche, maximal 80 Geräte, Namen/Adressen/Service-UUIDs, Signalstärke, Heizungsfilter, Auswahl speichern/löschen; UUIDs sind nur Protokollhinweise |
| WLAN | 2,4-GHz-Suche, Zugangsdaten speichern, Passwort beibehalten, automatische Wiederverbindung, Setup-Hotspot; kein 5 GHz |
| MQTT | Broker/Port/Benutzer/Passwort, Sendeintervall, Hintergrundverbindung, Status, Befehle und ehrliche Rückmeldungen; TCP ohne TLS |
| Home Assistant | 12 Discovery-Einträge mit stabilen IDs, Verfügbarkeit und erneuter Anmeldung nach HA-Neustart; Heizungsregler bleiben ohne aktuelle Heizungsantwort unavailable |
| Lüften | Eigener Demo-Modus und MQTT-Befehle vorbereitet; ABBA-Lüften aus Aus/Standby; Beenden am Originalbedienteil, übrige Profile gesperrt |
| Updates | Gezielte Suche nach stabilen GitHub-Releases, Versionshinweise, Installation nach Bestätigung, lokaler .bin-Upload, Fortschritt und Neustartkontrolle |
| System | Anzeigename, Laufzeit, Speicher, Ereignisse, Diagnose-Download, Neustart |

## Hardware und Erstinstallation

XIAO ESP32-S3 mit 8 MB Flash/PSRAM, externer Antenne und USB-Datenkabel. Nur dieses Board ist vorgesehen. Für die Erstinstallation das Projekt mit PlatformIO über USB aufspielen: `pio run -t upload`.

Ohne gespeichertes WLAN startet **DieselHeater-Setup**, Passwort **dieselheater**. Verbinden und [192.168.4.1](http://192.168.4.1) öffnen. Unter **WLAN** das eigene Netzwerk eintragen. Danach die im Router angezeigte IP oder [dieselheater.local](http://dieselheater.local) verwenden.

Der Setup-Hotspot startet nach etwa 30 Sekunden ohne WLAN und schaltet sich eine Minute nach erfolgreicher Verbindung aus. WLAN- und Bluetooth-Suchen laufen nacheinander. Geräteauswahl und Einstellungen bleiben bei Neustarts und App-Updates gespeichert.

## Heizung verbinden

1. Hersteller-App schließen und unter **Bluetooth-Geräte** nach der Heizung suchen.
2. Das eigene Steuergerät auswählen. Ein Wechsel des Geräts löscht die bisherige Profil-/PIN-Zuordnung.
3. Das passende Protokoll wählen, die vierstellige Geräte-PIN eingeben und **Profil & PIN speichern** drücken. Die Service-UUID allein bestimmt das Profil nicht eindeutig.
4. **Verbinden** drücken. Dabei werden ausschließlich Anmeldung und Statusabfragen gesendet. MVP2 verwendet dabei die lokale Uhrzeit des Browsers.
5. Erst bei **Live-Status empfangen** werden Messwerte und Regler freigegeben. Bei ausbleibender oder veralteter Antwort bleibt die Steuerung gesperrt.
6. Temperatur-/Leistungsmodus zuerst wählen und seine Bestätigung abwarten. Anschließend den Sollwert ändern. Der gemeinsame Bereich der Oberfläche ist 8–35 °C bzw. Stufe 1–10.

Ein Befehl durchläuft „angenommen“, „gesendet“ und gegebenenfalls „bestätigt“. Nach zehn Sekunden ohne passende Rückmeldung erscheint „nicht bestätigt“; es gibt keinen automatischen Wiederholungsversuch. Bei CBFF müssen Änderungen von Sollwert/Betriebsart aus ausgeschaltetem Zustand zunächst durch bewusstes Einschalten vorbereitet werden, weil der Datenbefehl selbst einschaltet. Fahrenheit-Sollwerte sind derzeit nur für Hcalory freigegeben; andere Geräte dafür zunächst auf Celsius stellen.

Vor Firmware-Update, Suche, Gerätewechsel oder Neustart die Bluetooth-Verbindung mit **Trennen** schließen. Das trennt die Funkverbindung und ist **kein Ausschalten der Heizung**. Kein automatisches Wiederverbinden nach Neustart oder Funkverlust. Der erste echte Gerätetest steht weiterhin aus.

## MQTT und Home Assistant

1. Broker bereitstellen, beispielsweise Mosquitto, und Home Assistant damit verbinden.
2. Im Gateway **MQTT** öffnen und Broker, Port (normalerweise 1883), Benutzer und Passwort eintragen.
3. **MQTT aktivieren** und **Home-Assistant-Erkennung aktivieren**, dann speichern.
4. Auf Verbindung und bestätigte Discovery warten. Die Bestätigung kommt vom Broker, nicht von Home Assistant.
5. Das Gateway erscheint über die MQTT-Integration als ein Gerät. Die Integration und der Broker werden nicht automatisch in HA eingerichtet.

Die zwölf Einträge: WLAN-Signal, Laufzeit, freier Speicher, Heizungsverbindung, Status aktualisieren, Neustart, Thermostat, Leistungsstufe, Betriebsart, Raumtemperatur, Versorgungsspannung und Gehäusetemperatur.

Gateway-Status und Service-Befehle funktionieren. Heizungsbefehle werden über denselben Treiber wie die Weboberfläche abgewickelt. Ohne aktuelle Heizungsantwort werden sie abgewiesen. Annahme und Rücklesebestätigung sind getrennte Meldungen. Feste Basis `dieselheater/<Geräte-ID>`; QoS 1 für Status und Discovery, retained Verfügbarkeit mit Last Will. Sendeintervall 10–300 Sekunden, Standard 30. Discovery aus entfernt bei erreichbarem Broker die Konfigurationen; wieder an verwendet dieselben IDs. TLS und benutzerdefinierte Topics sind nicht implementiert. Alle Topics und Payloads: [MQTT.md](MQTT.md).

## Lüften ohne Heizen

Die Demo zeigt **Lüften** mit einer beispielhaften Lüfterstufe. MQTT akzeptiert `command/mode = fan_only` und `command/operating_mode = ventilation`; ohne bestätigte Unterstützung lautet die Antwort `ventilation_not_supported`.

Home Assistant erhält die Lüftungsoption bei ausgewähltem ABBA-Profil. Lüften lässt sich nur aus fehlerfreiem Aus/Standby starten. **Beenden muss derzeit am Originalbedienteil erfolgen**, da die Vorlage keinen eindeutigen separaten Stoppbefehl dafür liefert. Änderungen während des Lüftens sind gesperrt. Hcalory- und CBFF-Lüftungsbefehle bleiben wegen unbestätigter bzw. widersprüchlicher Implementierung in der Vorlage gesperrt.

## Updates aus GitHub

Unter **Updates** das Repository **robbyatbln/esp32-diesel-heater-gateway** speichern.

1. **Nach Updates suchen** prüft die letzte stabile Veröffentlichung.
2. Eine neuere Version erscheint mit Versionshinweisen.
3. **Neue Version installieren** fragt vor dem Start nach Bestätigung.
4. Der Browser lädt über HTTPS, prüft SHA-256 und überträgt die App blockweise.
5. Der ESP prüft Größe, App-Kopf, Projektkennung, Prüfsumme und ESP-Image. Erst danach wird die neue App für den Neustart ausgewählt.
6. Die Oberfläche bestätigt nach dem Neustart die tatsächlich laufende Version.

Kein GitHub-Token auf dem ESP erforderlich. Eine Änderung auf main ist **noch kein Update**: Erst ein vorbereitetes, veröffentlichtes Release wird angeboten. Unterstützt werden öffentliche Repositories und stabile Tags `vX.Y.Z`. Firmware und Manifest liegen im jeweiligen Git-Tag. Netzwerkfehler und GitHub-Anfragelimits werden angezeigt.

## Update aus Datei

Unter **Updates → Aus Datei** die **firmware-app-at-0x10000.bin** einer passenden Projektversion wählen. Kein ZIP oder Gesamtimage. Der Dateiname beschreibt die USB-App-Position; beim Web-Update wird automatisch der inaktive App-Slot verwendet.

WLAN, MQTT und Geräteauswahl bleiben erhalten. Abgebrochene oder ungültige Übertragungen aktivieren die neue Firmware nicht. Stromversorgung und Browser während der Installation angeschlossen bzw. geöffnet lassen.

Die Prüfungen sind **keine digitale Herstellersignatur**. Formal gültige, aber fehlerhaft programmierte Firmware kann Probleme verursachen. Automatisches Boot-Rollback ist nicht aktiviert; USB bleibt der Wiederherstellungsweg. [Technische Update-Dokumentation](UPDATES.md).

## Entwicklung und Veröffentlichungen

Python, PlatformIO und Node.js installieren. Plattform und Bibliotheken sind in `platformio.ini` festgelegt.

```sh
python -m pip install platformio==6.1.19
node tests/update.test.cjs
python -m platformio run
```

Neue Version veröffentlichen:

1. `VERSION` in `src/main.cpp` erhöhen, `RELEASE_NOTES.md` aktualisieren und betroffene Funktionen testen.
2. `python scripts/prepare_release.py` baut App und Manifest.
3. Quellcode, Dokumentation, App und Manifest gemeinsam committen.
4. Tag `vX.Y.Z` auf den Commit setzen und Branch plus Tag pushen.
5. Der Release-Workflow prüft und veröffentlicht das Paket. CI baut und prüft den Quellcode unabhängig.

Manuell ist dasselbe über GitHub Releases möglich: Tag auf den vorbereiteten Commit setzen und App/Manifest anhängen. Veröffentlichte Tags nicht nachträglich ersetzen; für Änderungen eine neue Version verwenden.

## Projektaufbau

- `src/main.cpp`: WLAN, BLE-Suche, Web-API und gemeinsamer Treiberanschluss.
- `src/heater_gateway.*`: BLE-Verbindung, Anmeldung, Queues, Status und Befehlsbestätigung.
- `src/heater_protocol.*`, `src/heater_control.*`: Protokollformate und geprüfte Steuerungsregeln.
- `src/web_ui.h`: vollständige lokale Oberfläche ohne externe Schriftarten oder JavaScript-Bibliotheken.
- `src/mqtt_gateway.*`: MQTT, Discovery, Konfiguration und Befehlsprüfung.
- `src/firmware_update.*`: OTA-Sitzungen, Übertragung, Prüfung, Aktivierung.
- `scripts/prepare_release.py`: App-/Manifest-Paketierung.
- `tests/update.test.cjs`: SHA-256, Versionsvergleich, Manifestprüfung, JavaScript-Syntax.
- `.github/workflows/`: Build und Veröffentlichung vorbereiteter Releases.

## Netzwerk, Zugangsdaten und Grenzen

HTTP mit Sitzungstoken gegen fremde Formularaufrufe, aber ohne Benutzeranmeldung. Wer das Gateway im lokalen Netz erreichen kann, kann es konfigurieren und Firmware installieren. Nicht direkt ins Internet freigeben. MQTT ist ebenfalls unverschlüsselt. Passwörter liegen in NVS und werden von den Konfigurations-APIs nicht zurückgeliefert.

Keine WLAN-/MQTT-Zugangsdaten, persönlichen Flash-Backups oder privaten Netzwerkdetails gehören ins Repository. Der Diagnose-Download entfernt unter anderem WLAN-Namen, Passwörter und Sitzungstoken.

Der geprüfte Stand steht in [TESTERGEBNIS.md](TESTERGEBNIS.md). Reale Heizung und tatsächlicher HA-Import stehen noch aus.

Protokollrecherche: [homeassistant-diesel-heater](https://github.com/Spettacolo83/homeassistant-diesel-heater). Abhängigkeiten: [Arduino-ESP32](https://github.com/espressif/arduino-esp32), [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino), [ArduinoJson](https://arduinojson.org/). Deren Lizenzen gelten für die jeweiligen Bibliotheken; für den eigenen Projektcode wurde noch keine gesonderte Lizenz festgelegt.
