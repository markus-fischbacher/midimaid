# Checkliste Host-Verifikation (Phase 0)

> Zweck: Die manuellen Prüfungen aus `docs/ROADMAP.md` („Host-Verifikation“) so durchführen, dass die Ergebnisse
> direkt in `docs/DECISIONS.md` übernommen werden können. Zu O-22 und O-24 siehe dort.

## Stand des Plugins

Stand nach Hub & Voices (D-139 bis D-146): Das Plugin spielt die Patterns aus 16 Slots, kennt die Rollen Solo, Hub und
Voice, verteilt den Slot-Satz vom Hub an die Voices und wechselt Slots und Patterns gruppenweit zum selben PPQ. Es gibt
eine einfache Hub-UI (Stil, Tonart, Skala, Takte, Seed, Energie, Kreativität, Slot-Leiste, Notenansicht und Griff je Stimme) und die kompakte Voice-Oberfläche, aber noch kein Log und keine Übersetzungstabelle (Editor-Texte
fest auf Englisch). Daher gilt:

- **Jetzt prüfbar:** Teil A (Vorbereitung), B (Live), C (Logic), D (Drag & Drop), E (Vorzähler, O-24), F (Versatz
  zwischen zwei unabhängigen Instanzen) und **Teil H (Hub & Voices)**.
- **Noch nicht prüfbar** (Teil G): Dateiimport und Tastenbelegung.

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

Diese Punkte brauchen Referenz-Instrumente, Import oder belegbare Tasten. Hier nichts eintragen, sondern in der
Phase nachholen, in der das Feature entsteht. Die Punkte zu Hub, Voice, Registry, Slot-Parameter, Freeze und
inaktiven Parametern stehen jetzt in Teil H.

- [ ] Clip bzw. Region ins Plugin-Fenster ziehen (D3): kommt eine `.mid` an? Erst prüfbar, wenn das Plugin Dateien
      annimmt (Import, Phase 1); bis dahin entfällt D3
- [ ] Tastenbelegung: Wenn Tasten belegbar sind (v1.1, D-126), prüfen, welche Tasten Live, Logic und Windows-Hosts
      ans Plugin-Fenster weiterleiten und ob der Host zusätzlich reagiert (B10 und C8 entfallen bis dahin)
- [ ] Referenz-Instrumente: Legato-Glide und Velocity-Reaktion (kommt mit der Hörtest-Session, SPEC §12)

## H. Hub & Voices (Live und Logic)

**Vorbereitung:** Neuesten Build installieren, AU-Cache leeren, DAW neu starten (Teil A). Eine Gruppe besteht nur aus
Instanzen **derselben Variante** (D-141, O-28): in Logic alle als MIDI-FX `MidiMaid MIDI`, in Live alle als VST3. Einen
Hub und zwei Voices anlegen, je auf einer eigenen Spur mit einem Synth mit hartem Klick-Sound (wie in Teil F). In jeder
Instanz stellst du oben links die **Rolle** ein (Solo, Hub, Voice) und bei Voices daneben die **Stimme** (Voice 1 =
Bass, Voice 2 = Melodie). Der Hub zeigt „Hub: 2 Stimmen“, eine verbundene Voice „Mit dem Hub verbunden“. Vor dem Test
im Hub Slot 1 bis 3 mit **Erzeugen** füllen (zwischen den Klicks in der Slot-Leiste die Slots 1, 2, 3 wählen; belegte Slots sind heller). Die Zahl
„(n late)“ in der Statuszeile einer Voice zählt Wechsel, die zu spät ankamen; sie soll in allen Tests 0 bleiben.

**Was „taktgenau“ heißt:** Wie in Teil B: Hub und Voice schlagen im Bounce oder Mitschnitt sample-gleich an, und ein
Wechsel erscheint in beiden an derselben Taktlinie.

| Nr. | Prüfung | Erwartung | Live | Logic | Bemerkung |
|---|---|---|---|---|---|
| H1 | Zwei Instanzen mit Rolle Hub und Voice | Hub zeigt „1 voice“, Voice „Mit dem Hub verbunden“ (Registry im Prozess geteilt) | | | |
| H2 | Zweite Hub-Instanz anlegen | Zeigt „There is already a hub“, bleibt solo | | | |
| H3 | Hub und zwei Voices spielen, Loop ein, Stop, Start | Alle im Takt, keine hängenden Noten, Voices folgen dem Hub-Slot | | | |
| H4 | Slot-Parameter im Hub wechselt **weit vor** der Taktgrenze (z. B. auf Zählzeit 2) | Hub und Voices wechseln gemeinsam zum nächsten Takt | | | |
| H5 | Slot-Parameter im Hub wechselt **etwa 50 ms vor** der Taktgrenze (Klick, nicht Automation) | Hub und Voices wechseln in demselben Takt (ein Takt später als bei H4 ist richtig); „(n late)“ bleibt 0. Fällt „n late“ größer 0 aus: Versatz notieren, Ergebnis in H12 | | | |
| H6 | Im Hub **Erzeugen** drücken, etwa 50 ms vor der Taktgrenze | Neues Pattern erscheint in Hub und Voices im selben Takt (D-144) | | | |
| H7 | Slot-Automation im Hub, Start mitten im Arrangement (z. B. ab Takt 37) | Hub und Voices spielen sofort den dort automatisierten Slot; bei „Slot: folgt dem Hub“ keine Abweichung | | | |
| H8 | Voice auf „Slot: eigener“ stellen, Slot-Parameter der Voice automatisieren | Die Voice folgt ihrem eigenen Parameter, der Hub bleibt unberührt | | | |
| H9 | Hub: `Mute 2` an | Voice 2 schweigt mit Note-Off, Voice 1 und der Hub spielen weiter; wieder aus: Voice 2 spielt ab der nächsten Note | | | |
| H10 | Voice 1: eigenes `Mute 1` an | Nur Voice 1 schweigt | | | |
| H11 | In einer Voice **Erzeugen** drücken | Statuszeile „Hub zum Erzeugen aufgefordert“, der Hub erzeugt in seinen Slot, alle bekommen das Pattern zum selben Takt | | | |
| H12 | Hub und Voice getrennt bouncen (Offline) und die Wellenformen vergleichen | Anschläge sample-gleich. Sonst: Versatz in Samples und Millisekunden notieren (Grundlage für den Mindestvorlauf, D-90) | | | |
| H13 | Hub löschen | Voices spielen weiter, die älteste zeigt „Hub not found: you can take over“ und den Knopf „Hub werden“; die zweite Voice „Hub not found“ | | | |
| H14 | „Hub werden“ drücken | Die Voice wird Hub („Hub: 1 Stimme“), die andere Voice verbindet sich, Slot-Wechsel laufen wieder gruppenweit | | | |
| H15 | Hub auf „Ausgabe: keine“ | Der Hub schweigt, Voices spielen und folgen weiter | | | |
| H16 | Projekt **mit** Hub speichern, schließen, öffnen | Rolle, Stimme, Slot-Satz stimmen, Gruppe verbindet sich, Slot-Satz der Voices entspricht dem Hub | | | |
| H17 | Projekt speichern, **Hub vorher entfernen**, öffnen | Voice spielt ihre Stimme unverändert weiter und zeigt „Hub not found“ (SPEC §6.5) | | | |
| H18 | Projekt von **vor** Hub & Voices öffnen (Instanz ohne Rolle im State) | Instanz ist Solo und spielt wie bisher | | | |
| H19 | Einzelspur-Bounce und Freeze einer Voice mit Slot-Automation im Hub (O-22) | Notieren, ob der Slot stimmt; sonst „Slot: eigener“ mit eigener Automation (Ersatzweg D-131) | | | |
| H20 | Freeze einer Synth-Spur, die nur über „MIDI From“ gespielt wird (Live) | Erwartung: still. Abweichung notieren | – | | |
| H21 | Logic: „Record MIDI to Track Here“ auf der Voice-Spur; Live: Spur mit „MIDI From“ der Voice-Spur und Monitoring „In“ | Aufnahme enthält die Voice-Stimme | | | |
| H22 | Nicht aktive Parameter (SPEC §3.13): Zeigen Live (VST3) und Logic (AU) die 15 nicht aktiven Parameter (u. a. „Transpose“, „Erzeugen“, „Variation 1“ bis „Variation All“, „Evolve“, „Evolve Keep“, „Creativity“, „Energy“) in der Automationsliste, und lässt sich einer automatisieren? Lässt sich „Slot“ automatisieren, wechselt das Pattern zum Taktstart, und schaltet „Mute 1“ den Bass stumm? | Notieren je Host | | | |
| H23 | **Nur Hosts mit Plugin-Sandbox** (z. B. Bitwig „Individually“): Hub und Voice laden | Voice zeigt „Hub not found (if the hub is in this project: the host may run plug-ins in separate processes)“. Mit „By plug-in“ verbinden sich die Instanzen. Live und Logic sind nicht betroffen | – | – | |
| H24 | Piano-Roll im Hub-Fenster: Note per Doppelklick setzen, verschieben, Länge ziehen, Velocity-Balken ziehen, Auswahlrahmen, Zoom mit Cmd/Strg+Rad; Doppelklick auf eine Note löscht sie | Alles mit der Maus bedienbar; **während der Wiedergabe** kein Versatz und keine hängende Note | | | |
| H25 | Rechtsklick auf eine Note im Hub-Fenster (Live, Logic) | Menü „Akzent / Slide / Löschen“ öffnet und wirkt (das Öffnen prüft kein automatischer Test) | | | |
| H26 | Spurfokus: Schalter „Fokus“ einer Stimme, danach wieder aus; Mute, Sperren und Ziehfläche der zugeklappten Leiste | Roll füllt die Höhe, die andere Stimme bleibt als Leiste bedienbar | | | |

**Messung für den Mindestvorlauf (H5, H6, H12):** Der Standard ist 150 ms (D-90). Kommen bei H5 oder H6 verspätete Wechsel
vor oder ist H12 nicht sample-gleich, wiederholst du H5 mit Puffer 1024. Notiere Puffergröße, Samplerate und
den Zähler „n late“. Daraus bestimme ich, ob der Vorlauf erhöht werden muss; er ist heute eine Konstante mit Setter und
wird erst mit den globalen Einstellungen (SPEC §9.2) einstellbar.

**Abnahme 1b (ROADMAP):** erfüllt, wenn H3 bis H8, H12, H16 und H18 in beiden DAWs ✔ sind.

## Eintragsformat für `docs/DECISIONS.md`

Nach der Session meldest du mir die Tabellen (oder Fotos und Notizen). Ich trage sie ein:

```
- **D-xxx Host-Verifikation <Host>, <Datum>:** Setup (macOS x.y, <Host> x.y, Build <Typ> <Hash>, Puffer n).
  Ergebnis je Prüfung (B1 ✔, B2 ✔ …). Abweichungen und Ersatzweg laut ROADMAP „Host-Annahmen und Ersatzwege“.
  Folgen für O-22, O-24 und D-55.
```

Fällt eine Annahme negativ aus, gilt der Ersatzweg aus der Roadmap. Umfangsänderungen brauchen vor
Phase 1b eine neue Entscheidung.
