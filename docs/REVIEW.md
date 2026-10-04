# MidiMaid – Review der Spezifikation (04.10.2026)

Geprüft wurden SPEC, STYLES, ROADMAP, DECISIONS und CLAUDE.md, und zwar
1. auf innere Konsistenz und technische Korrektheit,
2. gegen Musiktheorie und Produktionspraxis im Techno,
3. gegen Reviews und Erfahrungsberichte zu vergleichbaren Produkten.

Übernommene Punkte (R-x) sind in die Dokumente eingearbeitet. Punkte, die früheren Entscheidungen
widersprechen oder den Umfang deutlich erweitern, stehen als offene Entscheidungen in `DECISIONS.md`.

---

## 1. Vergleichsprodukte und Kritik aus Reviews

| Produkt | Ansatz | Gelobt | Kritisiert / fehlt |
|---|---|---|---|
| Ableton Live 12 MIDI-Generatoren (Rhythm, Seed, Shape, Stacks, Euclidean) | regelbasiert, direkt im Clip | folgen der globalen Tonart/Skala von Live, schreiben direkt in den Clip, fixierte Noten bleiben bei neuer Generierung erhalten | kein Stilwissen, keine Kopplung von Bass und Melodie, Rhythm arbeitet nur mit einer Stimme |
| Mixed In Key Captain Plugins | Akkorde als Zentrum, Melodie/Bass folgen | Tonart wird zwischen Instanzen synchronisiert, Song-Abschnitte (Verse, Drop …) | generatorentypisch: Melodien klingen solo gut, passen aber rhythmisch nicht zum Groove des Tracks |
| Orb Producer Suite | KI-Module für Chords, Melody, Bass, Arp | Gegenmelodie-Optionen in früheren Versionen | Melodien basieren stark auf Akkordtönen und klingen fast wie Arpeggios; Allround-Ansatz liefert viele unbrauchbare Ergebnisse |
| Scaler 3 | Theorie-/Akkord-Werkzeug | automatische Stimmführung, Voice Grouping, weiche Akkordübergänge | interner Klang einfach (für MidiMaid irrelevant) |
| Audiomodern Riffer | Zufalls-Sequencer | Tonhöhe, Dauer und Velocity getrennt sperrbar, Infinity-Modus (neues Riff pro Durchlauf), Shuffle/Shift, MIDI-Eingang als Trigger, MIDI-Mapping | keine harmonische Kopplung mehrerer Stimmen |
| MIDI Agent | LLM-basiert (ChatGPT, Claude, Gemini, lokale Modelle) | iteratives Verfeinern per Prompt, Multi-Provider | Ergebnisse neigen zu generischen, „sicheren“ Mustern; Latenz durch Netz; keine Kenntnis des restlichen Tracks |
| InstaComposer | mehrere Spuren im Plugin, je ein MIDI-Kanal | eigener MIDI-Editor, Drag & Drop | – |

**Hinweis zur Quellenlage:** Die MIDI-Agent-Bewertung stammt von einer KI-gestützten Verzeichnisseite
und ist entsprechend vorsichtig zu gewichten. Ihre Kernkritik (generische LLM-Ausgaben) deckt sich aber
mit der allgemeinen Erfahrung mit Sprachmodellen und mit der Orb-Kritik.

### Abgeleitete Kernrisiken für MidiMaid
1. **Generische Ergebnisse** (LLM-Klischees, arpeggio-artige Melodien) → R-9, R-14
2. **Passt nicht zum Groove des Tracks** → R-3, R-4, R-15, offene Entscheidung O-16
3. **Starre Ergebnisse, nur „neu würfeln“** → R-11 (getrennt sperrbare Dimensionen, subtile Variationen)
4. **Reibung beim Übertragen in die DAW** → R-16
5. **Latenz der KI** → bereits gelöst (Hintergrund-Generierung, Puffer), ergänzt durch R-14

### Marktumfeld (für die spätere Kommerzialisierung)
Captain Plugins Epic 7 kostet 99 USD, Scaler 3 regulär 99 USD, Riffer 3 39 EUR, MIDI Agent 49 USD plus
optionalem Abo für verwaltete KI-Zugänge. Live 12 bringt kostenlose Generatoren mit. MidiMaid muss sich
deshalb klar über **Techno-Spezialisierung, gekoppelte Stimmen, Live-Einsatz mit Slots und Offline-Qualität**
abgrenzen (siehe SPEC §1.1).

---

## 2. Musiktheorie und Produktionspraxis

| # | Befund | Maßnahme |
|---|---|---|
| R-1 | Notennamen wie „E1–E3“ sind mehrdeutig: Ableton und Logic nennen MIDI 60 „C3“, die wissenschaftliche Notation „C4“. Eine Oktave Fehler im Bass ist fatal. | Alle Bereiche als **MIDI-Nummern**. Anzeige-Konvention in den Einstellungen wählbar (Standard C3 = 60). |
| R-2 | Die Tonart des Basses hängt in der Praxis an der Stimmung der Kick. Bass-Grundtöne im Sub-Bereich (ca. 41–62 Hz, E0–B0 in Ableton-Notation) sind typisch. | Optionaler **Kick-Grundton**. Tonart-Vorschlag passend dazu. Oktav-Offset pro Stimme, weil Synth-Patches unterschiedlich gestimmt sind. |
| R-3 | Rollende Basslines müssen der Kick ausweichen. Ausklingende Notenenden überlappen sonst trotzdem mit der Kick. | **Kick-Freiraum:** Bassnoten enden standardmäßig eine 1/32 vor dem nächsten Kick-Schritt. |
| R-4 | Bei Swing bleibt die Kick üblicherweise gerade. Ein stark geswingter Bass gegen eine gerade Kick erzeugt Flams. | Standard-Swing Bass 50–54 %, Melodie bis 58 %. Hinweis in der UI bei hohem Bass-Swing. |
| R-5 | Ein 303-Slide auf derselben Tonhöhe ist musikalisch ein Haltebogen. Die Regel „keine Überlappung gleicher Tonhöhe“ hätte ihn zerstört. | Slide auf gleiche Tonhöhe = Noten werden **zusammengeführt**. |
| R-6 | Viele 303-Emulationen lösen den Akzent über eine Velocity-Schwelle aus. Velocity ist im Techno außerdem oft Modulationsquelle (Filter, Hüllkurve). | Einstellbare **Akzent-Velocity** und Schwelle. Jeder Archetyp hat eine Velocity-Kontur. |
| R-7 | Bass und Melodie können auf betonten Zählzeiten unbeabsichtigt dissonieren (kleine Sekunde, Tritonus) oder im Register kollidieren. | **Intervall- und Registerregeln** auf betonten Schritten, abhängig vom Chromatik-Anteil. |
| R-8 | Stabs ohne Voicing-Regeln klingen sprunghaft. In Detroit- und Dub-Techno ist das parallele Verschieben einer Akkordform („Chord Memory“) stilprägend. Scaler wird gerade für weiche Stimmführung gelobt. | **Voicing-Regeln:** enge Lage, Register, Stimmführung. Optionaler Chord-Memory-Modus. |
| R-9 | Melodien, die nur aus Akkordtönen bestehen, klingen wie Arpeggios (Orb-Kritik). Techno lebt von einem wiedererkennbaren Motiv, das wiederholt wird, bevor es sich verändert. | **Motiv-zuerst-Generierung:** Motiv wiederholen, dann variieren. Durchgangstöne auf schwachen Schritten, Auftakte. |
| R-10 | Techno-Arrangements verändern sich in 8/16/32-Takt-Blöcken, oft mit einem „Turnaround“ im letzten Takt. | Phrasen-Rolle **Turnaround** (letzter Takt einer 8/16-Takt-Phrase). |
| R-11 | Hypnotischer Techno variiert oft subtil (Notenlänge, Akzent, Velocity) statt Noten zu tauschen. Riffer wird für getrennt sperrbare Dimensionen gelobt. | **Subtile Variationsoperatoren** und **Sperren pro Dimension** (Tonhöhe, Rhythmus, Velocity). |
| R-12 | Ratchets (schnelle Notenwiederholungen) sind in Hard Techno und Acid verbreitet. Euklidische Rhythmen sind Standard in generativen Werkzeugen (auch in Live 12). | **Ratchets** für Acid/Hard. **Euklid** als Rhythmusquelle für Polymeter und Sparse Hits. |
| R-13 | Moll-Pentatonik (hypnotische Motive) und Phrygisch-Dominant (orientalische Farbe im Melodic Techno) fehlten. | Skalen ergänzt, niedrig gewichtet. |
| R-14 | Gegen generische Ergebnisse helfen Stilvorgaben, Ausschlussregeln und eine Auswahl aus mehreren Kandidaten. | **Qualitätsbewertung:** Der Algorithmus erzeugt mehrere Kandidaten und wählt den besten. Wiederholungsschutz gegenüber dem Verlauf. Anti-Klischee-Regeln in den Prompts. |

## 3. Technik und Konsistenz

| # | Befund | Maßnahme |
|---|---|---|
| R-15 | Beim Slot-Wechsel war unklar, an welcher Position das neue Pattern einsetzt („PPQ modulo Länge“ springt mitten hinein). | Standard: **Neustart am Wechselzeitpunkt.** Alternativ „Legato“ (Position beibehalten) wie in Live. |
| R-16 | Live 12 schreibt eigene Generatoren direkt in Clips. Ein VST kann das nicht. | **Aufnahme-Workflow** dokumentiert (Plugin-Ausgabe auf einer gerouteten Spur als Clip aufnehmen) und in der UI erklärt. |
| R-17 | `std::uniform_int_distribution` & Co. liefern auf libc++, libstdc++ und MSVC unterschiedliche Ergebnisse, die Golden Files würden plattformübergreifend scheitern. `std::atomic<std::shared_ptr>` ist auf gängigen Plattformen nicht lock-free. | **Eigener PRNG und eigene Verteilungsfunktionen**, keine Fast-Math-Optimierung in `core`. Pattern-Übergabe über vorallozierte Zeiger statt `atomic<shared_ptr>`. |
| R-18 | Im Datenmodell fehlten Groove, Archetyp, Kick-Raster, Polymeter-Modus und Voicing pro Slot, obwohl Slots „vollständige Patterns“ sein sollen. | Datenmodell vervollständigt. |
| R-19 | Abschnittsnummer „6.4b“. | Nummerierung bereinigt. |
| R-20 | Ein VST/AU kann die globale Skala von Live 12 oder die Projekttonart von Logic nicht auslesen. | Hinweis in der UI. Die Tonart wird in der Hub-Gruppe geteilt. |
| R-21 | Abgrenzung zum Wettbewerb war nicht festgehalten. | SPEC §1.1 ergänzt. |

---

## 4. Vorschläge mit Entscheidungsbedarf (alle angenommen → D-47 bis D-50)

- **O-15 Verfeinern per Prompt:** Das bestehende Pattern per Text anpassen („weniger Noten“, „mehr
  Synkopen“). In Reviews wird das bei LLM-Werkzeugen als Stärke genannt. Es widerspricht D-34 (KI nur bei
  Generieren, Auto-Evolve und Vorlagen-Analyse).
- **O-16 Drum-MIDI als Rhythmus-Referenz:** Kick- und Hi-Hat-MIDI per Capture übernehmen, damit Bass und
  Melodie zum echten Groove des Tracks passen. Das adressiert die häufigste Kritik an MIDI-Generatoren,
  widerspricht aber D-28 (nur Kick-Raster in v1).
- **O-17 Live-Transponieren per MIDI-Eingang:** Eine Taste auf dem Keyboard transponiert das laufende
  Pattern (ähnlich wie Riffers MIDI-Input-Modus). Stark für Live-Sets, neues Feature.

**Entscheidung 04.10.2026:** Alle drei kommen in v1 (SPEC §3.14–3.17).

---

## 5. Quellen (Teil 1)
- Sound On Sound: Ableton Live 12 MIDI Generators (Oli Freke) – soundonsound.com
- Attack Magazine: Getting started with Ableton Live's generative MIDI tools – attackmagazine.com
- Attack Magazine: Warehouse Rolling Techno Bass; Beat Dissected (Rolling, Progressive, Deep Melodic Techno); Low End Theory: eight common bassline styles – attackmagazine.com
- Magnetic Magazine: Best MIDI plugins and generators (2024) – magneticmag.com
- KVR Audio Forum: Erfahrungen mit Orb Producer Suite – kvraudio.com
- MusicTech: Scaler 3 Review; W.A. Production InstaComposer Review – musictech.com
- Bedroom Producers Blog: Scaler 3 Review – bedroomproducersblog.com
- Sound On Sound: AudioModern Riffer Review (Robin Vincent) – soundonsound.com
- Gearnews: Riffer 3.0 – gearnews.com
- AI Indigo: MIDI Agent Review (KI-generierte Seite, vorsichtig gewichtet) – aiindigo.com
- Rekkerd.org: MIDI Agent v1.2.4; Captain Plugins Epic 7 – rekkerd.org
- Digital DJ Tips: Captain Plugins Review – digitaldjtips.com


---

# Teil 2 – Konsistenz- und Konfliktprüfung (04.10.2026, nach Hub & Voices)

Geprüft wurde der Gesamtstand aller Dokumente mit Fokus auf Funktionen, die sich gegenseitig stören.
Status: **behoben** = eingearbeitet, **Entscheidung** = offen in DECISIONS.md.

## A. Korrektur einer eigenen Fehlannahme
| # | Befund | Folge |
|---|---|---|
| K-1a | Die Annahme „Synth-Spuren in Live holen sich per MIDI From ihren Kanal“ war falsch. Laut mehreren Forenberichten mischt Live beim Routing von Spur zu Spur alle Kanäle; eine Kanalwahl gibt es nur bei Hardware-Eingängen. | Live-Standard ist jetzt eine Stimme pro MidiMaid-Spur (D-55). Mehrkanalig nur mit Kanalfilter. Phase 0 bestätigt das. **behoben** |

## B. Funktionskonflikte
| # | Konflikt | Auflösung | Status |
|---|---|---|---|
| K-1 | Auto-Evolve überschreibt sorgfältig kuratierte Slots | Nicht-destruktive Evolve-Ebene mit „Behalten“ (D-61) | behoben |
| K-2 | Transponieren per Tastatur und per Host-Parameter steuern denselben Wert; Rückschreiben aus dem Audio-Thread ist laut JUCE-Forum riskant und kollidiert mit gelesener Automation | Additiv, keine Rückschreibung (D-59) | behoben |
| K-3 | Kick-Freiraum kürzt Bassnoten vor der Kick, ein Slide verlangt aber Überlappung mit der Folgenote; Groove verschiebt Noten nachträglich in die Kick oder reißt Slide-Überlappungen auf | Feste Ausgabestufe (D-56), beim Bass schlägt Kick-Freiraum den Slide | behoben |
| K-4 | Skalen-Quantisierung bei 0 % Chromatik zerstört den V-Akkord einer harmonisch-moll-Progression (Leitton) und ♭VI-Akkorde bei Moll-Pentatonik | Akkord-Skalen-Prinzip (D-57); in der Musiktheorie üblich: natürlich Moll, über dem V-Akkord der erhöhte Leitton | behoben |
| K-5 | Wiederholungsschutz macht das Ergebnis vom Verlauf abhängig und verletzt „gleicher Seed → gleiches Ergebnis“ | Gewinner-Seed speichern (D-58) | behoben |
| K-6 | Eine Live-Spur hat nur eine MIDI-Quelle: Drum-Capture (MIDI From Drums) und Transponieren (Tastatur) gehen nicht auf derselben Spur | Capture in jeder Instanz der Gruppe, Übergabe als Daten an den Hub | behoben |
| K-7 | Wiederholungsschutz (> 85 % ähnlich = verworfen) würde subtile Variationen verwerfen, die per Definition sehr ähnlich sind | Schutz nur bei Generieren | behoben |
| K-8 | Hohe Kreativität erzeugt ungewöhnliche Kandidaten, die Qualitätsbewertung sortiert genau diese aus | Gewichtung abhängig von Kreativität | behoben |
| K-9 | Quantisierung „nächster Schlag“ + „Neustart“ verschiebt Schritt 0 von der Eins weg; Kick-Raster und Phrasen liegen falsch | Neustart nur an Taktgrenzen, sonst Legato | behoben |
| K-10 | Wechsel kurz vor der Taktgrenze erreichen Voices über den Message-Thread zu spät; Hub und Voice wechseln dann einen Takt versetzt | Geplante Wechsel mit PPQ-Zeitstempel und Mindestvorlauf | behoben |
| K-11 | „Ab der nächsten Note“ transponieren ist zwischen Instanzen nicht synchron möglich | In Gruppen frühestens „nächster Schlag“ | behoben |
| K-12 | Hub mit „alle Stimmen“ in Logic lässt den Synth der Hub-Spur Bass und Melodie gleichzeitig spielen | Ausgabemodus, Standard „eine Stimme“ | behoben |
| K-13 | Verfeinern + Sperren: Eine KI liefert eine neue Notenliste, gesperrte Noten lassen sich ohne Identität nicht zuordnen | Stabile Noten-IDs im Datenmodell und Schema | behoben |
| K-14 | Drum-Groove mit eigenem Swing + Swing-Regler = doppelter Swing | Regler deaktiviert bei Vorlagen mit Swing | behoben |
| K-15 | Akzent als Flag und Akzent als Velocity-Schwelle: zwei Wahrheiten | Flag ist maßgeblich, Schwelle nur für Import | behoben |
| K-16 | Energie und Dichte steuern beide die Dichte | Energie = Makro, Dichte nur als Korrektur | behoben |
| K-17 | Chord-Memory-Stabs erzeugen skalenfremde Töne, die Intervallregel verbietet sie auf betonten Schritten | Stab wird auf den nächsten Offbeat verschoben | behoben |
| K-18 | Verlauf und Undo-Stapel: unklar, was Cmd+Z nach „Generieren“ tut | Ergebnisse sind Verlaufseinträge und Undo-Schritte, Edits nur Undo-Schritte | behoben |
| K-19 | Voice mit eigener Halbton-Transposition bricht die Harmonie der Gruppe | Voices nur Oktav-Offset, Halbtöne nur gruppenweit | behoben |
| K-20 | Zwei Stimmen auf demselben Kanal können gleiche Tonhöhen überlappen (hängende Noten) | Stimmenübergreifende Prüfung, UI-Warnung | behoben |

## C. Unstimmigkeiten in den Dokumenten
| # | Befund | Status |
|---|---|---|
| K-21 | STYLES §1.2 sagte noch „kein Kick-MIDI-Import in v1“ (widerspricht D-48) | behoben |
| K-22 | Turnaround als Phrasen-Rolle, obwohl Phrasen mindestens 4 Takte haben; 16-Takt-Phrasen erwähnt, aber nicht erlaubt; `long_tied` beginnt natürlich auf der Eins, die Regel verbot das | behoben |
| K-23 | Rollenname „Standalone“ kollidiert mit dem Standalone-App-Format; Tastenkürzel 1/2/3 passen nicht zu bis zu 8 Stimmen; Einbuchstaben-Kürzel würden beim Tippen in Textfeldern auslösen | behoben |
| K-25 | Eine zusätzliche Max-for-Live-Variante mit mehrkanaliger Ausgabe spart in Live nur eine Spur pro Stimme, setzt Max for Live voraus und nimmt beim Aufnehmen auf der Synth-Spur beide Stimmen auf (Live zeichnet den Spur-Eingang vor den Geräten auf). Kosten: Tests, Setup-Hilfe, Kanalkonflikte | gestrichen (D-63) |
| K-24 | Antwort-Schema mit festen Schlüsseln `bass`/`melody` widerspricht der offenen Stimmenzahl; Formplan, Ratchets und IDs fehlten im Schema; Plugin-State und Verzeichnisstruktur veraltet; Phase 1 mit über 30 Aufgaben zu groß | behoben (Schema, State, CLAUDE.md, Phase 1a/1b) |

## D. Quellen (Teil 2)
- Ableton-Forum, Cycling '74-Forum, BrainModular-Forum: MIDI-Routing zwischen Spuren mischt Kanäle, Kanalwahl nur bei Hardware-Eingängen
- JUCE-Forum: Parameteränderungen aus dem Audio-Thread (`setValueNotifyingHost`), MIDI-Mapping vs. Automation
- For Dummies / Desi Serna: Harmonisch Moll und der V-Akkord
- Weiterhin gültig: JUCE-, Sound-Radix-, HISE- und Reddit-Beiträge zur Kommunikation zwischen Instanzen (Teil 1 der Hub-&-Voices-Recherche)


---

# Teil 3 – Produzenten-Sicht (04.10.2026)

Blick auf den Studioalltag in Live und Logic: Wo hakt es beim Arbeiten, unabhängig von der Technik?

## Stolperfallen im Workflow (eingearbeitet)
| # | Problem | Lösung |
|---|---|---|
| P-1 | Synth-Spuren in Live schweigen, wenn sie nicht scharf geschaltet sind; beim Aufnehmen hebt exklusives Scharfschalten den Keyboard-Eingang der Hub-Spur auf | Monitoring „In“ auf Hub- und Synth-Spuren, in Setup-Hilfe und Vorlage |
| P-2 | Freeze in Live nutzt die eigenen Clips einer Spur, nicht geroutetes MIDI: CPU-hungrige Synths ließen sich nicht einfrieren | „Erst aufnehmen, dann einfrieren“, Phase 0 bestätigt |
| P-3 | Start mitten im Arrangement: Der automatisierte Slot hätte erst nach der Quantisierung gewechselt, der erste Takt wäre falsch | Slot gilt beim Start und nach Sprüngen sofort (D-66) |
| P-4 | Cmd+Z, Cmd+D, Cmd+A belegt Live selbst; im Plugin-Fenster kommen sie womöglich nicht an | Schaltflächen für alle Kürzel-Aktionen (D-67) |
| P-5 | Vorlagen und Drums nur als .mid-Datei wäre umständlich; im Alltag zieht man Clips direkt aus der DAW | Clip/Region direkt ins Plugin ziehen als Standardweg, Phase 0 prüft |
| P-6 | Vier Spuren mit Routing einrichten schreckt beim ersten Mal ab | Fertige Vorlagen für Live und Logic (D-68) |
| P-7 | Häufigster Fall fehlte: eigene Bassline vorhanden, passende Melodie gesucht. Eine Vorlage erzeugt nur Ähnliches, übernimmt die Bassline aber nicht | „Eigene Stimme importieren“ (D-65) |
| P-8 | In der Session-Ansicht gab es keinen Weg, Slots mit Szenen zu verknüpfen | Clip-Hüllkurven für den Slot-Parameter, Beispiel in der Live-Vorlage |
| P-9 | Rund 20 Regler und Optionen überfordern im kreativen Moment | Basis- und Experten-Ebene mit stiltypischen Standards (D-67) |
| P-10 | Acid-Lines tauchen auch im Peak Time auf; Hard Techno läuft heute oft über 160 BPM | `acid_siren` mit Gewicht 1 im Peak-Time-Profil, Hard-Tempo bis 165 (Startwerte) |

## Musikalische Lücken (entschieden: O-19 nach v1 → D-69, O-20 → D-70, O-21 → D-71)
| # | Lücke | Warum relevant |
|---|---|---|
| O-19 | **Keine Modulation:** MidiMaid liefert nur Noten. Im Techno entsteht Entwicklung aber stark über Filter-, Resonanz- und Decay-Bewegungen. | Eine CC-Spur pro Stimme (z. B. CC 74 für den Filter) mit generierter Kontur würde Patterns lebendiger machen als jede Notenvariation |
| O-20 | **Keine Wahrscheinlichkeit pro Note:** Trig-Bedingungen wie bei Elektron oder die Note-Chance in Live 12 sorgen für Variation in jedem Durchlauf, ohne das Pattern zu ändern | Würde Evolve ergänzen; beim Export als .mid lässt sich Wahrscheinlichkeit nicht speichern |
| O-21 | **Kein persönlicher Stil:** Bewertete Favoriten aus der Bibliothek könnten als Beispiele in die KI-Prompts einfließen und die Gewichte des Algorithmus beeinflussen | So klingt MidiMaid mit der Zeit nach Klirrwerk statt nach Durchschnitt |

## Für nach v1 notiert
- Modulation: generierte CC-Spur pro Stimme, z. B. Filter-Kontur (D-69)
- Eigene Sub-Bass-Stimme, automatisch aus dem Bass abgeleitet (Grundtöne lang, eine Oktave tiefer); die
  Architektur für weitere Stimmen ist vorhanden (D-52)
- Plugin-Hosting, damit in Live eine Spur pro Stimme reicht
