# OpenEDDI 0.1.0 – Prüfung

Öffentliche Bilder zeigen ausschließlich die synthetische Demo. Native RmlUI-/Linux-Aufnahmen, keine HTML-Mockups.

| Theme | Deutsch | English |
| --- | --- | --- |
| Midnight Glass | [Bild](midnight-de.png) | [Image](midnight-en.png) |
| Silver Glass | [Bild](silver-de.png) | [Image](silver-en.png) |
| Matchday | [Bild](matchday-de.png) | [Image](matchday-en.png) |

Sechs lokale Testsuiten bestehen: Originaldaten, Grafiken, Transaktionen, Fehler-/Wiederherstellungsfälle, Version und MCP. Der HTTP-Test prüft Zugangstoken, Fremd-Origin-Sperre und den Werkzeugkatalog. Originalauflösung, Palettenkonvertierung, Nachbarzellen und unveränderte Bytes werden synthetisch geprüft.

Private Kopien von BMH, FLTD und Diamond3.6 wurden bearbeitet, gespeichert und erneut geöffnet. Namen, Attribute, Trikotsatz, Schiedsrichter, Stadion und Testlogo blieben korrekt. Ein anschließender unveränderter Speichervorgang schreibt keine Spieldateien.

Der originale DOS-Editor zeigt für BMH und Diamond den geänderten Verein und das Testwappen. Das originale FLTD erreicht seinen Optionsbildschirm; eine visuelle Abnahme der geänderten englischen Daten steht aus. Diese begrenzte Prüfung ersetzt keinen vollständigen Karrieretest.

Originalprogramme, Daten und daraus erstellte Screenshots bleiben außerhalb dieses öffentlichen Repositorys. Nachgewiesene beschreibbare EXE-Grafikbereiche: **0**; sie werden daher nicht freigeschaltet.

Die Pakete werden zusätzlich nativ unter Windows x64 (WARP), macOS ARM64 und Linux x64 geprüft. Die zugehörigen CI-Nachweise und Paketprüfsummen gehören zur Veröffentlichung.

## Veröffentlichte Pakete

[OpenEDDI 0.1.0](https://github.com/Weyoune/OpenEDDI/releases/tag/v0.1.0), Quellstand `fc2f6af7e4c170218bcc9df5ffcf05a99639a2bc`. [CI 40382798406](https://github.com/Weyoune/OpenEDDI/actions/runs/40382798406) besteht nativ auf allen drei Plattformen. Jeweils sechs Testsuiten und Start aus dem entpackten Paket ohne Datenpfad-Override. Windows prüft zusätzlich fehlende dynamische VC-Runtime-Abhängigkeiten; macOS verwendet `Contents/Resources` und eine ad-hoc Signatur.

`package-validation.json` enthält die ZIP-/Binary-Prüfsummen und Architekturprüfung. `published-release.json` bestätigt, dass alle acht hochgeladenen Dateien exakt den geprüften Artefakten entsprechen. Plattformordner enthalten Original-Testberichte und den nativen Paket-Screenshot.

Das korrespondierende Quellcodepaket enthält alle zwölf Abhängigkeitsquellen. Die Offline-Konfiguration wurde separat mit deaktivierten Downloads geprüft; ein vollständiger zusätzlicher Offline-Kompilierlauf wird damit nicht behauptet.
