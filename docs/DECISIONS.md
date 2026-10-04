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

Nicht geprüft: `windows-debug`/`windows-release` in `CMakePresets.json` setzen `cl` als Compiler und gehen davon aus,
dass CLion bzw. die Developer-Eingabeaufforderung die Visual-Studio-Umgebung lädt. `linux-*` ist ebenfalls noch
ungeprüft (kommt mit der Linux-CI).
