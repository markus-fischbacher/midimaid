# MidiMaid – Feature-Bewertung (04.10.2026)

Kritische Prüfung aller Funktionen auf Sinnhaftigkeit, Nutzen und Wechselwirkungen.

> **Entschieden (D-74, D-75):** v1.0 = alle Funktionen mit Bewertung 1 und 2, Bewertung 3 folgt in v1.1.
> Bewertung 4 ist aufgeteilt in **Backlog** (echter Nutzen, später prüfen) und **Gestrichen** (überflüssig,
> ersetzt oder schädlich). Die Spalte „Empf.“ zeigt den verbindlichen Stand.

**Bewertung:** 1 = Muss · 2 = Sollte · 3 = Kann (Mehrwert, aber verzichtbar) · 4 = Nice to have
**Aufwand:** S = klein · M = mittel · L = groß
**Empfehlung:** v1.0 = erste nutzbare Version · v1.1 = direkt danach · Backlog = später oder nie

---

## A. Fundament

| Feature | Bew. | Aufw. | Nutzen / Begründung | Wechselwirkungen, Risiken | Empf. |
|---|---|---|---|---|---|
| Algorithmischer Generator mit Stilprofilen und Archetypen | 1 | L | Herz des Produkts, offline voll nutzbar, unabhängig von KI-Launen | – | v1.0 |
| Gemeinsamer Kontext Bass + Melodie | 1 | M | Alleinstellungsmerkmal | Grundlage für Hub & Voices | v1.0 |
| Motiv-Engine (Motiv zuerst, Wiederholung, Durchgangstöne) | 1 | M | Hauptkritik an Konkurrenz: beliebige, arpeggio-artige Melodien | Motiv-Zellen aus Referenzen, Kopierschutz | v1.0 |
| Constraint-Schicht (Skala, Akkord-Skala, Register, Kick-Aussparung, Kick-Freiraum, Intervalle) | 1 | M | Versprechen „nie unbrauchbar“ | Viele Regeln greifen in der Ausgabestufe ineinander | v1.0 |
| Qualitätsbewertung mit 8 Kandidaten | 1 | M | Hebt die Qualität ohne KI-Kosten | Zieht in vier Richtungen (Stil, Kreativität, Nähe zum Set, Wiederholungs- und Kopierschutz): aufwendiges Tuning | v1.0 |
| Engine: Transport-Sync, Note-Off-Garantie, lock-free | 1 | M | Stabilität ist nicht verhandelbar | – | v1.0 |
| Zwei Plugin-Varianten (Instrument VST3/AU, MIDI-FX AU) | 1 | S | Ohne geht Live oder Logic nicht | – | v1.0 |
| Hub & Voices (eine Gruppe) | 1 | L | Ohne sind zwei gekoppelte Stimmen weder in Live noch in Logic möglich | Persistenz, geplante Wechsel, Formatwarnung, Sandboxing | v1.0 |
| Geplante Wechsel mit PPQ-Zeitstempel | 1 | S | Ohne laufen Hub und Voices auseinander | – | v1.0 |
| Eigener PRNG, Determinismus | 1 | S | Golden-File-Tests, Reproduzierbarkeit | Jede weitere Eingabe (Verlauf, Profil, Set) muss in den Schnappschuss | v1.0 |
| Offene Stimmenzahl (Datenmodell, 8 Parameter reserviert) | 2 | S | Jetzt billig, später teuer | – | v1.0 |
| Gruppen A–D (mehrere Hubs pro Projekt) | 4 | S | Selten gebraucht | Mehr UI und Testfälle | Backlog |
| Stilprofile als JSON | 2 | S | Feintuning ohne Neukompilieren, Hörtest-Schleife | – | v1.0 |

## B. Musikalische Werkzeuge

| Feature | Bew. | Aufw. | Nutzen / Begründung | Wechselwirkungen, Risiken | Empf. |
|---|---|---|---|---|---|
| Drei Stile mit je 3 Bass- und 4 Melodie-Archetypen | 1 | M | Kernnutzen | – | v1.0 |
| Slides (303, Überlappung) | 2 | S | Prägt Acid und viele Basslines | Kick-Freiraum, Groove, Wahrscheinlichkeit, Ratchets | v1.0 |
| Akzent-Flag und Velocity-Konturen | 2 | S | Velocity steuert im Techno oft den Filter | – | v1.0 |
| Stabs, mehrstimmige Melodie, Voicing (enge Lage, Stimmführung) | 2 | M | Stabs gehören zu allen drei Stilen | Intervallregeln | v1.0 |
| Chord-Memory | 3 | S | Detroit- und Dub-Farbe | Konflikt mit Intervallregel (gelöst durch Verschieben) | v1.1 |
| Swing pro Stimme | 2 | S | Groove ist Pflicht | Doppelter Swing mit Drum-Groove (gelöst) | v1.0 |
| Groove-Vorlagen-Sammlung (Laid back, Pushed, Humanize …) | 3 | S | Feinschliff | – | v1.1 |
| Chromatik-Regler | 3 | S | Stilstandards decken die meisten Fälle ab | Einer von sechs Intensitätsreglern (siehe W2) | v1.1 |
| Ratchets | 3 | S | Würze für Hard und Acid | Slide, Wahrscheinlichkeit, Ausgabestufe | v1.1 |
| Euklidische Rhythmen | 4 | S | Als interne Rhythmusquelle sinnvoll, als Feature unnötig | – | Gestrichen |
| Turnaround | 3 | S | Lebendigere Phrasen | – | v1.1 |
| Polymeter-Sequenzen (Neustart) | 3 | M | Hypnotische Verschiebungen | Export, Slots | v1.1 |
| Polymeter „frei laufend“ | 4 | M | Selten hörbarer Unterschied | Nicht exportierbar, Sonderfälle bei Sprüngen und Voices | Backlog |
| Patterns bis 16 Takte (Phrasen 4/8/16) | 2 | M | Reicht für Loops und Abschnitte | – | v1.0 |
| Patterns bis 64 Takte mit Formplan | 4 | L | Arrangiert wird ohnehin in der DAW (Slots + Automation) | KI-Schema, Tokens, Verfeinern auf Phrasenebene, Speicher | Backlog (v1.0: 16 Takte) |
| Kick-Raster (5 feste) | 2 | S | Bass weicht der Kick aus | – | v1.0 |
| Kick-Grundton mit Tonart-Vorschlag | 4 | S | Komfort; die Tonart wählt man ohnehin | – | Backlog |
| Energie (Makro) | 2 | S | Ein Regler für Dichte, Velocity, Akzente | – | v1.0 |
| Dichte-Korrektur | 4 | S | Redundant zu Energie | Verwirrt (W2) | Gestrichen |
| Kreativität | 2 | S | Spanne zwischen stiltreu und experimentell | Bewertung muss mitziehen | v1.0 |
| Wahrscheinlichkeit und Bedingungen A:B | 3 | M | Variation in jedem Durchlauf | Export nur aufgelöst; Aufnahme zeigt nur eine Realisierung; überschneidet sich mit Evolve (W5); Ausgabestufe | v1.1 |
| Wiederholungsschutz | 3 | S | Weniger Déjà-vu beim Generieren | Determinismus (gelöst) | v1.1 |

## C. Workflow

| Feature | Bew. | Aufw. | Nutzen / Begründung | Wechselwirkungen, Risiken | Empf. |
|---|---|---|---|---|---|
| Drag & Drop pro Stimme | 1 | S | Ergebnis kommt in die DAW | – | v1.0 |
| Kombinierter Export (alle Stimmen in einer Spur) | 4 | S | Landet als ein Clip mit gemischten Kanälen | – | Gestrichen |
| Aufnahme-Workflow (Doku, Setup-Hilfe) | 2 | S | Zweiter Weg in die DAW | – | v1.0 |
| Eigene Stimme importieren | 1 | M | Häufigster Studiofall: Bassline da, Melodie gesucht | – | v1.0 |
| Variationen (subtil, strukturell, Stärke) | 1 | M | Techno lebt von Variation | – | v1.0 |
| Undo/Redo | 1 | S | Pflicht | – | v1.0 |
| Verlauf (20 Ergebnisse) | 2 | S | Zurück zu einem verworfenen Ergebnis | – | v1.0 |
| Stimme sperren | 1 | S | Bass behalten, Melodie neu | – | v1.0 |
| Sperren pro Dimension und Note | 3 | M | Feinsteuerung (Riffer-Vorbild) | Noten-IDs, Verfeinern, Evolve | v1.1 |
| Slots (16) | 2 | M | Abschnitte, Live-Einsatz, Arrangement per Automation | Evolve, Gruppen, State | v1.0 |
| Slot sofort bei Start/Sprung | 1 | S | Sonst falscher erster Takt im Arrangement | – | v1.0 |
| Quantisierte Wechsel, einstellbar | 2 | S | Wechsel im Takt | Prioritäten (W4) | v1.0 |
| Legato-Einsatz | 3 | S | Nischig | Determinismus der Position | v1.1 |
| Vorhören bei gestopptem Transport | 3 | M | DAW-Play genügt meist | Logic rendert MIDI-FX im Stopp womöglich nicht; Voices | v1.1 |
| Piano-Roll Grundfunktionen | 2 | L | Kleine Korrekturen ohne Umweg über die DAW | Großer UI-Aufwand | v1.0 |
| Piano-Roll-Hilfen (Scale-Fold, Slide/Akzent-Zeile, Wahrscheinlichkeits-Spur, Hintergründe) | 3 | M | Komfort | – | v1.1 |
| Bibliothek: speichern, laden, .mid-Import/Export | 2 | M | Ergebnisse sichern | – | v1.0 |
| Bibliothek: Tags, Suche, Filter, Vorhören | 3 | M | Bei großer Sammlung wichtig | – | v1.1 |
| Auto-Evolve (algorithmisch) mit Evolve-Ebene | 3 | M | Live-Performance | Zustandslogik mit Slots, Variation, Verfeinern (W4, W6) | v1.1 |
| Auto-Evolve mit KI-Quelle | 4 | M | Kaum Mehrwert gegenüber algorithmisch | Kosten, Latenz, Fehler während der Wiedergabe | Backlog |
| Live-Transponieren per Tastatur | 3 | M | Live-Sets | MIDI-Eingang, Gruppen-Timing, Live-Monitoring, Parameter-Addition | v1.1 |
| Host-Parameter Slot, Mute, Transponieren | 2 | S | Arrangement, Controller | – | v1.0 |
| Trigger Generieren/Variation per Automation | 3 | S | Controller-Knopf im Live-Set | Wird beim Bounce ignoriert | v1.1 |
| Kreativität/Energie automatisierbar | 4 | S | Wirken erst bei der nächsten Generierung, Automation läuft ins Leere | – | Backlog (Parameter-ID reserviert) |
| Schaltflächen für alle Aktionen | 1 | S | Live fängt Kürzel ab | – | v1.0 |
| Tastenkürzel | 3 | S | Tempo für Profis | Host-Konflikte | v1.1 |
| Basis-/Experten-Ebene | 2 | S | Gegen Regler-Überladung | – | v1.0 |
| DAW-Vorlagen und Setup-Hilfe | 2 | S | Erster Einsatz scheitert sonst am Routing | – | v1.0 |

## D. Lenkung und KI

| Feature | Bew. | Aufw. | Nutzen / Begründung | Wechselwirkungen, Risiken | Empf. |
|---|---|---|---|---|---|
| Textprompt mit KI-Generierung | 2 | M | Markenkern „KI“; schnelle Richtungswechsel | Generische Ausgaben (abgefangen durch Constraints, Bewertung, Beispiele) | v1.0 |
| Provider: Anthropic + **ein** OpenAI-kompatibler Provider | 2 | M | Der OpenAI-kompatible Weg deckt OpenAI, Ollama, LM Studio und OpenRouter ab; Ollama bietet eine OpenAI-kompatible Schnittstelle | Strukturierte Ausgabe je Anbieter in Phase 3 prüfen | v1.0 |
| Eigene native Provider für OpenAI und Ollama | 4 | M | Doppelter Code für fast denselben Zweck | Mehr Tests | nur falls Phase 3 es erfordert |
| Struktur-Modus als zweiter KI-Pfad | 4 | M | Rückfallebene, die selten greift | Zweites Schema, zweite Prompts, mehr Tests | Gestrichen |
| Verfeinern per Prompt | 2 | M | In Reviews als Stärke von LLM-Werkzeugen genannt | Noten-IDs, Sperren, Verlauf | v1.0 |
| Offline-Verfeinern per Schlüsselwort | 4 | M | Fehleranfälliger Parser in zwei Sprachen; die Variationsknöpfe decken das meiste ab | – | Gestrichen |
| MIDI-Vorlage als eigenes Konzept | 4 | M | Überschneidet sich mit Referenzen und „Stimme importieren“ (W3) | Zweiter Analysepfad | Gestrichen (in Referenzen aufgegangen) |
| Referenzen: Sets, Import, Tonart, Progressionen, KI-Beispiele, Kopierschutz | 2 | L | Persönlicher Sound, Lenkung durch eigenes Material | Determinismus-Schnappschuss, Bewertung | v1.0 |
| Referenzen: Motiv-Zellen, Rhythmus-Statistik, Profil pro Set | 3 | L | Tiefer persönlicher Stil | Kopierschutz vs. Nähe zum Set, Tuning | v1.1 |
| Automatische Rollen-Erkennung | 3 | M | Spart Klicks bei vielen Dateien | Fehlerquote; manuelle Zuordnung beim Import genügt zunächst | v1.1 |
| Mehrere benannte Sets pro Stil | 3 | S | Verschiedene Projekte, verschiedene Färbung | – | v1.1 |
| Favoriten automatisch ins Set | 3 | S | Lernschleife ohne Aufwand | – | v1.1 |
| Regler „Persönlich“ | 3 | S | Dosierung | Siebter Intensitätsregler (W2) | v1.1 |
| Drum-Referenz per Clip/.mid (GM-Zuordnung, manuelle Liste) | 2 | M | Adressiert die häufigste Kritik: passt nicht zum Groove | Doppelter Swing (gelöst) | v1.0 |
| Drum-Capture live und „Lernen“ | 4 | M | Clip-Ziehen ist schneller | Live-Eingangskonflikt, Umweg über Voice | Gestrichen |
| Melodie „mit/gegen Hats“ | 3 | S | Rhythmische Verzahnung | – | v1.1 |
| Live-Capture für Vorlagen | 4 | S | Clip-Ziehen genügt | MIDI-Eingangs-Vorrang | Gestrichen |

## E. Plattform und Komfort

| Feature | Bew. | Aufw. | Nutzen / Begründung | Wechselwirkungen, Risiken | Empf. |
|---|---|---|---|---|---|
| Keychain für API-Keys | 1 | S | Sicherheit | – | v1.0 (mit Cloud-KI) |
| Token-/Kostenanzeige pro Anfrage und Sitzung | 3 | S | Kostenbewusstsein | `pricing.json` pflegen | v1.1 |
| Monatskosten | 4 | S | Anbieter zeigen das selbst | – | Gestrichen |
| Mehrsprachigkeit: Infrastruktur (Übersetzungstabelle) | 2 | S | Später nachrüsten ist teuer | – | v1.0 |
| Zweite Sprache (Englisch) | 3 | S | Für den Verkauf nötig, privat nicht | – | v1.1 |
| Notennamen-Konvention wählbar | 3 | S | Logic/Live nutzen C3 = 60 ohnehin gleich | – | v1.1 |
| Akzent-Schwelle für Import | 4 | S | Randfall | – | Backlog (intern fest) |
| Oktav-Offset pro Stimme | 2 | S | Synth-Patches sind unterschiedlich gestimmt | – | v1.0 |
| Formatwarnung AU/VST3 | 3 | S | Verhindert rätselhafte Fehler | – | v1.1 |
| Linux (Auslieferung) | 4 | M | Kein eigener Bedarf; Architektur vorbereitet, CI kompiliert ab v1.0 (D-81) | DAW-Tests | Backlog |
| CLAP | 4 | M | Bitwig/Reaper-Nutzer, Note-Effekte; nativ erst mit JUCE 9 geplant | Inoffizielle Erweiterung kann bei JUCE-Updates brechen | Backlog |
| AAX | 4 | L | Pro-Tools-Markt, für Techno kaum relevant | Avid-Programm, PACE-Signierung | Backlog |

---

## v1.2-Kandidaten

| Feature | Aufw. | Nutzen / Begründung | Wechselwirkungen, Risiken |
|---|---|---|---|
| **Modulation:** generierte CC-Spur pro Stimme (z. B. CC 74 Filter, Decay) | M | Im Techno entsteht viel Bewegung über Filter- und Hüllkurvenfahrten, oft mehr als über Notenvariation | Neue Ebene in der Ausgabestufe, CC-Zuordnung pro Synth, Export als CC-Daten in .mid (möglich) (D-69) |

## Gestrichen – mit Begründung

Diese Ideen werden nicht umgesetzt. Die Begründung steht hier, damit dieselbe Diskussion nicht später neu
beginnt. Eine Wiederaufnahme braucht eine neue Entscheidung mit neuem Argument.

| Feature | Begründung |
|---|---|
| Dichte-Korrektur | Doppelt zur Energie, macht die Regler unvorhersehbar (W2) |
| Kombinierter Export | Landet in Live und Logic als ein Clip mit gemischten Kanälen |
| Offline-Verfeinern per Schlüsselwort | Fehleranfälliger Parser in zwei Sprachen; Variationsknöpfe leisten dasselbe zuverlässiger |
| Struktur-Modus | Zweiter KI-Pfad mit eigenem Schema, der fast nie greift |
| MIDI-Vorlage als eigenes Konzept | In Referenzen und „Stimme importieren“ aufgegangen (W3) |
| Drum- und Vorlagen-Capture, „Lernen“ | Clip-Ziehen ist schneller; Capture erzeugt den Eingangskonflikt in Live (K-6) |
| Max-for-Live-Gerät / Mehrkanal-Variante | Spart kaum Spuren, setzt Live Suite voraus, Aufnahme enthält fremde Stimmen (K-25) |
| Monatskosten | Zeigen die Anbieter selbst an |
| Euklidische Rhythmen als Feature | Kein Mehrwert als Bedienelement; als interne Rhythmusquelle jederzeit ohne Entscheidung möglich |

## Backlog – später prüfen

Echter Nutzen, aber nicht für v1.x geplant. Aufnahme nur bei konkretem Bedarf aus der Praxis.

| Feature | Auslöser für eine Wiederaufnahme |
|---|---|
| Sub-Bass-Stimme (aus dem Bass abgeleitet) | Wunsch nach getrenntem Sub/Mid-Bass; Architektur vorhanden (D-52) |
| Linux-Plugin, LV2 | Kommerzieller Verkauf (Architektur vorbereitet) |
| CLAP, AAX | Kommerzieller Verkauf bzw. JUCE 9 mit nativer CLAP-Unterstützung |
| Plugin-Hosting (eine Spur pro Stimme in Live) | Spurzahl stört im Alltag |
| Auto-Evolve mit KI-Quelle | Algorithmische Evolve-Ebene hat sich bewährt |
| Patterns bis 64 Takte | Konkreter Bedarf trotz Slots und Automation |
| Gruppen A–D | Mehrere unabhängige MidiMaid-Setups in einem Projekt |
| Kick-Grundton mit Tonart-Vorschlag | Komfortwunsch |
| Polymeter „frei laufend“ | Polymeter mit Neustart (v1.1) reicht nicht |
| Kreativität/Energie als aktive Automation | Wunsch nach Controller-Steuerung; Parameter-ID ist reserviert |
| Eigene native Provider für OpenAI und Ollama | Nur falls die OpenAI-kompatible Schnittstelle in Phase 3 nicht reicht |

## Ungünstige Wechselwirkungen

**W1 – Stapel in der Ausgabestufe.** Wahrscheinlichkeit, Transposition, Groove, Slide, Kick-Freiraum,
Ratchets und Akzent greifen nacheinander auf dieselben Noten zu. Jede weitere Ebene vervielfacht die
Sonderfälle und Tests. Die feste Reihenfolge (SPEC §4.2a) entschärft das, beseitigt es aber nicht.
→ Ratchets und Wahrscheinlichkeit erst einbauen, wenn die übrigen Stufen stabil laufen.

**W2 – Sieben Intensitätsregler.** Energie, Kreativität, Chromatik, Persönlich, Stärke, Dichte-Korrektur und
Swing wirken teils auf dieselben Eigenschaften. Niemand kann vorhersagen, was ein Dreh bewirkt.
→ Basis-Ebene nur Energie und Kreativität; Dichte-Korrektur streichen; Stärke nur bei Variationen;
Chromatik und Persönlich in die Experten-Ebene.

**W3 – Vier Wege, die Generierung zu lenken.** MIDI-Vorlage, Referenzen, Stimme importieren und
Drum-Referenz überschneiden sich, dazu zwei Analysepfade.
→ Drei klare Begriffe: **Stimme importieren** (exakt übernehmen), **Referenzen** (Stil), **Drum-Referenz**
(Groove). Die MIDI-Vorlage geht in den Referenzen auf.

**W4 – Konkurrierende Wechsel am selben Taktpunkt.** Slot-Automation, neues Ergebnis (Generieren,
Variation, Verfeinern), Evolve-Stufe und Transposition können am selben Quantisierungspunkt eintreffen.
Die Spezifikation regelte nicht, wer gewinnt. → Jetzt geregelt (SPEC §6.8, D-73).

**W5 – Doppelte Variation pro Durchlauf.** Wahrscheinlichkeiten und Auto-Evolve erzeugen beide
Veränderung über die Zeit. Zusammen können sie ein Pattern unkenntlich machen.
→ Hinweis in der UI; bei aktivem Evolve wirken Wahrscheinlichkeiten nur auf Noten, die Evolve nicht
verändert hat.

**W6 – Manuelle Aktionen während Evolve.** Unklar war, ob Variation oder Verfeinern auf den Slot oder auf
die klingende Evolve-Stufe wirkt. → Jetzt geregelt (SPEC §6.8, D-73).

**W7 – Determinismus-Ballast.** Seed, Gewinner-Seed, Profil-Schnappschuss, Set-ID, Wahrscheinlichkeits-Hash
und Evolve-Ebene müssen alle zusammenpassen. Ein Fehler hier untergräbt das Vertrauen („gestern klang es
anders“). → Ein eigener Test: Pattern speichern, Sets und Favoriten ändern, Projekt neu laden, Ausgabe
bitgenau vergleichen.

**W8 – Lange Patterns.** 64 Takte belasten KI-Schema, Token-Budget, Verfeinern, Formplan und Speicher.
→ Auf 16 Takte begrenzen; längere Abläufe über Slots und Automation in der DAW.

---

## Zusammenfassung

| Bewertung | Anzahl | Charakter |
|---|---|---|
| 1 – Muss | 19 | Fundament, Qualität, Grund-Workflow |
| 2 – Sollte | 26 | macht das Produkt rund |
| 3 – Kann | 27 | Komfort, Live-Performance, Tiefe |
| 4 – Nice to have | 20 | davon 9 gestrichen, 11 im Backlog (eine bedingt durch Phase 3) |

**Entschieden:** v1.0 = alle 1er und 2er. Die 3er kommen in v1.1 in der Reihenfolge Live-Nutzen
(Evolve, Transponieren, Wahrscheinlichkeit, Sperren pro Dimension). Erster Kandidat für v1.2 ist die
Modulation. Die 4er sind aufgeteilt in Gestrichen und Backlog. Das senkt den Umfang von v1.0 deutlich, ohne das Alleinstellungsmerkmal anzutasten:
Techno-Spezialisierung, gekoppelte Stimmen, Qualität ohne KI-Abhängigkeit, persönlicher Stil.
