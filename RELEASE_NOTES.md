# v0.5.0 – Bluetooth-Heizungstreiber, Web und MQTT

Acht auswählbare Protokollprofile: AA55/AA66 mit und ohne Verschlüsselung, ABBA, CBFF/FEAA sowie Hcalory MVP1/MVP2. Profil und Geräte-PIN sind über die Weboberfläche konfigurierbar. Bluetooth läuft im Hintergrund; echte Messwerte und Steuerbefehle verwenden denselben Treiber für Web und MQTT.

Befehlsannahme, Versand und Rücklesebestätigung sind getrennt. Veraltete Antworten, Nachlauf, Fehlerzustände und unerwünschtes Einschalten durch CBFF-Einstellbefehle werden berücksichtigt. Keine automatische Wiederholung von Schaltbefehlen oder Wiederverbindung nach Neustart/Funkverlust. Vor Updates und Gerätesuchen zuerst Bluetooth trennen.

ABBA-Lüften ist aus Aus/Standby vorgesehen. Beenden derzeit am Originalbedienteil. Andere Lüftungsbefehle bleiben gesperrt. Die echte Heizung und der Import in die spätere Home-Assistant-Installation sind noch nicht physisch geprüft; dies ist ein Stand für den ersten beaufsichtigten Gerätetest.

Die 832 Protokollfälle, zusätzlichen Steuerungs-/Oberflächentests und der ESP32-Build bestehen. Installation über OTA und Tests des Gateways mit einem lokalen MQTT-Broker wurden durchgeführt. Einzelheiten in TESTERGEBNIS.md und PROTOCOLS.md. Lizenzhinweise zur MIT-Vorlage in THIRD_PARTY_NOTICES.md.

Für Web-Updates firmware-app-at-0x10000.bin verwenden. WLAN und MQTT bleiben erhalten.

---

## v0.4.1 – Firmware-Updates im Webinterface

Upload-Sitzungen werden auch nach hartem Verbindungsabbruch sofort vollständig freigegeben. Eine neue Übertragung kann direkt beginnen.

- Neue Update-Seite mit gezielter GitHub-Release-Suche.
- Versionsvergleich, Versionshinweise und Installation nach Bestätigung.
- Lokaler Gateway-App-Dateiupload ohne Internet.
- Blockweise OTA-Übertragung, Fortschritt und Neustartkontrolle.
- Prüfung von App-Ziel, Projektkennung, Größe, SHA-256 und ESP-Image.
- WLAN, MQTT und Geräteauswahl bleiben bei App-Updates erhalten.
- Vollständige deutsche Funktions- und Entwicklungsdokumentation.

MQTT, Home-Assistant-Discovery und Lüftungsmodus-Vorbereitung bleiben enthalten. Echte Bluetooth-Heizungssteuerung ist weiterhin nicht implementiert. Demo-Werte werden nicht als echte Messwerte gesendet.

Für Web-Updates firmware-app-at-0x10000.bin verwenden. Die Update-Funktion muss beim ersten Mal per USB installiert werden.
