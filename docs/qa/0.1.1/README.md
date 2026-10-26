# OpenEDDI 0.1.1 – Hotfixprüfung

[Release](https://github.com/Weyoune/OpenEDDI/releases/tag/v0.1.1) · [CI 40396780545](https://github.com/Weyoune/OpenEDDI/actions/runs/40396780545)

Mac/Retina: RmlUI-Kontext, Renderer und Mauskoordinaten verwenden konsistent die tatsächlichen Zeichenflächen-Pixel. Das SDL-Größenereignis darf diese nicht mit logischen Fenstermaßen überschreiben. Vor jedem Frame werden auch spätere Backing-Scale-Änderungen erkannt. Der Ladehinweis wird nach einem Hintergrundauftrag entfernt; geteilte Bilder erhalten eine sichtbare Bestätigung im Dialogfuß. Regelwerk ist entfernt.

Je **7/7 Testsuiten** nativ unter macOS ARM64, Windows x64 und Linux x64. Zusätzlicher nativer UI-Test: logisches Größenereignis mit halben Maßen, tatsächlicher asynchroner Ladevorgang, vollständige Bildabdeckung und echter RmlUI-Klick auf die gemeinsame Bildbestätigung. Reine Koordinatentests prüfen Faktoren 1, 1.25, 1.5, 2 und 3. Die virtuelle CI-Umgebung ersetzt keine persönliche Abnahme auf jedem Retina-Gerät.

[Deutsch](native-de.png) · [English](native-en.png). Diese Bilder zeigen ausschließlich synthetische Daten.

Die entpackten Pakete starten nativ aus einem unabhängigen Verzeichnis ohne Datenpfad-Override. Architektur, ZIP-Prüfsummen und Hash der tatsächlich signierten macOS-Binary sind geprüft. Plattformordner enthalten die originalen CI-Testberichte und Screenshots. Alle acht veröffentlichten Dateien entsprechen den verifizierten CI-Artefakten.

Diamond 3.7 wurde zusätzlich privat und ohne Schreibvorgänge geöffnet. Die bestehende EXE-Zuordnung führt 48 ausländische Hauptligavereine zu ihren eigenen W-Blättern; 189 weitere Vereine behalten den Original-Fallback. PNG und Pixelprojektion sind konsistent; beide Schreibvorbereitungen bleiben bei unveränderten Daten leer. Originalquellen wurden nicht geändert. Weitere EXE-Grafikerweiterungen sind nicht enthalten.
