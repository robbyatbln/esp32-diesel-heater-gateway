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
