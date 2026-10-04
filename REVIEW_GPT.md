# Konsolidierte Spezifikationsprüfung

Stand: 04.10.2026  
Grundlage: `docs/SPEC.md`, `docs/ROADMAP.md`, `docs/DECISIONS.md`,
`docs/STYLES.md`, `docs/REVIEW.md`, `docs/FEATURES.md`

Dieses Dokument ist die konsolidierte Arbeitsliste der bei der Prüfung gefundenen
Unklarheiten, Widersprüche und Abnahmerisiken. Es ersetzt keine Entscheidung in
`docs/DECISIONS.md`. Nach jeder weiteren Konsolidierungsrunde wird diese Datei
aktualisiert und veraltete Einzelbefunde werden entfernt.

## Gesamturteil

Die Dokumentation ist als Produktvision und Architekturentwurf weit fortgeschritten.
Sie ist jedoch noch nicht vollständig implementierungsreif. Vor allem die
Release-Abgrenzung, die Echtzeit-Semantik, das Datenmodell, die Host-Annahmen und
die Qualitätsmessung müssen verbindlich konsolidiert werden.

## Konsolidierungsentscheidungen

### C-1 Release-Abgrenzung

Die späteren Entscheidungen in `DECISIONS.md` haben Vorrang vor der bisherigen
Bewertung in `FEATURES.md`. Damit gehören Wahrscheinlichkeit pro Note,
Live-Transponieren, Evolve-Ebene, Chord Memory und mehrere Referenz-Sets bereits
zu v1.0. Die Release-Zuordnung in `SPEC.md`, `FEATURES.md`, `STYLES.md` und
`ROADMAP.md` muss entsprechend angeglichen werden.

### C-2 Umfang von v1.0

Der vollständige bisherige v1.0-Umfang bleibt erhalten. Die Roadmap wird
konsolidiert, ohne bestehende v1.0-Funktionen wie Referenzanalyse, KI-Provider,
Piano-Roll, Hub/Voices, beide Plugin-Varianten oder DAW-Vorlagen zurückzustellen.
Die zusätzlich in C-1 eingestuften Funktionen kommen obenauf.

### C-3 Drum-Capture und Offline-Verfeinern

Beide Funktionen bleiben gestrichen. v1.0 verwendet Drum-Clip/.mid-Import und
KI-Verfeinern, aber kein Offline-Verfeinern per Schlüsselwort und kein Capture
des eingehenden MIDI.

### C-4 Importierte Stimmen

Beim Import einer eigenen Stimme fragt MidiMaid nach, ob das Material
unverändert übernommen oder an Tonart, Register und Constraints angepasst
werden soll. Damit wird die bisherige harte Regel „immer unverändert“ durch
eine explizite Nutzerentscheidung ersetzt.

### C-5 Fehlgeschlagene Generierung

Wenn keiner der acht Kandidaten die Qualitätsmindestgrenze erreicht, bleibt das
bisherige Pattern aktiv. Die UI zeigt an, dass kein ausreichendes Ergebnis
gefunden wurde; ein unterwertiger Kandidat wird nicht automatisch übernommen.

### C-6 Kopierschutzmetrik

Die 85-%-Ähnlichkeit wird über normalisierte Intervall- und Rhythmusfolgen
berechnet und ist transpositionsunabhängig. Absolute Tonlage und Velocity
werden nicht berücksichtigt.

### C-7 Pattern-Wechsel und Slides

Beim quantisierten Pattern- oder Slot-Wechsel werden alle alten Noten sicher
beendet. Slides werden nicht über die Pattern-Grenze fortgesetzt.

### C-8 Überlastung der Pattern-Übergabe

Wenn mehrere Generierungen schneller eintreffen als der Audio-Thread sie
übernehmen kann, bleibt nur das zuletzt angeforderte gültige Pattern ausstehend.
Ältere ausstehende Ergebnisse werden verworfen; das aktuelle Pattern spielt
unterbrechungsfrei weiter.

### C-9 Cloud-Datenschutz

Referenzmaterial und importierte Stimmen werden standardmäßig nicht an
Cloud-KI-Provider gesendet. Eine Übertragung darf nur nach ausdrücklicher
Freigabe des Nutzers pro Anfrage erfolgen.

### C-10 KI-Verfeinerung und aktuelles Pattern

Beim KI-Verfeinern gilt die Betätigung von „Verfeinern“ als Zustimmung für die
Übertragung des aktuellen Patterns. Referenzmaterial und importierte Stimmen
bleiben zusätzlich geschützt und werden nur nach einer separaten Freigabe
übertragen.

### C-11 Mindestvorlauf geplanter Wechsel

Der Mindestvorlauf für geplante Hub-/Voice-Wechsel wird als
Experteneinstellung umgesetzt. Der Standardwert beträgt 150 ms.

### C-12 Formatunabhängiger State

Ein identisches, formatunabhängiges State-Format ist v1.0-Ziel. Ein direkter
VST3↔AU-Formatwechsel innerhalb einer DAW wird nicht versprochen; dafür ist kein
zusätzlicher Export-/Import-Workflow erforderlich.

## Priorität A – vor Implementierungsbeginn klären

### A1. Verbindliche Release-Matrix fehlt

Mehrere Funktionen sind verschiedenen Releases zugeordnet:

- Referenz-Sets: `SPEC.md` §3.20 beschreibt v1.0 mit einem Set, `D-72` nennt
  mehrere benannte Sets und Favoriten, `FEATURES.md` ordnet diese v1.1 zu.
- Drum-Capture: `D-48` nennt Capture in v1, `SPEC.md` und `FEATURES.md` führen
  Capture als gestrichen.
- Offline-Verfeinern: `D-47` erwähnt einen eingeschränkten Offline-Modus,
  `D-74/D-75` und `SPEC.md` führen ihn als gestrichen.
- Wahrscheinlichkeit pro Note: `D-70` formuliert sie als v1, `D-74` und
  `FEATURES.md` ordnen sie v1.1 zu.

**Erforderliche Konsolidierung:** Eine einzige Release-Tabelle mit genau einem
Status pro Funktion. Danach müssen `SPEC.md`, `DECISIONS.md`, `FEATURES.md`,
`STYLES.md` und `ROADMAP.md` daraus abgeleitet werden.

### A2. Echtzeitvertrag ist nicht vollständig spezifiziert

§6.3 fordert einen vorallokierten Pattern-Pool und lock-freie Übergabe, definiert
aber nicht:

- Poolgröße und maximale Zahl ausstehender Patterns,
- Verhalten bei vollem Pool oder voller Rückgabe-Queue,
- Generation-Counter/ABA-Schutz,
- maximale Zahl von MIDI-Events pro Audioblock,
- Verhalten bei Überlastung,
- Speicher- und Lebenszyklusregeln beim Plugin-Abbau.

Zusätzlich widersprechen sich Pattern-Wechsel und Slides über Pattern-Grenzen:
§6.2 fordert Note-Offs bei Pattern-Wechsel, §4.2 erlaubt einen Slide in den
nächsten Pattern-Durchlauf.

**Erforderliche Konsolidierung:** Ein verbindlicher Engine-Vertrag mit
Kapazitätsgrenzen, Überlaufverhalten, Note-Off-Reihenfolge und Lebenszyklus.

### A3. Transport- und Wechselsemantik ist unvollständig

Die Regel „wenn der nächste Quantisierungspunkt weniger als 150 ms entfernt
ist, wird der übernächste gewählt“ lässt offen, worauf sich die 150 ms beziehen
und wie sich die Regel bei Tempoänderungen, großen Audioblöcken und
Offline-Rendering verhält.

Nicht eindeutig definiert sind außerdem:

- Loop-Rücksprung gegenüber Arrangement-Sprung,
- Start/Stop ohne PPQ-Änderung,
- Sprung nach vorne oder hinten,
- fehlende oder ungültige Host-Playhead-Daten,
- Taktart- und Tempoänderung innerhalb eines Blocks,
- Pattern-Position beim Start mitten im Arrangement.

**Erforderliche Konsolidierung:** Zustandsautomat für Transport, Sprünge,
quantisierte Wechsel und Pattern-Position mit testbaren Beispielen.

### A4. Qualitätsversprechen sind nicht abnehmbar

Die Spezifikation verspricht „sofort musikalisch nutzbares“ MIDI und eine
Mindestbewertung, definiert aber keine Score-Skala, keinen Mindestwert und kein
Verhalten, wenn alle acht Kandidaten scheitern.

Auch der Kopierschutz „mehr als 85 % ähnlich“ ist nicht reproduzierbar definiert:
Unklar sind Vergleichsalgorithmus, Normalisierung, Transposition, Notenlängen,
Velocity, Akkorde und unterschiedliche Pattern-Längen.

Importierte Stimmen dürfen laut §3.18 unverändert bleiben, obwohl das globale
Qualitätsversprechen für jedes Ergebnis gilt.

**Erforderliche Konsolidierung:** Score-Modell, harte und weiche Kriterien,
Mindestschwellen, Fallback bei Kandidatenfehlern und eine präzise
Ähnlichkeitsmetrik. Importiertes Material muss als explizite Ausnahme behandelt
werden.

### A5. Datenmodell und JSON-Schema sind nicht vollständig verbindlich

Das C++-Modell verwendet nicht definierte oder nur angedeutete Typen wie
`GenerationInfo`, `PitchClass`, `ScaleType`, `Chord` und `PhraseRole`.
Außerdem bestehen folgende Lücken:

- Kick-Raster ist in `Pattern`, aber nicht pro Phrase speicherbar, obwohl das
  in `STYLES.md` erlaubt wird.
- Phrase-Rollen im Modell (`Main`, `Variation`, `Build`, `Breakdown`, `Answer`)
  stimmen nicht eindeutig mit den deutschen Beschreibungen überein.
- Phrasenlängen und Patternlängen sind für 1-, 2- und 4-Takt-Patterns nicht
  vollständig geregelt.
- Triolen sind im UI vorgesehen, aber die Tick-/Rastersemantik ist nicht
  beschrieben.
- Noten-ID-Vergabe, Identität nach Edit/Kopie/Variation und Kollisionsschutz
  fehlen.
- `GenerationInfo` und der Referenzprofil-Snapshot haben keine verbindliche
  Serialisierungsstruktur.

Das KI-Schema enthält v1.1-Felder, die in v1.0 ignoriert werden. Es ist nicht
definiert, ob solche Felder akzeptiert, entfernt oder als Schemafehler behandelt
werden.

**Erforderliche Konsolidierung:** Versioniertes JSON-Schema inklusive Enums,
Wertebereichen, ID-Regeln, Migration und Release-spezifischen Feldern.

## Priorität B – vor der jeweiligen Umsetzung klären

### B1. Stilprofil enthält einen Release-Widerspruch

Das JSON-Beispiel in `STYLES.md` setzt `chord_memory` auf `true`, obwohl
Chord Memory als v1.1 markiert ist und in v1.0 überall die normale Stimmführung
gelten soll.

**Erforderliche Änderung:** Beispiel auf den v1.0-Stand bringen oder ausdrücklich
als v1.1-Beispiel markieren.

### B2. Host-Annahmen sind noch keine belastbaren Entscheidungen

Live-Routing, AU-MIDI-FX-Verhalten, Drag & Drop von Clips/Regionen,
Sandboxing/Out-of-Process-Hosting, „Record MIDI to Track Here“ und Freeze
werden erst im Phase-0-Spike geprüft. Trotzdem hängen Architektur, Vorlagen und
Abnahmekriterien bereits von diesen Annahmen ab.

**Erforderliche Konsolidierung:** Für jede Host-Annahme muss ein positiver und
negativer Pfad dokumentiert werden. Ein negatives Spike-Ergebnis muss eine
konkrete Ersatzlösung oder eine Scope-Änderung auslösen.

### B3. Format- und Parameterkompatibilität ist zu abstrakt

„Formatunabhängiger State“ und „feste String-IDs“ garantieren noch keinen
Austausch zwischen VST3 und AU in einer DAW. Es fehlt außerdem eine verbindliche
Parameter-Tabelle mit ID, Typ, Bereich, Default, Automationsart, Version und
Sichtbarkeit für alle acht reservierten Stimmen.

**Erforderliche Konsolidierung:** Parameter- und State-Register als normativer
Bestandteil der Spezifikation. Austauschbarkeit muss als Serialisierbarkeit oder
als tatsächlicher DAW-Workflow präzisiert werden.

### B4. MIDI- und Musiksemantik enthält offene Details

Zu klären sind:

- Bedeutung von `degree` bei verschiedenen Modi und erhöhtem Leitton,
- genaue Interpretation von Progressionen wie `bVI`, `VII`, `sus`,
- Zeitpunkt von Akkordwechseln bei Takt/Halbtakt,
- Umgang mit mehreren gleichzeitigen Akkorden,
- Verhalten von Transposition außerhalb des Registers,
- Interaktion von Swing, Triolen und 16tel-Raster,
- Regel für importiertes Material in fremder Tonart oder Taktart.

### B5. KI-Provider und Abbruchverhalten sind nicht ausreichend definiert

Die Provider unterscheiden sich bei JSON-Schema, Authentifizierung, Modelllisten,
Streaming, Fehlerformaten und Limits. Die Fähigkeitsstufe aus §7.2 ist daher
nicht allein durch eine Testanfrage verlässlich bestimmbar.

Für `std::future` und `CancellationToken` fehlen konkrete Regeln zum tatsächlichen
Netzwerkabbruch, zur Thread-Beendigung und zum Warten beim Plugin-Schließen.

### B6. Datenschutz und Logging benötigen eine klare Regel

Referenz-MIDI, importierte Stimmen, Prompts und Pattern können an Cloud-Provider
gesendet werden. Es fehlt eine explizite Regel für Einwilligung, Standardverhalten,
Redaktion und Löschung.

Die Logging-Regel „vollständige Prompts auf Debug-Level“ ist für persönliche
Referenzen und Nutzerinhalte problematisch. Debug-Logging sollte standardmäßig
redigierte Metadaten statt vollständiger Prompts speichern.

### B7. Offline-Bounce ist nur teilweise definiert

Unklar bleiben laufende KI-Anfragen, nicht gespeicherte UI-Änderungen, ein Bounce
direkt nach Slot-Wechsel, fehlende Referenz-Sets und ein fehlender Hub. Die
Dokumentation muss definieren, welche gespeicherte Version beim Bounce immer
maßgeblich ist.

## Priorität C – Qualitäts- und Planungsrisiken

### C1. Umfang von v1.0 ist sehr groß

v1.0 umfasst gleichzeitig Generator, Musiktheorie, Constraints, zwei
Plugin-Varianten, Hub/Voices, große UI, Piano-Roll, Bibliothek, Referenzanalyse,
KI-Provider, Drag & Drop, DAW-Vorlagen und mehrere Host-Abnahmen. Die Roadmap
enthält dafür keine belastbaren Zwischenabnahmen oder interne Priorisierung.

### C2. Musikalische Qualität ist nur teilweise testbar

Golden Files prüfen Determinismus, aber nicht „stiltypisch“ oder „musikalisch
nutzbar“. Für Stilprofile, Referenzanalyse und Qualitätsbewertung fehlen
reproduzierbare Property-Tests, Score-Grenzen und ein standardisiertes
Hörtestprotokoll.

### C3. Referenzanalyse braucht ein präzises Testkorpus

Ziele wie „Tonart mindestens 90 %“ und „Rolle mindestens 95 %“ benötigen
Korpusgröße, Konfidenzintervall, Transpositionen, Polyphonie, Swing, schlechte
Quantisierung, Mehrspurmaterial und Regeln für Fehlklassifikationen.

### C4. CI-Abnahme ist nicht vollständig reproduzierbar

„Determinismus mit einer dritten Standardbibliothek“ ist ohne festgelegte
Compiler-, Standardbibliotheks- und Architekturversionen nicht eindeutig.
Ebenso fehlen verbindliche Versionen für pluginval, Xcode/auval und die
Windows-Toolchain.

## Bereits konsolidierte Punkte

Die folgenden früheren Konflikte sind in den Dokumenten grundsätzlich
aufgelöst und sollten nicht erneut als offene Punkte geführt werden:

- eine Stimme pro MidiMaid-Instanz in v1,
- Hub/Voice statt Echtzeit-MIDI-Kommunikation zwischen Instanzen,
- feste Ausgabereihenfolge,
- deterministischer eigener PRNG,
- Note-Off-Verfolgung über die tatsächlich gesendete Tonhöhe,
- Offline-Algorithmus als Fallback nur nach Nutzerentscheidung,
- keine API-Keys im Plugin-State,
- v1.0 mit maximal 16 Takten,
- v1.0 ohne Chord Memory, Ratchets, Wahrscheinlichkeit und Live-Transposition.

## Empfohlene Reihenfolge der nächsten Konsolidierung

1. Release-Matrix und widersprüchliche Entscheidungen bereinigen.
2. Normatives Datenmodell und KI-JSON-Schema festlegen.
3. Echtzeit-/Transportvertrag mit Zustandsdiagramm und Kapazitätsgrenzen festlegen.
4. Qualitäts- und Kopierschutzmetriken messbar machen.
5. Phase-0-Hostannahmen mit Negativpfaden definieren.
6. Datenschutz, Provider-Abbruch und Offline-Bounce verbindlich beschreiben.
