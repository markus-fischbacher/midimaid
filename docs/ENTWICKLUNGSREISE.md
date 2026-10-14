# MidiMaid – Resümee der Entwicklungsreise

Stand: 14. Oktober 2026, `main` nach PR 182. Dieses Dokument fasst zusammen, was in zehn Tagen entstanden ist, wie wir
gearbeitet haben und wo eine spätere Version 2 ansetzen kann. Die verbindlichen Quellen bleiben `SPEC.md`,
`ROADMAP.md` und `DECISIONS.md`; hier steht die Erzählung dazu.

## 1. Was MidiMaid ist

MidiMaid von Klirrwerk ist ein Plug-in (VST3, AU, AU-MIDI-FX, Standalone), das **live MIDI für Techno-Melodien und
Basslines erzeugt**. Der Kern ist ein deterministischer Generator mit Stilprofilen; eine KI (Anthropic, OpenAI, Ollama,
LM Studio, kompatible Server) lenkt ihn, ersetzt ihn aber nicht. Mehrere Instanzen arbeiten als **Hub und Voices**
zusammen: Der Hub erzeugt und hält die Patterns, jede Voice spielt eine Stimme auf ihrem eigenen Kanal in der DAW.

## 2. Zeitlinie in Zahlen

| | |
|---|---|
| Zeitraum | 4. bis 14. Oktober 2026 (Spezifikation am ersten Tag, danach Umsetzung) |
| Commits auf `main` | rund 470 |
| Entscheidungen | 204 Einträge in `DECISIONS.md` (D-1 bis D-261, einige ersetzt oder zusammengelegt) |
| Quelltext | rund 41 000 Zeilen in `source/` (davon `core` 20 000, `plugin` 15 600, `ai` 3 700, `engine` 1 800) |
| Tests | rund 2 070 Testfälle (Catch2) plus pluginval (Strictness 10) in 3 Seeds je Format |
| Host-Prüfungen | 104 Einträge H1 bis H104 in `HOST-VERIFIKATION.md`, die Markus selbst in Live (später Logic) abhakt |
| Dokumente | SPEC, ROADMAP, DECISIONS, FEATURES, REVIEW, STYLES, HANDBUCH, HOERTEST, SETUP-HILFE |

## 3. Die Etappen

1. **Spezifikation zuerst (Phase 0).** Vor der ersten Codezeile standen `SPEC.md`, eine Review (`REVIEW.md`) mit Kritik
   aus Musiktheorie und Technik, die Feature-Bewertung (`FEATURES.md`, Stufen 1 bis 4) und die Stilprofile
   (`STYLES.md`). Daraus folgte die harte Trennung in v1.0, v1.1, Backlog und Gestrichen. Host-Spike: JUCE, CMake/CPM,
   CI auf macOS, Windows und Linux, pluginval.
2. **Musikalischer Kern (Phase 1a).** `mm_core` ohne JUCE: Tonleitern, Pattern-Modell, eigener PRNG (kein
   `std::*_distribution`, damit alles auf allen Plattformen identisch ist), Generatoren je Stil und Archetyp,
   Constraints, Kick-Raster, Groove, Golden Files. Hier lag die Musikqualität; Hörtests laufen über `mmgen`.
3. **Engine, Slots, Hub & Voices (Phase 1b).** Echtzeit-Player ohne Allokation und Locks, garantiertes Note-Off,
   16 Slots, lock-freie Pattern-Übergabe, Gruppenkanal mit geplanten Wechseln an Taktgrenzen (PPQ-Zeitstempel).
4. **Bearbeitung und Variationen (Phase 2).** Piano-Roll, Undo, Locks pro Stimme, Variationen, Verfeinern.
5. **KI-Anbindung (Phase 3).** Provider-Schnittstelle, Schema und versionierte Prompts, Kosten- und Zustimmungslogik
   pro Provider, Schlüssel nur im Schlüsselbund, Offline-Fallback. Lokale Modelle (Qwen 27B, Gemma) waren das erste
   Testziel.
6. **Lenkung (Phase 4).** Stimme importieren, Drum-Referenz, Referenzen und Favoriten, Geschmack im Prompt.
7. **Bibliothek und Persistenz (Phase 5).** Speichern, Laden, Bewertungen, Export als MIDI per Drag & Drop.
8. **Feinschliff und v1.0 (Phase 6).** Vorlagen, Handbuch, Setup-Hilfe, Lizenzfragen vorbereitet.
9. **v1.1 in Etappen.** Evolve-Ebene, mehr Stimmen (bis acht, mit Rollen Bass, Melodie, Stabs, Arpeggio, Gegenmelodie),
   Hub-Vorschau ohne Host-Transport, Live-Transposition, Wechsel-Quantisierung, Befehlspalette.
10. **Oberfläche überarbeitet (D-252 bis D-261).** Neue Hub-Oberfläche mit Inspektor und Reitern, Seite „Gruppe“,
    Einstellungen auf vier Reitern, Fokus per Klick auf den Namen, Stimmenauswahl mit den Namen des Hubs.

## 4. Technische Leitplanken, die sich bewährt haben

- **`core` ohne JUCE.** Die Musiklogik ist reine Standardbibliothek und deshalb schnell und deterministisch testbar.
  Genau dieser Teil wäre für ein anderes Produkt (siehe Abschnitt 7) wiederverwendbar.
- **Echtzeit-Regeln als harte Regeln** in `CLAUDE.md`: nichts im Audio-Thread allozieren oder sperren, Parameter nur
  vom Message-Thread setzen, jedes Note-On braucht ein Note-Off.
- **Nur unveränderliche Patterns zwischen Instanzen,** nie MIDI-Events. Das hat Hub und Voices einfach und robust
  gemacht; die Grenze zeigte sich erst beim Wunsch nach Befehlen vom Hub zur Voice (es gibt bewusst keinen solchen Kanal).
- **Formate nur über `HostCapabilities`** und `MIDIMAID_FORMATS`; State und Parameter-IDs sind formatunabhängig.
- **Texte nur über Tabellen** (`de.json`, `en.json`, X-Makro in `TextKeys.h`), damit nichts fest im Code steht.
- **Wahrscheinlichkeiten per Hash** aus Seed, Noten-ID und Durchlauf statt laufendem Zufallszustand: gleiche Eingabe,
  gleiche Wiedergabe, auch im Offline-Render.

## 5. Arbeitsweise mit Claude Code

- **Eine Aufgabe, ein Branch, ein PR.** Reihenfolge: Plan, Umsetzung, Unit-Tests mit Mutationsprüfung, Doku
  (DECISIONS, ROADMAP, HOST-VERIFIKATION, HANDBUCH, SPEC-Markierungen), dann Push, PR und Merge.
- **Merge nur bei grüner CI** (PR-Lauf und Sanitizer-Lauf). Ein Skript (`finish2.sh`) bündelte Build, ctest, pluginval,
  Commit, PR, CI-Wartezeit und Merge.
- **Mutationsprüfung** als Zusatz zu Tests: Eine bewusst eingebaute Änderung muss mindestens einen Test brechen.
  Überlebende Mutationen führten mehrfach zu besseren Tests.
- **Entscheidungen so wenig wie möglich beim Menschen.** Claude machte Vorschläge, Markus übernahm sie meist unverändert
  („Ich will weniger entscheiden müssen“) und prüfte die Ergebnisse im Host.
- **Sprachen:** Code, Commits und PR-Texte englisch, Doku und Gespräch deutsch.
- **Host-Prüfungen bleiben Handarbeit.** Was nur im echten Host sichtbar ist, steht als H-Eintrag mit Sollverhalten.

## 6. Lehren

- **Tests im richtigen Fenster schreiben.** Die Stimmennamen (D-258 bis D-261) scheiterten dreimal nacheinander im Host,
  obwohl alle Tests grün waren: Sie liefen mit dem Hub-Fenster, der Fehler lag im Voice-Fenster und im gestoppten
  Transport. Künftig zuerst den Zustand testen, den der Nutzer wirklich sieht (Voice-Instanz, Transport gestoppt,
  Fenster neu geöffnet).
- **Gestoppter Transport ist ein eigener Zustand.** Vieles ist bei laufender Wiedergabe richtig und bei Stopp falsch,
  weil Hub und Voice dann verschiedene Quellen für „welcher Slot“ haben.
- **JUCE-Eigenheiten kosten Zeit:** `ComboBox::changeItemText` ändert den geschlossenen Feldtext nicht;
  `getSelectedId()` liefert 0, sobald Text und Eintrag auseinanderlaufen; Klicks sind asynchron.
- **Wartebefehle im Hintergrund melden nicht zuverlässig.** Das Ende einer langen Pipeline muss aktiv geprüft werden.
- **Spezifikation vor Code zahlt sich aus.** Die Stufen v1.0, v1.1, Backlog und Gestrichen haben Umfang und Streit
  klein gehalten.

## 7. Zustand zum Abschluss

- **Fertig und gemergt:** alles aus Phase 0 bis 6 (v1.0) sowie die v1.1-Blöcke bis zur Oberflächenüberarbeitung
  und den Korrekturen D-259 bis D-261. `main` ist grün (CI und Sanitizer).
- **Von Markus zu tun:** Host-Prüfungen H61 bis H104 in Live (Logic später); vor einem Release `pricing.json` prüfen,
  `en.json` von einer Muttersprachlerin oder einem Muttersprachler gegenlesen lassen, Plug-in-Codes festlegen (O-14);
  Version von `0.0.1` auf z. B. `1.0.0-rc1` anheben, wenn die Host-Tests durch sind.
- **Bewusst offen (ROADMAP):**
  - Formate: CLAP, Linux (VST3 und LV2), AAX, AUv3.
  - Phase 7: Lizenzprüfung JUCE und VST3-SDK, Signierung und Notarisierung, Installer, Aktivierungssystem, Website,
    Preise, Support.
  - v1.2-Kandidat: Modulation (generierte CC-Spur pro Stimme).
  - Außerdem die Session-View-Anbindung (Live API), ein optionaler Provider „Claude Code (lokal)“ und negative
    Signale aus 1- bis 2-Sterne-Bewertungen.
- **Backlog:** Sub-Bass-Stimme, Plugin-Hosting, Auto-Evolve mit KI, Patterns bis 64 Takte, Gruppen A bis D,
  Kick-Grundton, Polymeter „frei laufend“, Kreativität und Energie als Automation.

## 8. Ideen für Version 2

1. **Hub-zu-Voice-Befehle.** Heute fließen nur unveränderliche Patterns vom Hub zur Voice und Aktionen von der Voice zum
   Hub. Wer Mute, Solo oder Fokus gruppenweit steuern will, braucht einen sauber begrenzten Befehlskanal; das ist die
   größte Architekturfrage für V2.
2. **Modulation und CC-Spuren** (D-69) als zweite Ausgabeebene neben den Noten.
3. **Weitere Formate und Plattformen:** CLAP, Linux, AAX; AUv3 ohne Hub-Kopplung.
4. **Lernen aus Bewertungen:** negative Signale und Gewichtung nach Aktualität, rückwirkende Favoriten.
5. **Hörtest-Serien** mit `mmgen` (siehe `HOERTEST.md`) als feste Regression der Musikqualität.
6. **Ableton-Steuerung als eigenes Projekt.** Ein MCP-Server, der Ableton über Claude Code bedient (Gegenstück zu
   SMYLZ Producer, ohne API-Schlüssel, nur mit Abo), ist ausdrücklich **nicht** Teil von MidiMaid. MidiMaid bleibt das
   Live-MIDI-Plug-in; `source/core/` ist die wiederverwendbare Musikbasis für dieses neue Projekt.
