# MidiMaid – Roadmap

Jede Phase endet mit einem lauffähigen, getesteten Stand. Abnahmekriterien sind verbindlich.
Claude Code hakt Aufgaben ab, sobald alle Kriterien aus `CLAUDE.md` („Arbeitsweise“) erfüllt sind.

**Release-Umfang (D-74, D-75):** Phasen 0–6 ergeben **v1.0** (alle Funktionen mit Bewertung 1 und 2 aus
`docs/FEATURES.md`). Danach folgt **v1.1** (Bewertung 3), dann **v1.2-Kandidaten**. Bewertung 4 ist in
**Backlog** und **Gestrichen** aufgeteilt.

---

## Phase 0 – Fundament und Host-Spike
- [ ] Entwicklungsumgebung auf Mac und Windows prüfen, Versionen (OS, Live, Logic, Compiler) in
      `DECISIONS.md` dokumentieren (CLAUDE.md „Entwicklungsumgebung“)
- [x] Repo-Struktur nach `CLAUDE.md`, `.clang-format`, `.gitignore`, `.claude/settings.json`
- [x] CMake mit CPM: JUCE 8 (gepinnt), Catch2, nlohmann/json
- [x] `CMakePresets.json` (macos/windows/linux × debug/release), Warnungen als Fehler
- [x] Formatliste per `MIDIMAID_FORMATS` (macOS `VST3;AU;Standalone`, Windows/Linux `VST3;Standalone`),
      IDs aller Zielformate eingetragen (SPEC §2.2, §2.4)
- [x] Plugin-Targets: Instrument (VST3, AU, Standalone) und MIDI-FX (AU `aumi`), gemeinsame Kern-Bibliothek
- [x] `HostCapabilities`-Schicht (Format, Host, Fähigkeiten) mit Tests
- [x] Minimal-Plugin spielt ein fest codiertes 1-Takt-Pattern synchron zum Transport
- [x] Drag & Drop eines fest codierten .mid aus dem Plugin
- [x] GitHub-Actions-CI: macOS und Windows (Build, Tests, pluginval), Linux (core-Tests, VST3-Kompiliertest)
- [ ] **Host-Verifikation** (manuell, Ergebnisse in `DECISIONS.md`):
  - [ ] Live 12 macOS VST3 und AU, Windows VST3: MIDI From → MidiMaid-Spur. **Bestätigen**, dass Live beim
        Spur-Routing keine Kanäle trennt (Grundlage für D-55)
  - [ ] Logic: MIDI-FX-Slot, Sync, Loop, Stop
  - [ ] Zwei Instanzen teilen sich eine prozessweite Registry (Logic, Live)
  - [ ] Logic: Hub + Voice auf zwei Synth-Spuren, Aufnahme per „Record MIDI to Track Here“
  - [ ] Live: Hub + Voice auf zwei MidiMaid-Spuren, zwei Synth-Spuren per „MIDI From“, Monitoring „In“, Aufnahme
  - [ ] Geplanter Wechsel: Hub und Voice wechseln zum selben PPQ, auch bei Klick 50 ms vor der Taktgrenze
  - [ ] Drag & Drop aus dem Plugin in Live und Logic, Tempo-Event wird übernommen
  - [ ] Clip (Live) bzw. Region (Logic) direkt ins Plugin-Fenster ziehen: kommt eine .mid-Datei an?
  - [ ] Parameter-Automation (Slot), Start mitten im Arrangement mit automatisiertem Slot
  - [ ] Live: Freeze einer Synth-Spur, die nur über „MIDI From“ gespielt wird (Erwartung: still)
  - [ ] Welche Tasten Live und Logic an das Plugin-Fenster weiterleiten (Planung der Kürzel für v1.1)
  - [ ] Referenz-Instrumente: Legato-Glide für Slides und Velocity-Reaktion der Kandidaten prüfen,
        Auswahl pro Rolle festlegen (SPEC §12)
  - [ ] Versatz zwischen Spuren messen: Wie weit rechnet eine Voice dem Hub voraus oder hinterher? (Logic mit
        ausgewählter und nicht ausgewählter Spur, Live, Offline-Bounce; Grundlage für Mindestvorlauf und O-22)
  - [ ] Einzelspur-Bounce bzw. Freeze einer Voice-Spur mit Slot-Automation im Hub (Logic „Bounce in Place“,
        Freeze): Läuft der Hub mit, folgt die Voice? (O-22)
  - [ ] Nicht aktive Parameter: Wie zeigen Live und Logic als nicht automatisierbar markierte Parameter an?
        (SPEC §3.13)
  - [ ] Versionen von CI-Toolchains und pluginval in `DECISIONS.md` (SPEC §12)

**Host-Annahmen und Ersatzwege.** Fällt eine Prüfung negativ aus, gilt der Ersatzweg; Umfangsänderungen
brauchen eine neue Entscheidung, bevor Phase 1b beginnt.

| Annahme | Ersatzweg bei negativem Ergebnis |
|---|---|
| Live trennt beim Spur-Routing keine Kanäle (D-55) | Keiner nötig, eine Stimme pro Instanz funktioniert in beiden Fällen |
| Logic lädt den MIDI-FX `aumi`, Sync, Loop und Stop laufen sauber | Logic-Unterstützung zurückstellen, neue Entscheidung zum Umfang |
| Instanzen einer DAW teilen eine prozessweite Registry | Keine Kopplung in diesem Host: Solo-Instanzen und Hinweis „Kopplung nicht verfügbar“; Kopplung per Prozess-Kommunikation nur per neuer Entscheidung |
| Geplanter Wechsel trifft bei 150 ms Vorlauf in allen Instanzen denselben PPQ | Standardwert des Mindestvorlaufs anhand der Messung erhöhen (D-90) |
| Slot-Automation im Hub erreicht Voices rechtzeitig, auch bei Bounce und Freeze | Entscheidung O-22 (Automation vorziehen oder Slot-Automation pro Spur) |
| Drag & Drop aus dem Plugin in die DAW | Export in einen Ordner mit „Im Finder/Explorer zeigen“, Aufnahme-Workflow |
| Clip bzw. Region lässt sich ins Plugin ziehen | Import über Dateidialog und Drag aus Finder/Explorer; in Logic vorher als .mid exportieren (Setup-Hilfe) |
| „Record MIDI to Track Here“ nimmt die Ausgabe hinter MidiMaid auf | Drag & Drop als einziger Weg in Logic, Hinweis in der Setup-Hilfe |
| Freeze einer nur über „MIDI From“ gespielten Spur ist still (Erwartung) | Setup-Hilfe „erst aufnehmen, dann einfrieren“ (bereits geplant) |
| Referenz-Instrumente gleiten im Legato-Modus | anderes Live- bzw. Logic-eigenes Instrument je Rolle |

**Abnahme:** Beide DAWs spielen das Test-Pattern taktgenau, ohne hängende Noten bei Stop und Loop.
pluginval und auval sind grün. Die Host-Ergebnisse sind dokumentiert, für negative Ergebnisse ist der
Ersatzweg beschlossen.

## Phase 1a – Musikalischer Kern (offline)
- [x] Eigener PRNG (PCG32) und Verteilungsfunktionen, plattformübergreifend identische Tests (SPEC §4.3)
- [x] Musiktheorie: Tonhöhenklassen, Skalen (inkl. Moll-Pentatonik, Phrygisch-Dominant), Akkorde,
      Stufen mit Alteration → MIDI, Akkord-Skalen-Prinzip
- [x] Pattern-Datenmodell (SPEC §5) mit Noten-IDs, `voices`-Liste und den für v1.1 reservierten Feldern,
      Serialisierung (JSON) mit `stateVersion`
- [x] Constraint-Schicht (SPEC §4.2): Skala/Akkord-Skala, Register, Kick-Aussparung, Kick-Freiraum
      (schlägt Slide), Intervall- und Registerregeln, eigene Kanäle pro Stimme, mit vollständigen Tests
- [x] Ausgabestufe in fester Reihenfolge (SPEC §4.2a, v1.0-Stufen) mit Tests für jede Kombination
- [x] Register in MIDI-Nummern, Oktav-Offset (STYLES.md §1.6)
- [x] Velocity-Konturen, Akzent als Flag, Akzent-Velocity (STYLES.md §1.9)
- [x] Voicing-Engine: enge Lage, Stimmführung (STYLES.md §1.10)
- [x] Motiv-Engine: Motiv zuerst, Wiederholung vor Variation, Durchgangstöne, Auftakte (STYLES.md §1.11)
- [x] Qualitätsbewertung: harte und weiche Kriterien, Mindestbewertung aus dem Profil, 8 Kandidaten,
      weitere Runden und Fehlschlag-Regel, Gewinner-Seed (SPEC §4.3, §4.4, STYLES.md §1.14)
- [x] Kopierschutz-Metrik mit Regel „nur markante Einträge“, mit Grenzfall-Tests (SPEC §3.20)
- [x] Akkordsymbole: Parser und Darstellung nach STYLES.md §1.17
- [x] Stilprofile als JSON nach `docs/STYLES.md` inklusive Loader und Validierung
- [x] Archetyp-Schnittstelle und alle v1.0-Archetypen aus STYLES.md (ohne `polymeter_seq`)
- [x] Gewichtete Auto-Auswahl und manuelle Übersteuerung pro Stimme
- [x] Kick-Raster und Bass-Aussparung (inkl. Ausnahme `long_tied`)
- [x] Mehrstimmige Melodie (Akkordfarben je Stil)
- [x] Chromatik nach Stil-Standardwerten (SPEC §3.8)
- [x] Energie als Makro, Kreativität (SPEC §3.3, §7.6)
- [x] Swing pro Stimme, Groove-Anwendung in der Ausgabestufe (SPEC §3.9)
- [x] Harmonischer Kontext und Progressions-Generator
- [x] Bass- und Melodie-Generator (aufeinander abgestimmt), deterministisch über Seed
- [x] Patterns 1/2/4/8/16 Takte, Phrasen und Formplan ab 8 Takten (SPEC §3.7)
- [x] Golden-File-Tests pro Stil; Eigenschaftstests über Seed-Serien (SPEC §12); Tests mit 3 Test-Stimmen
      (offene Stimmenzahl)
- [x] Hörtest-Werkzeug `tools/mmgen`: .mid pro Stil, Archetyp, Energie, Seed; Serien-Modus (SPEC §12)
- [ ] **Hörtest-Session** mit dem Entwickler nach dem Protokoll in SPEC §12: `mmgen`-Serien in Live mit den
      Referenz-Instrumenten abhören, Gewichte, Bereiche, Bewertungsgewichte und Mindestbewertung in
      STYLES.md anpassen
- [ ] **Blindvergleich** gegen die Live-12-Generatoren (Seed, Shape) bei gleicher Tonart

**Abnahme 1a:** Offline entstehen stiltypische, abgestimmte Patterns. Gleicher Seed ergibt auf allen
Plattformen dasselbe Ergebnis. Alle Constraint- und Ausgabestufen-Tests sind grün.

## Phase 1b – Engine, Slots, Hub & Voices
- [ ] **Vorab:** Transport- und Wechsel-Zustandsautomat mit testbaren Beispielen in SPEC §6.1 ergänzen und
      O-22 entscheiden (auf Basis der Messungen aus Phase 0)
- [ ] Engine: PatternPlayer, Echtzeit-Vertrag (ausstehende Wechsel je Art, Rückgabe-Queue mit Überlaufregel,
      reservierter MIDI-Puffer, Bearbeitungen ohne Quantisierung), Note-Off-Tabelle, quantisierter Wechsel
      mit PPQ-Zeitstempel und Mindestvorlauf, Neustart an Taktgrenzen (SPEC §3.4, §6)
- [ ] Engine-Tests mit simulierten Playheads (inkl. Wechsel kurz vor Quantisierungspunkt, Sprünge, fehlende
      Host-Daten, volle Rückgabe-Queue)
- [ ] 16 Pattern-Slots, Slot sofort bei Start und nach Sprung, Vorrangregeln (SPEC §3.10, §3.13, §6.8)
- [ ] Hub & Voices: Rollen Hub/Voice/Solo, Ausgabemodus eine Stimme/keine, Registry, lock-freier Kanal
      für geplante Wechsel, eine Gruppe, Persistenz des kompletten Slot-Satzes, Hub-Verlust (SPEC §6.5)
- [ ] Voice-Oberfläche (SPEC §8.1)
- [ ] Alle Host-Parameter nach dem Parameter-Register mit fester ID anlegen; aktiv: Slot, Mute 1–8;
      nicht aktive als nicht automatisierbar (SPEC §3.13)
- [ ] Einfache Hub-UI: Stil, Tonart, Skala, Takte, Seed, Generieren, Anzeige (nur lesend), Drag & Drop pro Stimme

**Abnahme 1b:** In beiden DAWs laufen Hub und Voice taktgenau synchron, auch bei Slot-Wechseln
kurz vor der Taktgrenze und beim Start mitten im Arrangement. Projekte mit und ohne Hub öffnen korrekt.

## Phase 2 – Bearbeitung und Variationen
- [ ] Piano-Roll (eine Spur pro Stimme): Setzen, Löschen, Verschieben, Länge, Velocity-Spur, Slide, Akzent
- [ ] Raster, Skalen-Einrasten, Undo/Redo
- [ ] Spurfokus (alle / einzelne Stimme)
- [ ] Stimme sperren (SPEC §3.5)
- [ ] Variations-Engine mit subtilen und strukturellen Operatoren und Stärke-Regler (SPEC §3.6)
- [ ] Verlauf (20 Ergebnisse pro Slot) und Undo-Stapel nach SPEC §3.6
- [ ] Schaltflächen für alle Aktionen (Rückgängig, Wiederholen, Duplizieren, Generieren, Variation)
- [ ] Oberfläche in zwei Ebenen: Basis und Experte (SPEC §8.1), Starttonart
- [ ] Übersetzungs-Infrastruktur mit deutscher Sprachdatei

**Abnahme:** Alle Edits sind rückgängig zu machen. Gesperrte Stimmen bleiben bei Variationen erhalten.
Keine Klicks oder hängenden Noten beim Bearbeiten während der Wiedergabe.

## Phase 3 – KI-Anbindung
- [ ] Provider-Schnittstelle, Abbruch inklusive Netzwerkverbindung, Warten beim Schließen, Timeouts,
      Backoff (SPEC §7.1)
- [ ] Anthropic-Provider (native API, Structured Outputs)
- [ ] OpenAI-kompatibler Provider mit Voreinstellungen (OpenAI, Ollama, LM Studio, OpenRouter) und
      Fähigkeitsstufe pro Backend; Verbindungstest stuft nur herab (SPEC §7.2)
- [ ] Antwort-Schema v1 (v1.0-Felder, Stufen, Oktave, Progressionen, Motive bei über 8 Takten), Parser und
      Validierung, ein Reparaturversuch, Nachfrage bei Fehlschlag (SPEC §7.3)
- [ ] Zustimmung pro Cloud-Provider für Referenzen und importierte Stimmen; Logging ohne Prompts und
      Nutzerinhalte, Option „Prompts protokollieren“ (SPEC §3.20, §10)
- [ ] Prompt-Vorlagen v1 (Generieren, Verfeinern) mit Stilregeln, Motivik und Anti-Klischee-Liste
- [ ] KI-Ergebnisse durch Constraint-Schicht und Qualitätsbewertung, Anzeige bei Unterschreitung
- [ ] Verfeinern per Prompt mit Kontext, Noten-IDs und Verlauf (SPEC §3.15)
- [ ] Keychain-Wrapper macOS und Windows
- [ ] Einstellungsdialog inklusive Verbindungstest; `models.json` mit Empfehlungen
- [ ] Kreativitäts-Regler: Abbildung auf Temperatur, Prompt und Algorithmus (SPEC §7.6)
- [ ] Fehlerverhalten nach SPEC §7.9: Dialog, nicht blockierende Leiste während der Wiedergabe
- [ ] Mock-Provider und Tests mit kaputten Antworten

**Abnahme:** Textprompt und Verfeinern liefern über Anthropic, OpenAI und Ollama gültige Patterns.
Ungültige Antworten führen nie zu unbrauchbarem MIDI. Ohne Netz erscheint eine verständliche Meldung
mit Wahlmöglichkeit, und die Wiedergabe läuft ungestört weiter.

## Phase 4 – Lenkung: Stimme importieren, Drum-Referenz, Referenzen
- [ ] MIDI aus der DAW (Clip/Region) und .mid annehmen (Drag-Ziel im Hub und in Voices)
- [ ] Analyse: Tonart- und Skalenerkennung, Akkordfolge aus Akkorden bzw. Bass-Grundtönen, Rhythmus,
      Register; tonartunabhängige Speicherung; Testkorpus mit Zielwerten (SPEC §3.20, §12)
- [ ] Eigene Stimme importieren: Ersetzen, Sperre, harmonischer Kontext, übrige Stimmen passend
      generieren, „Sperre lösen“, Grenzen (nur 4/4, Länge, höchstens 16 Takte) (SPEC §3.18)
- [ ] Drum-Referenz per Clip/.mid: GM-Zuordnung plus Liste in den Einstellungen, `custom`-Kick-Raster,
      Hat-Groove, Akzente; kein doppelter Swing (SPEC §3.14, §3.9)
- [ ] Referenzen v1.0: ein Set pro Stil, Massen-Import (Dateien, Ordner, Clips), Rolle beim Import
      zuordnen, Hintergrund-Analyse mit Cache, Ansicht „Referenzen“ (Gewicht, an/aus)
- [ ] Referenz-Progressionen im Progressions-Generator, KI-Beispiele aus dem Set, Kopierschutz,
      Set-ID im Pattern, Datenschutz-Schalter

**Abnahme:** Eine importierte eigene Bassline bleibt unverändert, die generierte Melodie passt harmonisch
dazu. Mit Drum-Referenz spart der Bass die echten Kicks aus und übernimmt den Hat-Groove. Mit einem Set
eigener Referenzen tauchen typische eigene Progressionen auf, ohne dass eine Referenz kopiert wird.

## Phase 5 – Bibliothek und Persistenz
- [ ] Plugin-State mit Versionierung und Migration (SPEC §9.1)
- [ ] Bibliothek: Speichern, Laden, Import und Export .mid
- [ ] Atomares Schreiben, Index

**Abnahme:** DAW-Projekte stellen den Zustand exakt wieder her. Die Bibliothek übersteht Abstürze ohne
Datenverlust. Ein gespeichertes Pattern bleibt nach Änderungen am Referenz-Set reproduzierbar.

## Phase 6 – Feinschliff, Vorlagen, v1.0-Abschluss
- [ ] UI-Polish, Skalierung, HiDPI, Theme
- [ ] Performance-Messung gegen SPEC §11
- [ ] Instrument-Presets pro Rolle (Live und Logic, nur eigene Instrumente, SPEC §12)
- [ ] DAW-Vorlagen: Live-Set mit Gruppenspuren, Logic-Patches (SPEC §8.6)
- [ ] Setup-Hilfe: Routing, Monitoring, Aufnahme, Freeze, Arrangement (SPEC §6.5)
- [ ] Nutzerhandbuch (kurz)
- [ ] Vollständige manuelle Testmatrix (Anhang, v1.0-Zeilen)
- [ ] Determinismus-Gesamttest: Pattern speichern, Referenz-Set ändern, Projekt neu laden, Ausgabe
      bitgenau vergleichen (FEATURES.md W7)

**Abnahme v1.0:** Alle Phasen-Abnahmen erfüllt, Testmatrix v1.0 grün, eine Woche Einsatz in eigenen
Produktionen ohne Absturz und ohne hängende Noten.

---

## v1.1 – direkt nach v1.0 (Bewertung 3)
Reihenfolge nach Nutzen im Live-Einsatz. Jeder Block ist eine eigene kleine Phase mit Tests und Abnahme.

**Live-Performance**
- [ ] Evolve-Ebene (algorithmisch): Stufen, „immer vom Original“, Behalten, Zurück, Verwerfen, Parameter (SPEC §6.7)
- [ ] Live-Transponieren: Modi, Verhalten, Zeitpunkt, Ziel, Gruppen-Timing, Parameter + Tastatur-Offset (SPEC §3.16, §3.17)
      inkl. Host-Check: Monitoring „In“ auf der Hub-Spur, exklusives Scharfschalten
- [ ] Vorhören bei gestopptem Transport, auch für Voices (SPEC §3.11)
- [ ] Legato-Einsatz (SPEC §3.4)
- [ ] Trigger-Parameter Generieren/Variation (SPEC §3.13)
- [ ] Session-Ansicht: Slot per Clip-Hüllkurve, Beispiele in der Live-Vorlage

**Musik**
- [ ] Wahrscheinlichkeit und Bedingungen A:B inkl. Export aufgelöster Durchläufe (SPEC §3.19)
- [ ] Ratchets (STYLES.md §1.13)
- [ ] Polymeter-Sequenzen mit Neustart, Archetyp `polymeter_seq` (STYLES.md §1.3)
- [ ] Turnaround (STYLES.md §1.12)
- [ ] Chord-Memory (STYLES.md §1.10)
- [ ] Groove-Vorlagen-Sammlung (SPEC §3.9)
- [ ] Chromatik-Regler (SPEC §3.8)
- [ ] Wiederholungsschutz mit Gewinner-Seed (SPEC §4.3)

**Bearbeitung**
- [ ] Sperren pro Dimension und Note (SPEC §3.5)
- [ ] Piano-Roll-Hilfen: Scale-Fold, Slide/Akzent-Zeile, Wahrscheinlichkeits-Spur, Hintergründe
- [ ] Tastenkürzel nach SPEC §8.3

**Referenzen und Lenkung**
- [ ] Mehrere benannte Sets pro Stil, automatisches Set „Favoriten“
- [ ] Automatische Rollen-Erkennung mit Konfidenz
- [ ] Motiv-Zellen, Rhythmus-Statistik, Profil pro Set, Regler „Persönlich“, Kriterium „Nähe zum Set“,
      Profil-Schnappschuss (SPEC §3.20)
- [ ] Melodie „mit/gegen Hats“ (SPEC §3.14)

**Komfort**
- [ ] Bibliothek: Tags, BPM, Bewertung, Suche, Filter, Vorhören
- [ ] Englische Sprachdatei, Sprachumschaltung
- [ ] Notennamen-Konvention wählbar
- [ ] Token- und Kostenanzeige, `pricing.json`
- [ ] Warnung bei gemischten Formaten in einer Gruppe

## Formate nach v1.0 (Architektur vorbereitet, D-81)
- [ ] CLAP (Erweiterung oder JUCE 9 nativ), ggf. Note-Effekt-Variante für Bitwig/Reaper
- [ ] Linux: VST3 und LV2, Test in Reaper/Bitwig, Drag & Drop unter X11/Wayland
- [ ] AAX (Avid-Programm, PACE-Signierung, MIDI-Ausgabe in Pro Tools vorher prüfen)
- [ ] AUv3 optional (ohne Hub-&-Voices-Kopplung)

## Phase 7 – Kommerzialisierung (später)
- [ ] Lizenzprüfung JUCE und VST3-SDK
- [ ] Signierung, Notarisierung, Installer
- [ ] Lizenz- und Aktivierungssystem
- [ ] Website, Preise, Support

## v1.2-Kandidaten
- [ ] **Modulation:** generierte CC-Spur pro Stimme (z. B. Filter, Decay), Export als CC-Daten (D-69)

## Backlog – später prüfen (siehe FEATURES.md)
Sub-Bass-Stimme · Plugin-Hosting · Auto-Evolve mit KI · Patterns bis 64 Takte ·
Gruppen A–D · Kick-Grundton · Polymeter „frei laufend“ · Kreativität/Energie als aktive Automation ·
native OpenAI-/Ollama-Provider (nur falls Phase 3 es erfordert)

## Gestrichen (Begründungen in FEATURES.md)
Dichte-Korrektur · kombinierter Export · Offline-Verfeinern · Struktur-Modus · MIDI-Vorlage als eigenes
Konzept · Drum- und Vorlagen-Capture, „Lernen“ · Max-for-Live-Gerät/Mehrkanal-Variante · Monatskosten ·
Euklidische Rhythmen als Feature

---

## Anhang: Manuelle Testmatrix

| Test | Release | Live 12 macOS | Live 12 Win | Logic |
|---|---|---|---|---|
| Laden und Entfernen, auch Entfernen während der Wiedergabe | v1.0 | | | |
| Sync bei Start mitten im Takt | v1.0 | | | |
| Loop-Bereich, Rücksprung | v1.0 | | | |
| Tempowechsel und Automation | v1.0 | | | |
| Stop → keine hängenden Noten | v1.0 | | | |
| Pattern-Wechsel während der Wiedergabe | v1.0 | | | |
| Drag & Drop pro Stimme (Hub und Voice) | v1.0 | | | |
| Projekt speichern und neu öffnen | v1.0 | | | |
| Hub & Voices: Wechsel synchron, Hub gelöscht, Projekt ohne Hub geöffnet | v1.0 | | | |
| Slot-Wechsel 50 ms vor Taktgrenze: alle Instanzen gleichzeitig | v1.0 | | | |
| Start bei Takt 37 mit Slot-Automation | v1.0 | | | |
| Offline-Rendern / Bounce mit Slot-Automation | v1.0 | | | |
| Einzelspur-Bounce bzw. Freeze einer Voice mit Slot-Automation (nach O-22) | v1.0 | | | |
| Piano-Roll-Bearbeitung während der Wiedergabe: kein Versatz, keine hängenden Noten | v1.0 | | | |
| Bypass während der Wiedergabe | v1.0 | | | |
| Slides über Loop-Grenze, Synth im Legato-Modus | v1.0 | | | |
| Groove + Slide + Kick-Freiraum kombiniert: keine Lücken, keine Kick-Überlappung | v1.0 | | | |
| Eigene Bassline importiert, Melodie generiert | v1.0 | | | |
| Drum-Clip als Referenz: Bass weicht echten Kicks aus | v1.0 | | | |
| Referenz-Set: eigene Progressionen hörbar, keine Kopie | v1.0 | | | |
| Verfeinern per Prompt, danach Rückgängig | v1.0 | | | |
| Netzwerk weg während KI-Anfrage | v1.0 | | | |
| Freeze/Aufnahme-Workflow laut Setup-Hilfe | v1.0 | | | |
| DAW-Vorlagen laden und sofort spielen | v1.0 | | | |
| Evolve-Ebene: Behalten, Zurück, Verwerfen; Slot unverändert; Bounce spielt Slot | v1.1 | | | |
| Live-Transponieren per Keyboard, auch während Slides | v1.1 | | | |
| Vorhören bei Stop, Übergabe an Transport-Start | v1.1 | | | |
| Wahrscheinlichkeiten: gleiche Song-Position klingt gleich, Bounce = Wiedergabe | v1.1 | | | |
| Referenz-Set wechseln: hörbar anderer Charakter | v1.1 | | | |
