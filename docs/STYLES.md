# MidiMaid – Stilprofile v1

Diese Profile sind die musikalische Grundlage für Algorithmus, Constraint-Schicht und KI-Prompts.
Sie werden 1:1 als JSON umgesetzt (`resources/styles/*.json`), nicht als Code.

> **Release (D-74, D-75):** Punkte mit [v1.1], [Backlog] oder [Gestrichen] gehören nicht zu v1.0.
>
> Status: **Startwerte.** Gewichte und Bereiche sind fundierte Vorschläge und werden in Phase 1
> per Hörtest gemeinsam mit dem Entwickler feinjustiert. Änderungen werden hier dokumentiert.

Schrittangaben beziehen sich auf das 16tel-Raster eines 4/4-Takts: Schritt 0 = Zählzeit 1,
4 = Zählzeit 2, 8 = Zählzeit 3, 12 = Zählzeit 4. Die Offbeats liegen auf 2, 6, 10 und 14.

**Tonhöhen** werden als MIDI-Nummern angegeben. Notennamen in Klammern folgen der Ableton/Logic-Konvention
(C3 = MIDI 60). Beispiel: MIDI 28 = E0 ≈ 41 Hz, MIDI 33 = A0 = 55 Hz, MIDI 40 = E1 ≈ 82 Hz.

---

## 1. Gemeinsame Konzepte

### 1.1 Archetypen und Auswahl
- Jede Stimme hat pro Stil mehrere **Archetypen** (rhythmisch-melodische Grundmuster).
- Auswahl pro Stimme: **Auto** (Standard) oder ein bestimmter Archetyp (manuelle Übersteuerung).
- Auto wählt gewichtet nach Stilprofil. Die Gewichte werden verschoben durch den KI-Intent
  (z. B. „groove: offbeat“), Schlüsselwörter im Prompt („acid“, „rumble“, „arp“) und den Energie-Wert.
- Der gewählte Archetyp wird im Pattern gespeichert und in der UI angezeigt.

### 1.2 Kick-Raster
Der Bass orientiert sich an einem wählbaren Kick-Raster oder an einer Drum-Referenz (`custom`, SPEC §3.14).

| ID | Name | Kick-Schritte |
|---|---|---|
| `4otf` | Four-on-the-Floor (Standard) | 0, 4, 8, 12 |
| `4otf_pickup` | Four-on-the-Floor mit Auftakt | 0, 4, 8, 12, 15 |
| `halftime` | Halftime / Breakdown | 0, 8 |
| `broken_a` | Gebrochen A | 0, 6, 8, 12 |
| `broken_b` | Gebrochen B | 0, 4, 10, 12 |
| `custom` | Aus Drum-Referenz (SPEC §3.14) | wie aufgenommen, bis 2 Takte |

Ist eine Drum-Referenz vorhanden, wird `custom` automatisch vorgeschlagen.

Regel: Bassnoten beginnen nie auf einem Kick-Schritt. Ausnahme ist nur, wenn der Archetyp es ausdrücklich
erlaubt. Das Kick-Raster ist pro Pattern einstellbar und optional pro Phrase (z. B. `halftime` im Breakdown).

### 1.3 [v1.1] Polymeter-Sequenzen
- Sequenzlänge ungleich Taktlänge: 3, 5, 6, 7, 9 oder 10 Sechzehntel bzw. 5 oder 7 Achtel
- Einstellung **Phasenverhalten**:
  - *Neustart am Pattern-Anfang* (Standard): Die Sequenz beginnt bei jedem Pattern-Durchlauf neu
  - *[Backlog] Frei laufend*: Die Phase berechnet sich aus der absoluten Song-Position (PPQ). Das ist
    deterministisch, egal wo die Wiedergabe startet, und driftet über Loops hinweg.
- Drag & Drop exportiert immer mit Neustart, weil ein Clip keine Song-Position kennt.
  Optional kann ein längerer Clip bis zum gemeinsamen Vielfachen exportiert werden, max. 64 Takte.

### 1.4 Mehrstimmigkeit
- Die Melodie-Stimme darf Akkorde enthalten (Stabs, Chops). Der Bass ist immer einstimmig.
- Slides gibt es nur in einstimmigen Abschnitten. Bei Akkorden ignoriert die Constraint-Schicht
  das Slide-Flag.

### 1.5 Energie (0–1)
Die Energie skaliert stilübergreifend: Notendichte, Velocity-Grundniveau, Akzenthäufigkeit,
Oktavsprünge, Länge der Pausen und Anteil von Variationen innerhalb einer Phrase.

### 1.6 Register und Kick-Grundton
- Standard-Tonumfang: **Bass MIDI 28–52** (E0–E2), **Melodie MIDI 55–88** (G2–E5), **Stabs MIDI 55–79**
  (G2–G4). Die Profile können engere Bereiche festlegen.
- **Oktav-Offset pro Stimme** (−2 bis +2), weil Synth-Patches unterschiedlich gestimmt sind
- **[Backlog] Kick-Grundton** (optional, z. B. F): Ist er gesetzt, schlägt das Plugin Tonarten vor, deren Grundton
  dem Kick-Grundton entspricht oder eine Quinte darüber liegt. Der tiefste Bass-Grundton wird so gelegt,
  dass er möglichst zwischen MIDI 28 und 35 liegt (ca. 41–62 Hz).
- Bass und Melodie: Bei gleichzeitigem Anschlag liegt die Melodie mindestens 12 Halbtöne über dem Bass

### 1.7 Kick-Freiraum
Bassnoten enden standardmäßig eine 1/32 vor dem nächsten Kick-Schritt (einstellbar: aus, 1/64, 1/32,
1/16), damit Notenenden nicht in die Kick ragen. Ausnahme: Archetypen mit erlaubter Überlappung
(`long_tied`). Sidechain im Mix ersetzt das nicht, ergänzt es aber.

### 1.8 Intervallregeln Bass ↔ Melodie
Auf betonten Schritten (0, 4, 8, 12) gilt für gleichzeitig klingende Töne von Bass und Melodie:
- immer erlaubt: Prime/Oktave, Quinte, Terz, Sexte, Quarte
- kleine Sekunde, große Septime, Tritonus nur, wenn der Chromatik-Anteil ≥ 30 % ist **oder** der Stil
  Hard/Industrial ist
- auf unbetonten Schritten gelten keine Einschränkungen (Durchgänge)
- die Quarte über dem Bass gilt hier bewusst als konsonant (im klassischen Satz wäre sie eine Dissonanz);
  im Techno-Kontext ist das üblich
- **Chord-Memory-Stabs** sind von der Skalen-Quantisierung ausgenommen, nicht aber von dieser Regel: Ein
  Stab, der auf einem betonten Schritt verbotene Intervalle bildet, wird auf den nächsten Offbeat verschoben

### 1.9 Akzent und Velocity
- Jeder Archetyp hat eine **Velocity-Kontur** (Velocity pro 16tel-Schritt) als Grundlage. Velocity ist im
  Techno oft Modulationsquelle für Filter und Hüllkurven.
- **Akzent** ist ein Flag der Note (SPEC §3.5). Bei der Ausgabe bekommt eine Akzent-Note die
  Akzent-Velocity (Standard 124), normale Noten 80–100. Die **Akzent-Schwelle** (Standard 110) dient nur
  dazu, beim Import fremden MIDIs Akzente zu erkennen. Beides ist einstellbar, passend zu 303-Emulationen,
  die Akzente über eine Velocity-Schwelle auslösen.

### 1.10 Voicings (Stabs und Akkorde)
- Enge Lage (3–4 Töne innerhalb einer Oktave plus optionaler Oktavverdopplung), Register MIDI 55–79
- **Stimmführung:** Bei Akkordwechseln bewegt sich jede Stimme möglichst um höchstens 2 Halbtöne,
  gemeinsame Töne bleiben liegen
- **[v1.1] Chord-Memory-Modus:** Eine Akkordform wird parallel verschoben (Detroit/Dub-Techno-Farbe), auch wenn
  dabei skalenfremde Töne entstehen. Die Constraint-Schicht lässt das im Stab-Archetyp zu.
  Standard ab v1.1: an bei Peak Time und Hard/Industrial, aus bei Melodic (dort gilt die Stimmführung).
  In v1.0 gilt überall die Stimmführung.

### 1.11 Motivik (gegen beliebig klingende Melodien)
- **Motiv zuerst:** Erst entsteht ein Motiv (1–2 Takte) mit eigener rhythmischer Identität, dann die Phrase.
  [v1.1] Mit aktivem Referenz-Set kann das Motiv aus einer Motiv-Zelle des Sets hervorgehen, wird aber immer
  variiert (Kopierschutz, SPEC §3.20)
- Ein Motiv wird mindestens 2×, typischerweise 3× wiederholt, bevor es variiert wird (A A A A')
- Linien-Archetypen (`hypnotic_motif`, `lead_phrase`, `call_response`, `pluck_seq`, `polymeter_seq`)
  verwenden auf schwachen Schritten mindestens 20 % Nicht-Akkordtöne (Durchgangs- und Wechseltöne),
  damit sie nicht wie Arpeggios klingen. Nicht betroffen: Arps, Stabs, `atonal_motif`, `sparse_hits`,
  `acid_siren`
- **Auftakte** (1–2 Noten vor einem Taktbeginn) sind erlaubt und werden bei `lead_phrase` und
  `call_response` bevorzugt

### 1.12 [v1.1] Turnaround
In 8- und 16-Takt-Phrasen kann der letzte Takt als **Turnaround** variieren (Pause, Fill, Oktavsprung,
Ratchet). Die Wahrscheinlichkeit steigt mit der Energie. Turnaround ist ein Merkmal der Phrase, keine
eigene Phrasen-Rolle.

### 1.13 [v1.1] Ratchets
- **Ratchet:** eine Note wird innerhalb eines 16tels 2–4× wiederholt. Erlaubt in `acid_siren`,
  `roll16_aggressive` und Turnarounds. Maximal 1 Ratchet pro Takt als Standard.
- [Gestrichen] Euklidische Rhythmen als Feature; als interne Rhythmusquelle für `polymeter_seq` und
  `sparse_hits` sind sie ohne eigene Entscheidung zulässig

### 1.14 Qualitätsbewertung und Kandidaten
- Der Algorithmus erzeugt pro Generierung **8 Kandidaten** und wählt den mit der besten Bewertung
- Bewertung: Motiv-Wiederholung (§1.11), Tonumfang, **erwartete** Dichte im Zielbereich der Energie
  (Wahrscheinlichkeiten eingerechnet), keine Kick-Kollisionen, Intervallregeln, rhythmische Abwechslung,
  [v1.1] Abstand zum Verlauf, [v1.1] Nähe zum aktiven Referenz-Set (gewichtet mit dem Regler „Persönlich“, SPEC §3.20)
- **Kopierschutz:** Kandidaten, die einem Eintrag des aktiven Referenz-Sets zu mehr als 85 % gleichen,
  werden verworfen (SPEC §3.20)
- **[v1.1] Wiederholungsschutz:** Kandidaten, die einem der letzten 20 Ergebnisse zu ähnlich sind (gleiche
  Rhythmus- und Tonhöhenfolge zu mehr als 85 %), werden verworfen. **Gilt nur für Generieren**, nicht für
  Variationen und Verfeinern: Eine subtile Variation ist per Definition sehr ähnlich, sonst würde der
  Schutz genau die hypnotische Wiederholung verhindern, die Techno ausmacht.
- Gespeichert wird der Seed des gewählten Kandidaten (SPEC §4.3)
- Bei hoher Kreativität werden weiche Kriterien schwächer gewichtet (SPEC §7.6)
- KI-Ergebnisse durchlaufen dieselbe Bewertung. Unterschreitet ein KI-Ergebnis die Mindestbewertung, wird
  das angezeigt; es wird nicht still ersetzt (vgl. SPEC §7.9)

### 1.15 Groove-Standards
- Swing-Standard: Bass 50–54 %, Melodie bis 58 %. Die gedachte Kick bleibt gerade.
- Bei einer Groove-Vorlage mit eigenem Swing (z. B. aus der Drum-Referenz) ist der Swing-Regler
  deaktiviert (SPEC §3.9).
- Bei Bass-Swing über 56 % weist die UI auf mögliche Flams mit einer geraden Kick hin.

### 1.16 [v1.1] Wahrscheinlichkeit und Bedingungen (Startwerte)
- Allgemein sparsam: höchstens 15 % der Noten eines Patterns mit Wahrscheinlichkeit < 100 %
- **Peak Time:** Geisternoten im Bass (leise Zwischen-16tel) mit 50–75 %, Melodie-Zierungen mit 60–80 %
- **Melodic Techno:** kaum Wahrscheinlichkeit; Bedingungen für Variationen in Antwortphrasen (z. B. 2:2)
- **Hard/Industrial:** Ratchets und `sparse_hits` mit 40–70 %, Fills mit 4:4 vor Phrasenwechseln
- Bedingung 4:4 bzw. 8:8 bevorzugt für Turnaround-Noten (§1.12)
- Höhere Energie → etwas mehr Noten mit Wahrscheinlichkeit; Kreativität → breitere Spanne

---

## 2. Peak Time / Driving

| Merkmal | Wert |
|---|---|
| Tempo-Richtwert | 128–135 BPM |
| Skalen (Gewicht) | Natürlich Moll 3 · Phrygisch 2 · Dorisch 2 · Moll-Pentatonik 1 |
| Chromatik-Standard | 10 % |
| Harmonie | statisch 50 % · 2-Akkord-Wechsel 40 % · 4 Akkorde 10 % |
| Typische Wechsel | i–♭VII, i–♭VI, i–iv; Akkordlänge 4–8 Takte |
| Kick-Standard | `4otf` |

### Bass – Tonbewegung
Grundton ca. 70 %, Quinte/Oktave ca. 25 %, ♭3/♭7 als Durchgang ca. 5 %. Tonumfang MIDI 28–52.

### Bass – Archetypen
| Archetyp | Gewicht | Rhythmus | Charakter |
|---|---|---|---|
| `rolling16` | 4 | alle 16tel außer Kick-Schritten | kurz (50–75 % eines Schritts), Akzent auf Offbeat |
| `offbeat` | 3 | 2, 6, 10, 14 | 1–1,5 Schritte lang, gleichmäßige Velocity |
| `gallop` | 3 | Paare 2–3, 6–7, 10–11, 14–15 (Triolen-Variante bei aktiver Triolen-Option) | zweite Note leiser, wirkt nach vorne |

### Melodie – Archetypen
| Archetyp | Gewicht | Beschreibung |
|---|---|---|
| `hypnotic_motif` | 4 | 1–2 Takte, 3–6 Noten, Umfang ≤ 7 Halbtöne, alle 4 Wiederholungen eine Mikro-Variation (bevorzugt Notenlänge, Akzent, Velocity) |
| `stabs` | 3 | Akkorde auf Offbeats und synkopiert, 1–2 Schritte kurz |
| `arp` | 2 | 16tel über Akkordtöne (auf/ab/auf-ab/zufällig), 1–2 Oktaven |
| `polymeter_seq` [v1.1] | 3 | Sequenz in 3, 5 oder 7 Sechzehnteln über 4/4, 2–5 Töne |
| `acid_siren` | 1 | wie bei Hard/Industrial (§4), hier selten: Acid-Lines kommen auch im Peak Time vor. Startwert, im Hörtest prüfen |

### Akkordfarben (Stabs)
Moll-Triade 40 % · Quinte (ohne Terz) 30 % · Moll7 20 % · sus2/sus4 10 %

---

## 3. Melodic Techno

| Merkmal | Wert |
|---|---|
| Tempo-Richtwert | 120–128 BPM |
| Skalen (Gewicht) | Natürlich Moll 3 · Dorisch 2 · Harmonisch Moll 1 · Phrygisch-Dominant 1 |
| Chromatik-Standard | 0 % |
| Harmonie | 4-Akkord-Progression 60 % · 2-Akkord-Wechsel 30 % · statisch 10 % |
| Typische Progressionen | i–VI–III–VII · i–iv–VI–V (harm. Moll) · i–VII–VI–VII · i–VI–iv–VII; Akkordlänge 1–2 Takte |
| Kick-Standard | `4otf` |

### Bass – Tonbewegung
Grundton auf jedem Akkordwechsel, dazwischen Akkordtöne und Durchgänge aus der Skala.
Melodischer als die anderen Stile. Tonumfang MIDI 28–52.

### Bass – Archetypen
| Archetyp | Gewicht | Rhythmus | Charakter |
|---|---|---|---|
| `rolling16_harmonic` | 4 | alle 16tel außer Kick | folgt den Akkorden, Durchgangstöne vor Wechseln |
| `offbeat_changes` | 3 | 2, 6, 10, 14 | wechselt präzise mit der Harmonie |
| `long_tied` | 2 | halbe/ganze Noten, gebunden; **darf auf Kick-Schritten beginnen** und über Kicks halten | Slides zwischen Akkordgrundtönen erlaubt; ausgenommen von Kick-Aussparung und Kick-Freiraum |

### Melodie – Archetypen
| Archetyp | Gewicht | Beschreibung |
|---|---|---|
| `chord_arp` | 4 | 8tel/16tel über aktuelle Akkordtöne, Muster wechselt pro Phrase |
| `lead_phrase` | 3 | 4–8 Takte, Umfang ≤ Oktave + Quinte, Höhepunkt im 2./3. Viertel der Phrase, Schluss auf Akkordton, Auftakte und Durchgangstöne (§1.11) |
| `call_response` | 3 | 2+2 oder 4+4 Takte. Die Antwort variiert den Ruf (Transposition, Umkehrung, anderes Ende) |
| `pluck_seq` | 3 | wiederholte 8tel/16tel-Sequenz mit Akzentmuster, folgt der Harmonie |

### Akkordfarben (Stabs, falls genutzt)
Moll7/Moll9 50 % · Moll-Triade 30 % · sus2/add9 20 %. Voicing mit Stimmführung (§1.10), kein Chord-Memory.

---

## 4. Hard / Industrial

| Merkmal | Wert |
|---|---|
| Tempo-Richtwert | 140–165 BPM |
| Skalen (Gewicht) | Phrygisch 3 · Natürlich Moll 1 · Lokrisch 1 |
| Chromatik-Standard | 35 % |
| Harmonie | statisch / Drone 80 % · 2-Akkord-Wechsel 20 % (oft Halbton: i–♭II) |
| Kick-Standard | `4otf`, Varianten `broken_a`/`broken_b` |

### Bass – Tonbewegung
Grundton ca. 90 %, Oktave und ♭2 als Spannung. Tonumfang MIDI 28–50.

### Bass – Archetypen
| Archetyp | Gewicht | Rhythmus | Charakter |
|---|---|---|---|
| `rumble` | 4 | Start 1 Schritt nach jeder Kick, Länge bis zum Kick-Freiraum vor der nächsten | nur Grundton, gleichmäßig. Hinweis: Klassischer Rumble entsteht oft aus einer verhallten Kick; dieser Archetyp liefert die MIDI-Variante für einen Sub-Synth |
| `hard_offbeat` | 3 | 2, 6, 10, 14 | kurz, hohe Velocity |
| `roll16_aggressive` | 3 | alle 16tel außer Kick | starke Akzente, gelegentliche Oktavsprünge, [v1.1] vereinzelte Ratchets |

### Melodie – Archetypen
| Archetyp | Gewicht | Beschreibung |
|---|---|---|
| `aggro_stabs` | 3 | kurze, akzentuierte Akkorde: Cluster und Quinten |
| `atonal_motif` | 3 | 2–4 Noten, chromatische Intervalle (♭2, Tritonus), stur wiederholt |
| `acid_siren` | 3 | einstimmige 16tel-Line mit Slides und Akzenten ([v1.1] Ratchets), Umfang ≤ Oktave (303-Stil) |
| `sparse_hits` | 2 | 1–4 Treffer pro 1–2 Takte, Grundton/♭2/Tritonus, viel Raum |

### Akkordfarben (Stabs)
Cluster (mit ♭2) 40 % · Quinte 40 % · Moll-Triade 20 %

---

## 5. JSON-Format (Auszug)

```json
{
  "id": "peak_time",
  "version": 1,
  "name": { "de": "Peak Time / Driving", "en": "Peak Time / Driving" },
  "tempo": [128, 135],
  "scales": [{ "id": "natural_minor", "w": 3 }, { "id": "phrygian", "w": 2 }, { "id": "dorian", "w": 2 },
             { "id": "minor_pentatonic", "w": 1 }],
  "chromatic_default": 0.10,
  "harmony": {
    "modes": [{ "id": "static", "w": 50 }, { "id": "two_chord", "w": 40 }, { "id": "four_chord", "w": 10 }],
    "progressions": [["i", "bVII"], ["i", "bVI"], ["i", "iv"]],
    "chord_length_bars": [4, 8]
  },
  "kick_default": "4otf",
  "bass": {
    "range": [28, 52],
    "kick_clearance": "1/32",
    "swing_default": 0.52,
    "movement": { "root": 0.70, "fifth_octave": 0.25, "passing": 0.05 },
    "archetypes": [
      { "id": "rolling16", "w": 4 },
      { "id": "offbeat", "w": 3 },
      { "id": "gallop", "w": 3 }
    ]
  },
  "melody": {
    "range": [55, 88],
    "voicing": { "range": [55, 79], "chord_memory": true },
    "swing_default": 0.55,
    "archetypes": [
      { "id": "hypnotic_motif", "w": 4 },
      { "id": "stabs", "w": 3 },
      { "id": "arp", "w": 2 },
      { "id": "polymeter_seq", "w": 3 }
    ],
    "chord_colors": [{ "id": "min", "w": 40 }, { "id": "fifth", "w": 30 }, { "id": "min7", "w": 20 }, { "id": "sus", "w": 10 }]
  }
}
```

Archetypen sind Code-Bausteine (eine Klasse pro Archetyp mit gemeinsamer Schnittstelle).
Die Profile wählen und gewichten sie nur. Neue Stile entstehen so ohne neuen Code, solange sie
vorhandene Archetypen nutzen.
