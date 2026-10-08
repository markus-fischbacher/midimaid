# Setup-Hilfe Hub & Voices (Textvorlage)

> Zweck: Anleitung für Anwender und Textvorlage für die spätere Hilfe im Plugin (SPEC §6.5, §8.1). Die Hilfe im Plugin
> kommt mit der Übersetzungstabelle (Phase 2); bis dahin steht hier die Quelle. Stand: Hub & Voices (D-146). Der Ablauf
> in Live und Logic ist erst mit Teil H der `docs/HOST-VERIFIKATION.md` bestätigt; Abweichungen tragen wir hier nach.

## Das Prinzip

Ein **Hub** erzeugt und hält die Patterns aller Stimmen. Jede **Voice** spielt genau eine Stimme (Bass, Melodie …) auf
ihrer eigenen Spur und bekommt ihre Patterns vom Hub. Slot- und Pattern-Wechsel führt die ganze Gruppe zum selben Takt
aus. Nur Instanzen im selben Prozess finden sich.

## Einrichten (alle Hosts)

1. **Eine Variante für alle Instanzen.** Hub und Voices müssen aus demselben Plugin-Bundle stammen (Format und Variante):
   Logic: alle als MIDI-FX `MidiMaid MIDI`; Live: alle als VST3. Gemischte Varianten verbinden sich nicht.
2. **Eine Instanz als Hub.** Rolle „Hub“ wählen. Die Statuszeile zeigt „Hub: n voices“.
3. **Pro Stimme eine Instanz als Voice.** Rolle „Voice“, daneben die Stimme wählen (Voice 1 = Bass, Voice 2 = Melodie).
   Die Voice zeigt „Connected to the hub“ und übernimmt den Slot-Satz des Hubs.
4. **Pro Spur ein Instrument** für die Ausgabe der Voice. Der Hub spielt selbst eine Stimme; soll er schweigen, stellst
   du bei ihm „Output: none“ ein.
5. **Erzeugen im Hub** (Knopf „Generate“). In einer Voice leitet derselbe Knopf die Anfrage an den Hub weiter.

## Slot-Wechsel und Automation

- Eine Voice folgt standardmäßig dem Slot des Hubs („Slot: follows hub“). Automatisiere den Slot-Parameter **im Hub**.
- **Bounce einer einzelnen Spur und Freeze:** Dabei läuft der Hub eventuell nicht mit. Stelle die Voice auf „Slot: own“
  und automatisiere den Slot-Parameter der Voice selbst (D-131).
- Wechsel kurz vor der Taktgrenze verschiebt die Gruppe automatisch auf den Takt danach, damit alle gleichzeitig
  wechseln. Die Statuszeile einer Voice zählt Wechsel, die zu spät ankamen („n late“); der Wert sollte 0 bleiben.

## Mute

Mute wirkt doppelt: Schaltet der Hub `Mute n`, schweigt Voice n; schaltet eine Voice ihr eigenes Mute, schweigt nur sie.

## Wenn der Hub fehlt

- **Hub gelöscht:** Die Voices spielen weiter. Die älteste Voice zeigt „Hub not found: you can take over“ mit dem
  Knopf „Become hub“. Es gibt keine stille Übernahme.
- **Projekt öffnen ohne Hub:** Die Voice spielt ihre gespeicherte Stimme unverändert weiter und zeigt „Hub not found“.
  Weil jede Instanz alle Slots speichert, kann sie Hub werden.

## Wenn sich Instanzen nicht finden

- Gleiche Variante und gleiches Format? (Schritt 1)
- **Plugin-Sandbox:** Hosts, die Plugins in getrennten Prozessen laufen lassen, trennen die Instanzen. Bitwig: Einstellung
  „Plug-in-Hosting“ auf „By Plug-in“ stellen statt „Individually“. Live und Logic laden Plugins im selben Prozess.
- Mehrere Hubs: Ein zweiter Hub bleibt solo und zeigt „There is already a hub“.

## Aufnehmen und Exportieren

- **Logic:** Auf der Voice-Spur „Record MIDI to Track Here“ aktivieren (sonst entsteht keine Region); Logic nimmt die
  Ausgabe hinter dem Plugin auf (D-129).
- **Live:** Eine MIDI-Spur mit „MIDI From“ der Voice-Spur und Monitoring „In“.
- **Drag & Drop:** Jede Stimme hat in der Hub-UI einen eigenen Griff, der die `.mid`-Datei dieser Stimme (mit dem DAW-Tempo) in die DAW zieht; die Voice-Oberfläche hat einen Griff für ihre eigene Stimme.
