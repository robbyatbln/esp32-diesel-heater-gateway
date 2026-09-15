# Prüfstand v0.5.0 – 15. September 2026

## Erfolgreich geprüft

- 832 Vergleichs- und Ablehnungsfälle für die acht Protokollprofile gegen fest gespeicherte Referenzdaten der Python-Vorlage.
- Native Steuerungsprüfung: Fehler/Nachlauf, Lüftungsübergänge, keine CBFF-Einstellbefehle mit unbeabsichtigtem Einschalten, alte Antworten, verspätete Antworten und Überlauf des Millisekundenzählers.
- Oberflächentests direkt aus dem aktuellen JavaScript: echte Werte, fehlende Werte, Offline-Zustand, ausstehende Befehle, Nachlauf und Lüften. JavaScript-Syntax und bestehende Update-/SHA-256-Prüfungen.
- ESP32-S3-Build erfolgreich; App ca. 1,24 MB, statischer RAM ca. 56 KB.
- Tatsächliche OTA-Installation von v0.4.1 auf v0.5.0, Neustart und Versionsrückmeldung. WLAN, MQTT und Geräteauswahl erhalten. Kein automatischer Verbindungsaufbau zur Heizung.
- Auf dem ESP: alle acht Profile konfigurierbar; PIN wird nicht zurückgeliefert; ungültige PINs/Profile und Aufrufe ohne Sitzungstoken abgewiesen. Befehle und Verbindungsaufbau ohne ausgewählte Heizung abgewiesen. Ausgangszustand anschließend wiederhergestellt.
- Lokaler MQTT-Testbroker mit Anmeldung: zwölf Discovery-Einträge, stabile IDs, Brokerbestätigungen, HA-Birth-Neuanmeldung, Entfernen/Wiederherstellen von Discovery, Ablehnung alter retained Befehle und ungültiger Werte.
- MQTT-Neustart mit offline/online und erhaltenen Zugangsdaten. Gateway bei nicht erreichbarem Broker weiterhin bedienbar; im Test maximal 0,28 s für die Statusantwort. Testzugang entfernt und ursprüngliche MQTT-Einstellungen wiederhergestellt.
- Ohne Heizung bleiben Entitäten unavailable; keine Demo-Messwerte werden veröffentlicht. Retained Heizungsstatus wird mit `{}` geleert.
- Installierte Weboberfläche im Browser geprüft: v0.5.0, Bluetooth-Seite und Update-Seite erreichbar.

## Noch nicht am echten Gerät geprüft

Die Heizung ist nicht vor Ort. Deshalb wurden GATT-Verbindung zur Heizung, Geräte-PIN-Handshake, reale Statuspakete, physisches Ein/Aus, Sollwertänderungen und Lüften noch nicht im Zusammenspiel getestet. Auch der tatsächliche Import in die spätere Home-Assistant-Installation steht aus. Der Stand eignet sich für den ersten beaufsichtigten Gerätetest, nicht als Zusage universeller Heizungs-Kompatibilität.

ABBA-Lüften muss derzeit am Originalbedienteil beendet werden. Andere Lüftungsbefehle bleiben gesperrt. Keine automatischen Schaltwiederholungen. Details: PROTOCOLS.md.

---

## Frühere Versionen

# Prüfung v0.4.1 · 14.09.2026

Auf XIAO ESP32-S3 erfolgreich gebaut, per USB installiert und anschließend tatsächlich per OTA über WLAN übertragen und neu gestartet.

- JavaScript-Syntax, SHA-256 gegen Node.js-Kryptobibliothek, Versionsvergleich und Manifest-Abweisung geprüft.
- Invalid sizes, checksums, repository URLs and unauthenticated starts rejected
- Concurrent restart and scan blocked; foreign session, wrong offset and incomplete upload rejected
- Foreign/merged file header rejected without reboot
- Full upload with wrong SHA-256 rejected; previous firmware stays active
- Cancellation frees update session
- Actual OTA install and reboot successful; WiFi, MQTT, label, selected device and update repository preserved

MQTT wurde in der Vorversion mit authentifiziertem Testbroker geprüft: zwölf Discovery-Einträge, stabile IDs, HA-Birth, Rückmeldungen, Neustart, Discovery-Löschung/Wiederherstellung. Lüftungsbefehle wurden ohne Geräteunterstützung korrekt abgewiesen.

Reale Heizung und tatsächlicher Home-Assistant-Import sind noch nicht geprüft. GitHub- und Browser-Endprüfung erfolgreich: Release v0.4.0 gefunden; Dateiupdate im Browser inklusive Neustart abgeschlossen; danach v0.4.1 über die GitHub-Schaltfläche geladen, installiert und als laufende Version bestätigt. Harter Socket-Abbruch erlaubt anschließend sofort eine neue Update-Sitzung. GitHub-CI und Release-Workflow erfolgreich.
