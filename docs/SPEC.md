# MidiMaid – Spezifikation

Version 1.0 · Stand 04.10.2026 · Status: bereit für Phase 0 (Host-Spike), offene Punkte siehe `DECISIONS.md`

> **Release-Zuordnung (D-74, D-75):** Alles ohne Markierung gehört zu **v1.0**. Punkte mit **[v1.1]**
> folgen direkt nach v1.0, **[Backlog]** ist zurückgestellt, **[Gestrichen]** wird nicht umgesetzt.
> Maßgeblich ist `docs/FEATURES.md`.
> Datenmodell und Host-Parameter berücksichtigen v1.1 bereits (§3.13, §5), damit später keine Migration
> und keine neuen Parameter-IDs nötig sind.

---

## 1. Ziel

MidiMaid erzeugt **Melodien und Basslines als MIDI** für Techno. Sie entstehen aus einer Kombination
von KI-Modellen (Claude, ChatGPT, Ollama u. a.) und einem eigenen musikalischen Algorithmus.
Das Plugin erzeugt keinen Klang, sondern steuert die Synthesizer des Nutzers.

**Qualitätsanspruch:** Jedes Ergebnis ist sofort musikalisch nutzbar, also in Tonart, im Groove und
stiltypisch. Auch wenn die KI schlechte oder ungültige Antworten liefert, darf das Plugin nie
unbrauchbares MIDI ausgeben, hängende Noten erzeugen oder die DAW instabil machen.
Das Versprechen gilt für alles, was MidiMaid erzeugt (Generieren, Variation, Verfeinern). Eine importierte
eigene Stimme (§3.18) bleibt so, wie der Produzent sie geschrieben hat (D-85).

### 1.1 Abgrenzung zum Wettbewerb
Live 12 bringt regelbasierte Generatoren mit, Captain Plugins und Scaler arbeiten akkordzentriert,
MIDI Agent setzt rein auf Sprachmodelle. MidiMaid unterscheidet sich durch:
- **Techno-Spezialisierung** mit Stilprofilen, Archetypen und Kick-Bezug statt Allround-Ansatz
- **Gekoppelte Stimmen:** Bass und Melodie aus einem Kontext, auch über Instanzen hinweg
- **Hybrid statt reinem LLM:** Die Constraint-Schicht und die Qualitätsbewertung verhindern generische oder
  unbrauchbare Ergebnisse; offline voll nutzbar
- **Live-Einsatz:** Transport-Sync, 16 Slots, quantisierte Wechsel, Automation, ab v1.1 Auto-Evolve und Live-Transponieren
- Siehe `docs/REVIEW.md` für die Auswertung von Reviews vergleichbarer Produkte

### Zielgruppe
- Phase 1: privater Einsatz (Ableton Live 12 Suite, Logic Pro)
- Später: kommerzieller Verkauf. Architektur, Lizenzen und IDs sind von Anfang an darauf ausgelegt.

### Nicht-Ziele (v1)
- Keine Klangerzeugung, keine Drums, keine Akkord-Pads (eventuell später)
- Kein eigener Cloud-Server und kein Abo-Modell (später möglich, Architektur hält das offen)

---

## 2. Unterstützte Hosts und Formate

| Host | Plattform | Format | Variante | Release |
|---|---|---|---|---|
| Ableton Live 12 | macOS, Windows | VST3 | Instrument (MIDI-Out) | v1.0 |
| Logic Pro | macOS | AU (Logic lädt kein VST3) | MIDI-FX (`aumi`) | v1.0 |
| Standalone | macOS, Windows | – | Entwicklung, Test | v1.0 |
| Reaper, Bitwig u. a. | macOS, Windows, Linux | VST3, später CLAP/LV2 | Instrument | Linux später |

**Formatstrategie (D-81, D-82):** Die Architektur ist für alle üblichen Formate und für Linux ausgelegt
(§2.4). v1.0 liefert **VST3** (macOS, Windows) und **AU** (macOS, für Logic inklusive MIDI-FX-Variante);
weitere Formate folgen nachträglich ohne Umbau.

### 2.1 Zwei Plugin-Varianten aus einer Codebasis
Ableton und Logic behandeln MIDI-erzeugende Plugins grundverschieden. Deshalb gibt es zwei Build-Targets,
die denselben Kern nutzen:

1. **MidiMaid** (Instrument-Variante)
   - Stilles Stereo-Audio-Out (nötig, damit Hosts das Plugin laden), MIDI-In und MIDI-Out
     (`NEEDS_MIDI_INPUT` und `NEEDS_MIDI_OUTPUT`)
   - Ableton: Plugin auf eigener MIDI-Spur; eine Synth-Spur holt sich das MIDI über
     „MIDI From → MidiMaid-Spur → Plugin“
   - **Achtung:** Bei Spur-zu-Spur-Routing bietet Live keinen Kanalfilter, alle Kanäle kommen gemischt an.
     Deshalb gibt jede MidiMaid-Instanz **genau eine Stimme** aus (§6.5). Mehrkanalige Ausgabe ist in v1
     nicht vorgesehen (D-63); weil jede Stimme ihren MIDI-Kanal im Datenmodell trägt, bleibt sie nachrüstbar.
2. **MidiMaid MIDI** (MIDI-FX-Variante, nur AU)
   - Logic: im MIDI-FX-Slot einer Instrumentenspur
   - Gibt als **Voice** eine Stimme aus oder dient als **Hub** (siehe §6.5)
   - Hub und Voices auf verschiedenen Spuren bleiben harmonisch und rhythmisch abgestimmt

### 2.2 Identität (fixiert – nie mehr ändern)
Änderungen an diesen Werten machen bestehende DAW-Projekte unbrauchbar.

| Feld | Instrument-Variante | MIDI-FX-Variante |
|---|---|---|
| CMake-Target | `MidiMaid` | `MidiMaidFX` |
| Produktname (in der DAW) | MidiMaid | MidiMaid MIDI |
| Hersteller (`COMPANY_NAME`) | Klirrwerk | Klirrwerk |
| `PLUGIN_MANUFACTURER_CODE` | `Klrw` | `Klrw` |
| `PLUGIN_CODE` | `Mdmi` | `Mdmf` |
| `BUNDLE_ID` | `com.klirrwerk.midimaid` | `com.klirrwerk.midimaid.fx` |
| AU-Typ | `aumu` (Instrument) | `aumi` (MIDI-Prozessor) |
| Formate (Ziel) | VST3, AU, CLAP, LV2, AAX, Standalone | AU (`aumi`), später ggf. CLAP-Note-Effekt |
| CLAP-ID (reserviert) | `com.klirrwerk.midimaid` | `com.klirrwerk.midimaid.fx` |
| LV2-URI (reserviert) | `https://klirrwerk.com/plugins/midimaid` | `https://klirrwerk.com/plugins/midimaid-fx` |
| AAX (reserviert) | aus Manufacturer-/Plugin-Code abgeleitet | – |

Regeln (AU-Kompatibilität): Manufacturer-Code mit mindestens einem Großbuchstaben, Plugin-Code mit
genau einem Großbuchstaben. Die VST3-Klassen-IDs leitet JUCE aus diesen Codes ab.

In Ableton Live ist auf dem Mac **VST3** der Standard-Workflow (gleiches Verhalten wie unter Windows).
Die AU-Instrument-Variante wird trotzdem gebaut und getestet.

### 2.3 Phase-0-Spike (verpflichtend)
Bevor Features gebaut werden, wird das Host-Verhalten mit einem Minimal-Plugin verifiziert:
MIDI-Out-Routing in Live 12 (macOS und Windows, VST3 und AU), MIDI-FX in Logic, Drag & Drop in beiden
DAWs und Transport-Sync. Die Ergebnisse kommen nach `DECISIONS.md`. Für jede Host-Annahme legt
`ROADMAP.md` (Phase 0) einen Ersatzweg fest, falls der Test negativ ausfällt.

### 2.4 Format- und Plattform-Architektur
Ziel: Neue Formate und Linux kommen später per Build-Konfiguration und Validierung dazu, nicht per Umbau.

**Schichten**
- `core` (ohne JUCE) → `engine`, `ai`, `platform` → `plugin` (je Variante genau eine
  `AudioProcessor`-Implementierung) → Format-Wrapper (JUCE bzw. Erweiterung)
- Formatspezifischer Code liegt ausschließlich in einer dünnen Schicht **`HostCapabilities`** in
  `plugin/`. Sie ermittelt zur Laufzeit Format und Host (JUCE `wrapperType`, `PluginHostType`) und meldet
  Fähigkeiten: MIDI-Ausgang vorhanden, Note-Effekt-Slot möglich, Instanzen im selben Prozess (Kopplung),
  Drag & Drop nach außen, Tastatur-Weiterleitung. Funktionen passen sich daran an (z. B. Hinweis
  „Kopplung nicht verfügbar“), statt Formate im Code abzufragen.

**Formate**
| Format | Weg | Status | Hinweise |
|---|---|---|---|
| VST3 | JUCE | v1.0 | Standard für Live auf macOS und Windows |
| AU (v2), Instrument und MIDI-FX `aumi` | JUCE | v1.0 | Pflicht für Logic; Validierung mit `auval` |
| CLAP | Erweiterung `clap-juce-extensions`, später nativ | später | JUCE plant native CLAP-Unterstützung für JUCE 9; die Erweiterung ist inoffiziell und kann bei JUCE-Updates brechen. CLAP kennt Note-Effekte, damit wäre in Bitwig/Reaper auch eine echte MIDI-FX-Variante möglich |
| LV2 | JUCE | später | vor allem Linux |
| AAX | JUCE + Avid-SDK | später | erfordert Avid-Entwicklerprogramm und PACE-Signierung; ob Pro Tools MIDI-Ausgabe aus AAX-Instrumenten für diesen Zweck nutzbar macht, vorher prüfen |
| AUv3 | JUCE | optional | läuft außerhalb des Host-Prozesses → keine Hub-&-Voices-Kopplung ohne zusätzliche Prozess-Kommunikation |
| VST2 | – | ausgeschlossen | Steinberg vergibt keine neuen VST2-Lizenzen |

**Regeln, die ab v1.0 gelten**
- **Formatliste per CMake-Variable** `MIDIMAID_FORMATS` (Standard v1.0: `VST3;AU;Standalone` auf macOS,
  `VST3;Standalone` auf Windows und Linux; die MIDI-FX-Variante nur als AU). Ein neues
  Format = Eintrag ergänzen, Validierung einrichten, testen.
- **Plugin-State formatunabhängig:** gleiche Struktur und `stateVersion` in allen Formaten; jede Variante
  liest den State jeder anderen. Einen direkten Formatwechsel innerhalb der DAW verspricht MidiMaid
  nicht, weil DAWs keinen State zwischen Formaten übertragen. Slots wandern über die Bibliothek
  (Slot-Set speichern und laden) (D-91).
- **Parameter-IDs formatunabhängig:** feste String-IDs, keine Legacy-Parameter-IDs
- **IDs aller Zielformate jetzt reserviert** (§2.2), damit sie sich später nicht ändern
- **Hub & Voices** koppeln nur Instanzen desselben Formats im selben Prozess (§6.5); `HostCapabilities`
  erkennt, wenn das nicht gegeben ist
- **Linux-Bereitschaft:** Plattformcode nur in `platform/` (Keychain macOS, Credential Manager Windows,
  libsecret Linux), Pfade über JUCE-Funktionen, keine macOS-/Windows-Annahmen in `core`, `engine`, `ui`.
  Die CI **kompiliert das VST3-Plugin ab v1.0 auch unter Linux** (ohne DAW-Test), damit Portabilität
  nicht unbemerkt verloren geht. Drag & Drop unter Linux (X11, Wayland über XWayland) wird bei der
  Linux-Einführung geprüft.
- **Validierung je Format:** pluginval (VST3, AU), `auval` (AU), clap-validator (CLAP), lv2lint (LV2)

---

## 3. Funktionsumfang v1

### 3.1 Stimmen
- **Bass** und **Melodie** werden aus einem gemeinsamen **harmonischen und rhythmischen Kontext**
  erzeugt und sind dadurch immer aufeinander abgestimmt.
- Jede Stimme lässt sich einzeln neu erzeugen, variieren, sperren oder stummschalten.
- Pro Stimme gibt es eine Archetyp-Auswahl: „Auto“ (Standard) oder ein bestimmter Archetyp (STYLES.md §1.1).
- Der Bass ist einstimmig. Die Melodie darf Akkorde enthalten (Stabs, Chops).

### 3.2 Stile
| Stil | Tempo-Richtwert | Bass-Charakter | Melodie-Charakter | Typische Skalen |
|---|---|---|---|---|
| Peak Time / Driving | 128–135 | Rollende 16tel, Offbeat, grundtonlastig, spart Kick-Positionen aus | Kurze, hypnotische Motive, Stabs, wenig Tonumfang | Moll, Phrygisch, Dorisch |
| Melodic Techno | 120–128 | Rollend oder Offbeat, folgt der Harmonie | Arpeggien, längere Phrasen, Call & Response, 8–16 Takte | Natürlich Moll, Dorisch, Harmonisch Moll |
| Hard / Industrial | 140–165 | Rumble-taugliche lange Grundtöne oder harte Offbeats, wenig melodisch | Kurze, aggressive Motive, chromatisch und dissonant, Stabs | Phrygisch, Lokrisch, chromatisch |

Stilprofile sind Daten (JSON in `resources/styles/`), kein fest verdrahteter Code. So lassen sich Stile
erweitern, ohne den Generator anzufassen. **Die vollständigen Profile mit Archetypen, Gewichten,
Kick-Rastern, Harmonie und Akkordfarben stehen in `docs/STYLES.md` (verbindlich).**

### 3.3 Steuerung
- **Textprompt** („dunkle, treibende Bassline mit Slides“)
- **Eigene Stimme importieren** (§3.18): eigene Bassline übernehmen, MidiMaid schreibt die passende Melodie
- **Referenzen und persönlicher Stil** (§3.20): eigene MIDI-Dateien in benannten Sets pro Stil
  lenken Progressionen, Motive, Rhythmen und KI
- **Drum-Referenz** (§3.14): Drum-Clip aus der DAW ziehen, damit Bass und Melodie zum echten Groove
  des Tracks passen
- **Verfeinern per Prompt** (§3.15): das bestehende Pattern per Text anpassen
- **[v1.1] Live-Transponieren** per MIDI-Eingang (§3.16)
- **Basisparameter** (kompakt, immer sichtbar): Stil, Tonart, Skala, Länge in Takten, Seed
- **[Backlog] Kick-Grundton** mit Tonart-Vorschlag (STYLES.md §1.6)
- Hinweis: Die globale Skala von Live 12 und die Projekttonart von Logic sind für Plugins nicht lesbar.
  Die Tonart wird deshalb im Plugin gesetzt und innerhalb einer Hub-Gruppe geteilt.
- **Erweiterte Parameter** (einklappbar): Tonumfang und Oktav-Offset pro Stimme, Kick-Raster,
  [v1.1] Chromatik-Regler
- **Energie** ist der Makro-Regler (Hauptoberfläche): Er steuert Dichte, Velocity, Akzente, Oktavsprünge
  und Pausen gemeinsam (STYLES.md §1.5). Es gibt keinen separaten Dichte-Regler.
- **Groove pro Stimme** (siehe §3.9)

### 3.4 Nutzungsmodi
- **Live:** Das Plugin spielt das aktive Pattern synchron zum DAW-Transport (Loop). Neue Patterns und
  Variationen wechseln quantisiert. Der Zeitpunkt ist einstellbar: nächster Schlag, nächster Takt,
  nächste 4/8/16 Takte, Ende der Phrase, Ende des Patterns. Standard ist „nächster Takt“.
  - **Einsatzposition:** „Neustart“, das neue Pattern beginnt am Wechselzeitpunkt bei Takt 1.
    [v1.1] Alternativ „Legato“: Die Position läuft weiter wie bei Lives Legato-Modus.
  - „Neustart“ ist nur an Taktgrenzen erlaubt. Bei Quantisierung „nächster Schlag“ gilt automatisch
    „Legato“, sonst läge Schritt 0 des Patterns nicht mehr auf der Eins und Kick-Raster, Phrasen und
    Akzente wären verschoben.
  - **Geplante Wechsel:** Jeder Wechsel bekommt einen PPQ-Zeitstempel. Liegt der nächste
    Quantisierungspunkt näher als der **Mindestvorlauf** (Standard 150 ms, Einstellung in der
    Experten-Ebene, D-90), wird der übernächste gewählt. Gemessen wird ab der am weitesten
    fortgeschrittenen Position aller Instanzen der Gruppe (jede Instanz meldet ihre zuletzt verarbeitete
    PPQ-Position, §6.5); die Zeit wird mit dem aktuellen Tempo in PPQ umgerechnet. So erreichen Wechsel
    auch Voices auf anderen Spuren rechtzeitig. Für Slot-Automation in Hub-Gruppen siehe §6.1a (D-131).
- **Clip:** Das Pattern wird per Drag & Drop als .mid-Datei aus dem Plugin in die DAW gezogen, eine
  Datei pro Stimme.
  - Die Datei enthält **das aktuelle DAW-Tempo** als Tempo-Event sowie Taktart und Spurname
  - Dateiname: `MidiMaid_<Slotname>_<Stimme>.mid`
  - Groove wird gemäß §3.9 eingerechnet, Slides bleiben als Überlappung erhalten
  - [v1.1] Wahrscheinlichkeit und Bedingungen (§3.19) lassen sich in .mid nicht speichern. Standard: Der Export
    enthält so viele Durchläufe, wie der längste Bedingungs-Zyklus braucht (max. 8), mit genau den Noten,
    die beim Abspielen erklingen würden. Alternativ „alle Noten“ (ohne Auswertung).
  - Temporäre Exportdateien werden beim Beenden und beim nächsten Start aufgeräumt
  - Drag & Drop gibt es im Hub (jede Stimme einzeln) und in jeder Voice (eigene Stimme)
  - [Gestrichen] Kombinierter Export aller Stimmen in einer Datei
- **Aufnahme:** Alternativ zum Drag & Drop wird die MIDI-Ausgabe direkt als Clip/Region aufgenommen.
  In Live: Synth-Spur mit „MIDI From → MidiMaid-Spur (eine Stimme)“ scharf schalten und aufnehmen. In Logic:
  „Record MIDI to Track Here“ hinter MidiMaid (Hub oder Voice) einfügen und aufnehmen.
  Die UI erklärt den Workflow über ein Hilfe-Symbol.

### 3.5 Bearbeitung im Plugin
- Piano-Roll mit einer Spur pro Stimme (v1: Bass, Melodie): Noten setzen, löschen, verschieben, Länge
  und Velocity ändern, Slide und Akzent markieren
- **Akzent ist ein eigenes Merkmal der Note** (Flag), nicht aus der Velocity abgeleitet. Bei der Ausgabe
  bekommt eine Akzent-Note die Akzent-Velocity. Die Akzent-Schwelle wird nur beim Import und bei der
  Analyse von fremdem MIDI benutzt, um Akzente zu erkennen.
- Rasterung (1/4 bis 1/32, Triolen optional), Ausrichtung immer an Skala oder Chromatik umschaltbar.
  Triolen liegen als Ticks im Datenmodell (960 PPQ, 16tel-Triole = 160 Ticks); Groove und Swing
  verschieben sie nicht (§3.9).
- Rückgängig und Wiederholen (`juce::UndoManager`)
- Velocity-Spur unter den Noten (Balken ziehen, mehrere gleichzeitig)
- **Stimme sperren:** Eine gesperrte Stimme bleibt bei Generieren und Variieren unverändert
- **[v1.1] Hilfen:** Velocity-Spur umschaltbar auf Wahrscheinlichkeit und Bedingung (§3.19), Scale-Fold
  (nur Skalentöne als Zeilen, wie in Ableton), Phrasengrenzen und Kick-Raster als Hintergrund, eigene
  Zeile für Slide- und Akzent-Markierungen
- **[v1.1] Sperren pro Dimension:** Tonhöhe, Rhythmus (Position und Länge) und Velocity pro Note und pro
  Stimme getrennt sperren. Beispiel: Tonfolge behalten, neuen Rhythmus würfeln.

### 3.6 Variationen per Knopfdruck
Algorithmische Mutationen, deterministisch über einen Seed, in zwei Gruppen:
- **Subtil** (hypnotisch, Standard bei geringer Stärke): Notenlänge, Velocity-Kontur, Akzent
  verschieben, einzelne Note ersetzen; [v1.1] Ratchet hinzufügen oder entfernen, Wahrscheinlichkeit
  einzelner Zier- und Geisternoten setzen
- **Strukturell:** Rhythmus verschieben, Noten innerhalb von Skala oder Akkordtönen austauschen,
  Oktavsprünge, Dichte erhöhen oder verringern, Motiv umkehren oder spiegeln, Call & Response tauschen
- Gesperrte Stimmen (und ab v1.1 gesperrte Dimensionen, §3.5) bleiben unverändert
- Regler „Stärke“ (0–100 %)
- Variationen sind **rein algorithmisch** (schnell, offline, ohne Kosten). Für KI-Ergebnisse dient „Generieren“.
- **Verlauf und Rückgängig:** Der Verlauf ist eine Liste der letzten 20 *Ergebnisse* pro Slot
  (Generieren, Variation, Verfeinern, ab v1.1 mit „Behalten“ übernommene Auto-Evolve-Stufen). Jede dieser Aktionen ist zugleich *ein* Schritt im
  Undo-Stapel. Notenbearbeitungen sind nur Undo-Schritte, keine Verlaufseinträge. Cmd+Z macht also immer
  den letzten Schritt rückgängig, Cmd+[ / Cmd+] blättert nur durch Ergebnisse.

### 3.7 Patterns und Phrasen (bis 16 Takte)
Patterns sind 1, 2, 4, 8 oder 16 Takte lang ([Backlog]: bis 64 Takte). Längere Abläufe entstehen in der
DAW über Slots und Slot-Automation. Patterns ab 8 Takten werden aus **Phrasen** zusammengesetzt:
- Ein Abschnitt besteht aus Phrasen zu 4, 8 oder 16 Takten (z. B. A A' A B)
- Phrasen decken das Pattern lückenlos und ohne Überschneidung ab. Patterns mit 1, 2 oder 4 Takten haben
  genau eine Phrase in Pattern-Länge mit der Rolle Hauptmotiv.
- Pro Phrase gibt es eine Rolle: Hauptmotiv, Variation, Steigerung, Ausdünnung, Antwort
  (im Code `Main`, `Variation`, `Build`, `Breakdown`, `Answer`)
- Optional hat eine Phrase ein eigenes Kick-Raster, z. B. `halftime` im Breakdown (STYLES.md §1.2)
- **[v1.1] Turnaround** ist keine Rolle, sondern ein Merkmal einer Phrase: Ihr letzter Takt wird als
  Turnaround variiert (nur bei 8- und 16-Takt-Phrasen, STYLES.md §1.12)
- Die KI liefert Motive und einen Formplan, der Algorithmus erzeugt daraus die Varianten
- In der Piano-Roll sind Phrasengrenzen sichtbar, Phrasen lassen sich einzeln neu erzeugen, variieren
  und sperren
- Das hält Antworten klein (Kosten, Latenz, Fehlerquote) und sorgt für musikalisch sinnvolle Entwicklung
  statt zufälliger Wiederholung

### 3.8 Chromatik-Anteil
- Stilabhängige Werte: Melodic 0 %, Peak Time 10 %, Hard/Industrial 35 %. Sie bestimmen, wie viele Töne
  die Skala verlassen dürfen und wie stark.
- [v1.1] Als Regler 0–100 % in der Experten-Ebene einstellbar
- Bei 0 % rastet die Constraint-Schicht alles auf die Skala ein. Darüber werden skalenfremde Töne
  gezielt gesetzt (Durchgangs- und Wechseltöne, b2/b5-Spannungen, Cluster bei hohen Werten), nicht zufällig.
- Wird an die KI als Vorgabe übergeben und von der Constraint-Schicht unabhängig davon durchgesetzt.

### 3.9 Swing und Groove
- Pro Stimme: Swing-Regler (50–75 %, bezogen auf 16tel) und eine Groove-Vorlage. Standardwerte und
  Hinweise zu Bass-Swing in STYLES.md §1.15.
- Groove-Vorlage = Timing-Versatz und Velocity-Profil pro 16tel-Schritt über 1–2 Takte (JSON)
- v1.0: „Straight“ plus Swing-Regler sowie der Groove aus einer Drum-Referenz (§3.14)
- [v1.1] Mitgelieferte Vorlagen, z. B. 16tel-Swing in mehreren Stärken, Shuffle, „Laid back“, „Pushed“,
  Humanize (leichte Zufallsabweichung, deterministisch über Seed); Groove als eigene Vorlage speichern
- **Kein doppelter Swing:** Enthält die gewählte Vorlage selbst Swing (z. B. aus einer Drum-Referenz,
  §3.14), ist der Swing-Regler deaktiviert und zeigt den Wert der Vorlage an. Der Regler wirkt nur auf
  gerade Vorlagen.
- **Nicht-destruktiv:** Noten bleiben im Raster gespeichert. Groove wird erst bei der Ausgabe angewendet,
  in der Engine und beim Export (Drag & Drop). Die Piano-Roll kann die Groove-Positionen als Vorschau
  zeigen. Ein Schalter „Groove beim Export einrechnen“ ist standardmäßig an.
- Slide-Überlappungen bleiben auch mit Groove erhalten.
- Groove-Versatz und Swing gelten nur für Noten, die auf einem 16tel-Schritt beginnen. Triolen und frei
  gesetzte Noten bleiben zeitlich unverändert.

### 3.10 Pattern-Slots
- **16 Slots pro Instanz.** Jeder Slot ist ein vollständiges Pattern inklusive Archetypen, Groove,
  Kick-Raster, Formplan und Generierungsinfo.
- UI: Slot-Leiste (16 Felder, benennbar, farbig markierbar, z. B. Intro, Drop, Breakdown), leere Slots
  erkennbar. Kopieren, Einfügen, Leeren, Tauschen per Ziehen.
- Wechsel quantisiert nach §3.4. Generieren und Variieren wirken auf den gewählten Slot.
- Hub-Gruppe: Ein Slot-Wechsel im Hub wechselt alle Voices auf denselben Slot.
- **Datenmodell (`core/SlotBank`, D-137):** Ein Slot hat ein Pattern (alle Stimmen; leer = kein Pattern), einen
  Namen (höchstens 32 Bytes UTF-8, ohne Steuerzeichen; leer zeigt die UI als „Slot n“), eine Farbmarke (0 = keine,
  1 bis 8 = Palette der UI) und den Verlauf der letzten 20 Ergebnisse (§3.6). Generieren, Variation, Verfeinern,
  Einfügen, Laden aus der Bibliothek und Import sind Ergebnisse (ein Verlaufseintrag, das alte Pattern bleibt
  über ◀ erreichbar); Notenbearbeitungen ersetzen nur das aktuelle Pattern. ◀ und ▶ setzen das aktuelle Pattern
  auf den Eintrag. Kopieren übernimmt Pattern, Noten-IDs, Zähler, Name und Farbe; Leeren setzt den Slot
  vollständig zurück; Tauschen vertauscht alles. Jedes gespeicherte Pattern bekommt die nächste fortlaufende
  `Pattern::version` der Instanz.
- Bibliothek: einzelne Slots speichern und laden, zusätzlich ganze Slot-Sets (16 Slots) als „Set“.
- **[v1.1] Session-Ansicht in Live:** Slots lassen sich mit Szenen verknüpfen, indem man auf der Hub-Spur leere
  Clips mit einer Clip-Hüllkurve für den Slot-Parameter anlegt. Startet eine Szene, wechselt der Slot
  (Erklärung in der Setup-Hilfe, Vorlage in §8.6).

### 3.11 [v1.1] Vorhören bei gestopptem Transport
- Play-Knopf im Plugin spielt den aktiven Slot mit **interner Clock**: im Tempo der DAW, sonst mit dem
  zuletzt bekannten Tempo, ersatzweise 125 BPM
- Sobald der DAW-Transport startet, stoppt die Vorschau (mit Note-Offs) und der Transport-Sync übernimmt
- Die Vorschau gibt MIDI auf denselben Kanälen aus wie im Normalbetrieb. In einer Hub-Gruppe startet der
  Play-Knopf des Hubs auch die Voices (geplanter Start wie in §3.4).
- Ob Logic einen MIDI-FX bei gestopptem Transport rendert (eventuell nur bei ausgewählter oder
  aufnahmebereiter Spur), wird im Phase-0-Spike geprüft. Die UI erklärt die Einschränkung gegebenenfalls.

### 3.12 Bibliothek
- Speichern mit Name, Stil, Tonart, Seed und Prompt; Laden; Import und Export als .mid
- [v1.1] Tags, BPM-Bereich, Bewertung (1–5 Sterne), Suche, Filter, Vorhören
- **[v1.1] Favoriten** = Bewertung ab 4 Sternen; sie landen automatisch im Referenz-Set „Favoriten“ des Stils
  (§3.20). Bewerten geht mit einem Klick direkt im Hub, auch ohne vorheriges Speichern.
- Ansicht **Referenzen**: Set pro Stil, Import-Fläche, Liste mit Rolle und erkannter Tonart, Gewicht,
  an/aus; [v1.1] mehrere Sets, automatische Rollen-Erkennung mit Konfidenz, Tags, Set-Zusammenfassung
- Speicherort: Benutzer-Anwendungsdatenordner (siehe §9.2)

### 3.13 Automation und Controller
Automatisierbare Host-Parameter (`AudioProcessorValueTreeState`), über DAW-Automation und das
MIDI-Mapping der DAW steuerbar:

**Parameter-Register (normativ, D-93).** IDs sind feste Strings, `versionHint` ist 1 für alle hier
angelegten Parameter.

| ID | Parameter | Typ, Bereich | Standard | Hinweis | Aktiv ab |
|---|---|---|---|---|---|
| `slot` | Slot | ganzzahlig 1–16 | 1 | Wechsel quantisiert, beim Start und nach Sprüngen sofort | v1.0 |
| `mute_1` … `mute_8` | Mute Stimme 1 … 8 | Schalter | aus | sofort, mit Note-Off; aktiv: 1 = Bass, 2 = Melodie | v1.0 |
| `transpose` | Transponieren | ganzzahlig −12 … +12 | 0 | §3.16 | v1.1 |
| `generate` | Generieren | Trigger (Schalter) | aus | Flanke 0 → 1 löst aus | v1.1 |
| `variation_1` … `variation_8` | Variation Stimme 1 … 8 | Trigger (Schalter) | aus | Flanke 0 → 1 löst aus | v1.1 |
| `variation_all` | Variation alle | Trigger (Schalter) | aus | Flanke 0 → 1 löst aus | v1.1 |
| `evolve` | Auto-Evolve | Schalter | aus | §6.7 | v1.1 |
| `evolve_keep` | Evolve behalten | Trigger (Schalter) | aus | übernimmt die klingende Stufe in den Slot | v1.1 |
| `creativity` | Kreativität | 0–100 % | 40 % | wirkt erst bei der nächsten Generierung (§7.6) | Backlog |
| `energy` | Energie | 0–100 % | 50 % (Startwert) | wirkt erst bei der nächsten Generierung | Backlog |

**Umsetzung (D-138):** Das Register liegt als Tabelle in `core/ParameterRegister` (ohne JUCE); der Test
`the parameter IDs are fixed` schreibt die IDs fest. Die Namen sind host-sichtbar, englisch und sprachunabhängig
(„Slot“, „Mute 1“, „Variation All“ …), weil sie in Automationsspuren und gespeicherten Projekten stehen; sie laufen
nicht über die Übersetzungstabelle (§8.2). Ganzzahlige Parameter sind `AudioParameterInt`, Schalter und Trigger
`AudioParameterBool`; nicht aktive Parameter sind mit `withAutomatable(false)` angelegt. Die JUCE-Wrapper machen
daraus bei VST3 ein Parameter ohne `kCanAutomate` und bei AU das Flag `NonRealTime`; die Parameter bleiben sichtbar
(Verhalten in Live und Logic: ROADMAP Phase 0, „Nicht aktive Parameter“). Der Audio-Thread liest `slot` und
`mute_<Stimme>` am Blockanfang aus den Roh-Atomics und schreibt nie einen Parameter. Eine Instanz liest vorerst
`mute_1` (die erste Stimme); die Zuordnung der Stimme kommt mit den Rollen (§6.5).

Alle Parameter werden **schon in v1.0 mit fester ID angelegt**. Nicht aktive Parameter sind ohne Wirkung
und als nicht automatisierbar markiert; VST3 und AU können Parameter nicht zuverlässig ausblenden, deshalb
tragen sie schon ihren späteren Namen. Wie Live und Logic sie anzeigen, prüft Phase 0.

- Trigger werden im Audio-Thread nur als atomares Flag erkannt und auf dem Message-Thread ausgeführt
- **Parameter für 8 Stimmen werden schon in v1 angelegt** (inaktive ausgeblendet), weil Parameter-IDs
  später nicht mehr hinzugefügt werden sollten, ohne Automationen zu gefährden
- In einer Voice wirken Mute und Transponieren auf die eigene Stimme, Trigger werden an den Hub weitergereicht.
  Der Slot-Parameter einer Voice wirkt nur im Modus „Slot: eigen“; im Standard „folgt Hub“ folgt die Voice dem Hub (D-41, D-131, §6.1a).
- **Start mitten im Arrangement:** Beim Start der Wiedergabe und nach jedem Sprung gilt der Wert des
  Slot-Parameters **sofort**, ohne Quantisierung. Wer bei Takt 37 startet, hört direkt den dort
  automatisierten Slot. Die Position im Pattern richtet sich dann am Taktraster des Songs aus (§6.1);
  Slot-Wechsel im Arrangement deshalb auf Vielfache der Pattern-Länge legen (Hinweis in der Setup-Hilfe).
- **Offline-Rendern / Bounce (non-realtime):** Generieren- und Variations-Trigger werden ignoriert,
  weil KI-Anfragen weder schnell noch reproduzierbar sind. Slot-Wechsel und Mute funktionieren
  deterministisch. Empfohlener Workflow für Arrangements: Ergebnisse in Slots ablegen und Slots automatisieren.
  Gespielt wird der aktuelle Stand der Slots im Speicher, also das, was auch beim Abspielen klingt
  (ohne Evolve-Ebene). Ergebnisse von KI-Anfragen oder anderen Hintergrundjobs, die während des
  Renderns eintreffen, werden erst danach übernommen.
- Die Parameter-IDs sind ab dem ersten produktiven Einsatz fixiert, weil sonst Automationen in
  DAW-Projekten verloren gehen
- [v1.1] Beim Offline-Rendern ist Live-Transponieren per Tastatur inaktiv; MIDI aus Regionen/Clips auf
  derselben Spur wird normal verarbeitet

### 3.14 Drum-Referenz (Kick und Hi-Hat)
Zweck: Bass und Melodie richten sich nach dem tatsächlichen Drum-Groove statt nach einem Standard-Raster.

- **Quellen:** Drum-Clip bzw. -Region direkt aus der DAW ins Plugin ziehen oder eine .mid-Datei
  ([Gestrichen]: Capture des eingehenden MIDI)
- **Zuordnung:** Standard nach General MIDI (Kick 36, Snare 38, Clap 39, Closed Hat 42, Open Hat 46,
  Ride 51). Abweichende Belegungen über eine Liste in den Einstellungen (Note → Rolle), z. B. für Drum
  Racks ([Gestrichen]: „Lernen“ per Tastenanschlag).
- **Abgeleitet werden:**
  - eigenes Kick-Raster `custom` (bis 2 Takte) für Bass-Aussparung und Kick-Freiraum
  - Groove-Vorlage aus dem Timing und der Velocity der Hi-Hats (Versatz pro 16tel)
  - Akzentpositionen (z. B. Open-Hat-Offbeats) als Hinweis für Bass- und Melodie-Akzente
  - Dichte-Referenz für die Energie
- **[v1.1] Melodie-Rhythmus relativ zu den Hats:** „frei“ (Standard), „mit Hats“ (Noten bevorzugt auf
  Hat-Positionen), „gegen Hats“ (Lücken füllen)
- Die Referenz wird pro Slot gespeichert (neue Slots übernehmen die zuletzt verwendete) und in einer
  Hub-Gruppe geteilt
- Eine Drum-Referenz kann im Hub oder in einer Voice abgelegt werden; sie wird an den Hub übergeben

### 3.15 Verfeinern per Prompt
Zweck: ein gutes Ergebnis gezielt anpassen, statt neu zu würfeln („weniger Noten“, „mehr Synkopen“,
„letzter Takt höher“, „Bass mehr Slides“).

- Eingabefeld „Verfeinern“ unter dem Prompt, wirkt auf den aktiven Slot, wahlweise auf eine Stimme oder
  eine Phrase
- Die KI erhält das aktuelle Pattern kompakt im Schema v1 (§7.3), das Stilprofil, die Sperren und die
  Anweisung. Sie liefert das geänderte Pattern im selben Schema.
- **Kontext:** Die letzten 5 Verfeinerungen des Slots werden mitgeschickt, damit „noch etwas weniger“
  funktioniert. Neu generieren setzt den Kontext zurück.
- Jede Note trägt eine stabile ID (§5). Die KI muss IDs unveränderter oder geänderter Noten
  beibehalten und neue Noten ohne ID liefern. Gesperrte Dimensionen werden anhand der ID
  wiederhergestellt; fehlt eine gesperrte Note in der Antwort, wird sie wieder eingefügt. Unbekannte
  oder doppelte IDs gelten als neue Noten (Regeln für Noten-IDs in §5).
- Die KI erhält das Pattern ohne importierte Stimmen, außer der Nutzer hat für den Provider zugestimmt
  (§3.20, D-86); das Absenden von „Verfeinern“ gilt als Zustimmung für die übrigen Stimmen.
- Danach laufen Constraint-Schicht und Qualitätsbewertung wie immer (ohne Wiederholungsschutz, STYLES.md §1.14)
- Jede Verfeinerung landet im Verlauf und ist rückgängig zu machen
- Bei Patterns über 8 Takten wird auf Motiv- bzw. Phrasenebene verfeinert (§3.7)
- Ohne KI ist Verfeinern nicht verfügbar; für Anpassungen ohne KI dienen die Variationsknöpfe
  ([Gestrichen]: Offline-Verfeinern per Schlüsselwort)

### 3.16 [v1.1] Live-Transponieren
Zweck: das laufende Pattern im Live-Set per Keyboard transponieren, wie bei einem Hardware-Sequencer.

- Eingehende Note relativ zu einer **Referenznote** (Standard: Grundton der Tonart in der Bass-Lage,
  einstellbar) ergibt die Transposition in Halbtönen, begrenzt auf ±12
- **Modus:** chromatisch (alles wird verschoben, klassische Sequencer-Transposition, Standard) oder
  diatonisch (Verschiebung um Skalenstufen, die Tonart bleibt)
- **Verhalten:** „Halten“ (bleibt bis zur nächsten Taste, Standard) oder „Momentan“ (nur solange die
  Taste gedrückt ist, danach zurück auf 0)
- **Zeitpunkt:** ab der nächsten Note (Standard), nächster Schlag oder nächster Takt
- **Ziel:** Bass, Melodie oder beide (Standard: beide). In einer Hub-Gruppe wird die Transposition geteilt.
- Verlässt eine Stimme durch die Transposition ihren Tonumfang, wird die ganze Stimme um eine Oktave
  verschoben, damit die Kontur erhalten bleibt; Noten, die danach noch außerhalb liegen, werden einzeln
  oktaviert
- Nicht-destruktiv wie der Groove: wirkt auf die Ausgabe, nicht auf die gespeicherten Noten. Export per
  Drag & Drop übernimmt die aktuelle Transposition (abschaltbar).
- Auch als Host-Parameter automatisierbar (§3.13), damit Bounces reproduzierbar sind
- **Zwei Quellen, keine Rückschreibung:** Wirksame Transposition = Parameter + Tastatur-Offset
  (begrenzt auf ±12). Der Tastatur-Offset wird **nicht** in den Host-Parameter zurückgeschrieben, weil
  Parameteränderungen aus dem Audio-Thread host-abhängig unzuverlässig sind und mit gelesener
  Automation kollidieren würden.
- **In einer Hub-Gruppe** gilt als frühester Zeitpunkt „nächster Schlag“ mit geplantem Wechsel (§3.4),
  damit Hub und Voices gleichzeitig transponieren. „Ab der nächsten Note“ gibt es nur ohne Voices.
- Voices zeigen die Gruppen-Transposition an, transponieren aber nicht eigenständig in Halbtönen (das
  würde die Harmonie brechen); pro Voice gibt es nur den Oktav-Offset.
- **Technisch:** Die Note-Off-Tabelle speichert die tatsächlich gesendete Tonhöhe, damit eine
  Transposition während klingender Noten keine hängenden Noten erzeugt (§6.2)

### 3.17 [v1.1] MIDI-Eingang
In v1.0 wertet MidiMaid den MIDI-Eingang nicht aus (er ist aber schon deklariert). Ab v1.1:
1. **Live-Transponieren**, falls aktiviert (optional auf einen MIDI-Kanal beschränkt)
2. sonst wird der Eingang ignoriert

Eingehendes MIDI wird standardmäßig **nicht** an den Ausgang durchgereicht (Option „MIDI Thru“ für
Sonderfälle).

### 3.18 Eigene Stimme importieren
Zweck: Der häufigste Fall im Studio: „Ich habe schon eine Bassline, gib mir eine passende Melodie“
(oder umgekehrt).

- MIDI (Clip/Region aus der DAW oder .mid) auf die Spur einer Stimme ziehen → der Inhalt **ersetzt**
  diese Stimme im aktiven Slot und ist in allen Dimensionen gesperrt
- Die Analyse leitet daraus Tonart, Skala und Akkordfolge ab (bei einer Bassline: Grundton pro Takt bzw.
  Halbtakt). Diese bilden den harmonischen Kontext für die übrigen Stimmen.
- Generieren, Variieren und Verfeinern betreffen dann nur die anderen Stimmen
- Importiertes Material wird **nicht** von der Constraint-Schicht verändert (kein Einrasten, keine
  Kick-Aussparung) und es gibt keine Rückfrage dazu (D-85). Es bleibt so, wie der Produzent es geschrieben
  hat. Die anderen Stimmen richten sich danach.
- **Grenzen des Imports:** nur 4/4 (sonst Hinweis, kein Import). Die Länge wird auf die nächste erlaubte
  Pattern-Länge aufgerundet (1, 2, 4, 8, 16 Takte; der Rest bleibt leer), von längerem Material zählen nur
  die ersten 16 Takte (mit Hinweis). Positionen werden tickgenau übernommen, auch Triolen; quantisiert wird
  nur für die Analyse.
- Die erkannte Tonart ersetzt die Tonart des Slots bzw. der Hub-Gruppe und ist in der UI korrigierbar. Sind
  mehrere Stimmen importiert, wird der Kontext aus allen gemeinsam ermittelt.
- An einen Cloud-Provider geht eine importierte Stimme nur mit Zustimmung (§3.20, D-86); ohne sie erhält
  die KI nur den abgeleiteten Kontext.
- Optional „Sperre lösen“: Danach behandelt MidiMaid die Stimme wie eine generierte (Variationen möglich)

### 3.19 [v1.1] Wahrscheinlichkeit und Bedingungen pro Note
Zweck: Variation in jedem Durchlauf, ohne das Pattern zu verändern (wie Trig-Bedingungen bei Elektron
oder die Note-Chance in Live 12).

- **Wahrscheinlichkeit** pro Note: 0–100 % (Standard 100 %)
- **Bedingung A:B** pro Note: Die Note spielt nur im A-ten von je B Durchläufen (z. B. 4:4 = nur im
  vierten Durchlauf, typisch für Fills vor dem Wechsel). Standard 1:1 = immer. B maximal 8.
- Ein Durchlauf ist ein Pattern-Durchlauf; der Zähler ergibt sich aus der PPQ-Position seit dem
  Wechselzeitpunkt bzw. aus dem Songraster (§6.1)
- **Deterministisch:** Ob eine Note mit Wahrscheinlichkeit erklingt, entscheidet eine Hash-Funktion aus
  Pattern-Seed, Noten-ID und Durchlaufnummer. Dieselbe Song-Position klingt also immer gleich, beim
  erneuten Abspielen, beim Bounce und in allen Instanzen einer Hub-Gruppe.
- Fällt eine Note mit Slide weg, entfällt die Überlappung; fällt die Zielnote weg, endet die Slide-Note
  normal (Ausgabestufe, §4.2a). Ratchets gehören zur Note und fallen mit ihr weg.
- Wahrscheinlichkeit und Bedingung zählen zur Sperr-Dimension **Rhythmus**
- Generatoren setzen Wahrscheinlichkeiten sparsam und stilabhängig (STYLES.md §1.16); die
  Qualitätsbewertung rechnet mit der erwarteten Dichte

### 3.20 Referenzen und persönlicher Stil
Zweck: Eigene, bewährte Melodien, Basslines und Progressionen lenken die Generierung, damit MidiMaid
nach dem eigenen Sound klingt statt nach dem Durchschnitt.

**Stufen:**
- **v1.0:** ein Referenz-Set pro Stil; Import von Dateien, Ordnern und DAW-Clips; Rolle wird beim Import
  zugeordnet (Bass, Melodie, Akkorde, Drums); Tonart- und Skalenerkennung; Akkordfolgen fließen in den
  Progressions-Generator; KI-Beispiele aus dem Set; Kopierschutz; Set-ID pro Pattern
- **[v1.1]:** mehrere benannte Sets, automatisches Set „Favoriten“, automatische Rollen-Erkennung,
  Motiv-Zellen, Rhythmus-Statistik, Profil pro Set mit Regler „Persönlich“ und Bewertungskriterium
  „Nähe zum Set“, Profil-Schnappschuss
Die folgenden Unterpunkte beschreiben den Zielzustand nach v1.1.

**Referenz-Sets**
- Pro Stil beliebig viele **benannte Sets** (z. B. „Klirrwerk 2026“, „Warehouse“, „Live-Set Berlin“)
- Jeder Stil hat zusätzlich ein automatisches Set **„Favoriten“**: Patterns ab 4 Sternen landen dort
  automatisch, optional auch im gerade aktiven Set (Standard an)
- Eine Datei liegt nur einmal in der Sammlung und kann in mehreren Sets stecken. Pro Set und Eintrag:
  an/aus, Gewicht (1–5), Tags
- **Aktives Set** wählbar im Hub (Basis-Ebene, pro Stil gemerkt); jedes Pattern speichert, mit welchem
  Set es erzeugt wurde. In einer Hub-Gruppe gilt das Set des Hubs.

**Import**
- Viele .mid-Dateien oder ganze Ordner auf einmal, oder Clips/Regionen direkt aus der DAW
- Dateien mit mehreren Spuren werden in Spuren zerlegt; Nicht-4/4-Material wird übersprungen (mit Hinweis),
  Triolen werden aufs 16tel-Raster quantisiert oder übersprungen
- Analyse im Hintergrund, inkrementell, Ergebnis pro Datei zwischengespeichert (Schlüssel: Datei-Hash)

**Analyse pro Spur** (Ergebnis tonartunabhängig, als Stufen statt absoluter Töne)
- **Rolle:** Drums (Kanal 10 oder typische Drum-Noten), Akkorde (regelmäßig ≥ 3 gleichzeitige Töne),
  Bass (tiefste einstimmige Spur), sonst Melodie. Jede erkannte Rolle ist in der UI korrigierbar.
- **Tonart und Skala:** Tonhöhenklassen-Profil (dauergewichtet), Auswahl der bestpassenden Skala aus
  den Skalen des Stils, mit Konfidenzwert
- **Akkordfolge:** aus Akkordspuren pro Takt bzw. Halbtakt, sonst aus Bass-Grundtönen und
  Melodietönen abgeleitet; jede Folge trägt einen Konfidenzwert, unsichere Folgen zählen weniger
- **Rhythmusmuster** pro Takt und Rolle (16tel-Raster), **Groove** (Timing-Versatz, Velocity-Verlauf)
- **Motiv-Zellen:** typische Intervall- und Rhythmusfolgen über 1–2 Takte, mit Häufigkeit
- Dichte, Register, Wahrscheinlichkeits-Einsatz (bei Favoriten)
- Drum-Spuren können optional als Groove-Quelle dienen (wie die Drum-Referenz, §3.14)

**Wirkung auf den Algorithmus** (ab 5 aktiven Einträgen im Set)
- **Progressionen:** Die Akkordfolgen des Sets gehen gewichtet in den Progressions-Generator ein,
  neben den Progressionen des Stilprofils
- **Motive:** Die Motiv-Engine kann eine Motiv-Zelle des Sets als Ausgangspunkt nehmen und variiert sie
  (STYLES.md §1.11)
- **Rhythmen:** Rhythmusmuster des Sets ergänzen die Rhythmen der Archetypen
- **Profil:** Archetyp-Gewichte, Skalenwahl, Dichte, Chromatik, Register, Groove; Abweichungen vom
  Stilprofil höchstens ±50 %, damit der Stil erkennbar bleibt
- **Regler „Persönlich“** (0–100 %, Standard 50 %, Experten-Ebene): wie stark das Set wirkt; gleiches
  Gewicht für das Kriterium „Nähe zum Set“ in der Qualitätsbewertung

**Wirkung auf die KI**
- Pro Anfrage bis zu 3 Einträge des aktiven Sets als Beispiele: gleiche Rolle, ähnlichste Energie und
  Dichte, höchste Gewichtung; je Beispiel höchstens 4 Takte, damit der Prompt klein bleibt

**Kopierschutz** (D-87)
- Ein generiertes Ergebnis (Algorithmus und KI) darf keinem Eintrag des Sets in Rhythmus und
  Tonhöhenfolge zu mehr als 85 % gleichen. Solche Kandidaten werden verworfen bzw. bei der KI als
  Ergebnis unterhalb der Mindestbewertung angezeigt.
- **Metrik:** Verglichen wird jede erzeugte Stimme mit jedem aktiven Eintrag derselben Rolle. Beide werden
  auf das 16tel-Raster gebracht und als Folge von Einsätzen dargestellt; pro Einsatz zählt die Linie (bei
  Akkorden der höchste Ton, beim Bass der tiefste) als Intervall zum vorherigen Einsatz in Halbtönen.
  Absolute Tonhöhe, Notenlänge und Velocity zählen nicht, der Vergleich ist also transpositionsunabhängig.
- Ähnlichkeit = Zahl der Einsätze, die in beiden auf demselben Schritt liegen und dasselbe Intervall haben,
  geteilt durch die größere der beiden Einsatzzahlen. Verglichen werden Fenster in der Länge des kürzeren
  Materials, die in ganzen Takten gegeneinander verschoben werden; maßgeblich ist das ähnlichste Fenster.
- **Nur markante Einträge** sind geschützt: mindestens 3 verschiedene Tonhöhenklassen und mindestens
  4 Tonhöhenwechsel je 2 Takte. Einfache Muster aus Grundton, Quinte und Oktave (z. B. eine rollende
  Grundton-Bassline) sind Allgemeingut; sonst würde der Schutz fast jeden Bass-Kandidaten verwerfen.
- Dieselbe Metrik nutzt der Wiederholungsschutz [v1.1] (STYLES.md §1.14).
- Ziel ist der eigene Stil, nicht die Kopie alter Tracks. Wer eine bestehende Linie unverändert nutzen
  will, importiert sie direkt in eine Stimme (§3.18).

**Transparenz, Datenschutz, Reproduzierbarkeit**
- Set-Übersicht mit Zusammenfassung („häufigste Progression i–♭VI–♭VII, bevorzugter Bass-Archetyp
  rolling16, typische Dichte …“), Profil eines Sets zurücksetzen bzw. neu berechnen
- Referenzen liegen lokal im Datenordner (Originaldateien, Analysen, Set-Definitionen), nicht im Projekt
- **Zustimmung (D-86):** Referenz-Beispiele und importierte Stimmen (§3.18) gehen an einen Cloud-Provider
  nur, wenn der Nutzer für diesen Provider einmal ausdrücklich zugestimmt hat (Opt-in beim ersten Bedarf,
  widerrufbar in den Einstellungen). Lokale Provider (Basis-URL auf `localhost` bzw. `127.0.0.1`) sind
  ausgenommen. Ohne Zustimmung erhält die KI nur den abgeleiteten Kontext (Tonart, Skala, Akkordfolge),
  und die Anfrage läuft trotzdem.
- Jedes Pattern speichert Set-ID und einen Schnappschuss der angewendeten Abweichungen; neue Referenzen
  verändern alte Ergebnisse nicht (§4.3)

---

## 4. Generierungs-Architektur (Hybrid)

```
Prompt / Referenzen / importierte Stimme / Parameter
          │
          ▼
  ┌──────────────────┐     KI aus/Fehler     ┌──────────────────────┐
  │   KI-Schicht     │ ────────────────────▶ │ Algorithmus (Offline) │
  │ liefert Intent + │                       └──────────┬───────────┘
  │ optional Steps   │                                  │
  └────────┬─────────┘                                  │
           ▼                                            ▼
   ┌────────────────────────────────────────────────────────┐
   │ Harmonischer Kontext → Generatoren (Bass, Melodie)     │
   └────────────────────────┬───────────────────────────────┘
                            ▼
   ┌────────────────────────────────────────────────────────┐
   │ Constraint-Schicht: Skala, Tonumfang, Raster, Länge,   │
   │ keine Überlappung gleicher Tonhöhe, Kick-Aussparung    │
   └────────────────────────┬───────────────────────────────┘
                            ▼
                    Unveränderliches Pattern
```

### 4.1 Modi
- **KI-Modus:** Die KI liefert Intent und konkrete Noten (Skalenstufen im 16tel-Raster); Constraint-Schicht
  und Qualitätsbewertung bereinigen und prüfen sie.
- **Offline-Modus:** reiner Algorithmus, keine Netzwerkzugriffe.
- Ungültiges JSON löst intern einen Reparaturversuch aus. Scheitert auch der, entscheidet der Nutzer
  (§7.9); ein Wechsel in den **Offline-Modus geschieht nie ungefragt**. ([Gestrichen]: Struktur-Modus, in
  dem die KI nur den Intent liefert.)
- **Beim ersten Start ist der Offline-Modus aktiv**, bis der Nutzer einen Provider einrichtet.

### 4.2 Constraint-Schicht (läuft immer, auch bei manuellen Edits optional)
- Tonhöhen auf die Skala quantisieren, gesteuert über den Chromatik-Anteil (§3.8). **Die Töne des
  aktuell klingenden Akkords sind immer erlaubt** (Akkord-Skalen-Prinzip), z. B. der Leitton im V-Akkord
  einer harmonisch-moll-Progression oder die kleine Sexte eines ♭VI-Akkords bei Moll-Pentatonik.
  Ausnahme [v1.1]: Chord-Memory-Stabs (STYLES.md §1.10)
- Tonumfang pro Stimme als **MIDI-Nummern** (Standard: Bass 28–52, Melodie 55–88, Stabs 55–79),
  zuzüglich Oktav-Offset pro Stimme (STYLES.md §1.6)
- Notenanfänge aufs Raster, Länge mindestens ein Rasterschritt, nichts über das Pattern-Ende hinaus
- Keine überlappenden Noten gleicher Tonhöhe auf demselben Kanal
- Jede Stimme hat einen eigenen MIDI-Kanal (Standard Bass 1, Melodie 2), damit eine spätere mehrkanalige
  Ausgabe (D-63) eindeutig bleibt; doppelte Kanäle lässt die UI nicht zu
- Bass nicht auf Kick-Schritten des gewählten Kick-Rasters (STYLES.md §1.2), außer der Archetyp erlaubt es
- **Kick-Freiraum:** Bassnoten enden vor dem nächsten Kick-Schritt (STYLES.md §1.7)
- **Intervall- und Registerregeln** zwischen Bass und Melodie (STYLES.md §1.6, §1.8)
- Slides nur in einstimmigen Abschnitten, nicht kombiniert mit Ratchets
- **Bass: Kick-Freiraum schlägt Slide.** Würde ein Bass-Slide über einen Kick-Schritt reichen, wird das
  Slide-Flag entfernt (Ausnahme `long_tied`)
- Velocity 1–127, Akzente über einstellbare Akzent-Velocity und Schwelle (STYLES.md §1.9)
- **Slides (303-Style):** Eine Note mit `slide = true` überlappt die folgende Note um einen festen
  Betrag (Standard 1/64, einstellbar). Andere Tonhöhe auf demselben Kanal ist dabei erlaubt. Kein
  Pitchbend. Der Ziel-Synth muss im Mono/Legato-Modus mit Glide laufen; die UI zeigt dazu einen Hinweis.
  Ein Slide auf der letzten Note eines Patterns gleitet in die erste Note des nächsten Durchlaufs
  **desselben** Patterns. Beim Wechsel zu einem anderen Pattern oder Slot endet die Note ohne Überlappung
  (§6.2, D-89).
  **Slide auf dieselbe Tonhöhe** ist ein Haltebogen: Die beiden Noten werden zu einer zusammengeführt.

### 4.2a Ausgabestufe (Reihenfolge)
Wahrscheinlichkeit, Groove, Transposition und Kick-Freiraum wirken erst bei der Ausgabe (Engine und
Export). Damit sie sich nicht gegenseitig aushebeln, gilt diese feste Reihenfolge:
0. [v1.1] Wahrscheinlichkeit und Bedingungen auswerten (welche Noten erklingen in diesem Durchlauf)
1. Transposition (inkl. Oktavierung bei Bereichsüberschreitung)
2. Groove und Swing (Zeitversatz, Velocity-Profil)
3. Slide-Überlappung neu berechnen (die Überlappung bezieht sich auf die verschobene Folgenote)
4. Kick-Freiraum neu prüfen und Notenenden kürzen (Groove kann Notenenden in die Kick schieben)
5. Akzent-Velocity anwenden, [v1.1] Ratchets auflösen

### 4.3 Determinismus
Jedes Pattern speichert Seed, Stilprofil-Version, Parameter und (bei KI) die Rohantwort.
Der Algorithmus erzeugt bei gleichem Seed und gleichen Eingaben **bitgenau** dasselbe Ergebnis,
**auf allen Plattformen**. Dafür gilt:
- eigener PRNG (z. B. PCG32) und eigene Verteilungsfunktionen. `std::uniform_int_distribution` und
  verwandte Klassen sind verboten, weil ihre Ergebnisse je nach Standardbibliothek abweichen
- keine Fast-Math-Optimierung in `core`, Gewichtungen möglichst mit Ganzzahlen
- KI-Ergebnisse sind über die gespeicherte Rohantwort reproduzierbar
- [v1.1] Wahrscheinlichkeiten werden per Hash aus Seed, Noten-ID und Durchlauf entschieden, nicht per laufendem
  Zufallszustand (§3.19)
- Set-ID (v1.0) und Schnappschuss des Referenz-Profils ([v1.1], §3.20) sind Teil der Eingaben und werden im Pattern gespeichert
- **Gewinner-Seed (ab v1.0, D-88):** Gespeichert wird der **Seed des gewählten Kandidaten**. Die
  Wiedergabe eines gespeicherten Seeds erzeugt genau diesen Kandidaten, ohne Auswahl. Das ist schon in
  v1.0 nötig, weil der Kopierschutz die Auswahl vom Inhalt des Referenz-Sets abhängig macht, und der
  kann sich unter derselben Set-ID ändern.
- **[v1.1] Wiederholungsschutz und Determinismus:** Der Wiederholungsschutz hängt zusätzlich vom Verlauf
  ab; auch hier sichert der Gewinner-Seed „gleicher Seed → gleiches Ergebnis“.

### 4.4 Qualitätsbewertung
Jedes Ergebnis (Algorithmus und KI) wird bewertet, siehe STYLES.md §1.14. Der Algorithmus erzeugt
mehrere Kandidaten und wählt den besten, mit Kopierschutz gegenüber dem aktiven Referenz-Set (§3.20)
und ab v1.1 mit Wiederholungsschutz gegenüber dem Verlauf. Die Bewertung
ist in `core` implementiert und vollständig getestet.

- **Harte Kriterien** verwerfen einen Kandidaten: Kopierschutz, [v1.1] Wiederholungsschutz. Alle übrigen
  Regeln setzt die Constraint-Schicht schon vorher durch.
- **Weiche Kriterien** (STYLES.md §1.14) ergeben je einen Wert von 0 bis 100; der Gesamtwert ist ihr
  gewichteter Mittelwert (0–100, ganzzahlig). Gewichte und **Mindestbewertung** stehen im Stilprofil
  (JSON) und werden im Hörtest von Phase 1a kalibriert, so dass Fehlschläge selten bleiben (Ziel: unter
  1 % der Generierungen in den `mmgen`-Serien).
- **Gültig** ist ein Kandidat, der alle harten Kriterien erfüllt und die Mindestbewertung erreicht. Der
  beste gültige Kandidat gewinnt.
- **Kein gültiger Kandidat (D-88):** bis zu 2 weitere Runden mit je 8 Kandidaten, deren Seeds
  deterministisch aus dem Ausgangs-Seed abgeleitet sind. Bleibt es ohne gültigen Kandidaten, bleibt das
  bisherige Pattern aktiv und die UI meldet „kein ausreichendes Ergebnis“ (nicht blockierend, §7.9); ein
  unterwertiger Kandidat wird nie automatisch übernommen.
- KI-Ergebnisse unterhalb der Mindestbewertung werden angezeigt, nicht still ersetzt; der Nutzer
  entscheidet (STYLES.md §1.14).

---

## 5. Datenmodell (`mm::core`)

```cpp
struct LockFlags {
    bool pitch = false, rhythm = false, velocity = false;
};

struct Note {
    uint32_t  id;          // stabil innerhalb eines Slots (Verfeinern, Sperren, Undo)
    uint8_t   pitch;       // MIDI 0–127
    uint32_t  startTick;   // 960 PPQ
    uint32_t  lengthTicks;
    uint8_t   velocity;    // 1–127, Grundwert ohne Akzent
    uint8_t   ratchet = 1; // [v1.1] 2–4 = Wiederholungen innerhalb der Notenlänge (nie mit slide); Feld ab v1.0
    uint8_t   chance  = 100;        // [v1.1] Wahrscheinlichkeit in % (§3.19); Feld ab v1.0
    uint8_t   condA = 1, condB = 1; // [v1.1] Bedingung A:B, 1:1 = immer, B ≤ 8; Feld ab v1.0
    bool      slide  = false;
    bool      accent = false;
    LockFlags lock;
};

enum class VoiceRole { Bass, Melody /* später z. B. Stabs, Arp, Counter */ };
constexpr int kMaxVoices = 8;       // Architekturgrenze; v1 nutzt genau 2

struct GrooveSettings {             // pro Stimme, nicht Teil der Notenpositionen
    float       swing = 0.5f;       // 0.50–0.75
    std::string templateId;         // z. B. "straight", "swing16_62"
    float       amount = 1.0f;      // 0–1
};

struct VoicingSettings {            // nur Melodie
    bool chordMemory = false;
    int  lowNote = 55, highNote = 79;
};

struct Track {
    VoiceRole         role;
    uint8_t           midiChannel;  // 1–16
    std::string       archetypeId;  // gewählter oder automatisch bestimmter Archetyp
    bool              archetypeAuto = true;
    int8_t            octaveOffset = 0;   // −2 … +2
    GrooveSettings    groove;
    LockFlags         lock;         // Sperren für die ganze Stimme
    std::vector<Note> notes;        // sortiert nach startTick
    bool              muted = false;
};

using PitchClass = uint8_t;         // 0–11, 0 = C

enum class ChordQuality { Major, Minor, Diminished, Sus2, Sus4 };

struct Chord {                      // Symbol nach STYLES.md §1.17, z. B. "bVI" = {8, Major}
    uint8_t      rootOffset;        // Halbtöne über dem Grundton der Tonart (0–11)
    ChordQuality quality;           // Septimen und Optionen kommen aus den Akkordfarben (Voicing)
};

struct ChordEvent {
    Chord    chord;
    uint32_t startHalfBar;          // Position in halben Takten ab Pattern-Anfang
    uint32_t lengthHalfBars;        // 1 = halber Takt, 2 = ein Takt …
};

struct HarmonicContext {
    PitchClass              root;
    std::string             scaleId;     // aus der Skalentabelle in core, z. B. "natural_minor"
    std::vector<ChordEvent> progression; // lückenlos über das Pattern, zu jedem Zeitpunkt genau ein Akkord
};

enum class PhraseRole { Main, Variation, Build, Breakdown, Answer };
                                    // Hauptmotiv, Variation, Steigerung, Ausdünnung, Antwort

struct Phrase {
    uint32_t   startBar;
    uint32_t   lengthBars;          // 4, 8 oder 16; bei 1/2/4-Takt-Patterns = Pattern-Länge
    PhraseRole role;
    std::optional<std::string> kickGridId;  // überschreibt Pattern::kickGridId (STYLES.md §1.2)
    bool       turnaround = false;  // [v1.1] letzter Takt als Turnaround (nur 8/16)
    bool       locked = false;
};

struct GenerationInfo {
    std::string source;             // "algorithm", "ai", "variation", "refine", "import", "edit"
    uint64_t    seed = 0;           // Ausgangs-Seed
    uint64_t    winnerSeed = 0;     // Seed des gewählten Kandidaten (§4.3)
    uint32_t    styleProfileVersion = 0;
    uint8_t     creativityPct = 40, energyPct = 50;  // Parameter zum Zeitpunkt der Erzeugung
    std::string prompt;             // Nutzerprompt bzw. Anweisung beim Verfeinern
    uint32_t    promptVersion = 0;  // nur KI (§7.4)
    std::string providerId, modelId;   // nur KI
    std::string rawResponse;        // nur KI, unverändert
    std::string referenceSetId;     // leer = ohne Set
    // [v1.1] Schnappschuss des Referenz-Profils (§3.20); Feld wird in v1.1 ergänzt, ohne Migration
    int64_t     createdUnixMs = 0;
};

enum class PolymeterPhase { RestartAtPattern, FreeRunning };

struct RhythmReference {            // aus Drum-MIDI abgeleitet, max. 2 Takte = 32 Schritte
    std::bitset<32>           kickSteps, hatSteps, accentSteps;
    std::array<int16_t, 32>   timingOffsetTicks{};  // Groove aus den Hats
    std::array<uint8_t, 32>   velocity{};
    uint8_t                   bars = 1;
};

struct Pattern {
    uint32_t        lengthBars;     // 1, 2, 4, 8, 16 ([Backlog] bis 64)
    std::vector<Phrase> phrases;    // Formplan
    uint8_t         timeSigNum = 4, timeSigDen = 4;   // v1: nur 4/4
    std::string     styleId;
    std::string     kickGridId = "4otf";
    std::optional<PitchClass> kickRoot;
    PolymeterPhase  polymeterPhase = PolymeterPhase::RestartAtPattern;
    std::optional<RhythmReference> rhythmRef;   // Drum-Referenz (§3.14)
    std::vector<std::string> refineHistory;     // letzte 5 Verfeinerungen (§3.15)
    HarmonicContext context;
    VoicingSettings voicing;
    std::vector<Track> voices;      // v1: genau 2 (Bass, Melodie), nie fest verdrahtet
    uint8_t         qualityScore = 0;   // 0–100 (§4.4)
    uint32_t        nextNoteId = 1;     // Zähler für Noten-IDs dieses Slots
    uint64_t        version = 0;        // fortlaufend pro Instanz, für Übergabe und Anzeige (§6.3)
    GenerationInfo  info;
};
```

**Noten-IDs:** eindeutig innerhalb eines Slots, vergeben aus `Pattern::nextNoteId` (nur aufsteigend, nie
wiederverwendet). Bearbeiten, Variieren und Verfeinern behalten die ID einer Note, solange sie als
dieselbe Note gilt (verschoben, Tonhöhe, Länge oder Velocity geändert); neue Noten bekommen neue IDs.
Kopieren eines Slots übernimmt IDs und Zähler. Werden Noten zusammengeführt (Slide auf gleiche Tonhöhe,
§4.2), behält das Ergebnis die ID der ersten. IDs aus einer KI-Antwort, die unbekannt sind oder doppelt
vorkommen, werden verworfen; die Note gilt dann als neu.

**Serialisierung:** `core` serialisiert Patterns als JSON (ohne JUCE), in derselben Form für Plugin-State
und Bibliothek. Felder für v1.1 werden ab v1.0 mit Standardwerten geschrieben; unbekannte Felder werden
beim Laden ignoriert. Jede Änderung am Format erhöht `stateVersion` und bekommt eine Migrationsfunktion.

**Offene Stimmenzahl:** Code iteriert immer über `voices`, statt Bass und Melodie fest anzusprechen.
Regeln zwischen Stimmen (Intervalle, Register) gelten paarweise relativ zur Stimme mit Rolle Bass.
Neue Stimmrollen brauchen nur Archetypen und Profileinträge, keinen Umbau der Engine.

Patterns sind nach der Erzeugung **unveränderlich**. Edits erzeugen eine neue Version, damit die
Echtzeit-Übergabe lock-free bleibt.

---

## 6. Echtzeit-Engine (`mm::engine`)

### 6.1 Transport-Sync
- Pro Block den Playhead auslesen: PPQ-Position, BPM, isPlaying, Loop-Bereich, Taktart
- Noten emittieren, deren Start in `[ppqBlockStart, ppqBlockEnd)` liegt, mit sample-genauem Offset
- Pattern-Position = (PPQ − Wechselzeitpunkt) modulo Pattern-Länge. Der Wechselzeitpunkt ist der
  Taktanfang, an dem das Pattern während der Wiedergabe übernommen wurde (Modus „Neustart“, §3.4).
- **Sprung** ist jede Unstetigkeit der PPQ-Position zwischen zwei Blöcken: nach vorn, nach hinten und der
  Loop-Rücksprung. Nach dem Start und nach jedem Sprung gilt der Slot-Parameter sofort, und die Position
  richtet sich am Taktraster des Songs aus (Wechselzeitpunkt = PPQ 0), wie in §3.13 (D-66, D-89). So
  klingt dieselbe Song-Position immer gleich, auch beim Bounce.
- Ein Loop-Rücksprung innerhalb eines Blocks wird über den Loop-Bereich des Playheads erkannt und
  sample-genau behandelt.
- **Fehlende Host-Daten:** Meldet der Host keine PPQ-Position oder kein Tempo, gibt MidiMaid keine neuen
  Noten aus und sendet ausstehende Note-Offs (wie bei Stop).
- **Tempo und Taktart** gelten pro Block (Wert am Blockanfang); Änderungen innerhalb eines Blocks wirken
  ab dem nächsten. Gerechnet wird in PPQ, daher bleibt das Raster auch bei Tempoautomation korrekt.
  v1.0 spielt 4/4: Meldet der Host eine andere Taktart, rechnet MidiMaid im 4/4-Raster ab PPQ 0 weiter
  und zeigt einen Hinweis.
- Der vollständige Zustandsautomat (Stop, Start, Sprung, Loop, quantisierter Wechsel, geplanter
  Gruppenwechsel) mit testbaren Beispielen steht in §6.1a.
- [v1.1] Im Modus „Legato“ bleibt die Position beim Wechsel erhalten

### 6.1a Zustandsautomat (normativ, D-131)
Jede Instanz hat pro Block einen **Transportzustand** und höchstens einen **ausstehenden Wechsel** je Art (§6.3).
Eingaben pro Block: `hasPosition`, `isPlaying`, PPQ am Blockanfang, Tempo, Loop-Bereich. Alle Zeiten sind PPQ
des Songs (PPQ 0 = Taktanfang); Sample-Offsets werden aus PPQ und Tempo des Blocks berechnet und gerundet.

**Zustände**
| Zustand | Bedeutung |
|---|---|
| **Ruhend** | Transport steht, Host liefert keine Daten (Position oder Tempo fehlen), nach `prepareToPlay` und im Bypass. Keine neuen Noten. |
| **Laufend** | Der Transport läuft und die PPQ-Position ist stetig. Das Pattern wird gespielt. |

**Übergänge** (jeweils in dieser Reihenfolge innerhalb des Blocks)
| Nr. | Von → Nach | Auslöser | Wirkung |
|---|---|---|---|
| T1 | Ruhend → Laufend | `isPlaying` und Daten vorhanden | **Start.** Offene Note-Offs (z. B. nach `prepareToPlay`) im Offset 0. Ausstehende Wechsel und der Slot-Parameter gelten **sofort**. Position = PPQ modulo Pattern-Länge (Wechselzeitpunkt PPQ 0). Noten ab PPQ 0 (D-128: negative Positionen, also der Vorzähler, bleiben still). |
| T2 | Laufend → Ruhend | `isPlaying` fällt, Daten fehlen, Bypass | Note-Off für alle klingenden Noten im Offset 0 des Blocks. Ausstehende Wechsel bleiben stehen und gelten beim nächsten Start (T1). |
| T3 | Laufend → Laufend | PPQ stetig (Abweichung höchstens eine Sample-Länge plus Toleranz für Tempowechsel, D-125) | Der Scan setzt dort an, wo der letzte Block endete: keine Lücke, keine Doppelung. |
| T4 | Laufend → Laufend | **Sprung** (Abweichung größer als die Toleranz, auch nach hinten) | Wie T1 ohne Zustandswechsel: Note-Offs im Offset 0, ausstehende Wechsel und Slot-Parameter gelten sofort, Position richtet sich am Songraster aus. |
| T5 | Laufend → Laufend | **Loop-Rücksprung im Block** | Noten bis zum Loop-Ende, Note-Off für alle im Sample des Rücksprungs, danach Noten ab Loop-Anfang. Endet der Block genau auf dem Loop-Ende, folgen die Note-Offs im ersten Sample des nächsten Blocks (D-95). Der Rücksprung ist ein Sprung (T4): Ein ausstehender Wechsel, dessen Q vor dem Rücksprung nicht erreicht wird, gilt im Sample des Rücksprungs (D-132). |
| T6 | Laufend → Laufend | **Quantisierungspunkt Q** im Block erreicht, Wechsel ausstehend | Noten des alten Patterns mit Start vor Q spielen normal. Im Sample von Q: Note-Offs aller klingenden Noten, danach Note-Ons des neuen Patterns ab Position 0. Note-Off steht im selben Sample immer vor Note-On. Fällt der Start einer Note des alten Patterns in dasselbe Sample wie Q, endet sie ein Sample später (nie vor oder mit dem eigenen Note-On, D-132). |

**Regeln für Wechsel**
1. **Lesezeitpunkt:** Der Audio-Thread liest ausstehende Wechsel und den Slot-Parameter am **Blockanfang**.
   Änderungen des Slot-Parameters innerhalb eines Blocks gelten ab dem nächsten (wie Tempo, §6.1).
2. **Wahl von Q:** Q ist der erste Punkt des Quantisierungsrasters, der **nicht vor dem Blockanfang** liegt, in dem
   der Wechsel gelesen wurde. Liegt Q genau auf dem Blockanfang, wirkt der Wechsel im Offset 0. Ein Q in der
   Vergangenheit gibt es nie: Ein verpasster Punkt verschiebt den Wechsel auf den nächsten Rasterpunkt, nie rückwirkend.
3. **Raster:** Schlag = 1 PPQ, Takt = 4 PPQ, n Takte = 4n PPQ, jeweils ab PPQ 0; „Ende des Patterns“ und
   „Ende der Phrase“ sind das nächste Vielfache der Pattern- bzw. Phrasenlänge. v1.0 kennt nur „Neustart“
   (§3.4): Ein Q, das nicht auf einer Taktgrenze liegt (Quantisierung „nächster Schlag“), wird auf die nächste
   Taktgrenze aufgerundet. „Legato“ ist [v1.1].
4. **Sofort statt quantisiert:** Start (T1), Sprung (T4) und Bearbeitungen (§6.3). Beim Stop (T2) wird nichts
   übernommen; der Wechsel gilt erst beim nächsten Start.
5. **Mehrere Wechsel am selben Punkt:** Vorrang nach §6.8. Ein neuer ausstehender Wechsel ersetzt einen älteren
   derselben Art (§6.3). Ist die Rückgabe-Queue voll, verschiebt der Audio-Thread den Wechsel auf das nächste Q (§6.3).
6. **Offline-Rendern und Bounce:** Dieselben Regeln. Q hängt nur von PPQ und Block ab, nicht von der Uhrzeit;
   deshalb ist die Slot-Automation deterministisch. Der Mindestvorlauf (D-90) wird dabei in PPQ umgerechnet.
7. **Bypass und `prepareToPlay`:** Bypass gilt wie T2. `invalidateTransport` (von `prepareToPlay`) setzt den
   Zustand auf „Ruhend“, die Aktiv-Tabelle bleibt (§6.2); der nächste Block mit Position ist ein Start (T1).

**Hub-Gruppen und O-22 (D-131)**
- Jede Voice hat im State den Schalter **„Slot: folgt Hub / eigen“** (Standard: folgt Hub). Er ist kein Host-Parameter,
  die Parameter-IDs bleiben unverändert (§3.13).
- **Eigen:** Die Voice wertet ihren eigenen Slot-Parameter wie eine Solo-Instanz aus (Regeln 1 bis 6). Das
  funktioniert auch beim Einzelspur-Bounce und beim Freeze, weil nichts vom Hub abhängt. Der Anwender automatisiert
  dann den Slot auf jeder MidiMaid-Spur (Setup-Hilfe, Vorlagen).
- **Folgt Hub:** Der Hub verteilt einen Slot-Wechsel mit PPQ-Zeitstempel Q (§3.4, §6.5). Q wird aus der am weitesten
  fortgeschrittenen PPQ-Position der Gruppe plus Mindestvorlauf gewählt, aufgerundet auf das Raster. Jede Voice
  führt den Wechsel bei ihrem Q aus (T6). Liegt Q bei der Voice schon hinter dem Blockanfang, wechselt sie am
  nächsten Rasterpunkt, an dem der Wechsel vorliegt (Regel 2), und protokolliert den Versatz.
- **Start und Sprung in „folgt Hub“:** Die Voice übernimmt den zuletzt gemeldeten Slot des Hubs sofort, wenn die
  gemeldete Position des Hubs den Blockanfang der Voice erreicht hat. Sonst spielt sie ihren gespeicherten Slot
  weiter und übernimmt den des Hubs am ersten Rasterpunkt, nachdem er gemeldet wurde. Der Anwender legt Slot-Wechsel
  in „folgt Hub“ deshalb auf Taktgrenzen und friert Voices im Modus „eigen“ ein.
- Fehlt der Hub, bleibt der Schalter wirkungslos: Die Voice spielt ihren Slot weiter (§6.5, Persistenz).

**Testbare Beispiele.** Alle bei 120 BPM, 44,1 kHz (1 PPQ = 22 050 Samples), Platzhalter-Pattern (Länge 4 PPQ,
Noten bei Position 0,5 / 1,5 / 2,5 / 3,5 PPQ, Länge 0,375 PPQ). Jedes Beispiel wird ein Engine-Test (Phase 1b).
| Nr. | Ausgangslage | Erwartung |
|---|---|---|
| Z1 | Start bei PPQ 0,0, Blöcke zu 512 Samples | Erster Note-On bei PPQ 0,5 = Sample 11 025 (Block 21, Offset 273); Note-Off bei Sample 19 294 (Block 37, Offset 350) |
| Z2 | Start bei PPQ 5,0 (Position 1,0) | Die Note bei 4,5 entfällt; erster Note-On bei 5,5 = 11 025 Samples nach dem Blockanfang |
| Z3 | Start bei PPQ −4,0 (Vorzähler) | Bis PPQ 0 keine Ereignisse, erster Note-On bei 0,5 (D-128) |
| Z4 | Note klingt (Note-On bei 0,5), nächster Block mit `isPlaying = false` | Note-Off im Offset 0, danach keine Ereignisse |
| Z5 | Note bei 5,5 klingt, nächster Block beginnt bei PPQ 1,0 (Sprung zurück) | Note-Off im Offset 0, erster Note-On bei 1,5 (11 025 Samples später); das alte Note-Off bei 5,875 entfällt |
| Z6 | Loop 0,0 bis 3,75; Block (2048 Samples) beginnt bei 3,7, Note bei 3,5 klingt | Note-Off im Sample 1103 (Loop-Ende), Fortsetzung ab PPQ 0,0 ohne zweites Note-Off |
| Z7 | Tempo wechselt zwischen zwei Blöcken von 120 auf 121 BPM, Position stetig im Rahmen von D-125 | Kein Sprung, kein Note-Off, keine fehlende oder doppelte Note |
| Z8 | Host meldet keine Position, danach wieder Position bei PPQ 8,0 | Note-Offs beim Ausfall, beim Wiedereinstieg Start nach T1 |
| Z9 | Laufend bei 5,3, Slot-Wechsel gelesen, Quantisierung „nächster Takt“ | Q = 8,0: Das alte Pattern spielt bis Q; im Sample von Q Note-Offs, danach das neue Pattern ab Position 0 |
| Z10 | Wechsel wird im Block gelesen, der genau bei PPQ 8,0 beginnt | Q = 8,0, Wirkung im Offset 0 |
| Z11 | Wechsel wird erst im Block bei 8,0232 gelesen | Q = 12,0, nie rückwirkend |
| Z12 | Wechsel ausstehend (Q = 8,0), Sprung bei PPQ 7,0 nach 20,5 | Wechsel gilt sofort (T4), Note-Offs im Offset 0, Position = 20,5 modulo Pattern-Länge |
| Z13 | Wechsel ausstehend, Stop bei 7,0, später Start bei 3,0 | Beim Start (T1) spielt das neue Pattern, kein Wechsel zwischendurch |
| Z14 | Start bei PPQ 148,0 mit Slot-Parameter 3 | Slot 3 sofort, Position = 148,0 modulo Pattern-Länge |
| Z15 | Wechsel fällig, Rückgabe-Queue voll | Wechsel erst am nächsten Rasterpunkt, bis dahin spielt das alte Pattern, kein Absturz |
| Z16 | Hub meldet bei Position 7,8 einen Slot-Wechsel, Quantisierung „nächster Takt“ | Q = 8,0 liegt 0,2 PPQ = 100 ms voraus, weniger als 150 ms: Q = 12,0 für Hub und alle Voices (D-90) |
| Z17 | Voice „folgt Hub“ hat bei Eintreffen des Stempels Q = 8,0 schon PPQ 8,1 erreicht | Wechsel am nächsten Rasterpunkt (12,0), Versatz im Protokoll |
| Z18 | Voice „eigen“, Slot-Parameter wird bei 7,0 von 1 auf 2 automatisiert | Q = 8,0, unabhängig vom Hub; beim Einzelspur-Bounce identisch |
| Z19 | Voice „folgt Hub“ startet bei 148,0, Hub meldet dort Slot 3 | Slot 3 sofort. Hat der Hub die Position noch nicht erreicht: gespeicherter Slot, Wechsel zu Slot 3 am ersten Rasterpunkt nach seiner Meldung |
| Z20 | Slot 2 ist leer, laufend bei 5,3, Slot-Parameter wechselt auf 2 | Q = 8,0: Note-Offs im Sample von Q, danach Stille; Slot 1 wird dabei nicht zurückgegeben |
| Z21 | Ein Ergebnis für Slot 2 trifft ein, Slot 1 spielt | Es wird nur in Slot 2 abgelegt; Slot 1 spielt unverändert weiter, die gemeldete Version ändert sich nicht |
| Z22 | Slot-Wechsel 1 → 2 wartet auf Q = 8,0, davor trifft ein Ergebnis für Slot 1 ein | Bei Q spielt Slot 2 (SPEC §6.8, Punkt 1); das Ergebnis liegt in Slot 1 und klingt, sobald Slot 1 gewählt wird |
| Z23 | Slot-Wechsel 1 → 2 wartet, davor trifft ein neueres Ergebnis für Slot 2 ein | Bei Q spielt das neuere Ergebnis, das ältere wird zurückgegeben |
| Z24 | Bearbeitung von Slot 2, während Slot 1 klingt | Slot 1 bleibt unberührt (keine Note-Offs); der bearbeitete Stand spielt nach dem Wechsel zu Slot 2 |
| Z25 | Slot-Parameter 3 beim Start, Slot 3 gefüllt (Start bei 148,0) | Slot 3 sofort, Position 0 bei 148,0; gemeldeter Slot 3 |
| Z26 | Rückgabe-Queue voll, Wechsel zwischen zwei abgelegten Slots | Der Wechsel erfolgt bei Q, denn er gibt nichts zurück |
| Z27 | Eine Note klingt, `mute_<Stimme>` wird gesetzt | Im Offset 0 des Blocks, der den Wert liest, Note-Off; danach beginnt keine Note, solange stumm geschaltet ist; Position und Wechsel laufen weiter |
| Z28 | Mute wird aufgehoben, während eine Note des Patterns schon läuft | Diese Note wird nicht nachträglich gestartet; ab der nächsten Note-On-Zeit spielt die Stimme wieder |
| Z29 | Mute bei Stop, danach Start | Stumm ab dem ersten Block, bis Mute aufgehoben wird |
| Z30 | Slot-Wechsel fällig, während stumm geschaltet ist | Der Wechsel geschieht lautlos zum Wechselpunkt; nach dem Aufheben spielt das neue Pattern |

### 6.2 Note-Off-Garantie
- Aktiv-Tabelle 16 × 128 (fest allokiert). Gespeichert wird die tatsächlich gesendete Tonhöhe
  (nach Transposition, §3.16)
- Note-Offs senden bei: Stop, Sprung (§6.1), Pattern-Wechsel, Bypass (`processBlockBypassed`),
  Stummschalten einer Stimme, fehlenden Host-Daten
- `releaseResources` und der Destruktor haben keinen MIDI-Ausgang. Klingende Noten bleiben deshalb in der
  Aktiv-Tabelle stehen, ihre Note-Offs folgen im ersten Block nach dem nächsten `prepareToPlay`. Beim
  Entfernen des Plugins während der Wiedergabe hängt das Verhalten vom Host ab (Testmatrix).
- Zusätzlich auf Wunsch „All Notes Off“ (CC 123) bei Stop
- **Pattern- und Slot-Wechsel:** Alle klingenden Noten des alten Patterns erhalten am Wechselzeitpunkt
  ihr Note-Off, und zwar vor den Note-Ons des neuen Patterns. Slides werden nicht über die Pattern-Grenze
  fortgesetzt (D-89).
- Slide-Überlappungen innerhalb eines Patterns: Die Reihenfolge Note-On (neu) vor Note-Off (alt) wird
  eingehalten, auch über Block- und Loop-Grenzen hinweg. Bei Stop und Sprüngen werden alle überlappenden
  Noten beendet.

### 6.3 Lock-freie Pattern-Übergabe
- Message-Thread erzeugt ein neues unveränderliches Pattern
- Übergabe an den Audio-Thread über je einen atomaren Zeiger pro Art (Wechsel, Bearbeitung) auf ein
  unveränderliches Pattern (kein `std::atomic<std::shared_ptr>`, das ist auf gängigen Plattformen nicht
  lock-free). Einen Pool braucht es nicht: Höchstens ein ausstehendes Pattern je Art plus die Plätze der
  Rückgabe-Queue leben gleichzeitig; der Audio-Thread legt nie etwas an (D-133)
- Der Audio-Thread übernimmt zum eingestellten Quantisierungszeitpunkt (§3.4) und legt das alte Pattern
  in eine lock-freie Rückgabe-Queue
- Freigabe des alten Patterns **nur** auf dem Message-Thread (Timer), nie im Audio-Thread

**Echtzeit-Vertrag (D-89)**
- **Ausstehende Wechsel:** Pro Instanz gibt es ein aktives Pattern und höchstens einen ausstehenden
  Wechsel je Art (Slot-Wechsel, neues Ergebnis, [v1.1] Evolve-Stufe). Die Übergabe nutzt `exchange` auf
  einem atomaren Zeiger; damit entsteht kein ABA-Problem. Trifft ein neueres Ergebnis ein, bevor das
  ausstehende übernommen wurde, ersetzt es dieses; das ältere landet nur im Verlauf und wird vom
  Message-Thread freigegeben. Am Quantisierungspunkt entscheidet der Vorrang nach §6.8.
- **Rückgabe-Queue:** SPSC mit fester Kapazität (Engine-Konstante, 8). Ist sie voll, verschiebt der
  Audio-Thread den Wechsel auf den nächsten Quantisierungspunkt; er gibt nie selbst frei. Ein Engine-Test
  erzwingt diesen Fall.
- **Version:** Jedes Pattern trägt eine fortlaufende Version (`Pattern::version`). Der Audio-Thread meldet
  die aktive Version per Atomic an UI und Hub.
- **Bearbeitungen** in der Piano-Roll sind keine quantisierten Wechsel: Sie werden zum nächsten Block
  übernommen, ohne Neustart der Position. Klingende Noten, die im neuen Stand fehlen oder sich geändert
  haben, erhalten sofort ihr Note-Off.
- **MIDI-Ausgabe (D-134):** Der Ausgabepuffer (`juce::MidiBuffer`) gehört dem Host-Wrapper und lässt sich in
  `prepareToPlay` nicht reservieren. `processBlock` reserviert deshalb am Blockanfang einmal die Obergrenze
  (Kapazität der Engine-Ereignisliste, 1024 Ereignisse); das allokiert höchstens einmal pro Host-Puffer in den
  ersten Blöcken. Die Engine selbst (Ereignisliste, Aktiv-Tabelle, Übergabe) allokiert nie; ein Test zählt das.
  Wird die Obergrenze erreicht, haben Note-Offs Vorrang und neue Note-Ons entfallen.
- **Lebenszyklus:** Der Destruktor bricht Hintergrundjobs ab, wartet auf sie (§7.1) und gibt aktive und
  ausstehende Patterns sowie die Rückgabe-Queue auf dem Message-Thread frei; der Audio-Thread läuft dann
  laut Host-Vertrag nicht mehr.

### 6.4 [v1.1] Polymeter
- Phasenverhalten: Neustart am Pattern-Anfang ([Backlog]: frei laufend)
- Frei laufend: Die Phase wird aus der absoluten PPQ-Position berechnet und nicht aus einem internen
  Zähler, damit das Ergebnis unabhängig vom Startpunkt der Wiedergabe immer identisch ist
- Details in STYLES.md §1.3

### 6.5 Hub & Voices (Instanz-Kopplung)
Zweck: Ein zentraler **Hub** steuert alles, **Voices** spielen einzelne Stimmen auf eigenen Spuren.
So entstehen Drag & Drop pro Stimme und direktes Einspielen auf der Synth-Spur, auch in Logic, wo ein
MIDI-FX nur seine eigene Spur ansteuert.

**Rollen** (pro Instanz wählbar, beide Plugin-Varianten können jede Rolle übernehmen):
| Rolle | Aufgabe | Oberfläche |
|---|---|---|
| **Hub** | hält harmonischen Kontext, Formplan, Seed, Slots, Drum-Referenz; generiert, variiert, verfeinert alle Stimmen | volle Oberfläche (§8.1) |
| **Voice** | spielt genau eine Stimme des Hubs auf der eigenen Spur; nimmt abgelegte Drum-Referenzen an und übergibt sie an den Hub | schlank (§8.1): eigene Spur, Mute, Oktave, Drag & Drop der Stimme, Hilfe zum Aufnehmen; [v1.1] Anzeige der Gruppen-Transposition |
| **Solo** | Instanz ohne Gruppe, verhält sich wie ein Hub ohne Voices | volle Oberfläche |

(„Solo“ statt „Standalone“, um Verwechslung mit dem Standalone-App-Format zu vermeiden.)

**Ausgabemodus des Hubs:** „eine Stimme“ (Standard; welche, ist wählbar) oder „keine“. Jede Instanz
gibt also höchstens eine Stimme aus. Das hält Engine, Tests und Setups einfach und vermeidet
Kanalkonflikte (D-63).

**Empfohlene Aufbauten:**
- **Logic:** Hub als MIDI-FX auf einer Instrumentenspur (spielt dort z. B. den Bass), je eine Voice als
  MIDI-FX auf den weiteren Synth-Spuren. Einspielen über „Record MIDI to Track Here“ hinter MidiMaid.
- **Live:** Hub als Instrument-Variante auf einer MIDI-Spur gibt den Bass aus, eine Voice auf
  einer weiteren MIDI-Spur die Melodie. Jede Synth-Spur holt sich per „MIDI From“ genau eine
  MidiMaid-Spur. Das kostet pro Stimme eine zusätzliche Spur, weil ein VST3 in Live nicht vor einem Synth
  auf derselben Spur sitzen kann und Live beim Spur-Routing keine Kanäle trennt.
  Tipp für die Übersicht: MidiMaid-Spur und Synth-Spur einer Stimme in einer Gruppenspur zusammenfassen.
  - **Monitoring:** Synth-Spuren auf „Monitor: In“, sonst schweigen sie, solange sie nicht scharf
    geschaltet sind. Die Hub-Spur ebenfalls auf „In“ mit dem Keyboard als Eingang: So kommt das
    Transponieren auch an, wenn beim Aufnehmen eine andere Spur scharf ist (exklusives Scharfschalten).
  - **Freeze:** Live friert eine Spur aus ihren eigenen Clips ein, nicht aus geroutetem MIDI. Eine
    Synth-Spur, die nur über „MIDI From“ gespielt wird, deshalb **erst aufnehmen, dann einfrieren**
    (in Phase 0 bestätigen; Hinweis in der Setup-Hilfe).
- **Setup-Hilfe:** Ein Hilfe-Dialog im Hub zeigt den Aufbau für Live und Logic Schritt für Schritt,
  passend zum erkannten Host.
- Die Hub-Spur kann keine Master-Spur sein: Master und Stereo-Out nehmen keine MIDI-Effekte auf.

**Grundregeln der Umsetzung:**
- **Keine MIDI-Events in Echtzeit zwischen Instanzen.** Der Hub verteilt nur unveränderliche Patterns
  (Message-Thread). Jede Instanz spielt ihre Stimme selbst, synchron zum Transport, und übergibt sie wie in
  §6.3 an den eigenen Audio-Thread. Dadurch: keine Latenz, keine Thread-Kopplung.
- Prozessweite Registry (Singleton, Zugriff nur auf dem Message-Thread, nie im Audio-Thread)
- v1.0: eine Gruppe pro Projekt; eine Voice wählt ihre Stimme. ([Backlog]: Gruppen A–D für mehrere
  unabhängige Hubs pro Projekt.)
- Aktionen in einer Voice (z. B. Generieren per Tastenkürzel) werden an den Hub weitergereicht
- Wechsel (Slots, Patterns, Transposition) greifen in allen Instanzen der Gruppe zum selben musikalischen
  Zeitpunkt (gleiche Quantisierung, gleiche PPQ-Bezugsbasis)
- **Persistenz:** Jede Instanz speichert Gruppe, Rolle, zugeordnete Stimme und den **kompletten
  Slot-Satz aller Stimmen** (wenige Kilobyte). Ist der Hub vorhanden, gilt nach dem Laden sein Stand.
  Fehlt der Hub, spielt eine Voice ihre Stimme unverändert weiter und zeigt „Hub nicht gefunden“. Weil sie
  alle Stimmen kennt, kann sie bei Bedarf Hub werden.
- **Geplante Wechsel:** Slot-, Pattern- und Transpositionswechsel verteilt der Hub mit PPQ-Zeitstempel und
  Mindestvorlauf (§3.4, D-90). Jede Instanz führt sie in ihrem Audio-Thread zum selben PPQ aus. Der
  Zeitstempel wird über einen lock-freien, prozessweiten Kanal gelesen; die Registry selbst bleibt
  Message-Thread-only. Über denselben Kanal meldet jede Instanz ihre zuletzt verarbeitete PPQ-Position;
  der Hub plant ab der weitesten. Liegt einer Voice zum Zeitstempel das zugehörige Pattern noch nicht vor,
  wechselt sie am nächsten Quantisierungspunkt, an dem es vorliegt, und protokolliert den Versatz.
- **Slot-Automation und Start mitten im Arrangement in Gruppen:** Voices erfahren eine Slot-Automation des Hubs
  erst, wenn der Hub den betreffenden Block verarbeitet hat. Deshalb gibt es pro Voice den Schalter „Slot: folgt
  Hub / eigen“ (Standard: folgt Hub, im State gespeichert). Im Modus „eigen“ wertet die Voice ihre eigene
  Slot-Automation aus, auch bei Einzelspur-Bounce und Freeze. Regeln und Beispiele: §6.1a (D-131).
- **Mute** wirkt doppelt: Mute im Hub für Stimme X oder Mute in der Voice selbst schaltet die Stimme stumm.
- Wird der Hub gelöscht, bietet die älteste Voice an, Hub zu werden (keine stille Übernahme)
- **Gleiches Format:** Globale Daten werden zwischen AU- und VST3-Instanzen nicht zuverlässig geteilt.
  Setup-Hilfe und Vorlagen nutzen deshalb ein Format; [v1.1] die UI warnt bei gemischten Formaten.
- **Grenze:** funktioniert nur, wenn die Instanzen im selben Prozess laufen. Bei Plugin-Sandboxing
  (Bitwig im Modus „Individually“, Out-of-Process-Hosting) zeigt die UI „Kopplung nicht verfügbar“ mit
  Hinweis auf die Host-Einstellung (z. B. Bitwig „By Plug-in“). Wird in Phase 0 geprüft.

### 6.6 Live-Generierung
- Erzeugung läuft immer im Hintergrund, das aktuelle Pattern spielt weiter
- **[v1.1] Auto-Evolve**: alle N Durchläufe (einstellbar, Standard 4) automatisch eine neue Stufe
  einreihen. Quelle: algorithmische Variation ([Backlog]: KI-Generierung mit dem aktuellen Prompt).
- Auto-Evolve unterbricht nie die Wiedergabe. Kommt ein Ergebnis zu spät, wird es zum nächsten
  Quantisierungszeitpunkt übernommen.
- Bei Offline-Rendern ist Auto-Evolve inaktiv (vgl. §3.13)

### 6.7 [v1.1] Evolve-Ebene (nicht-destruktiv)
Auto-Evolve verändert **nie** den Slot selbst. Es spielt auf einer eigenen Ebene über dem aktiven Slot.

- **Start:** Auto-Evolve an → die Ebene startet mit einer Kopie des Slots. Der Slot bleibt unverändert.
- **Stufen:** Jede neue Stufe entsteht standardmäßig aus der vorherigen Stufe (Entwicklung). Option
  „immer vom Original“: Jede Stufe variiert den Slot selbst (hypnotische Varianten um einen festen Kern).
- Gesperrte Dimensionen des Slots (§3.5) bleiben auf allen Stufen erhalten
- **Behalten:** übernimmt die gerade klingende Stufe in den Slot, als ein Verlaufseintrag und ein
  Undo-Schritt. Auto-Evolve läuft danach von dort weiter.
- **Zurück:** Die letzten 8 Stufen bleiben während des Evolvens abrufbar (◀), um eine gerade verpasste
  Stufe noch zu behalten
- **Verwerfen/Aus:** Auto-Evolve aus → zum nächsten Quantisierungspunkt spielt wieder der unveränderte Slot
- **Slot-Wechsel** beendet die Ebene des alten Slots ohne Übernahme
- In einer Hub-Gruppe gehört die Ebene dem Hub; Voices spielen ihre Stimme der aktuellen Stufe
- Die Ebene wird **nicht** im Projekt gespeichert. Ein Bounce spielt deshalb immer den gespeicherten Slot.
- Die UI zeigt deutlich, dass gerade die Evolve-Ebene klingt (z. B. Slot-Rahmen pulsiert, Stufenzähler)

### 6.8 Vorrang bei gleichzeitigen Wechseln
(Punkte zu Evolve, Transposition und Wahrscheinlichkeit gelten ab v1.1.) Treffen mehrere Wechsel am selben Quantisierungspunkt ein, gilt diese Reihenfolge (höchste zuerst):
1. **Slot-Wechsel** (Automation oder Klick): gewinnt immer; eine laufende Evolve-Ebene des alten Slots
   endet, wartende Ergebnisse für den alten Slot landen nur im Verlauf dieses Slots. In der Engine ist das
   die Regel „Am Wechselpunkt spielt der Eintrag des gewählten Slots“ (D-135): Ein Ergebnis für einen anderen
   Slot wird nur abgelegt und klingt erst, wenn dieser Slot gewählt wird.
2. **Manuelles Ergebnis** (Generieren, Variation, Verfeinern, Laden aus der Bibliothek, Stimme importieren)
3. **Evolve-Stufe**: wird verworfen, wenn am selben Punkt ein manuelles Ergebnis kommt
- **Transposition** ist unabhängig davon und wird zusätzlich angewendet
- **Manuelle Aktionen während Evolve** wirken auf die gerade klingende Stufe: Das Ergebnis wird wie
  „Behalten“ in den Slot übernommen (ein Verlaufseintrag), und Evolve läuft von dort weiter. So geht
  nie verloren, was man gerade hört.
- Bei aktivem Evolve wirken Wahrscheinlichkeiten (§3.19) nur auf Noten, die Evolve in der laufenden
  Stufe nicht verändert hat, damit sich nicht zwei Variationsquellen überlagern

---

## 7. KI-Schicht (`mm::ai`)

### 7.1 Provider-Schnittstelle
```cpp
class IAiProvider {
public:
    virtual ~IAiProvider() = default;
    virtual ProviderInfo info() const = 0;                     // Name, Modelle, Fähigkeiten
    virtual std::future<AiResult> generate(const AiRequest&,   // asynchron
                                           CancellationToken) = 0;
    virtual std::future<ConnectionStatus> testConnection() = 0;
};
```
- **Abbruch:** Jede Anfrage läuft als abbrechbarer Job in einem Hintergrund-Thread (kein `std::async`).
  Der `CancellationToken` bricht auch die laufende Netzwerkverbindung ab (abbrechbarer HTTP-Stream,
  Verbindungsaufbau mit eigenem kurzen Timeout). Ergebnisse abgebrochener Jobs werden verworfen.
- **Plugin schließen:** Der Destruktor bricht alle Jobs ab und wartet auf ihr Ende (Ziel unter 1 s);
  danach greift kein Job mehr auf das Plugin zu.
- v1.0 nutzt keine Streaming-Antworten.

### 7.2 Provider v1
| Provider | Anbindung | Strukturierte Ausgabe |
|---|---|---|
| Anthropic (Claude) | Messages API, eigener API-Key | Tool Use mit JSON-Schema |
| OpenAI-kompatibel | Basis-URL + Key; Voreinstellungen für **OpenAI (ChatGPT)**, **Ollama** (lokal, OpenAI-kompatible Schnittstelle), LM Studio, OpenRouter | JSON-Schema-Antwortformat, wo unterstützt, sonst Prompt + Validierung |

Zwei Implementierungen statt vier: Der OpenAI-kompatible Provider deckt mehrere Anbieter über
Voreinstellungen ab. Eigene native Provider für OpenAI oder Ollama entstehen nur, falls die Prüfung in
Phase 3 es erfordert.

**Warum Claude nativ angebunden wird:** Anthropic bietet zwar eine OpenAI-kompatible Schnittstelle an,
laut Dokumentation aber zum Testen; `response_format` und `strict` werden dort ignoriert. Garantiert
schemakonforme Antworten gibt es nur über die native API mit Structured Outputs. Für MidiMaid ist
schemakonformes JSON zentral, deshalb bleibt der native Anthropic-Provider.

**„OpenAI-kompatibel“ heißt nicht „gleich“:** Die Unterstützung strukturierter Ausgabe unterscheidet sich je
Backend. Beispiel: Ollama setzt ab Version 0.5 lokal ein JSON-Schema über die kompatible Schnittstelle
durch, Ollama Cloud nimmt es dagegen an, ohne es durchzusetzen. Deshalb gilt:
- Jede Voreinstellung trägt eine **Fähigkeitsstufe**: *Schema erzwungen* (`json_schema`), *nur JSON*
  (`json_object`) oder *nur Prompt*. Maßgeblich ist die Stufe aus `models.json`. Der Verbindungstest
  (Testanfrage mit Schema, Ergebnis prüfen) kann sie nur herabstufen, nie heraufsetzen, weil eine
  einzelne gelungene Antwort nicht beweist, dass das Backend das Schema durchsetzt. Für eigene
  Basis-URLs ohne Voreinstellung ermittelt der Test höchstens *nur JSON*; *Schema erzwungen* nur per
  manueller Einstellung (Experte).
- Unabhängig von der Stufe wird jede Antwort validiert; Constraint-Schicht und Qualitätsbewertung
  laufen immer (§4.1, §4.4). Eine schwächere Stufe kostet höchstens Reparaturversuche, nie MIDI-Qualität.
- Über OpenRouter lassen sich weitere Modelle anbinden (auch Claude); für Claude bleibt der native
  Provider die empfohlene Wahl.

Modellnamen sind **nicht fest im Code verdrahtet**. Sie werden aus einer Konfigurationsdatei und, wo
möglich, aus der Modellliste des Anbieters geladen (bei Ollama die installierten Modelle).

### 7.3 Antwort-Schema (Version 1)
```json
{
  "schema_version": 1,
  "intent": {
    "energy": 0.8,
    "density": 0.6,
    "contour": "rising | falling | arch | static | wave",
    "motif_idea": "kurze Beschreibung",
    "groove": "straight | swing | offbeat | rolling"
  },
  "context": {
    "root": "A",
    "scale": "natural_minor",
    "progression": ["i", "i", "bVI", "bVII"]
  },
  "phrases": [{ "start_bar": 0, "bars": 8, "role": "main", "turnaround": true }],
  "voices": [
    { "role": "bass", "archetype": "rolling16",
      "notes": [{ "id": 12, "step": 1, "degree": 1, "alt": 0, "octave": 0, "len": 1, "vel": 100,
                  "accent": true, "slide": false, "ratchet": 1, "chance": 100, "cond": "1:1" }] },
    { "role": "melody", "archetype": "hypnotic_motif",
      "notes": [{ "id": 40, "step": 4, "degree": 5, "alt": 0, "octave": 1, "len": 1, "vel": 96 },
                { "step": 6, "degree": 2, "alt": -1, "octave": 1, "len": 1, "vel": 90 }] }
  ]
}
```
- `voices` ist eine Liste mit Rollen (offene Stimmenzahl, D-52); Akkorde = mehrere Noten auf demselben `step`
- `id` nur bei bestehenden Noten (Verfeinern); neue Noten ohne `id`
- `ratchet`, `chance`, `cond` werden ab v1.1 ausgewertet. In v1.0 sendet MidiMaid ein Schema ohne diese
  Felder. Der Parser ignoriert unbekannte Felder und Felder späterer Versionen ohne Fehler; die Rohantwort
  wird unverändert gespeichert.
- `step`: Position im 16tel-Raster (0 bis Takte × 16 − 1); `len` in 16teln (1 bis Pattern-Ende);
  `vel` 1–127
- `degree`: Skalenstufe 1 … n der Skala aus `context.scale` (n = Tonzahl, bei Pentatonik 5). Größere Werte
  werden mit Oktavübertrag umgerechnet, 0 und negative Werte sind ungültig. `alt`: Alteration in
  Halbtönen (−1, 0, +1) für skalenfremde Töne, z. B. der erhöhte Leitton als Stufe 7 mit `alt: 1` in
  natürlich Moll
- `octave` relativ zur **Grundlage der Stimme** = tiefster Ton mit der Tonhöhenklasse des Grundtons
  innerhalb ihres Tonumfangs (Bass, Grundton A, Umfang 28–52: MIDI 33)
- `progression`: Akkordsymbole nach STYLES.md §1.17 (`b`/`#` statt ♭/♯), ein Eintrag pro Takt; `"i|bVII"`
  teilt einen Takt in zwei Halbtakte. Ist die Liste kürzer als das Pattern, wiederholt sie sich; ihre
  Länge muss die Taktzahl teilen.
- Werte außerhalb der Bereiche werden begrenzt, Noten mit ungültiger Position verworfen; danach greift
  ohnehin die Constraint-Schicht.
- Bei Patterns über 8 Takte liefert die KI Motive (4–8 Takte) und einen Formplan, nicht die
  vollständige Notenfolge (§3.7): `voices[].notes` enthält dann nur das Motiv (Schritte ab Takt 0, Länge in
  `"motif_bars"`), `phrases` legt die Rolle jeder Phrase fest, und der Algorithmus leitet die Phrasen daraus
  ab.
- Bei ungültigem JSON: ein Reparaturversuch („gib nur gültiges JSON zurück“), danach Rückfall nach §4.1

### 7.4 Prompts
- Versionierte Vorlagen unter `resources/prompts/v1/` (System-Prompt allgemein, je Stil ein Zusatz,
  Generieren, Verfeinern)
- Eine Vorlage enthält: Rolle, Stilprofil, Regeln, Schema, 1–2 Beispiele, Nutzerprompt, ggf. importierte
  Stimme und Referenz-Beispiele als kompakte Notenliste (bei Cloud-Providern nur mit Zustimmung, §3.20)
- Prompt-Version wird im Pattern gespeichert
- **Gegen generische Ergebnisse:** Jede Vorlage enthält das vollständige Stilprofil als Regeln,
  Archetyp-Beschreibungen, Motivik-Regeln (STYLES.md §1.11) und eine Ausschlussliste typischer
  Klischees (z. B. Dur-Pop-Progressionen wie I–V–vi–IV, gleichförmige Achtelketten ohne Motiv,
  durchgehende Arpeggios außerhalb der Arp-Archetypen)
- Die Prompts fordern ausdrücklich Wiederholung mit gezielter Variation statt ständig neuer Töne
- **Persönliche Beispiele:** bis zu 3 Einträge des aktiven Referenz-Sets als Few-Shot-Beispiele (§3.20),
  deutlich als „Stil des Nutzers“ gekennzeichnet, mit der Anweisung, den Stil aufzugreifen, aber nicht
  zu kopieren; sie haben Vorrang vor den allgemeinen Beispielen der Vorlage

### 7.5 Robustheit und Kosten
- Timeouts: Cloud 30 s, Ollama 120 s (einstellbar), Abbruch-Knopf in der UI
- Wiederholung mit Backoff bei 429 und 5xx (max. 2), keine Wiederholung bei 401 und 400
- Klare Fehlermeldungen (Key ungültig, Modell nicht gefunden, Ollama nicht erreichbar …)
- [v1.1] Token-Verbrauch und **geschätzte Kosten** der letzten Anfrage und der Sitzung anzeigen
  ([Gestrichen]: laufender Monat). Bei Ollama nur Tokens.
- [v1.1] Preise pro Modell in einer editierbaren Datei (`pricing.json` im Datenordner, mit Standardwerten
  ausgeliefert), weil sich Anbieterpreise ändern. Die Anzeige ist ausdrücklich als Schätzung gekennzeichnet.
- Keine Limits und keine Sperren (D-32). `max_tokens` pro Anfrage einstellbar (v1.0).
- Kein Netzwerkzugriff, solange kein Provider aktiv ist

### 7.6 Kreativitäts-Regler
- Regler 0–100 % in der Hauptoberfläche, Standard 40 %
- Wirkung auf die KI: Temperatur (auf den gültigen Bereich des Providers abgebildet), Anweisung im
  Prompt (stiltreu ↔ experimentell)
- Wirkung auf den Algorithmus (auch offline): Streuung der Archetyp-Gewichte, Anteil von Variationen
  innerhalb einer Phrase, Wahrscheinlichkeit ungewöhnlicher Intervalle (begrenzt durch den Chromatik-Anteil)
- Die Constraint-Schicht bleibt unabhängig vom Regler immer aktiv
- **Kreativität und Qualitätsbewertung:** Bei hoher Kreativität werden die weichen Bewertungskriterien
  (Motiv-Wiederholung, Stiltypik) schwächer und Neuheit stärker gewichtet. Sonst würde die Bewertung
  gerade die ungewöhnlichen Kandidaten aussortieren, die der Regler erzeugen soll. Harte Regeln
  (Constraints) gelten immer.

### 7.7 Modellwahl
- Pro Provider ist ein empfohlenes Modell vorausgewählt, frei änderbar aus der Modellliste
- Empfehlungen stehen in `models.json` (ausgeliefert, im Datenordner überschreibbar) und nicht im Code
- Ist das empfohlene Modell beim Anbieter nicht verfügbar, wird das nicht still ersetzt, sondern
  angezeigt (§7.9)

### 7.8 Wann die KI angefragt wird
Nur bei diesen Aktionen, sonst nie:
1. Klick auf **Generieren** (oder Neu für eine einzelne Stimme oder Phrase)
2. **Verfeinern per Prompt** (§3.15)
([Backlog]: Auto-Evolve mit KI-Quelle.) Die Analyse von Referenzen und importierten Stimmen läuft immer
lokal, ohne KI.

Variationen per Knopf, Bearbeitung, Drum-Referenz, Referenzen, Transponieren und Bibliothek arbeiten ohne KI.

### 7.9 Fehlerverhalten: zeigen und nachfragen
- Fehler werden verständlich angezeigt (Ursache und Lösungsvorschlag, z. B. „API-Key ungültig →
  Einstellungen“, „Ollama nicht erreichbar → läuft Ollama?“)
- Der Nutzer entscheidet: **Erneut versuchen**, **Offline erzeugen** (nur bei Generieren) oder **Abbrechen**
- Option „Für diese Sitzung automatisch offline weiter“ im Dialog, Standard aus
- **Während der Wiedergabe nie ein modaler Dialog.** Stattdessen eine nicht blockierende Leiste im
  Plugin mit denselben drei Aktionen. Das laufende Pattern spielt unverändert weiter.
- Interne Wiederholungen (Backoff bei 429/5xx, ein JSON-Reparaturversuch) laufen ohne Nachfrage

---

## 8. Benutzeroberfläche (`mm::ui`)

### 8.1 Layout (skalierbar, HiDPI, dunkles Theme)
- Standardgröße ca. 1000 × 640 px, frei skalierbar von 75 % bis 200 % mit festem Seitenverhältnis,
  die Größe wird pro Instanz gespeichert
- **Zwei Ebenen gegen Regler-Überladung:**
  - *Basis* (Standard): Stil, Referenz-Set, Tonart/Skala, Takte, Slots, Prompt, Verfeinern, Generieren,
    Energie, Kreativität, pro Stimme Neu/Variation/Sperren/Mute/Ziehen; [v1.1] Evolve
  - *Experte* (einklappbar): Seed, Kick-Raster, Swing pro Stimme, Archetyp-Wahl, Quantisierung,
    Oktav-Offset, Stärke-Regler; [v1.1] Chromatik, Regler „Persönlich“, Groove-Vorlagen,
    Einsatzposition Legato, Wahrscheinlichkeits-Spur
  - Alle Experten-Werte haben stiltypische Standards; wer nur Basis nutzt, bekommt stimmige Ergebnisse
- **Starttonart:** zuletzt verwendete Tonart, beim allerersten Start A-Moll
- Look: dunkel, minimalistisch, an Ableton angelehnt: flache Flächen, wenig Farbe, eine Akzentfarbe
  pro Stimme (Bass, Melodie), gut lesbare Sans-Serif-Schrift, keine Skeuomorphie
- Piano-Roll: beide Spuren übereinander **oder** Fokus auf eine Spur (volle Höhe), umschaltbar per Knopf
  und Tastenkürzel
- **Voice-Oberfläche** (kompakt, ca. 600 × 320 px): Gruppe und Stimme wählen, Spur der eigenen Stimme
  (nur lesend, Bearbeitung im Hub), Mute, Oktave, Schalter „Slot: folgt Hub / eigen“ (§6.1a), Drag & Drop, Ablage für Drum-Referenzen,
  Status „mit Hub verbunden“, [v1.1] Anzeige der Gruppen-Transposition,
  Knopf „Hub öffnen“ (bringt das Hub-Fenster nach vorn, soweit der Host das erlaubt)

```
┌──────────────────────────────────────────────────────────────┐
│ Stil ▾  Set: Klirrwerk 2026 ▾  Tonart ▾  Skala ▾  Takte ▾  ▶  ⚙️│
│ Slots [1][2][3][4][5][6][7][8][9][10]…[16]                   │
├──────────────────────────────────────────────────────────────┤
│ Prompt: [ dunkle treibende Bassline mit Slides …          ]  │
│ Verfeinern: [ weniger Noten im Bass ]  ↵                     │
│ Drums: [ Clip hier ablegen ]                  [ Generieren ] │
│ Kreativität ━━━●━━━━  Energie ━━━━━●━━   (v1.1: Evolve, Transp.)│
├──────────────────────────────────────────────────────────────┤
│ BASS    🔒 M  [Neu] [Variation ▾ Stärke]   ⠿ Ziehen           │
│ ███  ██   ███  ██ … Piano-Roll                               │
│ MELODIE 🔒 M  [Neu] [Variation ▾ Stärke]   ⠿ Ziehen           │
│ ▆ ▆▆  ▆   ▆▆ …                                               │
├──────────────────────────────────────────────────────────────┤
│ ◀ Verlauf ▶  💾  📚   Offline ● · Tokens 1.2k · ~0,01 €        │
└──────────────────────────────────────────────────────────────┘
```

### 8.2 Sprache
- v1.0: Deutsch, mit vollständiger Übersetzungs-Infrastruktur; [v1.1] Englisch, umschaltbar, Standard
  folgt der Systemsprache
- Alle Texte über eine zentrale Übersetzungstabelle (`resources/i18n/de.json`, `en.json`), keine
  festen Strings im UI-Code
- Ein Test prüft, dass alle Sprachdateien dieselben Schlüssel enthalten (ab v1.1 relevant)
- Prompts an die KI bleiben unabhängig von der UI-Sprache in Englisch (bessere Ergebnisse); der
  Nutzerprompt darf in jeder Sprache sein

### 8.3 [v1.1] Tastenkürzel (vom Benutzer belegt)
**MidiMaid belegt keine Taste vorab (D-126).** Die Aktionen unten gibt es als Kürzel-Aktionen, aber ohne
Voreinstellung: Der Benutzer weist ihnen in den Einstellungen selbst Tasten zu. So kollidiert MidiMaid nicht mit
Host-Kürzeln, und Tasten, die ein Host abfängt, wählt der Benutzer bewusst oder gar nicht. Cmd steht für Ctrl unter
Windows und Linux. Kürzel greifen nur bei Fokus im Plugin-Fenster.

- Zuweisbare Aktionen: Zeichenmodus an/aus, Duplizieren, Alles auswählen, Löschen, Rückgängig / Wiederholen,
  Note(n) Halbton hoch/runter, Note(n) Oktave hoch/runter, Note(n) um Rasterschritt verschieben, Notenlänge ändern,
  Raster enger / weiter, Triolen-Raster, Raster-Einrasten an/aus, Scale-Fold an/aus, Spurfokus Stimme 1 … 8 / alle,
  Generieren, Verfeinern absenden, Variation (fokussierte Stimme), Verlauf zurück / vor
- Die Belegung ist eine instanzübergreifende Einstellung (§9.2), kein Projekt-Zustand
- Ein Eintrag „Standard wiederherstellen“ setzt alle Belegungen auf „keine“
- **Leertaste ist nicht belegbar**, sie bleibt Transport der DAW
- Kürzel ohne Modifier sind inaktiv, solange ein Textfeld (Prompt, Verfeinern) den Fokus hat
- Ergebnis der Host-Verifikation (Phase 0, Live 12.4.6, Logic 12.4): Alle Tasten außer den folgenden erreichen
  das Plugin-Fenster, manche lösen zusätzlich eine Host-Aktion aus. **Live:** Esc schließt das Plugin-Fenster.
  **Logic:** Cmd+Z führt das Logic-Undo aus (macht z. B. das Einfügen des MIDI-Effekts rückgängig und schließt
  damit das Fenster). Das ist Standardverhalten der Hosts; MidiMaid weist im Belegungsdialog nicht darauf hin.
- **Ab v1.0: Jede Aktion hat eine Schaltfläche** (Rückgängig/Wiederholen, Duplizieren, Generieren,
  Variation), damit nichts nur über die Tastatur erreichbar ist

### 8.4 Einstellungen
Provider aktivieren, Key eingeben (maskiert), Modell wählen, Verbindung testen, Timeouts, Temperatur,
Max-Tokens, MIDI-Kanäle, Slide-Überlappung, Akzent-Velocity, Kick-Freiraum, Oktav-Offset pro Stimme,
Drum-Zuordnung (Liste), Zustimmung pro Cloud-Provider für Referenz-Beispiele und importierte Stimmen
(anzeigen, widerrufen; §3.20), Mindestvorlauf geplanter Wechsel (Experte, §3.4), „Prompts protokollieren“
(§10),
[v1.1] Tastenbelegung (§8.3), Sprache, Notennamen-Konvention (C3 = 60 oder C4 = 60), Transponieren (Referenznote, Modus,
Verhalten, Zeitpunkt, Ziel, Kanal), MIDI Thru, Favoriten zusätzlich ins aktive Set, Export-Modus für
Wahrscheinlichkeiten, Tonumfänge, Standardstil, Quantisierung beim Wechsel, Logging an/aus.

### 8.5 Speicherung von API-Keys
- macOS: Keychain (Security.framework)
- Windows: Credential Manager (`CredWrite`/`CredRead`)
- Linux: libsecret. Fehlt es, wird der Key nicht gespeichert und muss pro Sitzung eingegeben werden
  (kein Klartext-Fallback).

### 8.6 Vorlagen für die DAW
Damit der erste Einsatz nicht an Routing scheitert, liefert MidiMaid fertige Vorlagen mit:
- **Live:** ein Live-Set mit Gruppenspuren „Bass“ und „Melodie“, jeweils MidiMaid-Spur (Hub bzw. Voice,
  Gruppe A) plus Synth-Spur mit Live-eigenem Instrument-Preset (§12, Referenz-Instrumente), Routing und
  Monitoring fertig eingestellt;
  [v1.1] Beispiel-Clips mit Slot-Hüllkurven für die Session-Ansicht. Spuren lassen sich per Drag aus dem Browser
  in bestehende Sets ziehen.
- **Logic:** Channel-Strip-Vorlagen (Patches) mit Logic-eigenem Instrument (§12), MidiMaid im
  MIDI-FX-Slot als Hub bzw. Voice und „Record MIDI to Track Here“ dahinter
- Installation in die Benutzerbibliothek, Beschreibung in der Setup-Hilfe

---

## 9. Persistenz

### 9.1 Plugin-State (im DAW-Projekt)
- Alle 16 Slots (aller Stimmen) mit Name und Farbe, Verlauf (begrenzt: das aktuelle Pattern und die letzten 5 Ergebnisse
  je Slot, D-137), Verfeinerungs-Kontext, Parameter, Rolle, Gruppe,
  (nicht gespeichert: die Evolve-Ebene, §6.7),
  zugeordnete Stimme, Ausgabemodus, gewählter Provider und Modell, **keine Keys**
- Instanzübergreifende Einstellungen (Drum-Zuordnung, Provider-Konfiguration, Notennamen-Konvention)
  liegen in den globalen Einstellungen (§9.2), nicht im Projekt
- Format: `juce::ValueTree` → XML (Wurzel `MidiMaid` mit Attribut `stateVersion`, derzeit 1, darin der Parameterzustand), mit Migrationsfunktionen; Patterns sind darin als
  JSON aus `core` eingebettet (§5, dieselbe Form wie in der Bibliothek).
  Stand D-139: Attribute `role` (`solo`/`hub`/`voice`), `outputMode` (`one_voice`/`none`), `outputVoice` (1 bis 8) und das
  Kindelement `Slots` mit dem Slot-Satz als JSON (D-137). Fehlende oder unbekannte Werte ergeben Solo, eine Stimme, Stimme 1
  und leere Slots; ein unlesbarer Slot-Satz bleibt leer und wird gemeldet, der übrige State lädt
- Laden eines Projekts aus einer neueren Plugin-Version darf nicht abstürzen

### 9.2 Bibliothek und Einstellungen
- macOS: `~/Library/Application Support/Klirrwerk/MidiMaid/`
- Windows: `%APPDATA%\Klirrwerk\MidiMaid\`
- Linux: `~/.local/share/Klirrwerk/MidiMaid/`
- Ein JSON pro Pattern, ein Index für die schnelle Suche, atomares Schreiben (temp + rename)
- Unterordner `References/`: Originaldateien, Analyse-JSON pro Datei (Schlüssel: Hash), Set-Definitionen
  pro Stil, berechnete Profile

---

## 10. Logging und Fehlerbehandlung
- Logdatei im Datenordner, rotierend (5 × 1 MB), Level einstellbar
- Nie aus dem Audio-Thread loggen (falls nötig: lock-free Ringpuffer, den der Message-Thread leert)
- API-Keys nie, auf keinem Level
- Prompts, KI-Antworten und Nutzerinhalte (Referenzen, importierte Stimmen, Patterns) werden
  standardmäßig nicht protokolliert, auch nicht auf Debug-Level. Dort stehen Metadaten: Provider, Modell,
  Dauer, Tokens, Statuscode, Schemafehler mit Pfad (D-86).
- Vollständige Prompts und Antworten nur mit der eigenen Option „Prompts protokollieren“ (Standard aus,
  gedacht für die Prompt-Entwicklung)

---

## 11. Nicht-funktionale Anforderungen
| Bereich | Ziel |
|---|---|
| CPU im Leerlauf und beim Abspielen | vernachlässigbar (< 1 % eines Kerns) |
| Offline-Generierung | < 100 ms für 16 Takte inklusive 8 Kandidaten |
| Laden des Plugins | < 500 ms |
| Referenz-Analyse | 200 Dateien in < 30 s im Hintergrund, ohne die Wiedergabe zu beeinflussen |
| Stabilität | 0 Abstürze, 0 hängende Noten im Testplan |
| Timing | sample-genau innerhalb des Blocks |
| macOS | Universal Binary (arm64 + x86_64), ab macOS 15 (`CMAKE_OSX_DEPLOYMENT_TARGET 15.0`) |
| Windows | Windows 10/11, x64 |

---

## 12. Tests
- **Unit (Catch2):** Skalen, Quantisierung, Constraints, Generatoren pro Stil (Eigenschaften und Golden
  Files), Variationen, Schema-Parsing inklusive kaputter und bösartiger KI-Antworten, State-Migration
- **Eigenschaftstests** über Seed-Serien (z. B. 1000 Seeds je Stil und Archetyp): Tonumfang, keine
  Bassnote auf Kick-Schritten (außer der Archetyp erlaubt es), Kick-Freiraum, Intervallregeln auf
  betonten Schritten, keine Überlappung gleicher Tonhöhe, Motiv-Wiederholung, Dichte im Zielband der
  Energie, Anteil von Nicht-Akkordtönen bei Linien-Archetypen, Grenzfälle des Kopierschutzes
- **Engine:** simulierte Playheads (Start, Stop, Loop, Sprünge vor und zurück, Tempowechsel, fehlende
  Host-Daten, Blockgrößen 16 bis 4096), volle Rückgabe-Queue, Bearbeitung während der Wiedergabe und
  Prüfung auf hängende Noten
- **KI:** Mock-Provider (auch Abbruch während der Anfrage und Schließen des Plugins); optional manuell
  auszulösende Integrationstests gegen echte APIs
- **Referenz-Analyse:** Testkorpus mit bekannter Tonart, Rolle und Akkordfolge (eigene oder synthetisch
  erzeugte Testdateien, keine fremden Songs): mindestens 20 Dateien je Stil, automatisch in alle 12
  Tonarten transponiert, dazu Varianten mit Swing, unquantisiert (±20 Ticks), mehrstimmig und mehrspurig.
  Eine Tonart gilt als richtig, wenn Grundton und Skala stimmen; Verwechslungen mit der Paralleltonart
  zählen als falsch und werden getrennt ausgewiesen. Zielwerte: Tonart ≥ 90 %, [v1.1] Rolle ≥ 95 %
  (automatische Rollen-Erkennung); Kopierschutz-Tests
- **Host:** pluginval (Strictness 10), auval, manuelle Testmatrix (`ROADMAP.md`, Anhang)
- **Hörtest-Werkzeug `mmgen`:** Kommandozeilenprogramm auf Basis von `core`, erzeugt .mid-Dateien pro
  Stil, Archetyp, Energie und Seed (auch Serien, z. B. 20 Seeds je Archetyp) in einen Ordner. Damit
  lassen sich Stilprofile schon in Phase 1a in der DAW abhören, bevor es ein Plugin gibt.
- **Hörtest-Protokoll:** je Stil und Archetyp 20 Seeds bei drei Energiewerten (0,3 / 0,6 / 0,9), jedes
  Ergebnis bewertet als „sofort nutzbar“, „nachbearbeiten“ oder „unbrauchbar“. Startziel je Stil:
  mindestens 80 % sofort nutzbar. Ergebnisse, Profiländerungen und kalibrierte Bewertungsgewichte kommen
  nach STYLES.md.
- **Referenz-Instrumente:** Hörtests, Testmatrix und DAW-Vorlagen nutzen **nur Live- bzw. Logic-eigene
  Instrumente**, damit Ergebnisse überall reproduzierbar sind. Anforderungen und Kandidaten (Eignung in
  Phase 0 bestätigen):

| Rolle | Anforderung | Live 12 Suite (Kandidaten) | Logic Pro (Kandidaten) |
|---|---|---|---|
| Bass, Acid | Mono, Legato-Glide (für Slides), Velocity → Filter/Lautstärke | Analog, Drift | ES2, Retro Synth |
| Stabs, Akkorde | Polyphon, kurze Hüllkurve, Velocity → Filter | Wavetable, Analog | ES2, Alchemy |
| Lead, Arp | Polyphon oder Mono, Velocity-Reaktion | Wavetable, Drift | Retro Synth, Alchemy |

  Für jede Rolle entsteht ein Instrument-Preset (Live-Preset bzw. Logic-Patch), das in die DAW-Vorlagen
  (§8.6) eingeht.
- **CI:** GitHub Actions: macOS und Windows für Build, Tests und pluginval; Linux für die `core`-Tests
  (Determinismus mit einer dritten Standardbibliothek) und als reiner Kompiliertest des VST3-Plugins
  - Toolchains: macOS arm64 (AppleClang, libc++), Windows x64 (MSVC), Linux x64 (GCC, libstdc++). Die
    Golden Files müssen auf allen drei identisch sein; die `core`-Tests laufen auf macOS zusätzlich als
    x86_64 unter Rosetta (Universal Binary).
  - Versionen von Runner-Image, Xcode, MSVC, GCC und pluginval sind in der Workflow-Datei festgeschrieben
    und stehen in `DECISIONS.md`.

---

## 13. Technologie
- C++20, CMake ≥ 3.25, JUCE 8 (über CPM, Version festgepinnt)
- Catch2 v3, nlohmann/json
- IDE: JetBrains CLion mit `CMakePresets.json`
- Lizenzen von JUCE und VST3-SDK **vor dem Verkauf** prüfen und das Ergebnis in `DECISIONS.md` festhalten

## 14. Spätere Kommerzialisierung (nicht v1, aber vorbereitet)
Marktumfeld und Preisanker siehe `docs/REVIEW.md` §1. Rechte an KI-Ausgaben: Nutzungsbedingungen der
Provider vor dem Verkauf prüfen und im Handbuch erklären. Der algorithmische Kern funktioniert ohne KI
und ohne Trainingsdaten, das ist ein Verkaufsargument.

Lizenzierung und Aktivierung, Code-Signierung und Notarisierung (Apple Developer Account), Installer
(macOS .pkg, Windows Inno Setup), Absturzberichte nur mit Zustimmung, Handbuch, Website.
