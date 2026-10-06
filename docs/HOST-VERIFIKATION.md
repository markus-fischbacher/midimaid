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

- [ ] Plugins gebaut und installiert (VST3, AU `aumu`, AU MIDI-FX `aumi`), AU-Cache geleert
      (`killall -9 AudioComponentRegistrar`), DAW neu gestartet
- [ ] Build-Typ notieren (Debug oder Release), Commit-Hash notieren (`git rev-parse --short HEAD`)
- [ ] Versionen notieren: macOS, Live, Logic, Xcode, CMake; später Windows und Visual Studio
- [ ] Beide DAWs: Das Plugin erscheint in der Plugin-Liste (Hersteller „Klirrwerk“)

## B. Live 12 (macOS: VST3 und AU; Windows: VST3)

Je Format einmal durchgehen und ankreuzen. Ergebnis: ✔ / ✘ plus Notiz.

| Nr. | Prüfung | Erwartung | VST3 | AU | Win VST3 |
|---|---|---|---|---|---|
| B1 | Neue MIDI-Spur, MidiMaid als Instrument laden, Fenster öffnet | Fenster mit Namen und Griff | | | |
| B2 | Play | Pattern startet exakt mit dem Takt | | | |
| B3 | Stop | Alle Noten enden sofort, keine hängenden Töne | | | |
| B4 | Loop im Arrangement ein, Sprung am Loop-Ende | Kein hängender Ton, Pattern bleibt im Takt | | | |
| B5 | Tempoänderung während der Wiedergabe | Pattern folgt, keine Aussetzer | | | |
| B6 | Zweite Spur mit Synth, Eingang „MIDI From“ = MidiMaid-Spur, Monitoring „In“ | Der Synth spielt das Pattern | | | |
| B7 | In der MidiMaid-Spur aufnehmen (Synth-Spur scharf) | Aufgenommene Noten stimmen mit der Wiedergabe überein | | | |
| B8 | **D-55:** Kanäle trennen beim Routing? (Einzelne MIDI-Kanäle im „MIDI From“-Menü wählen, sofern das Plugin mehrere Kanäle sendet; sonst notieren: „nur ein Kanal, nicht prüfbar“) | Notieren, ob Live Kanäle trennt | | | |
| B9 | Plugin-Fenster schließen, Set speichern, neu öffnen | Plugin lädt ohne Fehler | | | |
| B10 | Tasten: Leertaste, Cmd+Z, Pfeile und Buchstaben bei fokussiertem Plugin-Fenster | Notieren, welche Tasten Live ans Plugin weitergibt (Planung v1.1-Kürzel) | | | |

## C. Logic Pro

MidiMaid als **MIDI-FX** (`MidiMaid MIDI`, MIDI-FX-Slot einer Instrumentenspur) und als **Instrument** prüfen.

| Nr. | Prüfung | Erwartung | MIDI-FX | Instrument |
|---|---|---|---|---|
| C1 | `MidiMaid MIDI` im MIDI-FX-Slot laden, Synth auf derselben Spur | Plugin erscheint im Slot, lädt ohne Fehlermeldung | | – |
| C2 | Play | Pattern startet im Takt | | |
| C3 | Stop | Keine hängenden Noten | | |
| C4 | Zyklus (Loop) ein, Sprung am Zyklusende | Kein hängender Ton, Pattern im Takt | | |
| C5 | Tempoänderung, Positionssprung per Lineal | Pattern folgt | | |
| C6 | Start mitten im Arrangement | Pattern an der richtigen Taktposition | | |
| C7 | „Record MIDI to Track Here“ auf einer Synth-Spur, die von MidiMaid gespielt wird | Notiz: Nimmt Logic die Ausgabe **hinter** MidiMaid auf? (Ersatzweg laut Roadmap: nur Drag & Drop) | | |
| C8 | Tasten ans Plugin-Fenster (wie B10) | Notieren | | |
| C9 | `auval -v aumi Mdmf Klrw` und `auval -v aumu Mdmi Klrw` | Beide „AU VALIDATION SUCCEEDED“ | | |

## D. Drag & Drop (Live und Logic)

| Nr. | Prüfung | Erwartung | Live | Logic |
|---|---|---|---|---|
| D1 | Griff aus dem Plugin auf eine MIDI-Spur ziehen | Clip bzw. Region mit dem Pattern entsteht | | |
| D2 | Tempo-Event der Datei | Notieren: Wird das Tempo übernommen oder ignoriert? Passt die Länge bei abweichendem Projekttempo? | | |
| D3 | Clip bzw. Region **ins** Plugin-Fenster ziehen | Kommt eine `.mid` im Plugin an? (Im Spike noch ohne Import: Notieren, was der Host beim Ziehen anbietet; Cursor, Verbotszeichen?) | | |

## E. Vorzähler und negative PPQ (O-24)

Heute spielt die Engine bei negativen Positionen nach derselben Rechnung (Position modulo Länge).

| Nr. | Prüfung | Erwartung | Live | Logic |
|---|---|---|---|---|
| E1 | Count-in (1 bzw. 2 Takte) einschalten, Aufnahme starten | Notieren: Spielt MidiMaid im Vorzähler? An welchen Positionen (modulo Länge, also phasenrichtig zum ersten Takt)? | | |
| E2 | Aufnahme von einer MidiMaid-Spur: Landen Noten aus dem Vorzähler im Clip? | Notieren, ob das stört | | |
| E3 | Subjektives Urteil | Wunsch: Vorzähler **still** oder **spielend**? | | |

Folge: Ist „still“ gewünscht, wird die Engine so geändert, dass sie bei `ppq < 0` schweigt; die Entscheidung
steht danach als D-xxx in `docs/DECISIONS.md` (O-24 entfällt).

## F. Versatz zwischen Spuren (Grundlage O-22 und Mindestvorlauf)

Zwei MidiMaid-Instanzen auf zwei Spuren, je ein Synth mit hartem Klick-Sound (kurzer Ton, schnelles Attack).
Beide Instanzen spielen dasselbe feste Pattern, so dass Versatz hörbar und im Bounce messbar ist.

| Nr. | Prüfung | Messung | Ergebnis |
|---|---|---|---|
| F1 | Logic, beide Spuren, eine **ausgewählt** | Offline-Bounce beider Spuren auf eine Datei, Wellenform in Samples vergleichen (Versatz der Anschläge) | |
| F2 | Logic, die andere Spur ausgewählt | Wie F1 | |
| F3 | Live, Echtzeit | Mitschnitt (Resampling-Spur) und Wellenform, Versatz in Samples | |
| F4 | Live, Offline-Export | Wellenform, Versatz in Samples | |
| F5 | Puffergröße 64, 256, 1024 Samples | F1 bis F4 wiederholen, mindestens einen Fall | |

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
- [ ] Nicht aktive Parameter in Live und Logic (SPEC §3.13)
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
