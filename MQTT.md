# MQTT-Schnittstelle v0.4.0

## Verbindung

MQTT 3.1.1 über TCP, Standard-Port 1883, optional Benutzername/Passwort. Der Broker muss diesen Protokollmodus unterstützen; moderne Mosquitto-Broker können HA und das Gateway mit ihren jeweiligen Protokollversionen bedienen. TLS, WebSockets, ein abweichendes Discovery-Präfix und ein benutzerdefiniertes Basis-Topic sind in dieser Version nicht implementiert.

Sendeintervall: 10–300 Sekunden, Standard 30. Keepalive: 20 Sekunden. Automatische Wiederverbindung im Hintergrund; Wiederholungsabstand etwa zehn Sekunden. Status und Discovery verwenden QoS 1. Diagnose zeigt gesendete Nachrichten und PUBACK-Bestätigungen getrennt.

Das Passwort wird nur gespeichert und nicht an den Browser zurückgesendet. „Passwort beibehalten“ ist nur bei unverändertem Broker, Port und Benutzer erlaubt. Die NVS-Speicherung ist auf diesem Board nicht zusätzlich verschlüsselt.

## Eindeutige Topics

Basis: `dieselheater/<MAC ohne Doppelpunkte>`; Client-ID: `dieselheater_<MAC ohne Doppelpunkte>`. Der tatsächliche Wert steht im Webinterface. IDs bleiben bei Neustarts und Änderungen des Anzeigenamens gleich.

| Suffix | Inhalt | Retain |
|---|---|---|
| `/availability` | `online` / `offline`; Last Will bei unerwartetem Abbruch | ja |
| `/state` | Gateway-JSON: version, uptime, wifi_signal, free_heap, heater_connected, driver_ready | ja |
| `/heater/availability` | derzeit immer `offline` | ja |
| `/heater/state` | Live-Heizungswerte; `{}` ohne gültigen Status | ja |
| `/result` | JSON mit command, ok, error, uptime | nein |

Bei regulärem Neustart wird `offline` gesendet. Beim Konfigurationswechsel wartet die Firmware begrenzt auf ausstehende Bestätigungen, bevor sie die Verbindung wechselt. Ein nicht erreichbarer Broker kann eine saubere Entfernung alter HA-Konfigurationen verhindern; dann weist das Ereignisprotokoll darauf hin.

## Befehle

Befehle an `<Basis>/command/<Name>` veröffentlichen. Keine retained Befehle verwenden. Beim Abonnieren zugestellte alte retained Befehle werden verworfen; als Duplikat markierte Zustellungen werden nicht erneut ausgeführt. MQTT 3.1.1 kennzeichnet eine live weitergeleitete Nachricht nicht zuverlässig als ursprünglich mit Retain veröffentlicht. Deshalb muss der Sender Retain bei Befehlen ausschalten.

| Name | Payload | Aktuelle Wirkung |
|---|---|---|
| `refresh` | `PRESS` | Gateway-Status senden |
| `restart` | `PRESS` | Gateway neu starten; bei laufender Funksuche abgewiesen |
| `scan` | `PRESS` | Bluetooth-Suche starten; bei laufender Funksuche abgewiesen |
| `power` | `ON` / `OFF` | über gemeinsamen Bluetooth-Treiber; ohne Status abgewiesen |
| `mode` | `heat` / `off` | über gemeinsamen Bluetooth-Treiber; ohne Status abgewiesen |
| `temperature` | ganze Zahl 8–35 | über gemeinsamen Bluetooth-Treiber; ohne Status abgewiesen |
| `level` | ganze Zahl 1–10 | über gemeinsamen Bluetooth-Treiber; ohne Status abgewiesen |
| `operating_mode` | `temperature` / `level` | über gemeinsamen Bluetooth-Treiber; ohne Status abgewiesen |

Antwortbeispiel auf `/result`:

```json
{"command":"power","ok":false,"error":"heater_state_unavailable","uptime":42}
```

Weitere Rückmeldungen: `invalid_payload`, `unknown_command`, `retained_command_rejected`, `duplicate_command_ignored`, `radio_busy`. Nachrichten während eines Konfigurationswechsels können verworfen werden; danach erneut senden. Überlange oder nicht vollständig empfangene Nachrichten werden nicht ausgeführt und erhöhen den Verworfen-Zähler. Es gibt keine automatische Erfolgsmeldung für eine physisch nicht bestätigte Heizungsaktion.

## Home-Assistant-Discovery

Retained Konfigurationen unter `homeassistant/<Komponente>/<Client-ID>/<Entität>/config`. Eine gemeinsame Geräte-ID gruppiert die zwölf Einträge:

1. WLAN-Signal
2. Laufzeit
3. Freier Speicher
4. Heizungsverbindung
5. Status aktualisieren (Taste)
6. Gateway neu starten (Taste)
7. Heizung (Thermostat)
8. Leistungsstufe
9. Betriebsart
10. Raumtemperatur
11. Versorgungsspannung
12. Gehäusetemperatur

Gateway-Diagnose und Tasten sind verfügbar, sobald das Gateway verbunden ist. Heizungsregler und Heizungsmesswerte verlangen zusätzlich `heater/availability = online` und bleiben ohne aktuelle Heizungsantwort nicht verfügbar. Regler arbeiten nicht optimistisch. Der gemeinsame Bedienbereich beträgt 8–35 °C und Stufe 1–10.

Discovery erfolgt nach Verbindung, auf `homeassistant/status = online`, nach Änderung des Anzeigenamens und über die Taste „Discovery senden“. Eine kurze zufällige Verzögerung und versetzte Veröffentlichungen verteilen die Nachrichten. Nach Discovery werden Zustandsmeldungen erneuert.

Discovery ausschalten entfernt bei erreichbarem Broker die zwölf retained Konfigurationen und wartet auf PUBACK. Nur MQTT ausschalten lässt die Geräte-Konfiguration bestehen und meldet das Gateway offline. Bei einem Brokerwechsel wird das Entfernen auf dem alten Broker versucht; ist er offline, kann manuelles Aufräumen nötig sein.

MQTT Discovery richtet Entitäten ein, aber weder den Broker noch die HA-MQTT-Integration selbst. Der tatsächliche Import in die spätere HA-Installation wurde noch nicht geprüft.

## Erweiterung um die echte Heizung

`gatewayHeaterCommand()` verwendet denselben Treiber wie die Weboberfläche. `/result` meldet die Annahme (`status: accepted`) oder Ablehnung. Dies bestätigt noch keine physische Änderung. Das zusätzliche, nicht retained Topic `/heater/result` meldet `sent_awaiting_confirmation`, `confirmed`, `not_confirmed_no_retry` oder einen Treiberfehler. Der letzte Befehlsstatus steht außerdem in `/heater/state` als `command_result`. Heizungswerte werden nur aus empfangener Telemetrie veröffentlicht; bei Verlust der Verfügbarkeit wird retained `{}` gesendet, um alte Werte zu entfernen.

## Lüften

`mode = fan_only` und `operating_mode = ventilation` starten Lüften bei ausgewähltem ABBA-Profil ausschließlich aus Aus/Standby. Discovery ergänzt dafür die bestehenden IDs um diese Optionen. **Das Beenden erfolgt derzeit am Originalbedienteil.** Die Vorlage enthält keinen eindeutig bestätigten separaten Lüften-Stoppbefehl. Während Lüften werden Änderungen gesperrt. Für andere Profile bleiben Lüftungsbefehle gesperrt.

## Profile und Verbindungen

Profil, PIN und Verbindung werden in der Weboberfläche eingestellt. Nach Neustart oder Funkverlust manuell neu verbinden. MQTT verbindet sich selbst zum Broker zurück, stellt aber keine unterbrochene Heizungssteuerung wieder her und spielt keine Befehle nach. Siehe [PROTOCOLS.md](PROTOCOLS.md) für profilabhängige Einschränkungen.
