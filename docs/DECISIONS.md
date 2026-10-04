# MidiMaid – Entscheidungen

Format: `D-<Nr>` für getroffene, `O-<Nr>` für offene Entscheidungen. Getroffene Entscheidungen werden nur
durch eine neue Entscheidung ersetzt, nie stillschweigend geändert.

## Getroffen

- **D-1** Das Plugin gibt nur MIDI aus, es gibt keinen internen Synth.
- **D-2** Generierung hybrid: KI für Intent und Noten, Algorithmus und Constraint-Schicht sichern die Qualität.
- **D-3** KI-Anbindung: eigener API-Key (Anthropic, OpenAI, OpenAI-kompatibel), Ollama lokal, Offline-Modus.
- **D-4** Steuerung über Textprompt und MIDI-Vorlage. Melodie und Bass sind harmonisch gekoppelt.
- **D-5** Nutzung live (Transport-Sync) und als Clip (Drag & Drop).
- **D-6** Stile v1: Peak Time/Driving, Melodic Techno, Hard/Industrial.
- **D-7** Plugin-Funktionen: Noten bearbeiten, Variationen per Knopfdruck, Bibliothek.
- **D-8** Erst privat, später Verkauf. Lizenzen und IDs sind von Beginn an verkaufsfähig zu planen.
- **D-9** Umsetzung durch Claude Code. Entwickler kann C++ und nutzt CLion.
- **D-10** JUCE 8 + CMake. Zwei Targets: Instrument (VST3/AU) für Live, MIDI-FX (AU) für Logic.
- **D-11** Logic: zwei Instanzen (Bass, Melodie), automatisch gekoppelt. *(Ersetzt durch D-51.)*
- **D-12** Pattern-Wechsel im Live-Modus: Zeitpunkt einstellbar, Standard „nächster Takt“ (SPEC §3.4).
- **D-13** Patterns bis 64 Takte, aufgebaut aus Phrasen mit Formplan (SPEC §3.7).
- **D-14** Slides als Noten-Überlappung (Legato, 303-Style), kein Pitchbend (SPEC §4.2).
- **D-15** Formate: nur VST3 und AU, auch unter Linux nur VST3. *(Ersetzt durch D-81.)*
- **D-16** Oberfläche Deutsch und Englisch, umschaltbar. KI-Prompts intern Englisch (SPEC §8.2).
- **D-17** Swing und Groove-Vorlagen pro Stimme, nicht-destruktiv angewendet (SPEC §3.9).
- **D-18** Chromatik-Anteil als Regler 0–100 % mit Standardwerten je Stil (SPEC §3.8).
- **D-19** macOS ab Version 15, Universal Binary.
- **D-20** Ableton auf dem Mac: VST3 als Standard-Workflow, AU wird mitgebaut.
- **D-21** Name „MidiMaid“, Hersteller „Klirrwerk“. Codes und Bundle-IDs siehe SPEC §2.2 (fixiert).
- **D-22** Domain `klirrwerk.com` gehört dem Entwickler, Bundle-IDs bleiben wie in SPEC §2.2.
- **D-23** Pro Stil drei Bass-Archetypen. Auswahl automatisch, manuell übersteuerbar (STYLES.md).
- **D-24** Bass-Tonbewegung stilabhängig: Peak Time Grundton plus Quinte/Oktave, Melodic harmonisch, Hard Grundton.
- **D-25** Melodie-Archetypen: alle vorgeschlagenen Typen pro Stil. Melodie darf mehrstimmig sein.
- **D-26** Polymeter-Phasenverhalten einstellbar, frei laufend wird aus der Song-Position berechnet.
- **D-27** Harmoniewechsel und Akkordfarben stilabhängig gewichtet.
- **D-28** Mehrere Kick-Raster zur Auswahl. *(Kick-MIDI-Import ergänzt durch D-48.)*
- **D-29** Stilprofile v1 sind Startwerte und werden in Phase 1 per Hörtest feinjustiert.
- **D-30** Erster Start im Offline-Modus. KI erst nach Einrichtung durch den Nutzer.
- **D-31** Kreativitäts-Regler in der Hauptoberfläche, wirkt auf KI und Algorithmus (SPEC §7.6).
- **D-32** Token- und Kostenanzeige (geschätzt, Preise editierbar), keine Limits oder Sperren.
- **D-33** Modell: Empfehlung vorausgewählt, frei änderbar, Empfehlungen in `models.json`.
- **D-34** KI nur bei Generieren, Auto-Evolve (KI-Quelle) und Vorlagen-Analyse. Variationen rein algorithmisch. *(Erweitert um Verfeinern durch D-47.)*
- **D-35** KI-Fehler: anzeigen und nachfragen. Während der Wiedergabe nur nicht blockierend (SPEC §7.9).
- **D-36** Fenster mittel (ca. 1000 × 640), frei skalierbar 75–200 %.
- **D-37** Look dunkel, minimalistisch, Ableton-nah.
- **D-38** Piano-Roll: beide Spuren übereinander oder Fokus auf eine, umschaltbar.
- **D-39** Vorhören bei gestopptem Transport mit interner Clock (SPEC §3.11).
- **D-40** Automatisierbar: Kreativität, Energie, Generieren, Variation, Mute pro Stimme, Slot (SPEC §3.13).
- **D-41** 16 Pattern-Slots pro Instanz. Slot-Wechsel folgt der Hub-Gruppe (SPEC §3.10).
- **D-42** Bei Offline-Rendern werden Generieren- und Variations-Trigger ignoriert.
- **D-43** Tastenkürzel an Ableton angelehnt, Leertaste bleibt bei der DAW (SPEC §8.3).
- **D-44** Piano-Roll-Hilfen: Velocity-Spur, Scale-Fold, Phrasen- und Kick-Raster, Slide/Akzent-Zeile.
- **D-45** Drag & Drop-Export mit aktuellem DAW-Tempo, getrennt pro Stimme oder kombiniert in einer Spur.
- **D-46** Review vom 04.10.2026 übernommen (R-1 bis R-21, siehe `docs/REVIEW.md`).
- **D-47** Verfeinern per Prompt in v1, mit Kontext der letzten 5 Anweisungen und eingeschränktem Offline-Modus (SPEC §3.15).
- **D-48** Drum-Referenz (Kick, Hi-Hat) per Capture oder .mid in v1: eigenes Kick-Raster, Groove, Akzente (SPEC §3.14).
- **D-49** Live-Transponieren per MIDI-Eingang in v1, chromatisch/diatonisch, als Parameter automatisierbar (SPEC §3.16).
- **D-50** Vorrang am MIDI-Eingang: Capture vor Transponieren, kein MIDI Thru als Standard (SPEC §3.17).
- **D-51** Hub & Voices statt fester Leit-Instanz: Rollen Hub, Voice, Standalone. Keine Echtzeit-MIDI-Übertragung zwischen Instanzen (SPEC §6.5).
- **D-52** v1 mit 2 Stimmen, Architektur für bis zu 8 offen (Datenmodell, Parameter, Engine).
- **D-53** Empfohlene Aufbauten: Logic mit Hub + Voices als MIDI-FX, Live mit mehrkanaligem Hub. Formatmischung in einer Gruppe wird gewarnt. *(Live-Teil ersetzt durch D-55.)*
- **D-54** Review 2 vom 04.10.2026 übernommen (K-1 bis K-24, siehe `docs/REVIEW.md` Teil 2).
- **D-55** Live-Standard: eine Stimme pro MidiMaid-Spur (Hub + Voices), weil Live beim Spur-Routing Kanäle mischt. In Phase 0 zu bestätigen. *(Mehrkanal-Option entfällt, siehe D-63.)*
- **D-56** Ausgabestufe in fester Reihenfolge: Transposition → Groove → Slide-Überlappung → Kick-Freiraum → Akzent/Ratchets (SPEC §4.2a).
- **D-57** Akkord-Skalen-Prinzip: Töne des klingenden Akkords sind immer erlaubt (SPEC §4.2).
- **D-58** Wiederholungsschutz nur bei Generieren; gespeichert wird der Seed des Gewinner-Kandidaten (SPEC §4.3).
- **D-59** Live-Transponieren: Parameter + Tastatur-Offset additiv, keine Rückschreibung; in Hub-Gruppen frühestens „nächster Schlag“ (SPEC §3.16).
- **D-60** Rolle „Standalone“ heißt „Solo“. Hub-Ausgabemodus: eine Stimme (Standard) oder keine *(„alle mehrkanalig“ entfällt, D-63)*.
- **D-61** Auto-Evolve läuft auf einer nicht-destruktiven Evolve-Ebene; „Behalten“ übernimmt die klingende Stufe in den Slot, die Ebene wird nicht gespeichert (SPEC §6.7).
- **D-62** Live: beide Aufbauten werden unterstützt und getestet. *(Ersetzt durch D-63.)*
- **D-64** Produzenten-Review vom 04.10.2026 übernommen (P-1 bis P-10, siehe `docs/REVIEW.md` Teil 3).
- **D-65** Eigene Stimme importieren: ersetzt die Stimme, gesperrt, unverändert, bestimmt den harmonischen Kontext (SPEC §3.18).
- **D-66** Slot-Parameter gilt beim Start und nach Sprüngen sofort (Start mitten im Arrangement).
- **D-67** Oberfläche in zwei Ebenen (Basis/Experte); alle Kürzel-Aktionen auch als Schaltfläche.
- **D-68** DAW-Vorlagen für Live und Logic werden mitgeliefert (SPEC §8.6).
- **D-69** Modulation (CC-Spur pro Stimme) erst nach v1.
- **D-70** Wahrscheinlichkeit und Bedingungen A:B pro Note in v1, deterministisch per Hash aus Seed, Noten-ID und Durchlauf (SPEC §3.19).
- **D-71** Persönlicher Stil aus Favoriten in v1: Few-Shot-Beispiele für die KI, begrenztes persönliches Profil für den Algorithmus, Regler „Persönlich“, Schnappschuss pro Pattern (SPEC §3.20). *(Erweitert durch D-72.)*
- **D-72** Referenzen pro Stil in v1: eigene MIDI-Dateien in mehreren benannten Sets pro Stil, automatisches Set „Favoriten“, Analyse von Rolle, Tonart, Akkordfolge, Motiven und Rhythmen, Wirkung auf Progressionen, Motive, Rhythmen, Profil und KI-Beispiele, Kopierschutz > 85 % Ähnlichkeit (SPEC §3.20).
- **D-73** Vorrang bei gleichzeitigen Wechseln: Slot > manuelles Ergebnis > Evolve-Stufe; manuelle Aktionen während Evolve wirken auf die klingende Stufe und übernehmen sie (SPEC §6.8). Feature-Bewertung in `docs/FEATURES.md`.
- **D-74** Release-Umfang: v1.0 = Bewertung 1 + 2, v1.1 = Bewertung 3, Backlog = Bewertung 4 (`docs/FEATURES.md`). Damit gelten u. a.: Wahrscheinlichkeit (D-70), Live-Transponieren (D-49), Evolve-Ebene (D-61), Vorhören (D-39), Chord-Memory, mehrere Referenz-Sets und Profil (D-72) ab v1.1; Patterns bis 16 statt 64 Takte (D-13); Offline-Verfeinern (D-47), Capture (D-48), Struktur-Modus, native OpenAI-/Ollama-Provider (D-3), Gruppen A–D, Kick-Grundton im Backlog. Datenfelder und Parameter-IDs für v1.1 werden in v1.0 angelegt.
- **D-75** Bewertung 4 aufgeteilt: **Gestrichen** (Dichte-Korrektur, kombinierter Export, Offline-Verfeinern, Struktur-Modus, MIDI-Vorlage als Konzept, Capture/„Lernen“, Max-for-Live-Variante, Monatskosten, Euklid als Feature) und **Backlog**. Modulation (D-69) ist erster Kandidat für v1.2. Wiederaufnahme gestrichener Punkte nur per neuer Entscheidung mit neuem Argument.
- **D-76** Windows-Testrechner vorhanden: Live unter Windows bleibt Teil von Phase 0 und der Testmatrix.
- **D-77** Sprachen: Code, Kommentare, Commits und PRs Englisch; Dokumentation Deutsch; Kommunikation mit dem Entwickler Deutsch.
- **D-78** Hörtests, Testmatrix und DAW-Vorlagen nutzen nur Live- bzw. Logic-eigene Instrumente; Auswahl pro Rolle in Phase 0 (SPEC §12).
- **D-79** Hörtest-Werkzeug `tools/mmgen` (CLI auf Basis von `core`) für Phase 1a; `.claude/settings.json` mit Freigaben für Build, Test und lokales Git.
- **D-80** KI-Anbindung: nativer Anthropic-Provider (Structured Outputs) plus ein OpenAI-kompatibler Provider mit Voreinstellungen und Fähigkeitsstufe pro Backend (Schema erzwungen / nur JSON / nur Prompt). Antworten werden immer validiert (SPEC §7.2).
- **D-81** Architektur für alle üblichen Formate (VST3, AU, CLAP, LV2, AAX, optional AUv3; VST2 ausgeschlossen) und für Linux. Ausgeliefert wird zuerst VST3, weitere Formate folgen nachträglich. Formatcode nur in `HostCapabilities`, Formatliste per CMake-Variable, formatunabhängiger State und Parameter-IDs, IDs aller Zielformate reserviert, Linux-Kompiliertest in der CI ab v1.0 (SPEC §2.4).
- **D-82** AU kommt in v1.0: Instrument-Variante als VST3 und AU, MIDI-FX-Variante als AU (`aumi`) für Logic. Logic ist von Anfang an unterstützt.
- **D-63** Konzentration auf Hub + Voices: keine Max-for-Live-Variante und keine mehrkanalige Ausgabe in v1. Jede Instanz gibt höchstens eine Stimme aus. MIDI-Kanal pro Stimme bleibt im Datenmodell, damit Mehrkanal später nachrüstbar ist. Setup-Hilfe im Hub für Live und Logic.

## Offen

- **O-14 Code-Kollision:** Vor dem Release prüfen, dass `Klrw`/`Mdmi`/`Mdmf` von keinem anderen
  Hersteller genutzt werden (z. B. per `auval -a` auf mehreren Systemen).
