# MidiMaid – Entscheidungen

Format: `D-<Nr>` für getroffene, `O-<Nr>` für offene Entscheidungen. Getroffene Entscheidungen werden nur
durch eine neue Entscheidung ersetzt, nie stillschweigend geändert. Verweise in *(Klammern)* zeigen, welche
spätere Entscheidung eine frühere ersetzt, einschränkt oder einem anderen Release zuordnet.

**Release-Zuordnung:** Maßgeblich sind D-74, D-75 und `docs/FEATURES.md` (bestätigt durch D-84). Ältere
Entscheidungen beschreiben teils den Zielzustand; ihr Release steht im Verweis.

## Getroffen

- **D-1** Das Plugin gibt nur MIDI aus, es gibt keinen internen Synth.
- **D-2** Generierung hybrid: KI für Intent und Noten, Algorithmus und Constraint-Schicht sichern die Qualität.
- **D-3** KI-Anbindung: eigener API-Key (Anthropic, OpenAI, OpenAI-kompatibel), Ollama lokal, Offline-Modus. *(Provider-Aufteilung durch D-80 präzisiert.)*
- **D-4** Steuerung über Textprompt und MIDI-Vorlage. Melodie und Bass sind harmonisch gekoppelt. *(MIDI-Vorlage als eigenes Konzept gestrichen, aufgegangen in Referenzen und „Stimme importieren“, D-75.)*
- **D-5** Nutzung live (Transport-Sync) und als Clip (Drag & Drop).
- **D-6** Stile v1: Peak Time/Driving, Melodic Techno, Hard/Industrial.
- **D-7** Plugin-Funktionen: Noten bearbeiten, Variationen per Knopfdruck, Bibliothek.
- **D-8** Erst privat, später Verkauf. Lizenzen und IDs sind von Beginn an verkaufsfähig zu planen.
- **D-9** Umsetzung durch Claude Code. Entwickler kann C++ und nutzt CLion.
- **D-10** JUCE 8 + CMake. Zwei Targets: Instrument (VST3/AU) für Live, MIDI-FX (AU) für Logic.
- **D-11** Logic: zwei Instanzen (Bass, Melodie), automatisch gekoppelt. *(Ersetzt durch D-51.)*
- **D-12** Pattern-Wechsel im Live-Modus: Zeitpunkt einstellbar, Standard „nächster Takt“ (SPEC §3.4).
- **D-13** Patterns bis 64 Takte, aufgebaut aus Phrasen mit Formplan (SPEC §3.7). *(v1.0: bis 16 Takte; 64 Takte im Backlog, D-74.)*
- **D-14** Slides als Noten-Überlappung (Legato, 303-Style), kein Pitchbend (SPEC §4.2).
- **D-15** Formate: nur VST3 und AU, auch unter Linux nur VST3. *(Ersetzt durch D-81.)*
- **D-16** Oberfläche Deutsch und Englisch, umschaltbar. KI-Prompts intern Englisch (SPEC §8.2). *(Englisch und Umschaltung ab v1.1, D-74.)*
- **D-17** Swing und Groove-Vorlagen pro Stimme, nicht-destruktiv angewendet (SPEC §3.9). *(v1.0: Straight, Swing-Regler, Groove aus der Drum-Referenz; Vorlagen-Sammlung ab v1.1, D-74.)*
- **D-18** Chromatik-Anteil als Regler 0–100 % mit Standardwerten je Stil (SPEC §3.8). *(v1.0 nutzt die Stil-Standardwerte; Regler ab v1.1, D-74.)*
- **D-19** macOS ab Version 15, Universal Binary.
- **D-20** Ableton auf dem Mac: VST3 als Standard-Workflow, AU wird mitgebaut.
- **D-21** Name „MidiMaid“, Hersteller „Klirrwerk“. Codes und Bundle-IDs siehe SPEC §2.2 (fixiert).
- **D-22** Domain `klirrwerk.com` gehört dem Entwickler, Bundle-IDs bleiben wie in SPEC §2.2.
- **D-23** Pro Stil drei Bass-Archetypen. Auswahl automatisch, manuell übersteuerbar (STYLES.md).
- **D-24** Bass-Tonbewegung stilabhängig: Peak Time Grundton plus Quinte/Oktave, Melodic harmonisch, Hard Grundton.
- **D-25** Melodie-Archetypen: alle vorgeschlagenen Typen pro Stil. Melodie darf mehrstimmig sein. *(`polymeter_seq` ab v1.1, D-74.)*
- **D-26** Polymeter-Phasenverhalten einstellbar, frei laufend wird aus der Song-Position berechnet. *(Polymeter mit Neustart ab v1.1, „frei laufend“ im Backlog, D-74.)*
- **D-27** Harmoniewechsel und Akkordfarben stilabhängig gewichtet.
- **D-28** Mehrere Kick-Raster zur Auswahl. *(Kick-MIDI-Import ergänzt durch D-48.)*
- **D-29** Stilprofile v1 sind Startwerte und werden in Phase 1 per Hörtest feinjustiert.
- **D-30** Erster Start im Offline-Modus. KI erst nach Einrichtung durch den Nutzer.
- **D-31** Kreativitäts-Regler in der Hauptoberfläche, wirkt auf KI und Algorithmus (SPEC §7.6).
- **D-32** Token- und Kostenanzeige (geschätzt, Preise editierbar), keine Limits oder Sperren. *(Anzeige ab v1.1, Monatskosten gestrichen, D-74/D-75.)*
- **D-33** Modell: Empfehlung vorausgewählt, frei änderbar, Empfehlungen in `models.json`.
- **D-34** KI nur bei Generieren, Auto-Evolve (KI-Quelle) und Vorlagen-Analyse. Variationen rein algorithmisch. *(Erweitert um Verfeinern durch D-47. Auto-Evolve mit KI-Quelle im Backlog, D-74; die Analyse von Referenzen und importierten Stimmen läuft lokal ohne KI, SPEC §7.8.)*
- **D-35** KI-Fehler: anzeigen und nachfragen. Während der Wiedergabe nur nicht blockierend (SPEC §7.9).
- **D-36** Fenster mittel (ca. 1000 × 640), frei skalierbar 75–200 %.
- **D-37** Look dunkel, minimalistisch, Ableton-nah.
- **D-38** Piano-Roll: beide Spuren übereinander oder Fokus auf eine, umschaltbar.
- **D-39** Vorhören bei gestopptem Transport mit interner Clock (SPEC §3.11). *(Ab v1.1, D-74.)*
- **D-40** Automatisierbar: Kreativität, Energie, Generieren, Variation, Mute pro Stimme, Slot (SPEC §3.13). *(v1.0 aktiv: Slot und Mute; Generieren und Variation ab v1.1; Kreativität und Energie im Backlog; alle IDs ab v1.0 angelegt, D-74, D-93.)*
- **D-41** 16 Pattern-Slots pro Instanz. Slot-Wechsel folgt der Hub-Gruppe (SPEC §3.10).
- **D-42** Bei Offline-Rendern werden Generieren- und Variations-Trigger ignoriert.
- **D-43** Tastenkürzel an Ableton angelehnt, Leertaste bleibt bei der DAW (SPEC §8.3). *(Kürzel ab v1.1, Schaltflächen ab v1.0, D-67, D-74.)*
- **D-44** Piano-Roll-Hilfen: Velocity-Spur, Scale-Fold, Phrasen- und Kick-Raster, Slide/Akzent-Zeile. *(v1.0: Velocity-Spur; übrige Hilfen ab v1.1, D-74.)*
- **D-45** Drag & Drop-Export mit aktuellem DAW-Tempo, getrennt pro Stimme oder kombiniert in einer Spur. *(Kombinierter Export gestrichen, D-75.)*
- **D-46** Review vom 04.10.2026 übernommen (R-1 bis R-21, siehe `docs/REVIEW.md`).
- **D-47** Verfeinern per Prompt in v1, mit Kontext der letzten 5 Anweisungen und eingeschränktem Offline-Modus (SPEC §3.15). *(Offline-Verfeinern gestrichen, D-75; Verfeinern per KI bleibt v1.0.)*
- **D-48** Drum-Referenz (Kick, Hi-Hat) per Capture oder .mid in v1: eigenes Kick-Raster, Groove, Akzente (SPEC §3.14). *(Capture gestrichen, D-75; v1.0: Clip/Region oder .mid.)*
- **D-49** Live-Transponieren per MIDI-Eingang in v1, chromatisch/diatonisch, als Parameter automatisierbar (SPEC §3.16). *(Ab v1.1, D-74.)*
- **D-50** Vorrang am MIDI-Eingang: Capture vor Transponieren, kein MIDI Thru als Standard (SPEC §3.17). *(Capture gestrichen, D-75; MIDI-Eingang ab v1.1, D-74.)*
- **D-51** Hub & Voices statt fester Leit-Instanz: Rollen Hub, Voice, Standalone. Keine Echtzeit-MIDI-Übertragung zwischen Instanzen (SPEC §6.5).
- **D-52** v1 mit 2 Stimmen, Architektur für bis zu 8 offen (Datenmodell, Parameter, Engine).
- **D-53** Empfohlene Aufbauten: Logic mit Hub + Voices als MIDI-FX, Live mit mehrkanaligem Hub. Formatmischung in einer Gruppe wird gewarnt. *(Live-Teil ersetzt durch D-55; Formatwarnung ab v1.1, D-74.)*
- **D-54** Review 2 vom 04.10.2026 übernommen (K-1 bis K-24, siehe `docs/REVIEW.md` Teil 2).
- **D-55** Live-Standard: eine Stimme pro MidiMaid-Spur (Hub + Voices), weil Live beim Spur-Routing Kanäle mischt. In Phase 0 zu bestätigen. *(Mehrkanal-Option entfällt, siehe D-63.)*
- **D-56** Ausgabestufe in fester Reihenfolge: Transposition → Groove → Slide-Überlappung → Kick-Freiraum → Akzent/Ratchets (SPEC §4.2a).
- **D-57** Akkord-Skalen-Prinzip: Töne des klingenden Akkords sind immer erlaubt (SPEC §4.2).
- **D-58** Wiederholungsschutz nur bei Generieren; gespeichert wird der Seed des Gewinner-Kandidaten (SPEC §4.3). *(Wiederholungsschutz ab v1.1, D-74; Gewinner-Seed ab v1.0, D-88.)*
- **D-59** Live-Transponieren: Parameter + Tastatur-Offset additiv, keine Rückschreibung; in Hub-Gruppen frühestens „nächster Schlag“ (SPEC §3.16). *(Ab v1.1, D-74.)*
- **D-60** Rolle „Standalone“ heißt „Solo“. Hub-Ausgabemodus: eine Stimme (Standard) oder keine *(„alle mehrkanalig“ entfällt, D-63)*.
- **D-61** Auto-Evolve läuft auf einer nicht-destruktiven Evolve-Ebene; „Behalten“ übernimmt die klingende Stufe in den Slot, die Ebene wird nicht gespeichert (SPEC §6.7). *(Ab v1.1, D-74.)*
- **D-62** Live: beide Aufbauten werden unterstützt und getestet. *(Ersetzt durch D-63.)*
- **D-63** Konzentration auf Hub + Voices: keine Max-for-Live-Variante und keine mehrkanalige Ausgabe in v1. Jede Instanz gibt höchstens eine Stimme aus. MIDI-Kanal pro Stimme bleibt im Datenmodell, damit Mehrkanal später nachrüstbar ist. Setup-Hilfe im Hub für Live und Logic.
- **D-64** Produzenten-Review vom 04.10.2026 übernommen (P-1 bis P-10, siehe `docs/REVIEW.md` Teil 3).
- **D-65** Eigene Stimme importieren: ersetzt die Stimme, gesperrt, unverändert, bestimmt den harmonischen Kontext (SPEC §3.18). *(Bestätigt und um Import-Grenzen ergänzt durch D-85.)*
- **D-66** Slot-Parameter gilt beim Start und nach Sprüngen sofort (Start mitten im Arrangement). *(„Sprung“ präzisiert durch D-89; in Hub-Gruppen offen: O-22.)*
- **D-67** Oberfläche in zwei Ebenen (Basis/Experte); alle Kürzel-Aktionen auch als Schaltfläche.
- **D-68** DAW-Vorlagen für Live und Logic werden mitgeliefert (SPEC §8.6).
- **D-69** Modulation (CC-Spur pro Stimme) erst nach v1.
- **D-70** Wahrscheinlichkeit und Bedingungen A:B pro Note in v1, deterministisch per Hash aus Seed, Noten-ID und Durchlauf (SPEC §3.19). *(Ab v1.1, D-74; Datenfelder ab v1.0.)*
- **D-71** Persönlicher Stil aus Favoriten in v1: Few-Shot-Beispiele für die KI, begrenztes persönliches Profil für den Algorithmus, Regler „Persönlich“, Schnappschuss pro Pattern (SPEC §3.20). *(Erweitert durch D-72. v1.0: KI-Beispiele aus dem Set; Favoriten, Profil, Regler „Persönlich“ und Schnappschuss ab v1.1, D-74.)*
- **D-72** Referenzen pro Stil in v1: eigene MIDI-Dateien in mehreren benannten Sets pro Stil, automatisches Set „Favoriten“, Analyse von Rolle, Tonart, Akkordfolge, Motiven und Rhythmen, Wirkung auf Progressionen, Motive, Rhythmen, Profil und KI-Beispiele, Kopierschutz > 85 % Ähnlichkeit (SPEC §3.20). *(v1.0: ein Set pro Stil, Rolle beim Import zuordnen, Tonart, Progressionen, KI-Beispiele, Kopierschutz; mehrere Sets, Favoriten, Rollen-Erkennung, Motiv-Zellen, Rhythmus-Statistik und Profil ab v1.1, D-74. Kopierschutz präzisiert durch D-87.)*
- **D-73** Vorrang bei gleichzeitigen Wechseln: Slot > manuelles Ergebnis > Evolve-Stufe; manuelle Aktionen während Evolve wirken auf die klingende Stufe und übernehmen sie (SPEC §6.8). Feature-Bewertung in `docs/FEATURES.md`. *(Evolve-Teile ab v1.1, D-74.)*
- **D-74** Release-Umfang: v1.0 = Bewertung 1 + 2, v1.1 = Bewertung 3, Backlog = Bewertung 4 (`docs/FEATURES.md`). Damit gelten u. a.: Wahrscheinlichkeit (D-70), Live-Transponieren (D-49), Evolve-Ebene (D-61), Vorhören (D-39), Chord-Memory, mehrere Referenz-Sets und Profil (D-72) ab v1.1; Patterns bis 16 statt 64 Takte (D-13); Offline-Verfeinern (D-47), Capture (D-48), Struktur-Modus, native OpenAI-/Ollama-Provider (D-3), Gruppen A–D, Kick-Grundton im Backlog. Datenfelder und Parameter-IDs für v1.1 werden in v1.0 angelegt. *(Bewertung 4 aufgeteilt durch D-75; bestätigt durch D-84.)*
- **D-75** Bewertung 4 aufgeteilt: **Gestrichen** (Dichte-Korrektur, kombinierter Export, Offline-Verfeinern, Struktur-Modus, MIDI-Vorlage als Konzept, Capture/„Lernen“, Max-for-Live-Variante, Monatskosten, Euklid als Feature) und **Backlog**. Modulation (D-69) ist erster Kandidat für v1.2. Wiederaufnahme gestrichener Punkte nur per neuer Entscheidung mit neuem Argument.
- **D-76** Windows-Testrechner vorhanden: Live unter Windows bleibt Teil von Phase 0 und der Testmatrix.
- **D-77** Sprachen: Code, Kommentare, Commits und PRs Englisch; Dokumentation Deutsch; Kommunikation mit dem Entwickler Deutsch.
- **D-78** Hörtests, Testmatrix und DAW-Vorlagen nutzen nur Live- bzw. Logic-eigene Instrumente; Auswahl pro Rolle in Phase 0 (SPEC §12).
- **D-79** Hörtest-Werkzeug `tools/mmgen` (CLI auf Basis von `core`) für Phase 1a; `.claude/settings.json` mit Freigaben für Build, Test und lokales Git.
- **D-80** KI-Anbindung: nativer Anthropic-Provider (Structured Outputs) plus ein OpenAI-kompatibler Provider mit Voreinstellungen und Fähigkeitsstufe pro Backend (Schema erzwungen / nur JSON / nur Prompt). Antworten werden immer validiert (SPEC §7.2).
- **D-81** Architektur für alle üblichen Formate (VST3, AU, CLAP, LV2, AAX, optional AUv3; VST2 ausgeschlossen) und für Linux. Ausgeliefert wird zuerst VST3, weitere Formate folgen nachträglich. Formatcode nur in `HostCapabilities`, Formatliste per CMake-Variable, formatunabhängiger State und Parameter-IDs, IDs aller Zielformate reserviert, Linux-Kompiliertest in der CI ab v1.0 (SPEC §2.4). *(AU in v1.0 durch D-82; Formatwechsel präzisiert durch D-91.)*
- **D-82** AU kommt in v1.0: Instrument-Variante als VST3 und AU, MIDI-FX-Variante als AU (`aumi`) für Logic. Logic ist von Anfang an unterstützt.
- **D-83** Externe Prüfung vom 04.10.2026 ausgewertet (ehemals `REVIEW_GPT.md`): Vorschläge und Befunde übernommen, präzisiert oder abgelehnt, dazu eigene Zusatzbefunde (siehe `docs/REVIEW.md` Teil 4).
- **D-84** Release-Zuordnung bestätigt: D-74, D-75 und `docs/FEATURES.md` bleiben maßgeblich. Der Vorschlag, Wahrscheinlichkeit, Live-Transponieren, Evolve-Ebene, Chord Memory und mehrere Referenz-Sets nach v1.0 zu holen, ist abgelehnt. Ältere Entscheidungen tragen Verweise auf ihr Release.
- **D-85** Eigene Stimme bleibt beim Import unverändert, ohne Rückfrage (D-65 bestätigt). Das Qualitätsversprechen gilt für erzeugtes Material. Import nur in 4/4, Länge auf die nächste erlaubte Pattern-Länge aufgerundet, höchstens 16 Takte (SPEC §1, §3.18).
- **D-86** Datenschutz: Referenz-Beispiele und importierte Stimmen gehen an Cloud-Provider nur nach einmaliger ausdrücklicher Zustimmung pro Provider (Opt-in, widerrufbar); lokale Provider sind ausgenommen. Verfeinern gilt als Zustimmung für das aktuelle Pattern ohne importierte Stimmen. Logs enthalten keine Prompts und Nutzerinhalte, außer mit der Option „Prompts protokollieren“ (SPEC §3.15, §3.20, §10).
- **D-87** Kopierschutz: Vergleich von Einsätzen und Intervallen im 16tel-Raster, transpositionsunabhängig, nur gegen Einträge gleicher Rolle, ähnlichstes taktweise verschobenes Fenster, Grenze 85 %. Geschützt sind nur markante Einträge (mindestens 3 Tonhöhenklassen und 4 Tonhöhenwechsel je 2 Takte). Dieselbe Metrik gilt für den Wiederholungsschutz ab v1.1 (SPEC §3.20).
- **D-88** Kandidatenwahl: Der Gewinner-Seed wird schon ab v1.0 gespeichert, weil der Kopierschutz die Auswahl vom Set-Inhalt abhängig macht. Gibt es keinen gültigen Kandidaten, folgen bis zu 2 weitere Runden mit je 8 Kandidaten; danach bleibt das bisherige Pattern mit Hinweis, ein unterwertiger Kandidat wird nie automatisch übernommen (SPEC §4.3, §4.4).
- **D-89** Wechsel in der Engine: Beim Pattern- oder Slot-Wechsel enden alle alten Noten am Wechselzeitpunkt, Slides gehen nicht über die Pattern-Grenze. Höchstens ein ausstehender Wechsel je Art; ein neueres Ergebnis ersetzt ein älteres. Bearbeitungen wirken sofort, ohne Quantisierung. Sprung = jede PPQ-Unstetigkeit inklusive Loop-Rücksprung; danach richtet sich die Position am Songraster aus (SPEC §6.1–6.3).
- **D-90** Mindestvorlauf geplanter Wechsel: Experteneinstellung, Standard 150 ms, gemessen ab der am weitesten fortgeschrittenen Position aller Instanzen der Gruppe und mit dem Tempo in PPQ umgerechnet (SPEC §3.4, §6.5).
- **D-91** Plugin-State: formatunabhängig und in jeder Variante lesbar. Einen direkten Formatwechsel innerhalb der DAW verspricht MidiMaid nicht; Slots wandern über die Bibliothek (SPEC §2.4).
- **D-92** Akkordsymbole einheitlich als römische Ziffern relativ zur Dur-Tonleiter auf dem Grundton (z. B. i–♭VI–♭VII), in Profilen, KI-Schema und Code (STYLES.md §1.17).
- **D-93** Parameter-Register mit festen String-IDs, Typ, Bereich und Standard ist normativ (SPEC §3.13). Nicht aktive Parameter sind ohne Wirkung und als nicht automatisierbar markiert, weil VST3 und AU kein zuverlässiges Ausblenden kennen.
- **D-94** Abhängigkeiten über CPM, Versionen gepinnt: JUCE **8.0.15**, Catch2 3.16.0, nlohmann/json 3.12.0, CPM.cmake v0.43.2 (im Repo unter `cmake/CPM.cmake`, SHA-256 `49a3bef91ceb65bb66d57255e12d1ffd22abc2f6408fa9fe4534c544a2f232aa`). JUCE 9 (seit 21.07.2026, aktuell 9.0.3) wird vorerst nicht verwendet, weil SPEC und Roadmap JUCE 8 voraussetzen; ein Wechsel braucht eine eigene Entscheidung. Der CPM-Cache liegt unter `~/.cache/CPM`. Warnungen als Fehler gelten nur für eigene Targets (Interface-Bibliothek `mm_warnings`).
- **D-95** Phase-0-Engine (`source/engine/PatternPlayer`): reine Standardbibliothek, feste Kapazitäten, keine Allokation im Audio-Thread. Position = Song-PPQ modulo Pattern-Länge (Wechselzeitpunkt PPQ 0, SPEC §6.1). Sprung = PPQ-Abweichung größer als eine Sample-Länge plus 1e-6 PPQ; der Loop-Rücksprung innerhalb eines Blocks wird sample-genau behandelt, endet das Loop-Ende genau auf einer Blockgrenze, folgen die Note-Offs im ersten Sample des nächsten Blocks. `prepareToPlay` lässt die Aktiv-Tabelle bestehen (Note-Offs im ersten Block danach, SPEC §6.2). Note-Offs haben Vorrang vor neuen Note-Ons (Kapazität 1024 Ereignisse pro Block). Das Platzhalter-Pattern ist ein Takt Offbeat-Bass (Schritte 2, 6, 10, 14; A1 = MIDI 33; Akzent auf Schritt 14) und wird durch das Datenmodell aus SPEC §5 ersetzt.
- **D-96** Drag & Drop in Phase 0: Das Pattern-Primitiv (`PatternNote`, `PatternView`, Platzhalter-Pattern) liegt in `core/PlaybackPattern.h`, die `engine` hängt von `core` ab. Der MIDI-Schreiber `core/MidiFile` erzeugt Standard-MIDI-Dateien im Format 0 (960 PPQ) mit Spurname, Tempo (aktuelles DAW-Tempo), Taktart und End-of-Track am Pattern-Ende. Die Datei entsteht in einem Hintergrund-Thread (`MidiExporter`) in einem Unterordner pro Instanz unter dem Temp-Verzeichnis, wird beim Zerstören der Instanz entfernt, verwaiste Ordner (älter als 1 Tag) beim nächsten Start. Der Editor stößt den Export beim Öffnen und bei Tempoänderung an; der Drag startet nur, wenn die Datei fertig ist. Dateiname: `MidiMaid_Slot1_Bass.mid` (Slots und Stimmen folgen mit dem Datenmodell).
- **D-97** macOS wird standardmäßig nur für arm64 gebaut (lokal und in der CI). Das Universal Binary (arm64 + x86_64, SPEC §11) und x86_64-Tests unter Rosetta (SPEC §12) laufen nur auf ausdrückliche Anforderung: CMake-Option `MIDIMAID_UNIVERSAL=ON` bzw. die Eingabe „universal“ beim manuellen CI-Start. Spätestens für das Release (Phase 7) ist ein Universal-Lauf Pflicht. Ebenso laufen Windows und Linux in der CI nur auf Zuruf (`workflow_dispatch`); automatisch baut die CI nur macOS.
- **D-98** Zufall in `core` (`core/Random.{h,cpp}`): PCG32 (XSH-RR 64/32) nach dem Referenzalgorithmus; der Nutzer-Seed (`uint64_t`) wird über SplitMix64 in Zustand und Stream umgesetzt (`Pcg32::fromSeed`), abgeleitete Seeds (Kandidaten, weitere Runden, D-88) über `deriveSeed(basis, index)`. Verteilungsfunktionen rein ganzzahlig und ohne Modulo-Verzerrung: `bounded`, `range`, `chance`, `weightedIndex`, `shuffle` (Fisher-Yates). Keine Fließkomma-Zufallsfunktionen. Die Goldenwerte in `tests/test_random.cpp` stammen aus einer unabhängigen Python-Umsetzung und stimmen mit den veröffentlichten Referenzvektoren überein; jede Änderung an einer dieser Funktionen verändert alle erzeugten Patterns und braucht eine neue Entscheidung samt State-Migration.
- **D-99** Musiktheorie in `core/Theory.{h,cpp}`: Skalen-IDs im Schema der Stilprofile (`natural_minor`, `phrygian`, `dorian`, `minor_pentatonic`, `harmonic_minor`, `phrygian_dominant`, `locrian`). Stufen werden nach SPEC §7.3 zu MIDI-Nummern (Oktavübertrag, Alteration −1/0/+1, Oktave relativ zur Grundlage der Stimme = tiefster Grundton im Tonumfang). Akkorde sind Dreiklänge (Dur, Moll, vermindert, sus2, sus4); Septimen kommen aus den Akkordfarben des Voicings. Akkord-Skalen-Prinzip (D-57): erlaubt ist ein Ton aus der Skala oder dem klingenden Akkord. Einrasten wählt den nächsten erlaubten Ton, bei gleichem Abstand standardmäßig den unteren (`TieBreak::Down`, umschaltbar).
- **D-100** Pattern-Datenmodell (`core/Pattern.h`, `PatternValidation`, `PatternJson`): exakt nach SPEC §5, `Chord` und `PitchClass` aus `Theory.h`. JSON mit `stateVersion = 1`, sortierte Schlüssel (gleiche Patterns ergeben byte-gleichen Text), Enums als Strings, v1.1-Felder mit Standardwerten, unbekannte Felder werden ignoriert, 64-Bit-Zahlen bleiben exakt, `float`-Felder (Swing, Amount) laufen exakt durch einen Round-Trip. Pflichtfelder sind nur `stateVersion`, `lengthBars`, `nextNoteId`, `context`, `phrases`, `voices` und die Kernfelder von Noten, Stimmen, Phrasen und Akkorden. Der Loader **lehnt ungültige Daten ab** (Fehlermeldung mit JSON-Pfad, keine stille Reparatur); `validatePattern` prüft die Invarianten aus SPEC §3.5, §3.7, §4.2 und §5. Eine neuere `stateVersion` wird so weit wie verstanden geladen und markiert (`fromNewerVersion`). Migrationen sind eine Funktionstabelle (Schritt i hebt Version i auf i+1), für v1 ist sie leer. Die Phase-0-Primitive (`PatternNote`, `PatternView`, `core/PlaybackPattern.h`) bleiben neben dem Modell bestehen, bis Engine und MIDI-Export in Phase 1b darauf umgestellt werden; dann werden sie entfernt.
- **D-101** Constraint-Schicht (`core/Constraints.{h,cpp}`), Teil 1: `applyConstraints(Pattern&, ConstraintSettings)` ist deterministisch und läuft seine Regeln wiederholt bis zum Fixpunkt, ein zweiter Aufruf ändert nichts. Eingabe pro Stimme ist der wirksame Tonumfang in MIDI-Nummern (Profile und Oktav-Offset folgen mit eigenen Roadmap-Punkten). Gesperrte Dimensionen (Stimme und Note) werden nie geändert, ein dafür nötiger Eingriff wird übersprungen und gezählt; vollständig gesperrte Stimmen bleiben unberührt, `slide` zählt zur Dimension Rhythmus. Regeln: doppelte Kanäle werden mit dem niedrigsten freien Kanal repariert; Velocity wird auf 1–127 begrenzt; Töne werden in den Umfang oktaviert (nächste Oktave, ohne passende Tonhöhenklasse im Umfang wird die Note verworfen) und auf Skala und klingenden Akkord eingerastet (Gleichstand: nach unten); Chromatik über 0 %: höchstens `floor(Anteil × Notenzahl)` skalenfremde Töne je Stimme, nur auf unbetonten Schritten und im Halbtonabstand zu einem erlaubten Ton, die frühesten bleiben; Notenanfänge werden auf das Raster eingerastet (nächste Position, Gleichstand: früher; fiele das Ergebnis auf das Pattern-Ende, gilt die Position davor; 160-Tick-Triolen bleiben auf Wunsch erhalten); Längen werden am Pattern-Ende abgeschnitten, Anfänge ab dem Ende verworfen; gleiche Töne mit gleichem Start verschmelzen (längere Länge, erste ID bleibt), der Bass behält pro Start nur die tiefste Note; gleiche Tonhöhen überlappen nie, der Bass ist einstimmig (frühere Note endet am Start der nächsten); Slides werden bei überlappenden Noten, Akkord-Zielen und Ratchets entfernt, ein Slide auf die gleiche Tonhöhe wird zum Haltebogen (auch über eine Lücke; die erste ID bleibt, über die Pattern-Grenze wird nicht verschmolzen). Abweichung von SPEC §4.2 („Länge mindestens ein Rasterschritt“): Die Mindestlänge ist einstellbar (Standard 1/64 = 60 Ticks), weil die Bass-Archetypen Noten von 50–75 % eines Schritts verlangen; kürzere Noten werden verworfen. Teil 2 siehe D-102.
- **D-102** Constraint-Schicht, Teil 2 (`core/KickGrid`, `core/Constraints`): Kick-Raster als Tabelle (`4otf`, `4otf_pickup`, `halftime`, `broken_a`, `broken_b`), pro Takt aus der Phrase (`Phrase::kickGridId`) oder dem Pattern; `custom` liest die Kick-Schritte der Drum-Referenz (wiederholt alle `bars` Takte), `custom` ohne Referenz und unbekannte IDs fallen auf `4otf` zurück. Nur für **Bass-Stimmen** (nicht für `long_tied`, `VoiceConstraints::ignoresKick`): Eine Note auf einem Kick-Schritt rückt um je einen 16tel nach hinten, bis der Schritt frei ist; sie wird verworfen, wenn sie dabei das Pattern verlässt oder auf einer anderen Note landet. Kick-Freiraum (Standard 1/32 = 120 Ticks, einstellbar 0/60/120/240): Eine Bassnote endet so lange vor der nächsten Kick, auch vor der ersten Kick des nächsten Durchlaufs; der Freiraum hat Vorrang vor der Länge, Reste unter der Mindestlänge werden verworfen. Ein Bass-Slide, der bis `Zielnotenstart + Slide-Überlappung` (Standard 1/64) über einen Kick-Schritt reicht, verliert das Slide-Flag; der letzte Slide eines Patterns zielt auf die erste Note des nächsten Durchlaufs. Für alle Nicht-Bass-Stimmen gilt relativ zur ersten Bass-Stimme: Auf betonten Schritten (Vielfache von 960 Ticks) sind kleine Sekunde, Tritonus und große Septime nur erlaubt, wenn der Chromatik-Anteil ≥ 30 % ist oder der Stil Hard/Industrial ist (`harshStyle`); große Sekunde und kleine Septime sind erlaubt (SPEC nennt sie nicht); bei gleichzeitigem Anschlag liegt die Stimme mindestens 12 Halbtöne über dem Bass. Verstößt eine Note, rückt sie auf den nächsten erlaubten, im Umfang liegenden und regelkonformen Ton (erst abwärts, dann aufwärts, bis zwei Oktaven) oder wird verworfen; der Bass weicht nie aus, auch ein gesperrter (importierter) Bass bestimmt die Regeln für die anderen Stimmen. Gesperrte Noten werden nie verändert oder verworfen.
- **D-103** Sanitizer-Lauf: CMake-Option `MIDIMAID_SANITIZE=ON` baut unsere Ziele mit AddressSanitizer und UndefinedBehaviorSanitizer (nicht MSVC; Drittbibliotheken bleiben unverändert, UBSan-Funde brechen ab). Die CI hat dafür einen manuell startbaren Job (`workflow_dispatch`, Eingabe `sanitizers`, macOS arm64), der `mm_core_tests`, `mm_engine_tests` und `mm_plugin_tests` baut und direkt ausführt (die JUCE-Tests sind nicht enthalten). Anlass: Ein `vector::assign` mit Referenz auf ein eigenes Element ließ einen Test unter MSVC-Debug hängen; ASan hat den Fehler als heap-use-after-free auch auf macOS gezeigt, normale Läufe nicht. Vor größeren Änderungen an `core` und `engine` sollte der Job einmal laufen.
- **D-104** Ausgabestufe (`core/OutputStage`): `renderOutput(Pattern, OutputSettings)` ist eine reine, deterministische Funktion und erzeugt aus dem gespeicherten Raster die klingenden Noten (`OutputNote` mit Start und Ende in Ticks). Reihenfolge nach SPEC §4.2a, v1.0-Stufen: 2 Groove und Swing, 3 Slide-Überlappung, 4 Kick-Freiraum, 5 Akzent-Velocity; Wahrscheinlichkeit (0), Transposition (1) und Ratchets (5) sind v1.1 und kommen an derselben Stelle dazu. **Swing:** nur Noten, die genau auf einem 16tel beginnen; jeder zweite 16tel wird um `(Swing − 50 %) × 480` Ticks verspätet (75 % = 120 Ticks); der Swing-Wert wird zuerst in ganze Promille gerundet und dann rein ganzzahlig gerechnet (plattformgleich). **Drum-Referenz** (`templateId = "drum_reference"`, sonst leer oder `"straight"`): Versatz je 16tel aus `timingOffsetTicks`, Velocity-Profil = Hi-Hat-Velocity relativ zum Mittelwert der belegten Schritte (Faktor 0,5–1,5); der Swing-Regler ist dann wirkungslos; ohne Referenz gilt der Swing-Regler. `amount` skaliert Versatz und Profil linear. **Grenzen:** Der Start bleibt in [0, Pattern-Ende), ein Versatz bricht nie über die Loop-Grenze um; eine so ans Ende geschobene Note unter der Mindestlänge (60 Ticks) entfällt. **Slides:** Das Ende liegt `Überlappung` (Standard 1/64) hinter dem Start der verschobenen Folgenote (Folgenote = die nächste Note im gespeicherten Pattern), auch über eine Lücke; die letzte Note gleitet in die erste des nächsten Durchlaufs (Ende hinter dem Pattern-Ende); gleiche Tonhöhe ergibt einen Haltebogen ohne Überlappung (kein Slide-Flag in der Ausgabe); steht die Folgenote nach dem Groove vor der Slide-Note, entfällt der Slide. Ohne Slide endet eine Note spätestens am Start der nächsten Note derselben Linie (Bass: nächste Note, sonst gleiche Tonhöhe), damit der Groove keine ungewollten Überlappungen erzeugt (Ergänzung zur SPEC). **Kick-Freiraum nach dem Groove** (nur Bass, nicht `long_tied`): Notenenden enden `Freiraum` vor der nächsten Kick (auch der des nächsten Durchlaufs); ein Slide, der bis zum Ende der Überlappung über einen Kick-Schritt reicht, verliert sie; Notenreste unter der Mindestlänge entfallen, ebenso der Slide der Note davor. Die gedachte Kick bleibt gerade. **Akzent:** Akzent-Noten erhalten die Akzent-Velocity (Standard 124) nach dem Velocity-Profil, alle anderen behalten ihre (durch das Profil angepasste) Velocity.
- **D-105** Register und Oktav-Offset (`core/Register`): Bereiche sind MIDI-Nummern (`Range`), Standard Bass 28–52, Melodie 55–88, Stabs 55–79 (STYLES.md §1.6); Profile dürfen engere Bereiche setzen (`RegisterProfile`), der Stab-Bereich steht in `Pattern::voicing`. Der Oktav-Offset (−2 bis +2, größere Werte werden begrenzt) verschiebt den **Bereich** um `12 × Offset` Halbtöne (begrenzt auf MIDI 0–127); gespeicherte Noten klingen so, wie sie gespeichert sind, es gibt keine Offset-Stufe in der Ausgabe (SPEC §4.2a), importierte Stimmen bleiben unverändert, und die Grundlage der Stimme (SPEC §7.3) wandert mit dem Bereich. Der wirksame Bereich einer Stimme ist Bass-Bereich, Melodie-Bereich oder für die Stab-Archetypen `stabs` und `aggro_stabs` der Voicing-Bereich des Patterns, jeweils plus Offset; er muss mindestens 12 Töne umfassen (sonst `nullopt`, `ConstraintSettings` fällt dann auf den unverschobenen Bereich zurück). Die Liste der Stab-Archetypen steht in `Register`, bis die Archetyp-Schnittstelle diese Eigenschaft liefert. `ConstraintSettings::defaultsFor` und `forPattern` nutzen den Offset jeder Stimme. `changeOctaveOffset` setzt den Offset und verschiebt die Noten der Stimme um dieselben Oktaven (Tonhöhenklasse bleibt, gesperrte Tonhöhen bleiben stehen); die Verdrahtung in der UI folgt mit der Oberfläche.
- **D-106** Akkordsymbole (`core/ChordSymbol`): Der Parser liest die ASCII-Schreibweise aus STYLES.md §1.17 (römische Ziffern relativ zur Dur-Tonleiter auf dem Grundton; `b` oder `#` höchstens einmal vorangestellt; Großbuchstaben Dur, Kleinbuchstaben Moll; `o` nach einer kleingeschriebenen Ziffer vermindert; `sus2`/`sus4` mit beliebiger Schreibweise der Ziffer). Gemischte Groß- und Kleinschreibung (`Vi`), Leerzeichen, Ziffern über VII, Septimen (`I7`) und Unicode-Zeichen (`♭`, `°`) werden abgelehnt; die Unicode-Darstellung gehört in die UI. `formatChordSymbol` schreibt kanonisch mit `b` (`bII`, `bIII`, `bV`, `bVI`, `bVII`, nie `#IV`), Moll klein, vermindert als klein plus `o`, sus als groß plus `sus2`/`sus4`; Parser und Formatter bilden für alle 12 Halbtöne und 5 Qualitäten einen Round-Trip. Die Aufteilung eines Takts (`i|bVII`) und das Wiederholen kurzer Listen (SPEC §7.3) gehören zur KI-Schema-Auswertung und kommen später.

## Offen

- **O-14 Code-Kollision:** Vor dem Release prüfen, dass `Klrw`/`Mdmi`/`Mdmf` von keinem anderen
  Hersteller genutzt werden (z. B. per `auval -a` auf mehreren Systemen).
- **O-22 Slot-Automation in Hub-Gruppen:** Voices erfahren eine Slot-Automation erst, wenn der Hub den
  betreffenden Block verarbeitet hat. Rechnen Spuren versetzt (Logic berechnet nicht ausgewählte Spuren
  voraus, parallele Threads), wechseln Voices womöglich zu spät; bei Einzelspur-Bounce oder Freeze ohne
  Hub gar nicht. Das betrifft auch den Start mitten im Arrangement (D-66). Optionen:
  (a) Automation wirkt erst am ersten Quantisierungspunkt nach dem Mindestvorlauf, Slot-Automation
  gehört also vor die Taktgrenze (Setup-Hilfe, Vorlagen);
  (b) jede Instanz wertet ihre eigene Slot-Automation aus, Vorlagen automatisieren alle MidiMaid-Spuren
  (deterministisch auch bei Einzelspur-Bounce, aber doppelte Pflege);
  (c) die eigene Automation einer Voice hat Vorrang, sonst folgt sie dem Hub.
  Entscheidung nach den Messungen in Phase 0, vor Phase 1b.
- **O-23 MIDI-Ausgabepuffer:** SPEC §6.3 verlangt, den Ausgabepuffer in `prepareToPlay` zu reservieren. Der `juce::MidiBuffer` gehört aber dem Host-Wrapper; MidiMaid kann ihn dort nicht vorab vergrößern, `addEvent` kann beim ersten Ereignis allozieren. Zu klären (Phase 1b, Echtzeit-Vertrag): eigener vorreservierter Puffer plus Kopie, oder die Annahme des JUCE-Verhaltens messen und dokumentieren.
- **O-24 Negative PPQ (Vorzähler):** Hosts melden beim Vorzähler negative Positionen; die Engine spielt das Pattern dort nach derselben Rechnung (Position modulo Länge). Ob das gewünscht ist oder der Vorzähler still bleiben soll, ist in der Host-Verifikation zu prüfen.

## Entwicklungsumgebung (Stand 04.10.2026)

Dokumentiert nach CLAUDE.md „Entwicklungsumgebung“ (ROADMAP Phase 0). Der Mac wird zuerst eingerichtet,
Windows folgt, sobald der Mac-Teil von Phase 0 fertig ist (Entscheidung des Entwicklers); bis dahin gilt
Windows als **offen**.

### macOS (Entwicklungsrechner)
| Bereich | Version |
|---|---|
| macOS | 27.0 (Build 26A428), arm64 |
| Compiler | Apple clang 21.0.0 (Xcode-Kommandozeilenwerkzeuge, kein vollständiges Xcode installiert) |
| CMake | 4.4.4 (Homebrew; Anforderung ≥ 3.25) |
| Ninja | 1.13.2 (Homebrew) |
| clang-format | 23.1.2 (Homebrew) |
| pluginval | 1.0.4 (`/Applications/pluginval.app`, Symlink `/opt/homebrew/bin/pluginval`; ZIP-SHA-256 `3c4c533bda0c5059eea3ddaea752d757ee2025041f0f47e6bcb0e87f6082b29f`) |
| auval | vorhanden (`/usr/bin/auval`) |
| git / gh | 2.54.0 / 2.102.0 (gh angemeldet, SSH) |
| Ableton Live | 12 Suite 12.4.6 |
| Logic Pro | 12.3.1 |

Anmerkungen:
- Es sind nur die Kommandozeilenwerkzeuge aktiv (`xcodebuild` fehlt). Für Builds mit CMake und Ninja reicht
  das; ein vollständiges Xcode wird erst für Signierung und Notarisierung (Phase 7) relevant.
- Aufgaben mit Bedarf an Xcode, Rosetta-Test (x86_64) und Universal Binary werden bei der jeweiligen
  Roadmap-Aufgabe geprüft.

### Windows
Offen. Zu dokumentieren: Windows-Version, Visual Studio 2022 Build Tools (MSVC), CMake, Ninja, `gh`,
pluginval, Live-Version.

Geprüft in der CI (Stand 05.10.2026): `windows-debug` (mit geladener Visual-Studio-Umgebung) und `linux-debug` konfigurieren,
bauen und testen grün. Release-Presets und lokale Windows-Entwicklung (CLion) sind noch ungeprüft.

## Host-Fähigkeiten (Annahmen, Stand 04.10.2026)

`HostCapabilities` (`source/plugin/HostCapabilities.{h,cpp}`) leitet aus Format, Host und Variante die Fähigkeiten
ab (SPEC §2.4). Grundsatz: **unbekannt = nicht verfügbar**. Folgende Werte sind **Annahmen**, die die
Host-Verifikation in Phase 0 bestätigen muss; bei einem negativen Ergebnis wird die Tabelle in
`HostCapabilities.cpp` samt Test angepasst.

| Host | MIDI-Ausgang | MIDI-FX-Slot | Kopplung im selben Prozess | Drag & Drop nach außen | Tasten-Weiterleitung |
|---|---|---|---|---|---|
| Standalone | ja | nein | nein | ja | ja |
| Live (VST3, AU Instrument) | ja | nein | ja (Annahme) | ja (Annahme) | nein (ungemessen) |
| Logic (AU `aumi`) | ja | ja | ja (Annahme) | ja (Annahme) | nein (ungemessen) |
| Logic (AU Instrument) | nein | nein | ja (Annahme) | ja (Annahme) | nein (ungemessen) |
| Reaper, Bitwig (VST3 Instrument) | ja | nein | nein (ungeprüft, Bitwig kann Plugins auslagern) | nein | nein |
| unbekannt, CLAP/LV2/AAX | nein | nein | nein | nein | nein |

Nur `detectHostCapabilities` (`HostDetection.cpp`) liest JUCEs `wrapperType` und `PluginHostType`.

### CI-Toolchains (festgeschrieben in `.github/workflows/ci.yml`, Stand 04.10.2026)
| Job | Runner | Toolchain | Status |
|---|---|---|---|
| macOS (automatisch bei PR und `main`) | `macos-15`, Image 20260907.0337.1, macOS 15.7.9, arm64 | Xcode 16.4, Apple clang 17.0.0, CMake 4.4.3, Ninja 1.13.2, pluginval 1.0.4 | grün: Build, 37 Tests, pluginval Strictness 10 (VST3, AU, AU MIDI-FX), auval `aumu`/`aumi` |
| Windows (nur auf Zuruf) | `windows-2022` (Windows Server 2022), Image 20260927.320.1 | Visual Studio 2022 Build Tools über `ilammy/msvc-dev-cmd` (v1.13.0, per SHA gepinnt), Windows SDK 10.0.26100, CMake 3.31.6, Ninja 1.13.2, pluginval 1.0.4 (SHA-256 des Archivs `c08e61ce…15ab`, beim ersten Download ermittelt) | grün: Build, 37 Tests, pluginval Strictness 10 (VST3). Der genaue MSVC-Toolset-Wert wird ab dem nächsten Lauf im Log ausgegeben |
| Linux (nur auf Zuruf) | `ubuntu-24.04`, Image 20260927.320.1 (Ubuntu 24.04.5) | GCC 13.3.0, CMake 3.31.6, Ninja (apt) | grün: Build inklusive VST3 (Kompiliertest) und Standalone, 37 Tests unter `xvfb` |

Gepinnte Actions: `actions/checkout` v7, `actions/cache` v6 (Tags), `ilammy/msvc-dev-cmd` per Commit-SHA.
