# mmgen – Hörtest-Werkzeug

Kommandozeilenprogramm auf Basis von `core` (D-79, D-124). Es erzeugt `.mid`-Dateien für die Hörtests der Phase 1a
(SPEC §12), schreibt eine Liste zum Bewerten und wertet die Bewertungen aus. Es nutzt dieselbe Kette wie das Plugin
später: `generatePattern` (Auswahl, Qualitätsbewertung, Gewinner-Seed), Ausgabestufe (Groove, Slides, Kick-Freiraum)
und MIDI-Datei.

## Bauen

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug --target mmgen
# Programm: build/macos-debug/tools/mmgen/mmgen
```

## Befehle

```bash
# ein Muster (Standard: 4 Takte, Seed 1, Energie 30)
mmgen one --style peak_time --seed 7 --energy 60 --out hoertest
mmgen one --style melodic_techno --bass rolling16_harmonic --melody lead_phrase --bars 8 --out hoertest

# die Serie des Hörtest-Protokolls: je Archetyp 20 Seeds bei Energie 30, 60, 90
mmgen series --style all --out hoertest
mmgen series --style hard_industrial --archetype acid_siren --seeds 40 --energies 50 --out hoertest

# Bewertungen auswerten
mmgen summary hoertest/manifest.csv
```

Weitere Schalter: `--bars 1|2|4|8|16`, `--creativity 0-100` (Standard 40), `--bpm X` (Standard: Mitte des
Tempobereichs des Stils), `--no-groove` (Export ohne Groove, SPEC §4.2a), `--first-seed N`, `--styles ORDNER`
(Stilprofile, Standard: `resources/styles` des Builds). Rückgabewerte: 0 Erfolg, 1 falsche Aufrufe, 2 Fehler (Datei,
Stil, keine gültige Variante).

## Dateien

Je Muster entstehen drei Dateien, bei `series` in `<Ordner>/<stil>/<archetyp>/`:

| Datei | Inhalt |
|---|---|
| `<stil>_<archetyp>_e030_s007_bass.mid` | die Bass-Stimme |
| `<stil>_<archetyp>_e030_s007_melody.mid` | die Melodie-Stimme(n) |
| `<stil>_<archetyp>_e030_s007_all.mid` | Bass auf Kanal 1, Melodie auf Kanal 2 und die **Kick** auf Kanal 10 (Note 36) nach dem Kick-Raster des Musters |

Die Kick gehört nur zum Abhören (ein Bass klingt erst gegen die Kick richtig) und ist nicht Teil der Plugin-Ausgabe.
`e030` ist die Energie in Prozent, `s007` der Seed. Der Archetyp im Namen ist der, den die Serie festgelegt hat; die
andere Stimme wird automatisch gewählt. Bei `one` ohne festen Archetyp steht `auto`.

## Ablauf der Hörtest-Session (SPEC §12)

Die ausführliche Anleitung (Live-Aufbau, Stufen, Stichworte, Blindvergleich) steht in `docs/HOERTEST.md`.

1. `mmgen series --style all --out hoertest` erzeugt alle Muster und `hoertest/manifest.csv`.
2. In Live (oder Logic) die `_all.mid` auf Spuren mit den Referenz-Instrumenten ziehen (Bass: Mono-Synth mit Glide,
   Melodie: je nach Archetyp), Kick auf Kanal 10 mit einer Drum-Rack-Kick belegen.
3. Jedes Ergebnis in der Spalte `rating` der CSV bewerten: `ok` (sofort nutzbar), `edit` (nachbearbeiten) oder
   `bad` (unbrauchbar). Auch `sofort`, `nachbearbeiten`, `unbrauchbar`, `+`, `~`, `-` werden verstanden. Die CSV darf
   in einer Tabellenkalkulation mit Semikolon gespeichert werden.
4. `mmgen summary hoertest/manifest.csv` zeigt je Stil, Archetyp und Energie die Anteile und markiert Stile unter dem
   Ziel von 80 % „sofort nutzbar“.
5. Gewichte, Bereiche, Bewertungsgewichte und Mindestbewertung in `docs/STYLES.md` und den Profilen anpassen. Die
   Golden Files ändern sich dadurch und werden bewusst erneuert (siehe CLAUDE.md).

Eine gute Datei lässt sich exakt wiederfinden: `manifest.csv` enthält Seed, Gewinner-Seed, Archetypen, Tonart, Skala,
Akkorde und Qualitätswert. Ein Muster ohne gültigen Kandidaten steht mit `status = no_valid_candidate` in der Liste
und hat keine Dateien.
