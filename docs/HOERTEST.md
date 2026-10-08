# Hörtest – Anleitung für die Session

> Durchführung des Hörtest-Protokolls aus SPEC §12 (Phase 1a). Das Protokoll selbst und seine Ziele stehen dort;
> hier steht, **was du vorbereiten und tun musst**. Die Stufen und die Auswertung sind in D-136 festgelegt.

**Ziel:** Die Startwerte der Stilprofile (`resources/styles/*.json`, `docs/STYLES.md`) mit dem Gehör kalibrieren:
Archetyp-Gewichte, Bereiche, Bewertungsgewichte und Mindestbewertung. Startziel je Stil: mindestens 80 % der
Ergebnisse „sofort nutzbar“.

**Aufwand:** Stufe 1 (Screening) etwa 40 bis 60 Minuten. Stufe 2 nur für das, was Stufe 1 auffällig macht.
Der Blindvergleich (Teil C) ist eine eigene, kürzere Sitzung.

---

## Teil A – Einmalige Vorbereitung

### A1. Muster erzeugen

Die Muster liegen schon in `hoertest/screening/` (nicht im Repository, `hoertest/` ist ignoriert). Neu erzeugen:

```bash
cmake --build --preset macos-debug --target mmgen
build/macos-debug/tools/mmgen/mmgen series --style all --seeds 3 --energies 30,60,90 --out hoertest/screening
```

Das sind **189 Muster**: 3 Stile × 7 Archetypen × 3 Energien (30, 60, 90 %) × 3 Seeds. Dazu entsteht
`hoertest/screening/manifest.csv` (die Bewertungsliste). Jedes Muster hat drei Dateien:

| Datei | Wohin in Live |
|---|---|
| `…_bass.mid` | Spur **Bass** |
| `…_melody.mid` | Spur **Melodie** |
| `…_all.mid` | Spur **Kick** (nur die Kick klingt, siehe A2) |

### A2. Live-Set (einmal anlegen, danach speichern)

Tempo und Taktart setzt du selbst, die Dateien übernehmen sie nicht (Tempo je Stil: Peak Time **132** BPM,
Melodic Techno **124**, Hard / Industrial **152**; das ist die Mitte des Richtbereichs).

| Spur | Instrument (Live 12 Suite, SPEC §12) | Hinweis |
|---|---|---|
| **Kick** | Drum Rack, eine Kick auf **C1** (Note 36), sonst alles leer | Die Datei `_all.mid` enthält auch Bass und Melodie; auf leeren Pads klingen sie nicht. So bleibt die Kick des Musters (das Kick-Raster ist Teil der Prüfung). |
| **Bass** | Analog oder Drift, **Mono, Legato-Glide an**, Velocity wirkt auf Filter oder Lautstärke | Die Slides des Musters (überlappende Noten) brauchen Legato. |
| **Melodie** | Wavetable oder Drift, polyphon, kurze Hüllkurve, Velocity wirkt auf den Filter | Für Stabs und Akkorde Wavetable, für Leads und Arps Drift. |

Arbeite in der **Session-Ansicht**. Notiere dir die Presets (Name der Instrumente und was du eingestellt hast); sie
gehören später in die Vorlagen (SPEC §12, Phase 0: „Referenz-Instrumente“). Spiele die Spuren mit einem Limiter auf
dem Master, damit nichts übersteuert.

### A3. Dateien in Live laden

Pro Archetyp-Ordner (z. B. `hoertest/screening/peak_time/rolling16/`) liegen 9 Muster. So laden:

1. Im Finder in den Ordner gehen, nach Name sortieren. Mit der Suche (Cmd+F, Suchen in „diesem Ordner“) nach
   `_bass` filtern, alle markieren und **auf die Spur Bass in der Session-Ansicht ziehen**. Live legt die Clips in
   aufeinanderfolgende Slots (eine Szene pro Muster).
2. Dasselbe mit `_melody` auf Spur Melodie und `_all` auf Spur Kick.
3. Weil alle drei Mengen gleich sortiert sind, stehen zusammengehörige Clips in derselben Szene. Eine Szene starten
   spielt Bass, Melodie und Kick des Musters zusammen.

Falls Live beim Ziehen einer Datei nachfragt oder die Clips nicht in Slots verteilt, zieh die Dateien einzeln.
Schleife (Loop) der Clips an lassen: Ein Muster ist 4 Takte lang, etwa 7 Sekunden.

---

## Teil B – Stufe 1: Screening

**Reihenfolge:** Stil für Stil, Archetyp für Archetyp (7 Ordner je Stil). Pro Archetyp 9 Szenen anhören.

**Je Muster** (ein bis zwei Durchläufe reichen):

1. Szene starten, 4 Takte hören, bei Bedarf noch einmal.
2. In die Spalte `rating` von `hoertest/screening/manifest.csv` eintragen:

| Eintrag | Bedeutung |
|---|---|
| `ok` | sofort nutzbar: Ich würde es so in einen Track legen |
| `edit` | nachbearbeiten: Die Idee passt, aber ich müsste Noten ändern |
| `bad` | unbrauchbar: klingt falsch, beliebig oder kollidiert mit der Kick |

   Auch `sofort`, `nachbearbeiten`, `unbrauchbar`, `+`, `~`, `-` werden verstanden. Die CSV darf in einer
   Tabellenkalkulation mit Semikolon gespeichert werden.
3. Bei `edit` und `bad` **ein Stichwort** in `hoertest/notizen.md` (eine Zeile je Muster, Dateiname plus Wort). Das
   ist der wichtigste Teil, denn aus den Stichworten leite ich die Änderungen ab. Nimm diese Wörter, wenn sie passen:

| Stichwort | Gemeint |
|---|---|
| `dicht` / `leer` | zu viele oder zu wenige Noten für die Energie |
| `falsche-toene` | Töne passen nicht zu Tonart oder Akkord |
| `kick` | Bass oder Melodie kollidiert mit der Kick, oder es klingt ohne Kick-Freiraum schwammig |
| `beliebig` | Melodie hat kein Motiv, wirkt zufällig |
| `langweilig` | zu gleichförmig, nichts passiert in 4 Takten |
| `unruhig` | zu viele Sprünge oder Rhythmuswechsel |
| `groove` | Swing oder Timing stimmt nicht |
| `velocity` | Betonung falsch oder zu flach |
| `slide` | Slides klingen falsch, zu lang oder fehlen |
| `lage` | zu hoch oder zu tief |
| `akkord` | Akkordfarbe oder Akkordwechsel passen nicht |

Hörtipp: Hör Bass **mit** Kick, die Melodie danach dazu. Ein Archetyp, der nur mit Kick richtig klingt, ist
normal.

**Zeitplan-Vorschlag:** 20 bis 25 Sekunden je Muster (hören, eintragen, Stichwort), also etwa 60 bis 80 Minuten
für alle 189. Wenn es zu viel wird: Reicht ein Seed je Energie (63 Muster), und ich werte nach.

**Gespeichert wird in der CSV.** Zwischendurch speichern. Eine Datei lässt sich exakt wiederfinden: Das Manifest
enthält Seed, Gewinner-Seed, Archetypen, Tonart, Skala, Akkorde und Qualitätswert.

---

## Auswertung und Änderungen (mache ich)

```bash
build/macos-debug/tools/mmgen/mmgen summary hoertest/screening/manifest.csv
```

`summary` zeigt je Stil, Archetyp und Energie die Anteile und markiert Stile unter 80 % „sofort nutzbar“.
Aus den Stichworten leite ich die Änderungen ab:

| Auffälligkeit | Stellschraube im Profil |
|---|---|
| ein Archetyp fällt häufig durch | Gewicht des Archetyps senken oder ihn für den Stil entfernen |
| `dicht`, `leer` | Dichtebereiche und Makros der Energie (STYLES.md §1.5), Gewicht `quality.weights.density` |
| `falsche-toene` | Chromatik (`chromatic_default`), Skalengewichte, Bass-Bewegung (`movement`) |
| `kick` | `kick_clearance`, Gewicht `quality.weights.kick` |
| `beliebig`, `langweilig` | Gewicht `quality.weights.motif` und `rhythm_variety`, Kreativität |
| `unruhig` | Gewicht `quality.weights.intervals`, Archetyp-Gewichte |
| `groove`, `velocity` | `swing_default`, Velocity-Kontur (STYLES.md §1.9) |
| `lage` | `bass.range`, `melody.range`, `melody.voicing.range` |
| `akkord` | `chord_colors`, Harmonie-Gewichte |
| gute und schlechte Muster liegen im Qualitätswert nah beieinander | `quality.min_score` anheben, Gewichte neu verteilen |

Die Golden Files ändern sich dadurch und werden bewusst erneuert (CLAUDE.md). Ergebnisse und Änderungen kommen in
`docs/DECISIONS.md` und `docs/STYLES.md`.

### Stufe 2 (nur bei Bedarf)

Stile oder Archetypen unter dem Ziel hören wir nach der Änderung mit dem **vollen Protokoll** (20 Seeds je
Archetyp und Energie) noch einmal an:

```bash
build/macos-debug/tools/mmgen/mmgen series --style peak_time --archetype rolling16 --seeds 20 --energies 30,60,90 --out hoertest/full
```

---

## Teil C – Blindvergleich gegen die Live-12-Generatoren

**Ziel:** Bei gleicher Tonart hören, ob unsere Basslines gegen die Generatoren in Live 12 (Seed, Shape) bestehen.
Gemessen wird, wie oft unser Muster bevorzugt wird.

**Ablauf** (12 Durchgänge, je 4 pro Stil; Zeit etwa 30 Minuten):

1. Durchgänge wählen. Das Skript kopiert unser Muster nach `hoertest/blind/ours/NN.mid` und schreibt Tonart,
   Skala, Takte und Tempo nach `hoertest/blind/trials.csv`:

   ```bash
   python3 tools/hoertest/blind_sheet.py make --manifest hoertest/screening/manifest.csv --out hoertest/blind
   ```

2. Für **jeden Durchgang** in Live 12 mit dem Generator (**Seed** oder **Shape**, wie du magst) ein Bass-Pattern in **derselben Tonart und Skala** und **derselben
   Länge** erzeugen, bis es dir als „bestes Ergebnis“ gefällt (höchstens 5 Versuche, damit der Vergleich fair
   bleibt; nimm für alle Durchgänge denselben Generator). Das Clip als MIDI exportieren: Rechtsklick auf den Clip (oder Datei-Menü) → **Export MIDI Clip…**,
   Name `hoertest/blind/live/NN.mid` mit der Nummer des Durchgangs.
3. Mischen und Antwortschlüssel schreiben:

   ```bash
   python3 tools/hoertest/blind_sheet.py pack --out hoertest/blind
   ```

   Das legt `hoertest/blind/pairs/NN_A.mid` und `NN_B.mid` an (Zuordnung zufällig), `sheet.csv` zum Ausfüllen und
   `key.csv` (**nicht öffnen**, bevor du fertig bist).
4. Je Durchgang A und B auf derselben Spur nacheinander hören (Bass plus Kick), in `sheet.csv` in der Spalte
   `better` **A**, **B** oder **=** eintragen.
5. Auswerten:

   ```bash
   python3 tools/hoertest/blind_sheet.py score --out hoertest/blind
   ```

Ein Wert **über 50 %** (bevorzugt oder gleich) ist das Mindestziel; darunter schaue ich mir die Durchgänge an, die
verloren haben, und leite wie oben Änderungen ab.

---

## Checkliste für den Abend

- [ ] Live-Set mit drei Spuren und Referenz-Instrumenten (A2), Presets notiert
- [ ] Ordner `hoertest/screening/<stil>/<archetyp>/` je Archetyp in Live laden (A3)
- [ ] Muster bewerten: `rating` in der CSV, Stichworte in `hoertest/notizen.md`
- [ ] Danach Bescheid geben: Ich werte aus (`summary`) und schlage die Profiländerungen vor
- [ ] Optional, eigene Sitzung: Blindvergleich (Teil C)
