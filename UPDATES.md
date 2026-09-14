# Updateformat und API

Das veröffentlichte Git-Tag `vX.Y.Z` enthält im Hauptverzeichnis `firmware-app-at-0x10000.bin` und `update-manifest.json`.

Manifestfelder: schema=1, project=diesel-heater-gateway, target=xiao_esp32s3, firmwareId=DIESELHEATER_GATEWAY:XIAO_ESP32S3:OTA1, version=X.Y.Z, file=firmware-app-at-0x10000.bin, size als Bytezahl, sha256 als 64 kleine Hexzeichen.

Suche: `https://api.github.com/repos/<owner>/<repo>/releases/latest`. Manifest und App: `https://raw.githubusercontent.com/<owner>/<repo>/<tag>/`. Beliebige Download-URLs aus dem Manifest werden nicht ausgeführt. Nur öffentliche Projekte; kein GitHub-Token nötig.

## Lokale API

Alle Änderungen benötigen `X-Gateway-Token` aus `/api/status` (CSRF-Schutz, keine Anmeldung).

| Route | Methode | Inhalt |
| --- | --- | --- |
| `/api/update` | GET | Repository, Ziel, Größe, Status, Fehler |
| `/api/update/repository` | POST | repository als owner/repo oder GitHub-URL |
| `/api/update/start` | POST | size und sha256; Antwort session und chunkSize |
| `/api/update/chunk` | POST | Eine multipart-Datei, maximal 16384 Bytes; X-Update-Id und X-Update-Offset |
| `/api/update/finish` | POST | X-Update-Id; Prüfung, Aktivierung, Neustart |
| `/api/update/cancel` | POST | X-Update-Id; Sitzung abbrechen |

45 Sekunden ohne gültigen Datenblock führen zum Abbruch. Laufende App, NVS und Partitionstabelle werden nicht überschrieben. Falsche Größe, Kopf, Projektkennung, Prüfsumme oder ESP-Image verhindern die Aktivierung. Während der Sitzung sind Konfigurationsänderungen, Funk-Suchen, Heizungsbefehle und MQTT-Neustarts gesperrt; MQTT-Status kann weiterlaufen.

## Partitionen und Wiederherstellung

Die bestehende default_8MB-Partitionierung bleibt erhalten: app0 ab 0x10000, app1 ab 0x340000, je 0x330000 Bytes; NVS ab 0x9000. Update-Funktion einmal per USB installieren, danach App-Updates über WLAN.

Automatisches Boot-Rollback ist nicht aktiviert. Bei nicht erreichbarer Firmware eine funktionierende Projektversion per PlatformIO/USB übertragen; kein vollständiges Flash-Löschen nötig. Gesamtimages können NVS-Zwischenbereiche überschreiben und sind kein Format für diese Seite.

Prüfsumme und Projektkennung sind keine Authentizitätssignatur. Nur vertrauenswürdige Projektdateien verwenden. Dateiupload erlaubt dieselbe oder ältere kompatible Versionen; die GitHub-Schaltfläche bietet ausschließlich neuere an.
