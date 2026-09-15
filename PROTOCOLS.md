# C++-Portierung der Heizungsprotokolle

Stand: 14. September 2026. Die Portierung liegt als eigenständiges C++-Modul in `src/heater_protocol.h` und `src/heater_protocol.cpp`. Sie benötigt weder Python noch Home Assistant und wird vom ESP32-Projekt mitkompiliert.

**Seit v0.5.0 an Bluetooth, Web und MQTT angebunden.** Das reine Codec-Modul sendet selbst nichts; `heater_gateway.cpp` übernimmt Verbindung, Anmeldung, Empfang und Befehle. Ein Verbindungstest mit einer echten Heizung steht weiterhin aus.

## Herkunft

Grundlage ist [Spettacolo83/homeassistant-diesel-heater](https://github.com/Spettacolo83/homeassistant-diesel-heater/tree/0de895884f597975f2ef90638bf45e358db00519), festgehalten auf Commit `0de895884f597975f2ef90638bf45e358db00519`. Übertragen wurden die grundlegenden Statusdecoder und Steuerbefehle aus `diesel_heater_ble/src/diesel_heater_ble/protocol.py`. Die Zuordnung für Leistungsstufen stammt zusätzlich aus `custom_components/diesel_heater/coordinator.py` (`async_set_level`). Lizenz und Urheberhinweise stehen in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Umfang

| Familie | Statusdecoder | Befehle im C++-Modul | Besonderheit |
| --- | --- | --- | --- |
| AA55 | 18/20 Bytes | Abfragen, Ein/Aus, Temperatur-/Leistungsmodus, Sollwert | Befehl 4 gilt für den Sollwert der aktuellen Betriebsart |
| AA55 verschlüsselt | 48 Bytes, XOR `password` | Wie AA55, Befehle unverschlüsselt | Andere Byte-Reihenfolge und Temperaturauflösung |
| AA66 | 20 Bytes | Wie AA55 | Gehäusetemperatur-Skalierung wie Vorlage |
| AA66 verschlüsselt | 48 Bytes, XOR `password` | Wie AA55 | Anderes Fehlerfeld; Fahrenheit-Zielwert wie Vorlage nach Celsius umgerechnet |
| ABBA / HeaterCC | Ab 21 Bytes | Abfragen, Umschalten Ein/Aus, Modus, Sollwert, Lüften | Ein/Aus ist ein Toggle, kein absoluter Schaltbefehl |
| CBFF / FEAA / Sunster | 46/47 Bytes, Klartext oder MAC-abhängiges XOR | Abfragen, PIN-Handshake, Ein/Aus, Temperatur/Leistung | Kombinierte Befehle enthalten Betriebsart, Sollwert und Ein/Aus |
| Hcalory MVP1 | Gemeinsamer Hcalory-Decoder ab 38 Bytes | Abfragen, Ein/Aus, Modus, Temperatur, Stufe | Zuordnung des gemeinsamen Decoders folgt der Vorlage; physisch ungeprüft |
| Hcalory MVP2 | Gemeinsamer Hcalory-Decoder ab 38 Bytes | Wie MVP1, zusätzlich PIN-Handshake und zeitgestützte Abfrage | Abfrage benötigt gültige lokale Uhrzeit |

Der Status enthält Laufzustand, Phase, Betriebsart, Fehler, Temperatureinheit, Raum-/Gehäusetemperatur, Spannung, gegebenenfalls Höhe, Solltemperatur und Stufe. Fehlende Sollwerte werden mit eigenen Gültigkeitsflags markiert.

**Nicht Teil dieser Portierung:** Die vielen zusätzlichen Geräteeinstellungen der HA-Integration, Timer, Tank-/Verbrauchsschätzung, Pumpen-/Verbrennungseinstellungen, Kalibrierungsoberflächen, Zigbee sowie automatische Wiederverbindungen. Die Tabelle ist keine Zusage, dass jede Heizung einer Marke funktioniert.

## Bewusste Sperren und Abweichungen

- Unbekannte Befehle liefern `Unsupported`, statt wie teilweise in der Vorlage still eine Statusabfrage zu erzeugen.
- Ungültige Pakete überschreiben den letzten Status nicht. Fehler beim Erzeugen eines Befehls hinterlassen keinen sendbaren Rest im Ausgangspuffer.
- AAxx und ABBA benötigen für Temperatur/Stufe einen fehlerfreien Status mit passender Betriebsart. `Command::Level` wird auf den tatsächlichen Drahtbefehl 4 umgesetzt.
- ABBA erzeugt den Heizungs-Toggle nur bei eindeutigem Aus-/Standby- oder Heizstatus und nur wenn eine Änderung nötig ist. Während Nachlauf oder Lüften bleibt dieser Toggle gesperrt. Die aktuelle Vorlage liefert keinen hinreichend eindeutigen separaten Lüften-Aus-Befehl.
- ABBA-Lüften ist nur aus fehlerfreiem Aus/Standby codierbar. Das bedeutet noch keine Freigabe in `gatewaySupportsVentilation()`.
- Der CBFF-Moduswechsel verwendet nur tatsächlich bekannte Sollwerte. Die Vorgabewerte 5 beziehungsweise 21 aus der Vorlage werden nicht erfunden. Eine Temperatur-/Stufenänderung über FEAA enthält außerdem Einschalten; der Adapter sperrt solche Änderungen bei ausgeschalteter Heizung.
- CBFF-Lüften ist als Befehl gesperrt: Der Builder der Vorlage bildet Modus 3 derzeit auf Temperaturbetrieb ab. Ein solcher Befehl darf hier nicht als Lüften ausgegeben werden.
- Hcalory-Lüften bleibt gesperrt: Der in der Vorlage genannte Wert `0x08` ist dort unbestätigt. Hcalory-Authentifizierungsantworten gelten nicht als Messwerte.
- MVP2-Abfragen ohne gültige Uhrzeit liefern `NeedClock`; es wird keine erfundene Uhrzeit an das Heizgerät geschrieben.
- CBFF akzeptiert nur einen passenden Statusheader vor/nach Entschlüsselung. Die Vorlage versucht teilweise eine Erkennung allein anhand plausibler Messwerte. Eine abweichende echte Gerätevariante muss anhand eines Mitschnitts ergänzt werden.
- Empfangsprüfsummen sind nicht durchgehend in der Vorlage dokumentiert. Diese Portierung behauptet deshalb keine vollständige Integritätsprüfung der Telemetrie. Befehlsprüfsummen werden nach den jeweiligen Formaten erzeugt.

## API und Anbindung an Web/MQTT

`parse(profile, data, length, state, mac)` verarbeitet vollständige Statuspakete. `encode(profile, command, argument, context, packet)` erzeugt ausschließlich Bytes. Das Modul hat keine Funk-, Speicher- oder Netzwerk-Nebenwirkungen und verwendet für Pakete feste 64-Byte-Puffer.

`Context::state` muss aus derselben Heizung und Verbindung stammen und aktuell sein. Der Aufrufer muss Alter, Verbindungsabbruch und ausstehende Befehle verwalten. Der Codec kann das Alter allein nicht beurteilen. Insbesondere dürfen Toggles nicht automatisch wiederholt werden.

Der Adapter normalisiert native Fahrenheit-Messwerte nach Celsius und berücksichtigt den bereits umgerechneten AA66-Zielwert. Hcalory-Sollwerte werden in die gemeldete Geräteeinheit zurückgerechnet. Wegen uneinheitlicher Vorlagen bleiben Fahrenheit-Sollwertbefehle anderer Familien gesperrt. Es wird kein zusätzlicher Kalibrierungswert erfunden.

GATT-Auswahl erfolgt profilabhängig über FFE0, FFF0 oder BD39. Der MTU-Wunsch beträgt 128 Bytes; kurze Benachrichtigungen werden begrenzt zusammengesetzt. MVP2 benötigt eine positive PIN-Antwort, CBFF reagiert auf AA77 mit einem PIN-Handshake. Nach spätestens 20 Sekunden ohne gültigen Erststatus wird getrennt. Nach 15 Sekunden ohne neue Telemetrie wird die Steuerung gesperrt.

Der Hintergrundprozess besitzt alle BLE-Objekte. Nachrichten und unveränderliche Statuskopien gehen über begrenzte FreeRTOS-Queues zur Hauptschleife. Web und MQTT verwenden `gatewayHeaterCommand()`. Eine passende, erst nach dem Schreibvorgang empfangene Antwort bestätigt den Befehl. Nach zehn Sekunden endet das Warten ohne Wiederholung. Demo-Werte gelangen nicht in den Treiber.

Web-API (POST mit Sitzungstoken): `/api/heater/config` (profile 0–7, pin, keepPin; alternativ reset=true), `/api/heater/connect` (localEpoch als lokale Browserzeit), `/api/heater/disconnect`, `/api/heater/command` (command/value wie MQTT). Status unter `/api/status` → `heater`. PINs werden nicht zurückgegeben.


## Prüfung

Die Tests vergleichen das C++-Modul mit fest gespeicherten, aus der oben genannten Python-Version erzeugten Referenzfällen. Enthalten sind alle acht Profile, native/verschlüsselte Statuspakete, negative Temperaturen, Fehler-/Nachlaufzustände, PINs, Prüfsummen, MVP2-Uhrzeit und die zusätzlichen Ablehnungsregeln.

```sh
g++ -std=c++11 -Wall -Wextra -Werror -Isrc src/heater_protocol.cpp tests/protocol_runner.cpp -o /tmp/protocol_runner
python tests/protocol_test.py /tmp/protocol_runner
pio run
```

Die GitHub-CI enthält dieselbe native Prüfung. `scripts/generate_protocol_vectors.py` erzeugt die Referenzfälle erneut aus einem lokalen Checkout der genannten Vorlage. Normale Tests benötigen keinen Download und keine laufende Home-Assistant-Installation.

Lokal geprüft am 14. September 2026: **832 Referenz- und Ablehnungsfälle bestanden**, ESP32-S3-Build erfolgreich, Protokollmodul mit GCC `-Wall -Wextra -Werror` ohne Warnungen übersetzt. Die bestehenden JavaScript-/SHA-256-/Update-Prüfungen bestehen ebenfalls. Die CI-Datei wurde entsprechend erweitert; ein neuer GitHub-CI-Lauf wurde für diese lokalen Änderungen noch nicht ausgeführt.

Diese Tests prüfen die Übereinstimmung der Datenformate und unsere Sperren. Sie ersetzen keinen Versuch mit dem konkreten Heizungssteuergerät.
