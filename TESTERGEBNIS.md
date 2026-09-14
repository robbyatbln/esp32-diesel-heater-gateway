# Prüfung v0.4.0 · 14.09.2026

Auf XIAO ESP32-S3 erfolgreich gebaut, per USB installiert und anschließend tatsächlich per OTA über WLAN übertragen und neu gestartet.

- JavaScript-Syntax, SHA-256 gegen Node.js-Kryptobibliothek, Versionsvergleich und Manifest-Abweisung geprüft.
- Invalid sizes, checksums, repository URLs and unauthenticated starts rejected
- Concurrent restart and scan blocked; foreign session, wrong offset and incomplete upload rejected
- Foreign/merged file header rejected without reboot
- Full upload with wrong SHA-256 rejected; previous firmware stays active
- Cancellation frees update session
- Actual OTA install and reboot successful; WiFi, MQTT, label, selected device and update repository preserved

MQTT wurde in der Vorversion mit authentifiziertem Testbroker geprüft: zwölf Discovery-Einträge, stabile IDs, HA-Birth, Rückmeldungen, Neustart, Discovery-Löschung/Wiederherstellung. Lüftungsbefehle wurden ohne Geräteunterstützung korrekt abgewiesen.

Reale Heizung und tatsächlicher Home-Assistant-Import sind noch nicht geprüft. GitHub- und Browser-Endprüfung folgt im Veröffentlichungsablauf.
