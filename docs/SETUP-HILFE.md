# Setup-Hilfe Hub & Voices (Textvorlage)

> Zweck: Anleitung für Anwender. Die Hilfe im Plugin (Knopf „Hilfe“, D-194) zeigt eine Kurzfassung davon mit einer Seite
> je Host; die Texte stehen in `resources/i18n/de.json` (`help.text.*`). Ändert sich hier etwas, bitte dort nachziehen. Stand: Hub & Voices (D-146). Der Ablauf
> in Live und Logic ist erst mit Teil H der `docs/HOST-VERIFIKATION.md` bestätigt; Abweichungen tragen wir hier nach.

## Das Prinzip

Ein **Hub** erzeugt und hält die Patterns aller Stimmen. Jede **Voice** spielt genau eine Stimme (Bass, Melodie …) auf
ihrer eigenen Spur und bekommt ihre Patterns vom Hub. Slot- und Pattern-Wechsel führt die ganze Gruppe zum selben Takt
aus. Nur Instanzen im selben Prozess finden sich.

## Einrichten (alle Hosts)

1. **Eine Variante für alle Instanzen.** Hub und Voices müssen aus demselben Plugin-Bundle stammen (Format und Variante):
   Logic: alle als MIDI-FX `MidiMaid MIDI`; Live: alle als VST3. Gemischte Varianten verbinden sich nicht.
2. **Eine Instanz als Hub.** Rolle „Hub“ wählen. Die Statuszeile zeigt „Hub: n Stimmen“.
3. **Pro Stimme eine Instanz als Voice.** Rolle „Voice“, daneben die Stimme wählen (Voice 1 = Bass, Voice 2 = Melodie).
   Die Voice zeigt „Mit dem Hub verbunden“ und übernimmt den Slot-Satz des Hubs.
4. **Pro Spur ein Instrument** für die Ausgabe der Voice. Der Hub spielt selbst eine Stimme; soll er schweigen, stellst
   du bei ihm „Ausgabe: keine“ ein.
5. **Erzeugen im Hub** (Knopf „Erzeugen“). In einer Voice leitet derselbe Knopf die Anfrage an den Hub weiter.

## Slot-Wechsel und Automation

- Eine Voice folgt standardmäßig dem Slot des Hubs („Slot: folgt dem Hub“). Automatisiere den Slot-Parameter **im Hub**.
- **Bounce einer einzelnen Spur und Freeze:** Dabei läuft der Hub eventuell nicht mit. Stelle die Voice auf „Slot: eigener“
  und automatisiere den Slot-Parameter der Voice selbst (D-131).
- Wechsel kurz vor der Taktgrenze verschiebt die Gruppe automatisch auf den Takt danach, damit alle gleichzeitig
  wechseln. Die Statuszeile einer Voice zählt Wechsel, die zu spät ankamen („n late“); der Wert sollte 0 bleiben.

## Mute

Mute wirkt doppelt: Schaltet der Hub `Mute n`, schweigt Voice n; schaltet eine Voice ihr eigenes Mute, schweigt nur sie.

## Wenn der Hub fehlt

- **Hub gelöscht:** Die Voices spielen weiter. Die älteste Voice zeigt „Hub not found: you can take over“ mit dem
  Knopf „Hub werden“. Es gibt keine stille Übernahme.
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

## Routing, Monitoring, Aufnahme, Freeze, Arrangement je Host

Diese Abschnitte sind **geplante Abläufe aus SPEC §6.5**, bis Teil H der `docs/HOST-VERIFIKATION.md` sie in der DAW
bestätigt hat (Host-Check H46). Weichen Live oder Logic ab, steht die Korrektur hier.

### Ableton Live (VST3)

**Aufbau pro Stimme:** Eine MIDI-Spur mit MidiMaid (Hub bzw. Stimme) und eine zweite Spur mit dem Synth. Live setzt ein VST3
nicht vor einen Synth auf derselben Spur, und es trennt beim Spur-Routing keine Kanäle: Darum eine MidiMaid-Spur je Stimme.
Fasse beide Spuren einer Stimme zu einer **Gruppenspur** zusammen („Bass“, „Melodie“).

| Schritt | Einstellung |
|---|---|
| Routing | Synth-Spur: **MIDI From** = die MidiMaid-Spur dieser Stimme (Ausgang *Post FX* der Plugin-Spur) |
| Monitoring Synth-Spur | **In**. Sonst schweigt sie, solange sie nicht scharf geschaltet ist |
| Monitoring Hub-Spur | **In** (für spätere Eingabe vom Keyboard; v1.1) |
| Aufnahme | Synth-Spur scharf schalten und aufnehmen: Live schreibt das MIDI hinter MidiMaid in einen Clip |
| **Freeze** | Live friert eine Spur aus ihren eigenen Clips ein, nicht aus geroutetem MIDI. Eine Synth-Spur, die nur über „MIDI From“ gespielt wird, deshalb **erst aufnehmen, dann einfrieren**. Steht das Monitoring der Spur auf **In**, verweigert Live den Freeze mit der Meldung „Track … cannot be frozen because its monitoring state is set to In“: vorher das Monitoring auf **Auto** oder **Off** stellen (H20) |
| Bounce einer Spur | Voice auf **Slot: eigener** stellen und ihren Slot-Parameter automatisieren (siehe oben) |
| Arrangement | Slot-Wechsel im Hub automatisieren; ein Start mitten im Arrangement übernimmt den Slot der Automation an dieser Stelle |
| Gleiche Variante | Alle Instanzen als VST3 |

Die Hub-Spur kann keine Master-Spur sein: Master und Stereo-Out nehmen keine MIDI-Effekte auf.

### Logic Pro (AU, MIDI-FX „MidiMaid MIDI“)

| Schritt | Einstellung |
|---|---|
| Aufbau | Hub als **MIDI-FX** auf einer Instrumentenspur (spielt dort z. B. den Bass), je eine Stimme als MIDI-FX auf den weiteren Synth-Spuren |
| Gleiche Variante | Alle Instanzen als `MidiMaid MIDI` (Logic: MIDI-FX-Slot) |
| Aufnahme | Auf **jeder** Spur mit einer Stimme „**Record MIDI to Track Here**“ einschalten: Logic nimmt dann die Ausgabe **hinter** MidiMaid auf, sonst entsteht keine Region |
| Freeze | Eine Instrumentenspur mit MIDI-FX lässt sich einfrieren; stehen Hub und Stimmen auf verschiedenen Spuren, ist der Hub beim Einfrieren nur einer Spur eventuell nicht aktiv: Stimme auf **Slot: eigener** und ihren Slot-Parameter automatisieren |
| Bounce | Wie Freeze: bei Einzelspur-Bounce Stimme auf **Slot: eigener** (Ablauf mit H46 zu bestätigen) |
| Arrangement | Slot-Automation im Hub; aufgenommene Regionen sind normales MIDI |

### Andere Hosts

Bitwig: Plug-in-Hosting auf **By Plug-in** stellen (siehe oben). Hosts, die Plugins in eigenen Prozessen laufen lassen,
verbinden Hub und Stimmen nicht; MidiMaid zeigt „Kopplung nicht verfügbar“ bzw. „Hub nicht gefunden“.

### Aufnehmen: was du wissen solltest

- MidiMaid erzeugt MIDI. Aufnehmen heißt, dieses MIDI als Region/Clip festzuhalten. Danach ist es ein normaler Clip,
  unabhängig vom Plugin.
- Die **Drag-&-Drop-Griffe** der Stimmen sind der schnellste Weg, ein einzelnes Pattern in einen Clip zu legen
  (Tempo der DAW, Oktave der Instanz).
- Ein Pattern, das du **gespeichert** hast, klingt nach dem Neuladen gleich; deine Aufnahme ändert sich also nicht, wenn
  du später Referenzen änderst.

## Vorlagen für die DAW

Fertige Vorlagen (Live-Set mit Gruppenspuren „Bass“ und „Melodie“, Logic-Patches) sind noch **nicht** Teil der
Auslieferung; sie entstehen mit der Aufgabe „DAW-Vorlagen“ der Phase 6. Bis dahin gelten die Tabellen oben.

