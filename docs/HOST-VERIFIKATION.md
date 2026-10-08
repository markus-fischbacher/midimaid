# Checkliste Host-Verifikation (Phase 0)

> Zweck: Die manuellen Prüfungen aus `docs/ROADMAP.md` („Host-Verifikation“) so durchführen, dass die Ergebnisse
> direkt in `docs/DECISIONS.md` übernommen werden können. Zu O-22 und O-24 siehe dort.

## Stand des Plugins

Das Plugin ist der Phase-0-Spike: ein fest codiertes 1-Takt-Pattern synchron zum Transport, Drag & Drop eines
`.mid` über den Griff unten im Fenster. **Es gibt noch keinen Hub, keine Voices, keine Parameter und kein Log.**
Daher gilt:

- **Jetzt prüfbar:** Teil A (Vorbereitung), B (Live), C (Logic), D (Drag & Drop), E (Vorzähler, O-24), F (Versatz
  zwischen zwei unabhängigen Instanzen).
- **Erst mit späteren Phasen prüfbar** (Teil G): alles, was Hub, Voice, Registry, Slot-Parameter oder Recording
  über den Hub braucht. Die Punkte bleiben in der Roadmap offen.

## A. Vorbereitung

- [x] Plugins gebaut und installiert (VST3, AU `aumu`, AU MIDI-FX `aumi`), AU-Cache geleert
      (`killall -9 AudioComponentRegistrar`), DAW neu gestartet
- [x] Build-Typ notieren (Debug oder Release), Commit-Hash notieren (`git rev-parse --short HEAD`) - Debug, 81e826d
- [x] Versionen notieren: macOS, Live, Logic, Xcode, CMake; später Windows und Visual Studio
  - macOS 27.0 (Build 26A428), Live 12.4.6 (Ableton Live 12 Suite), Logic Pro 12.4
  - Kein Xcode installiert, nur Command Line Tools 27.0.0.0.1788430756 (macOS-SDK 27.0)
  - Apple clang 21.0.0 (clang-2100.3.34.2, arm64-apple-darwin27.0.0), CMake 4.4.4, Ninja 1.13.2, gh 2.102.0
  - pluginval 1.0.4 (lokal und in der CI gepinnt), CI-Runner `macos-15` (Xcode-Version im CI-Log, Schritt „xcodebuild -version“)
  - Windows und Visual Studio: noch offen
- [x] Beide DAWs: Das Plugin erscheint in der Plugin-Liste (Hersteller „Klirrwerk“)

## B. Live 12 (macOS: VST3 und AU; Windows: VST3)

Je Format einmal durchgehen und ankreuzen. Ergebnis: ✔ / ✘ plus Notiz.

**Was „im Takt“ heißt (B2, B4, C2, C4, C6):** Das feste Pattern ist ein Offbeat-Bass (D-95). Die vier Noten pro Takt
liegen auf den 16tel-Schritten 2, 6, 10 und 14, also genau zwischen den Zählzeiten („1 und 2 und 3 und 4 und“). Es liegt
**nie** eine Note auf 1, 2, 3 oder 4. In der Positionsanzeige der DAW stehen sie auf `x.1.3`, `x.2.3`, `x.3.3` und `x.4.3`
(Takt.Zählzeit.16tel); das „3“ ist die dritte 16tel der Zählzeit, nicht Zählzeit 3. Prüfung: Metronom einschalten, ab
Takt 1 spielen. Im Takt ist das Pattern, wenn jede Note exakt in der Mitte zwischen zwei Metronomschlägen liegt, auch nach
Loop-Sprung und Start mitten im Arrangement. Fällt eine Note auf einen Schlag, ist das Pattern um eine halbe Zählzeit oder
mehr verschoben. Dann bitte notieren, um wie viel.

| Nr. | Prüfung | Erwartung | VST3 | AU | Win VST3 | Bemerkung |
|---|---|---|---|---|---|---|
| B1 | Neue MIDI-Spur, MidiMaid als Instrument laden, Fenster öffnet | Fenster mit Namen und Griff | ✔ | ✔ | | |
| B2 | Play | Pattern startet exakt mit dem Takt | ✔ | entfällt | | |
| B3 | Stop | Alle Noten enden sofort, keine hängenden Töne | ✔ | entfällt | | |
| B4 | Loop im Arrangement ein, Sprung am Loop-Ende | Kein hängender Ton, Pattern bleibt im Takt | ✔ | entfällt | | |
| B5 | Tempoänderung während der Wiedergabe | Pattern folgt, keine Aussetzer |✔| entfällt| | |
| B6 | Zweite Spur mit Synth, Eingang „MIDI From“ = MidiMaid-Spur, Monitoring „In“ | Der Synth spielt das Pattern | ✔ | ✘ | | AU: Das Plugin erscheint in „MIDI From“ nicht, nur Pre FX und Post FX (vermutlich Hostgrenze: Live liest das MIDI-Ausgangssignal von AU-Plugins nicht aus). In Live daher VST3. Beim VST3 in der zweiten Zeile von „MIDI From“ das Plugin wählen, Spur 2 auf Monitor „In“ |
| B7 | In der MidiMaid-Spur aufnehmen (Synth-Spur scharf) | Aufgenommene Noten stimmen mit der Wiedergabe überein | ✔ | entfällt| | Spur 2 muss scharf sein |
| B8 | **D-55:** Kanäle trennen beim Routing? (Einzelne MIDI-Kanäle im „MIDI From“-Menü wählen, sofern das Plugin mehrere Kanäle sendet; sonst notieren: „nur ein Kanal, nicht prüfbar“) | Notieren, ob Live Kanäle trennt | - |entfällt | |nur ein Kanal, nicht prüfbar|
| B9 | Plugin-Fenster schließen, Set speichern, neu öffnen | Plugin lädt ohne Fehler | ✔ | entfällt| | |
| B10 | Tasten: Leertaste, Cmd+Z, Pfeile und Buchstaben bei fokussiertem Plugin-Fenster | Notieren, welche Tasten Live ans Plugin weitergibt. Keine Folgeprüfung nötig: MidiMaid belegt keine Kürzel vorab (D-126, SPEC §8.3) | entfällt | entfällt | entfällt | Erst prüfen, wenn Tasten belegbar sind (v1.1, D-126), siehe Teil G |

## C. Logic Pro

MidiMaid als **MIDI-FX** (`MidiMaid MIDI`, MIDI-FX-Slot einer Instrumentenspur) und als **Instrument** prüfen.

| Nr. | Prüfung | Erwartung | MIDI-FX | Instrument | Bemerkung |
|---|---|---|---|---|---|
| C1 | `MidiMaid MIDI` im MIDI-FX-Slot laden, Synth auf derselben Spur | Plugin erscheint im Slot, lädt ohne Fehlermeldung | ✔| – | |
| C2 | Play | Pattern startet im Takt | ✔| | |
| C3 | Stop | Keine hängenden Noten |✔ | | |
| C4 | Zyklus (Loop) ein, Sprung am Zyklusende | Kein hängender Ton, Pattern im Takt |✔ | | |
| C5 | Tempoänderung, Positionssprung per Lineal | Pattern folgt |✔ | | |
| C6 | Start mitten im Arrangement | Pattern an der richtigen Taktposition | ✔ | | |
| C7 | „Record MIDI to Track Here“ auf einer Synth-Spur, die von MidiMaid gespielt wird | Notiz: Nimmt Logic die Ausgabe **hinter** MidiMaid auf? (Ersatzweg laut Roadmap: nur Drag & Drop) | ✔ | | Logic nimmt die Ausgabe hinter MidiMaid auf, wenn „Record MIDI to Track Here“ aktiviert ist. Ohne diese Einstellung entsteht keine Region (R rot allein reicht nicht) |
| C8 | Tasten ans Plugin-Fenster (wie B10) | Notieren; keine Folgeprüfung nötig (D-126) | entfällt | entfällt | Erst prüfen, wenn Tasten belegbar sind (v1.1, D-126), siehe Teil G |
| C9 | `auval -v aumi Mdmf Klrw` und `auval -v aumu Mdmi Klrw` | Beide „AU VALIDATION SUCCEEDED“ | ✔| | |

## D. Drag & Drop (Live und Logic)

| Nr. | Prüfung | Erwartung | Live | Logic | Bemerkung |
|---|---|---|---|---|---|
| D1 | Griff aus dem Plugin auf eine MIDI-Spur ziehen | Clip bzw. Region mit dem Pattern entsteht | ✔|✔ | |
| D2 | Tempo-Event der Datei | Notieren: Wird das Tempo übernommen oder ignoriert? Passt die Länge bei abweichendem Projekttempo? | ✔ | ✔ | In Live und Logic ändert sich das Tempo des Projekts nicht, egal ob bei der Rückfrage „Übernehmen“ Ja oder Nein gewählt wird (Live fragt nach Tempo und Taktart). Getestet bei Projekttempo 140 (Fenster 2 s offen, dann gezogen). Die Datei trägt das aktuelle DAW-Tempo (D-96), deshalb ist das Ergebnis unkritisch |
| D3 | Clip bzw. Region **ins** Plugin-Fenster ziehen | Kommt eine `.mid` im Plugin an? (Im Spike noch ohne Import: Notieren, was der Host beim Ziehen anbietet; Cursor, Verbotszeichen?) | – | – | Verschoben nach Teil G: erst mit Import im Plugin prüfbar. Beobachtet in Live und Logic: Es kann nichts ins Plugin-Fenster gezogen werden, der gezogene Clip bleibt in der DAW. Der Pointer wechselt auf den Standardpointer, sobald er ins Plugin-Fenster eintritt (erwartbar, der Spike hat kein Ziel für abgelegte Dateien) |

## E. Vorzähler und negative PPQ (O-24)

Vor D-128 spielte die Engine bei negativen Positionen nach derselben Rechnung (Position modulo Länge). Seither bleibt sie dort still.

| Nr. | Prüfung | Erwartung | Live | Logic | Bemerkung |
|---|---|---|---|---|---|
| E1 | Count-in (1 bzw. 2 Takte) einschalten, Aufnahme starten | Notieren: Spielt MidiMaid im Vorzähler? An welchen Positionen (modulo Länge, also phasenrichtig zum ersten Takt)? | nein | ja | Live: MidiMaid spielt erst nach dem Vorzähler. Logic: MidiMaid spielt schon während des Vorzählers |
| E2 | Aufnahme von einer MidiMaid-Spur: Landen Noten aus dem Vorzähler im Clip? | Notieren, ob das stört | nein | nein | Live: Es landen keine Noten aus dem Vorzähler im Clip. Logic: Mit „Record MIDI to Track Here“ landen keine Noten aus dem Vorzähler im Clip; der Aufnahmekopf ist während des Vorzählens noch nicht in der Spur. Mit rotem R allein entsteht keine Region (siehe C7) |
| E3 | Subjektives Urteil | Wunsch: Vorzähler **still** oder **spielend**? | still | still | Gewünscht: Vorzähler still, in Live und Logic. Live ist bereits so; in Logic muss die Engine bei negativer Position still sein (O-24) |

Folge: Ist „still“ gewünscht, wird die Engine so geändert, dass sie bei `ppq < 0` schweigt; die Entscheidung
steht danach als D-129 in `docs/DECISIONS.md` (O-24 entfällt).

## F. Versatz zwischen Spuren (Grundlage O-22 und Mindestvorlauf)

Zwei MidiMaid-Instanzen auf zwei Spuren, je ein Synth mit hartem Klick-Sound (kurzer Ton, schnelles Attack).
Beide Instanzen spielen dasselbe feste Pattern, so dass Versatz hörbar und im Bounce messbar ist.

| Nr. | Prüfung | Messung | Ergebnis |
|---|---|---|---|
| F1 | Logic, beide Spuren, eine **ausgewählt** | Offline-Bounce beider Spuren auf eine Datei, Wellenform in Samples vergleichen (Versatz der Anschläge) | Kein Versatz (nach Angabe, nicht in Samples vermessen), Puffergröße 128 Samples, Samplerate 44,1 kHz |
| F2 | Logic, die andere Spur ausgewählt | Wie F1 | Kein Versatz (nach Angabe, nicht in Samples vermessen), Puffergröße 128 Samples, Samplerate 44,1 kHz |
| F3 | Live, Echtzeit | Mitschnitt (Resampling-Spur) und Wellenform, Versatz in Samples | Kein Versatz: Beide Instanzen schlagen sample-genau gleichzeitig an (Spur 2 auf 50L, Spur 4 auf 50R; linker und rechter Kanal der Aufnahme sind in allen 354.944 Samples identisch, 16 Anschläge). Puffergröße 128 Samples, 44,1 kHz, 24 Bit |
| F4 | Live, Offline-Export | Wellenform, Versatz in Samples | Kein Versatz: Beide Instanzen schlagen sample-genau gleichzeitig an (linker und rechter Kanal des Exports sind in allen 352.800 Samples identisch, 16 Anschläge). 44,1 kHz, 24 Bit |
| F5 | Puffergröße 64, 256, 1024 Samples | F1 bis F4 wiederholen, mindestens einen Fall | Live, Puffergröße 1024 Samples, 44,1 kHz, 24 Bit (Datei `Test F3-1024.wav`): Kein Versatz, linker und rechter Kanal in allen 352.800 Samples identisch, 16 Anschläge. Logic mit anderer Puffergröße nicht getestet |

Zu notieren: Versatz in Samples und Millisekunden, Puffergröße, Samplerate, ob der Versatz konstant ist.
Ergebnis fließt in den Standardwert des Mindestvorlaufs (D-90) und in die Wahl der Variante bei O-22 ein.

## G. Erst mit späteren Phasen prüfbar (Roadmap-Punkte bleiben offen)

Diese Punkte brauchen Hub, Voices, Registry, Slot-Parameter oder Referenz-Instrumente. Hier nichts eintragen,
sondern in der Phase nachholen, in der das Feature entsteht.

- [ ] Zwei Instanzen teilen eine prozessweite Registry (Logic, Live)
- [ ] Hub plus Voice auf zwei Synth-Spuren: Logic („Record MIDI to Track Here“), Live („MIDI From“, Monitoring „In“)
- [ ] Geplanter Wechsel: Hub und Voice wechseln zum selben PPQ, auch bei Klick 50 ms vor der Taktgrenze
- [ ] Parameter-Automation (Slot), Start mitten im Arrangement mit automatisiertem Slot
- [ ] Freeze einer Synth-Spur, die nur über „MIDI From“ gespielt wird (Erwartung: still)
- [ ] Einzelspur-Bounce und Freeze einer Voice mit Slot-Automation im Hub (O-22)
- [ ] Nicht aktive Parameter in Live und Logic (SPEC §3.13): **jetzt prüfbar (D-138).** Neuen Build laden. Zeigen Live (VST3) und Logic (AU) die 15 nicht aktiven Parameter (u. a. „Transpose“, „Generate“, „Variation 1“ bis „Variation All“, „Evolve“, „Evolve Keep“, „Creativity“, „Energy“) in der Automationsliste, und lässt sich einer automatisieren? Und: Lässt sich „Slot“ automatisieren, wechselt das Pattern zum Taktstart, und schaltet „Mute 1“ den Bass stumm?
- [ ] Clip bzw. Region ins Plugin-Fenster ziehen (D3): kommt eine `.mid` an? Erst prüfbar, wenn das Plugin Dateien
      annimmt (Import, Phase 1); bis dahin entfällt D3
- [ ] Tastenbelegung: Wenn Tasten belegbar sind (v1.1, D-126), prüfen, welche Tasten Live, Logic und Windows-Hosts
      ans Plugin-Fenster weiterleiten und ob der Host zusätzlich reagiert (B10 und C8 entfallen bis dahin)
- [ ] Referenz-Instrumente: Legato-Glide und Velocity-Reaktion (kommt mit der Hörtest-Session, SPEC §12)

## Eintragsformat für `docs/DECISIONS.md`

Nach der Session meldest du mir die Tabellen (oder Fotos und Notizen). Ich trage sie ein:

```
- **D-xxx Host-Verifikation <Host>, <Datum>:** Setup (macOS x.y, <Host> x.y, Build <Typ> <Hash>, Puffer n).
  Ergebnis je Prüfung (B1 ✔, B2 ✔ …). Abweichungen und Ersatzweg laut ROADMAP „Host-Annahmen und Ersatzwege“.
  Folgen für O-22, O-24 und D-55.
```

Fällt eine Annahme negativ aus, gilt der Ersatzweg aus der Roadmap. Umfangsänderungen brauchen vor
Phase 1b eine neue Entscheidung.
