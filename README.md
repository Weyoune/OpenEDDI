# OpenEDDI

**[OpenEDDI 0.1.0 herunterladen](https://github.com/Weyoune/OpenEDDI/releases/tag/v0.1.0)** · [Screenshots und Prüfung](docs/qa/0.1.0/README.md)

Ein eigenständiger Editor für die originalen DOS-Grunddaten von **Bundesliga Manager Hattrick** und **Football Limited**. Oberfläche auf SDL2/RmlUI mit drei Themes, Deutsch/Englisch, Rückgängig/Wiederholen, Navigation und lokalem MCP-Server.

## Originaldaten bearbeiten

Eine eigene Spielinstallation öffnen. Unterstützte Profile: deutsches DOS-BMH mit dem bekannten LE-/V2.24-Layout, Football Limited mit dem bekannten X-32-/V2.20-Layout und formatkompatible Diamond 3.6-Daten. Andere Formate werden ausdrücklich abgewiesen; Amiga- und Windows-Ausgaben sind nicht enthalten.

Spieler, Kader, Vereinsnamen, Trikotsätze, Stadionnamen und Schiedsrichter lassen sich innerhalb der Originalgrenzen bearbeiten. Abgeleitete oder nicht gespeicherte Werte bleiben schreibgeschützt. Die Anzahl von Vereinen/Spielern und die Wettbewerbsstruktur bleiben fest. Es werden weder OBW-Pakete noch laufende FBM-Spielstände gespeichert.

**Speichern schreibt in die geöffnete Installation.** Zuvor werden alle betroffenen Originaldateien vollständig und prüfsummengesichert unter `.openeddi/backups/` gesichert. Die Sicherungsübersicht bietet Wiederherstellung. Externe Änderungen werden vor dem Schreiben erkannt; ein Journal ermöglicht die Rücknahme unterbrochener Schreibvorgänge. Ohne Änderungen werden keine Spieldateien geschrieben.

## Grafiken

PNG, JPEG, WebP und SVG können vorhandene Grafikplätze ersetzen. Zuschneiden, Drehen, Spiegeln, Einpassen/Ausfüllen und optionales Dithering sind verfügbar. Die zweite Bildvorschau zeigt das tatsächliche Ergebnis mit Originalauflösung und Originalpalette.

Unterstützt werden die vorhandenen VGA-Grafiken einschließlich Wappen-Zellen und Porträt-Bauteilen. Benachbarte Zellen, nicht bearbeitete Pixel und Palette bleiben erhalten. Gemeinsam verwendete Grafiken benötigen eine ausdrückliche Bestätigung. Das Original besitzt keine beliebig zuweisbaren individuellen Spielerfotos.

**Grenze von 0.1:** Für Grafiken innerhalb der EXE sind bisher keine beschreibbaren Bildbereiche nachgewiesen. Ein getesteter, begrenzter Slot-Writer ist vorbereitet; es werden keine EXE-Adressen geraten oder Programmteile geändert. Nicht bestätigte Ressourcen bleiben schreibgeschützt.

## Start und MCP

ZIP vollständig entpacken, `OpenEDDI.exe`, `OpenEDDI.app` oder `OpenEDDI` starten. Eine Originalinstallation wird nicht mitgeliefert. `--demo` zeigt ausschließlich fiktive Testdaten; diese lassen sich nicht als Originalinstallation speichern.

MCP ist in den Optionen aktivierbar und standardmäßig auf `127.0.0.1:8766` mit Zugangstoken eingeschränkt. Die Verbindungskonfiguration lässt sich kopieren. Einstellungen und Token sind von OpenBMHEditor getrennt. `--stdio-mcp` bietet dieselbe Befehlsgrenze für lokale Clients. `--headless --open <Spielordner> --commands <JSON-Datei>` führt dieselben Aktionen ohne Oberfläche aus.

## Kompilieren

Git, Python 3, CMake ab 3.24 und ein C++20-Compiler werden benötigt. Abhängigkeiten werden mit festgelegten Versionen geladen.

- **Windows x64:** Visual Studio 2022 C++ Build Tools und Ninja installieren; in der x64 Native Tools-Konsole konfigurieren. WARP unterstützt Windows Server/Remote-Desktop ohne OpenGL-Treiber.
- **macOS ARM64:** Xcode Command Line Tools (`xcode-select --install`), CMake und Ninja verwenden. Mit `-DCMAKE_OSX_ARCHITECTURES=arm64` konfigurieren.
- **Linux:** Compiler, Ninja, pkg-config, GTK3- und X11/OpenGL-Entwicklungspakete installieren (siehe CI-Paketliste).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target OpenEDDI legacy_tests data_tests graphics_tests transaction_failure_tests mcp_tests --parallel 3
ctest --test-dir build -R '^openeddi_' --output-on-failure
```

Der korrespondierende Quellcode-Download enthält `deps/`; damit lassen sich die Abhängigkeiten ohne erneuten Download verwenden. Das Linux-Testpaket wird unter Ubuntu 24.04 gebaut. Das macOS-Paket ist ad-hoc signiert, nicht notarisiert.

## Herkunft und Lizenz

GPL-3.0-only. Die Oberfläche und gemeinsame Leser stammen aus [OpenBMH](https://github.com/Weyoune/openbmh), festgelegt auf `b879d5be008739f69afef562fb4bce56f1bdc778`. OpenEDDI besitzt eigene Legacy-Schreibadapter und keine eigene Spiel-Engine. Die dokumentierte Abhängigkeitsanpassung verwendet die ursprünglichen Schiedsrichter-/Stadion-Namensverweise auch bei angehängten Namen und ermöglicht die Einbindung als Unterprojekt. Fremdlizenzen bleiben in den Paketen erhalten.

Originalprogramme, Daten, Grafiken, Backups und daraus gewonnene private QA-Bilder werden nicht in diesem Repository verteilt. Öffentliche Tests verwenden synthetische Daten.
