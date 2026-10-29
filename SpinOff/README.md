# SpinOff

Anwendungen, die aus der CatFinder-Infrastruktur (Sensoren, Bus, Welt-Koordinaten, VPS) entstehen, aber einem anderen Zweck dienen als der Katzenabwehr.

Jedes Spin-off hat einen eigenen Ordner mit `Dokumentation/` und, sobald vorhanden, eigenen Programmen (z. B. `Firmware/`). Erweiterungen an den normalen CatFinder-Programmen (gemeinsamer Header `xComDef6_3.h`, Radar-Firmware, VPS-Dashboard) bleiben in deren Ordnern. Das Konzept des Spin-offs verweist darauf.

| Spin-off | Zweck | Stand |
|---|---|---|
| [BurglerProtect](BurglerProtect/Dokumentation/Konzept.md) | Besucher auf der Veranda schaltet das Wohnzimmerlicht über Philips Hue ein (Anwesenheitssimulation gegen Dämmerungseinbrüche) | Schritt 1 (Meldungen + VPS-Tab) 2026-10-27, Schritt 2 (Steuergerät Besucherlicht, M5 AtomS3, [Firmware](BurglerProtect/Firmware/Besucherlicht6_3_0/)) 2026-10-29 |
