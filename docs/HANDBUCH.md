# MidiMaid – Kurzhandbuch

> Für Anwender von MidiMaid (Klirrwerk). Texte und Knopfnamen entsprechen der deutschen Oberfläche (`de.json`). Wie du
> Hub und Stimmen in Live und Logic einrichtest, steht in `docs/SETUP-HILFE.md`. Was in **v1.1** kommt (Evolve,
> Live-Transponieren, Tastenkürzel, Wahrscheinlichkeiten …), gehört nicht in dieses Handbuch.

## 1. Was MidiMaid macht

MidiMaid erzeugt **Basslines und Melodien für Techno**, als MIDI, die dein Instrument spielt. Die Noten kommen aus einem
**eigenen Regelwerk** (Stil, Tonart, Skala, Kick-Raster, Akkordfolge). Optional fragst du zusätzlich eine **KI**. Alles, was
die KI liefert, läuft durch dieselben Regeln und dieselbe Qualitätswertung wie die eigenen Muster. Ohne KI und ohne
Internet funktioniert MidiMaid vollständig.

- **Stile:** Peak-Time, Melodic Techno, Hard Industrial.
- **Format:** VST3 und AU. In Logic gibt es zusätzlich die MIDI-FX-Variante „MidiMaid MIDI“.
- **Gleiches Ergebnis:** Dasselbe Pattern spielt nach dem Speichern und Neuladen des Projekts Note für Note gleich, auch
  wenn du danach deine Referenzen geändert hast.

## 2. Der erste Klang

1. Plugin auf eine Spur legen (Instrument-Variante, oder in Logic die MIDI-FX-Variante vor einen Synth).
2. **Erzeugen** drücken. Das Pattern spielt ab dem nächsten Takt, sobald der Transport läuft.
3. Mit **Energie** und **Kreativität** bestimmst du Dichte und Abenteuerlust. Mit **Experte** (Abschnitt 6) siehst du auch
   den **Seed**: derselbe Seed mit denselben Einstellungen gibt dasselbe Pattern, **Zufälliger Seed** würfelt neu.
4. Gefällt es nicht: noch einmal **Erzeugen**. Das alte Pattern bleibt über **◀** (Verlauf) erreichbar.

Eine neue Instanz ist **still**, bis du etwas erzeugt hast.

## 3. Die Oberfläche

| Bereich | Was du dort tust |
|---|---|
| **Rolle** | *Solo* (eine Instanz allein), *Hub* (steuert alle Stimmen), *Stimme* (spielt genau eine Stimme des Hubs) |
| **Stil, Tonart, Skala, Takte** | Tonart und Skala auf *auto* lassen oder festlegen; 1 bis 16 Takte |
| **Energie, Kreativität** | Dichte und Abwechslung (Seed und Zufälliger Seed im Experten-Modus) |
| **Slots 1–16** | 16 Pattern-Plätze je Instanz bzw. Hub; ein Klick wählt den Slot (Wechsel zum nächsten Takt); Rechtsklick: kopieren, einfügen, leeren, tauschen; ein Feld auf ein anderes ziehen tauscht die Slots, mit Alt kopiert es |
| **Piano-Roll** je Stimme | Noten ansehen und bearbeiten (Abschnitt 6) |
| **Größe** | Fenstergröße von 75 bis 200 %: Auswahl „Größe“ (Hub oben rechts, Stimme unten rechts) oder **stufenlos** am Griff in der unteren rechten Ecke ziehen; wird je Instanz im Projekt gespeichert |
| **Hilfe** | Öffnet die Anleitung für Hub und Voices, Schritt für Schritt, auf der Seite für deine DAW (Live, Logic oder andere Hosts) |
| **Statuszeile** | Was gerade passiert, auch Fehler und Hinweise |

Eine **Stimme** zeigt nur ihre eigene Spur: Stumm, Oktave, Griff zum Herausziehen der Stimme, Hilfe.
**Hub öffnen** holt das Fenster des Hubs nach vorn. Ist es geschlossen, kann das Plugin es nicht öffnen (das kann kein Plugin): Die Statuszeile sagt dann, dass du in der DAW das Gerät MidiMaid auf der Hub-Spur öffnen musst.

## 4. Erzeugen, Variation, Neu

- **Erzeugen** füllt den gewählten Slot neu. **Gesperrte** Stimmen bleiben, mit ihnen Tonart, Harmonie und Länge
  („Fertig: gesperrte Stimmen, Tonart und Länge bleiben“). Sind alle gesperrt, gibt es nichts zu erzeugen.
- **Variation** (je Stimme) ändert ein Pattern; wie stark, bestimmt der Regler **Stärke** (im Experten-Modus, Standard 30).
  **Variation (alle)** (Experte) wirkt auf alle Stimmen. Die Harmonie bleibt.
- **Chromatik** (Experte, neben Energie und Kreativität): Wie viele Töne die Skala verlassen dürfen. *Stil* nimmt den Wert des Stils (Melodic 0 %, Peak Time 10 %, Hard/Industrial 35 %), 0 % bleibt auf der Skala, 100 % setzt viele Durchgangs- und Wechseltöne. Das Pattern merkt sich den Wert; Variation und *Neu* bleiben dabei.
- **Groove** (je Stimme, oben rechts in der Roll): *Gerade* oder eine Vorlage (Swing 54, 58, 62 und 66 %, Shuffle, *Laid back* = alles etwas hinter dem Schlag, *Pushed* = davor, *Humanize* = kleine feste Abweichungen). Daneben der **Swing**-Regler (50–75 %): Bei einer Vorlage mit eigenem Swing ist er grau und zeigt deren Wert, sonst kommt er zur Vorlage dazu. Die Noten bleiben im Raster gespeichert, der Groove wirkt erst bei der Ausgabe; ein Zug am Regler ist ein *Rückgängig*-Schritt.
- **Neu** (je Stimme) erzeugt nur diese Stimme neu; Harmonie, Kick-Raster und die anderen Stimmen bleiben.
- **Duplizieren** (je Stimme) verdoppelt die **gewählten Noten** der Piano-Roll; **Rückgängig** und **Wiederholen** gelten für
  jede Änderung.
- **Verlauf** (◀ ▶, „Verlauf 2/5“): jeder Slot behält seine letzten Ergebnisse. Vorher gab es nichts, was du verlieren
  könntest.

**Evolve** (Hub, ab v1.1): Der Schalter *Evolve* in der Verlaufsleiste lässt das Pattern von selbst weiterwachsen, ohne den Slot
anzutasten. Läuft der Transport, entsteht alle N Durchläufe (Standard 4) eine neue **Stufe**, die ab dem nächsten Durchlauf
klingt; *Stufe n* zählt mit. Jede Stufe geht aus der vorigen hervor (Entwicklung); mit **Vom Original** variiert jede Stufe den
Slot selbst (ein fester Kern mit wechselnden Varianten). Gesperrte Stimmen bleiben. **Behalten** übernimmt die Stufe, die gerade
klingt, in den Slot (ein Verlaufseintrag, *Rückgängig* geht zurück), Evolve läuft von dort weiter. **◀** holt eine der letzten 8 Stufen
zurück. Schaltest du *Evolve* aus oder wechselst den Slot, spielt wieder der unveränderte Slot. Änderst du von Hand etwas (Variation, Neu, Noten
bearbeiten), wirkt es auf die Stufe, die du gerade hörst. Das Intervall, **Vom Original** und die **Evolve-Stärke** (Standard 20 %) stehen im
Experten-Modus. Die Ebene wird nicht im Projekt gespeichert, ein Bounce spielt den Slot. **Die Stimmen der Gruppe spielen die Stufe des Hubs mit**
und wechseln zugleich mit ihm (zum Beginn des nächsten Durchlaufs). Die Host-Parameter **Evolve** (Schalter) und **Evolve Keep**
(Trigger, springt nach dem Auslösen von selbst zurück) lassen sich automatisieren oder auf einen Controller legen.

**Slots kopieren, tauschen, leeren:** Ein Rechtsklick auf ein Feld der Slot-Leiste öffnet das Menü *Slot kopieren*, *Slot einfügen*, *Slot leeren* und *Tauschen mit …*. Du kannst auch ein Feld auf ein anderes **ziehen**: Das tauscht die beiden Slots, mit gehaltener **Alt-Taste** (Option) kopiert es; der Zeiger zeigt dann ein „+“. Einfügen ist ein Ergebnis wie *Erzeugen* (◀ und *Rückgängig* gehen zurück). **Leeren und Tauschen** lassen sich mit *Rückgängig* zurücknehmen (der ganze Slot kommt mit Verlauf, Name und Farbe wieder); das Durchblättern des Verlaufs (◀ ▶) beendet die Schritte des Slots.

**Turnaround:** Bei Patterns mit 8 oder 16 Takten kann der **letzte Takt einer Phrase** anders klingen (die letzten zwei Schläge schweigen, ein Lauf aus 16teln
über dem letzten Schlag oder ein Oktavsprung). Das wird beim Erzeugen gezogen, bei hoher Energie öfter als bei niedriger. Gesperrte Stimmen bleiben unverändert.

**Chord-Memory:** Bei Peak Time und Hard/Industrial spielen die Akkord-Stabs (*Stabs*, *Aggro-Stabs*) **eine Akkordform, die parallel mit den Akkorden wandert**, auch mit Tönen außerhalb der Skala (Detroit- und Dub-Farbe). Melodic Techno bleibt bei weicher Stimmführung. Kollidiert ein Stab auf der Eins mit dem Bass, rückt er als Ganzes um ein 16tel. Der Stil entscheidet; einen eigenen Schalter gibt es nicht.

**Polymeter-Sequenz:** Der Melodie-Archetyp *polymeter_seq* (bei Peak Time) spielt eine kurze Zelle von 3, 5, 6, 7, 9, 10 oder 14 Sechzehnteln, die sich über die Taktgrenzen hinweg wiederholt, mit 2 bis 5 Tönen. Sie beginnt mit jedem Durchlauf des Patterns wieder am Anfang, so dass der Loop stabil bleibt.

**Sperren:** Der Knopf *Sperren* an einer Stimme hält sie fest, bis du sie wieder freigibst. Erzeugen, Variation und
Verfeinern lassen sie in Ruhe. Ein **Rechtsklick auf *Sperren*** öffnet ein Menü, mit dem du nur eine **Dimension** sperrst: *Tonhöhe*, *Rhythmus* (Position und Länge) oder *Velocity* (mit Akzent). So behältst du die Tonfolge und würfelst einen neuen Rhythmus (oder umgekehrt). Dasselbe gilt für einzelne Noten: Noten wählen, Rechtsklick → *Sperren*; gesperrte Noten tragen ein kleines Quadrat. Sobald etwas gesperrt ist, bleiben beim Erzeugen auch Tonart, Skala, Akkordfolge und Länge. Die KI-Erzeugung beachtet nur ganz gesperrte Stimmen.

## 5. Mit der KI arbeiten (optional)

Unter **Einstellungen** wählst du einen Anbieter: *Anthropic (Claude)*, *OpenAI (ChatGPT)*, *OpenRouter*, *Ollama
(lokal)*, *LM Studio (lokal)* oder *Eigene Adresse (OpenAI-kompatibel)*. Ohne Anbieter („Aus (nur offline)“) erzeugt
MidiMaid ohne KI.

1. **API-Schlüssel** einfügen und *Speichern*. Er liegt im **Schlüsselbund** des Betriebssystems, nie im Projekt und nie
   in einer Datei. Lokale Anbieter brauchen keinen Schlüssel.
2. **Modelle laden**, ein Modell wählen, **Verbindung testen**.
3. Im Feld darüber schreibst du, was du hören willst („dunkler, treibender Bass mit Synkopen“), und drückst *Erzeugen*.

Danach prüft MidiMaid die Antwort: unbrauchbare Noten werden entfernt oder zurechtgerückt, und das Pattern bekommt eine
**Wertung**. Liegt sie unter dem Mindestwert des Stils, siehst du es („Fertig, aber unter dem Qualitätsmindestwert“).
Es wird **gezeigt, nicht abgelehnt**.

**Verfeinern:** Im zweiten Textfeld beschreibst du eine Änderung („weniger Noten“, „letzter Takt höher“), mit Bereich
*Ganzes Muster*, eine Stimme oder *Takte n–m*. Das geht **nur mit KI**.

**Wenn die KI nicht antwortet:** Die Statuszeile nennt die Ursache (Schlüssel abgelehnt, Modell nicht gefunden, zu viele
Anfragen, keine Verbindung, Zeitüberschreitung …). Du kannst *Erneut versuchen*, *Offline erzeugen* oder *Abbrechen*;
„Automatisch offline weiter“ nimmt dir die Frage für den Rest der Sitzung ab. Der Slot bleibt unverändert, solange
nichts Gutes zurückkommt.

**Zeitlimit:** Große lokale Modelle brauchen mehrere Minuten. Das *Zeitlimit (s)* in den Einstellungen gilt pro Anfrage;
bei einem 27B-Modell nimm mindestens 600.

**Tokens und Kosten:** Nach jeder KI-Anfrage zeigt die Statuszeile die verbrauchten Tokens und, bei Cloud-Anbietern, die *geschätzten* Kosten, dazu die Summe der Sitzung (bis zum Schließen des Hosts). Lokale Modelle zeigen nur Tokens. Die Preise stehen in `pricing.json`; legst du eine Datei gleichen Namens in den Datenordner (`Klirrwerk/MidiMaid`), ersetzt sie die mitgelieferten Preise je Modell.

## 6. Noten bearbeiten (Piano-Roll)

- **Klick** wählt eine Note, **Ziehen im Leeren** zieht einen Auswahlrahmen, **Doppelklick** setzt eine Note oder löscht
  die getroffene. Eine Note **ziehen** verschiebt die Auswahl, ihren **rechten Rand** ziehen ändert die Länge.
  **Cmd/Strg + Mausrad** oder **Pinch** (zwei Finger auseinanderziehen) zoomt, das Mausrad scrollt (Umschalt: seitwärts);
  auf dem Trackpad scrollt es in beide Richtungen stufenlos.
  Im Hub zeigen **alle Rolls denselben Zeitausschnitt**: Zoomen und seitwärts Scrollen gilt für alle Stimmen; die
  Tonhöhe scrollt je Roll für sich. Die Roll im Fenster einer **Stimme** zoomt und scrollt genauso.
- **Folgen** (Hub: rechts neben Rückgängig und Verlauf, Stimme: unten neben Hilfe) hält den **Playhead im Bild**: Ist der
  Schalter an, springt der Ausschnitt seitenweise weiter, sobald der Playhead ihn verlässt, und beim Loop-Sprung
  zurück an den Anfang. Zum freien Scrollen ausschalten. Der Zustand wird mit dem Projekt gespeichert.
- Die **Anschlagstärke** stellst du in der Leiste unter den Noten ein: Balken ziehen; mit Alt setzt ein Strich über
  leere Leiste alle Balken, die er kreuzt.
- **Raster**, **Triolen** und **Einrasten** (nur im Experten-Modus sichtbar): Das Raster bestimmt die Schrittweite,
  **Einrasten: Skala** hält dich in der Tonleiter, **Einrasten: Chromatisch** nicht.
- Hilfen für die Roll schaltest du in den Einstellungen unter *Piano-Roll* ein: **Nur Skalentöne (Fold)** zeigt nur die Töne der Skala als Zeilen (wie in Ableton), **Phrasen und Kick-Raster im Hintergrund** (Standard an) zeichnet die Phrasengrenzen und die Kick-Schritte hinter die Noten, **Zeile für Akzent und Slide** fügt unter den Noten eine Zeile hinzu, in der du Akzent (oben) und Slide (unten) einer Note anklickst.
- **Wahrscheinlichkeit und Bedingung:** Rechtsklick auf Noten → *Wahrscheinlichkeit* (z. B. 50 %: die Note klingt in etwa jedem zweiten Durchlauf) oder *Bedingung* (A:B, z. B. 2:4: nur im zweiten von vier Durchläufen). Welche Durchläufe klingen, steht fest: dieselbe Song-Position klingt immer gleich, auch beim Bounce und in allen Instanzen einer Gruppe. Die Spur unter den Noten zeigt mit einem Klick auf „Vel“ links daneben die Wahrscheinlichkeit („%“), Balken ziehst du wie bei der Velocity. Beim Herausziehen als MIDI enthält die Datei die aufgelösten Durchläufe hintereinander. Der Generator setzt nur sparsam etwas (Geisternoten bei Peak Time).
- **Akzent**, **Slide**, **Ratchet** und **Löschen** wirken auf die gewählten Noten. *Ratchet* (Untermenü, ×2 bis ×4) wiederholt die Note innerhalb ihrer Länge; die Roll zeigt es mit Teilstrichen. Ein Ratchet nimmt den Slide der Note weg und umgekehrt. Beim Erzeugen setzen nur die Archetypen *acid_siren* und *roll16_aggressive* (und manchmal ein Turnaround) Ratchets, höchstens einen pro Takt.
- **Fokus** (je Stimme) zeigt eine Stimme groß, die anderen als schmale Streifen.
- Die **weiße senkrechte Linie** ist der Playhead: Sie zeigt, wo im Pattern gerade gespielt wird (in der Roll des spielenden Slots, im Hub und im Stimmen-Fenster). Steht der Transport, ist sie weg.
- Jede Geste ist **ein** Rückgängig-Schritt und steht im **Verlauf**. Während der Wiedergabe kannst du bearbeiten; der
  Wechsel kommt zum nächsten Takt, ohne dass die Position springt.

**Experte** blendet Seed, Raster, Triolen, Einrasten, *Variation (alle)* und die Stärke ein. Die Wahl bleibt gespeichert.

## 7. Eigene Stimme, Drums und Referenzen

**Eigene Stimme importieren:** Ziehe eine `.mid`-Datei auf die **Zeile einer Stimme** (in einer Stimmen-Instanz auf das
Fenster); nur 4/4, die ersten 16 Takte zählen. Die Stimme landet im Slot, **gesperrt**. *Erzeugen* ergänzt dann die
anderen Stimmen darum.

**Drum-Referenz:** Ziehst du einen Drum-Clip (z. B. aus Live) auf dieselbe Stelle, erkennt MidiMaid ihn als Schlagzeug. Kick und Hi-Hat bestimmen dann Kick-Freiraum und
Groove („Drum-Referenz übernommen: n Kicks, m Hi-Hats“). **Drums lösen** entfernt sie. Weicht deine Belegung von General
MIDI ab, stellst du sie in den Einstellungen unter *Drum-Belegung* ein. Im Experten-Modus legt **Melodie zu Hats** fest, wie sich der Melodie-Rhythmus zu den Hi-Hats der Referenz verhält: *Frei* (Standard), *Mit Hats* (Noten rücken um ein 16tel auf Hat-Schritte) oder *Gegen Hats* (sie füllen die Lücken). Die Schläge bleiben, gesperrte Rhythmen und Slides auch; es wirkt beim Ablegen der Referenz auf ein Pattern und beim Erzeugen.

**Sprache:** Unter *Einstellungen → Sprache* wählst du *Automatisch* (die Systemsprache), *Deutsch* oder *English*. Die Wahl gilt für Fenster, die du danach öffnest; schließe das Plug-in-Fenster und öffne es neu.

**Notennamen:** Unter *Einstellungen → Notennamen* wählst du, ob die mittlere Taste (MIDI 60) C3 heißt (Live, Logic) oder C4. Das gilt für die Tastenbeschriftung der Roll und die Referenznote beim Transponieren.

**Notennamen:** Unter *Einstellungen → Notennamen* wählst du, ob die mittlere Taste (MIDI 60) C3 heißt (Live, Logic) oder C4. Das gilt für die Tastenbeschriftung der Roll und die Referenznote beim Transponieren.

**Referenzen (dein Stil):** Mit *Referenzen* öffnest du ein Fenster je Stil. Ziehe `.mid`-Dateien oder Ordner hinein: MidiMaid
liest Tonart, Skala, Rolle (Bass, Melodie, Akkorde, Schlagzeug) und Akkordfolge. Je Eintrag kannst du *an/aus* schalten,
das *Gewicht* ändern, *Tags* vergeben und *Entfernen*. Es wirkt in dieser Form:

- **Profil:** Ab **5 aktiven Einträgen** zeigt das Fenster das Profil deines Sets: häufigste Progression, Dichte von Bass und Melodie, bevorzugte Skala und die Zahl der typischen **Motiv-Zellen** (Rhythmus- und Intervallfolgen, die mindestens zwei Einträge haben). Es wird immer aus den aktiven Einträgen berechnet.
- **Persönlich (Experten-Ebene):** Der Regler *Persönlich* (Standard 50 %) bestimmt, wie stark dein aktives Set wirkt, sobald es mindestens fünf aktive Einträge hat: Er verschiebt die Wahl der Skala, der Bass- und Melodie-Typen und der Chromatik um höchstens die Hälfte in Richtung deines Sets und verstärkt oder streicht deine Akkordfolgen (0 % = das Set tut nichts). Jedes Pattern merkt sich das Profil, mit dem es entstand; eine Variation bleibt dabei, auch wenn du das Set später änderst.
- **Wirkung auf das Spiel:** Bei einem Set mit Profil kann die Melodie (Archetypen *hypnotic_motif*, *lead_phrase*, *call_response*) von einer **typischen Zelle** deines Sets ausgehen, die MidiMaid immer abwandelt; der Bass bevorzugt die Schritte, die deine Bässe oft benutzen; und unter den acht Kandidaten gewinnt eher einer, der deinem Set rhythmisch und in der Dichte ähnelt. Wie stark, bestimmt *Persönlich*.
- **Rolle:** MidiMaid schätzt die Rolle jeder Spur (Bass, Melodie, Akkorde, Schlagzeug). Ist es sich nicht sicher, steht in der Zeile orange „Rolle unsicher“ mit Prozent; ändere die Rolle dann in der Auswahl der Zeile, danach gilt sie als sicher.
- **Sets:** Je Stil kannst du mehrere benannte Sets führen (z. B. „Warehouse“, „Live-Set Berlin“). Oben im Fenster wählst du das Set,
  legst mit dem Namensfeld ein neues an, benennst um oder löschst es. Eine Datei liegt nur einmal in deiner Sammlung und kann in
  mehreren Sets stecken (*Aus anderem Set …*); *an/aus*, *Gewicht* und *Tags* gelten je Set. Das **aktive Set** wählst du auch im Hub
  neben dem Stil; es gilt für alle Instanzen und jedes Pattern merkt sich, mit welchem Set es entstand. Das Set **Favoriten**
  gibt es immer: Patterns, die du in der Bibliothek mit 4 Sternen oder mehr bewertest, landen dort (in den Einstellungen auch im
  aktiven Set).
- Ab **5 aktiven Einträgen** mischt MidiMaid deren Akkordfolgen in die des Stils (höchstens zur Hälfte).
- **Kopierschutz:** Ein Ergebnis, das einem aktiven Bass- oder Melodie-Eintrag zu ähnlich ist, wird gekennzeichnet
  und nach unten gewertet.
- An eine **Cloud-KI** gehen deine Beispiele nur mit deiner **Zustimmung für diesen Anbieter**
  (*Einstellungen → Datenschutz*, Standard: aus). Lokale Anbieter brauchen keine.

## 8. Bibliothek

*Bibliothek* öffnet die Liste aller gespeicherten Einträge. **Slot speichern** legt das Pattern des Slots ab, **Set
speichern** alle belegten Slots. **Laden** bringt es zurück, mit Name und Farbe; das alte Pattern bleibt über ◀
erreichbar. **Als .mid …** exportiert, **.mid importieren …** legt eine Datei als Eintrag ab. **Löschen** fragt einmal
nach. Zu jedem Eintrag setzt du **Sterne** (ein Klick), **Tags** (durch Komma getrennt) und einen **BPM-Bereich** und bestätigst mit *Übernehmen*. Oben im Fenster suchst du nach Name, Prompt oder Tag und filterst nach Sternen, Tag, Stil und Tempo. **Vorhören** spielt den gewählten Eintrag anstelle deines Slots (auch bei stehendem Transport) und gibt den Slot bei *Stopp*, beim Start der DAW oder beim Schließen des Fensters unverändert zurück. Die Bibliothek fasst 5000 Einträge und liegt als Dateien im Datenordner (Abschnitt 12).

## 9. MIDI herausziehen und aufnehmen

Jede Stimme hat einen **Griff**, den du in die DAW ziehst: Das ergibt eine `.mid`-Datei dieser Stimme im Tempo der DAW,
mit der Oktave der Instanz. Zum **Aufnehmen** auf die Synth-Spur siehe `docs/SETUP-HILFE.md` (Routing, Monitoring,
Aufnahme, Freeze, Arrangement).

**Einsatz und Vorhören:** Wechselt ein Slot beim Spielen, beginnt das neue Pattern beim nächsten Takt mit seinem ersten Takt (*Einsatz: Neustart*). Im Experten-Modus stellst du *Einsatz: Legato* ein: Dann läuft die Position weiter, wie in Lives Legato-Modus. Stell das bei Hub und Voices gleich ein. Mit **Vorhören** (rechts in der Leiste des Hubs) spielt der gewählte Slot auch bei stehendem Transport, im Tempo der DAW (sonst mit 125 BPM); die Voices spielen mit. Der Start des Transports beendet das Vorhören. Beim Herausziehen als MIDI kannst du in den Einstellungen *alle Noten* wählen (ohne die Auswertung von Wahrscheinlichkeit und Bedingungen).

**Tastenkürzel:** MidiMaid belegt von sich aus keine Taste, damit es nicht mit dem Host kollidiert. In den Einstellungen unter *Tastenkürzel* wählst du eine Aktion (Erzeugen, Rückgängig, Variation, Raster enger, Noten eine Oktave hoch, Spurfokus und weitere), drückst *Taste zuweisen* und dann die Taste, die du willst. *Entfernen* löscht die Belegung, *Standard wiederherstellen* alle. Die Leertaste bleibt immer bei der DAW. Ein Kürzel gilt nur, wenn das Plugin-Fenster den Fokus hat (ein Klick ins Fenster genügt); eine Taste ohne Umschalt-, Cmd/Strg- oder Alt-Taste wirkt nicht, solange du in ein Textfeld tippst. Manche Hosts verarbeiten einzelne Tasten selbst (Esc in Live, Cmd+Z in Logic).

## 10. Automation

Die Host-Parameter heißen **Slot** (1–16), **Mute 1 … Mute 8**, **Transpose**, **Evolve** und **Evolve Keep** (nur am Hub wirksam) sowie die Trigger **Generate**, **Variation 1 … 8** und **Variation All**: Die Flanke 0 → 1 löst aus (auf einen Controller-Knopf oder eine Automationsspur legbar), der Parameter springt danach von selbst zurück. *Variation* nimmt die Stärke aus den Einstellungen; eine Stimme reicht den Trigger an den Hub weiter; ein Bounce ignoriert die Trigger. Damit schaltest du Patterns und Stimmen im
Arrangement um. In einer Gruppe automatisierst du den **Slot im Hub**; eine Stimme kann auf *Slot: eigener* gestellt
werden, wenn sie ihren Slot selbst automatisieren soll (Bounce, Freeze). Die Wechsel greifen zum nächsten Takt.

**Live-Transponieren** (ab v1.1, Etappe 1): Ein Keyboard am MIDI-Eingang der MidiMaid-Spur transponiert das laufende Pattern. Die **Referenznote**
ist der Grundton der Tonart in den MIDI-Noten 36 bis 47 (bei A-Moll Note 45, das ist in Live und Logic A1): Spielst du sie, bleibt alles; einen Halbton darüber transponiert um +1, bis
höchstens ±12. Die Transposition wirkt ab der nächsten Note, bleibt bis zur nächsten Taste (*Halten*) und wirkt auf die Ausgabe, nicht auf
die gespeicherten Noten. Verlässt eine Stimme dadurch ihren Tonumfang, geht sie als Ganzes um eine Oktave, damit die Kontur bleibt. Der
Host-Parameter **Transpose** addiert sich zur Taste. Das Keyboard selbst klingt nicht mit; ein Bounce ignoriert die Tasten und spielt nur den Parameter.
Zeitpunkt, Modus (chromatisch oder diatonisch nach Skalenstufen), Momentan statt Halten, Ziel (Bass, Melodie, beide), MIDI-Kanal, Referenznote,
den Export mit Transposition und **MIDI Thru** (das eingehende MIDI geht zusätzlich an den Ausgang, sonst nicht) stellst du im Dialog
**Einstellungen** im Abschnitt *Transponieren* ein. Die Werte gelten für alle Instanzen und stehen in der Datei `settings.json`; eine Änderung wirkt
nach einer Viertelsekunde. **In einer Gruppe** nimmt der Hub das Keyboard
und plant die Änderung für alle: Hub und Stimmen transponieren zum selben Schlag (mit „nächste Note“ ist das der nächste Schlag, den
alle noch halten können; „nächster Takt“ gilt auch). Die Stimmen zeigen „Transponiert +n“ in der Statuszeile und folgen dem Hub; eine
Stimme auf *Slot: eigener* spielt stattdessen ihren eigenen Parameter **Transpose** (für den Bounce einer einzelnen Spur). Eine Notenzeile, die
du aus dem Fenster ziehst, enthält die Noten so, wie sie gerade klingen.

## 11. Hub und Stimmen

Ein **Hub** hält alle Patterns, jede **Stimme** spielt eine Stimme auf eigener Spur. So spielt der Bass auf einem Synth, die
Melodie auf einem anderen, und beide wechseln zusammen. Einrichten: `docs/SETUP-HILFE.md`. Mute wirkt doppelt: im Hub
(„Mute n“) und in der Stimme selbst.

## 12. Wo liegt was

| Was | Wo (macOS) |
|---|---|
| Einstellungen | `~/Library/Application Support/Klirrwerk/MidiMaid/settings.json` |
| Bibliothek | `…/Klirrwerk/MidiMaid/library/` |
| Referenzen | `…/Klirrwerk/MidiMaid/references/` |
| Protokoll | `…/Klirrwerk/MidiMaid/Logs/midimaid.log` |
| API-Schlüssel | Schlüsselbund (Dienst `com.klirrwerk.midimaid`), nie in einer Datei |

Das Protokoll enthält **keine** Prompts und Antworten, solange du *Prompts und Antworten protokollieren* nicht einschaltest
(nur für die Prompt-Entwicklung). MidiMaid sendet **keine Telemetrie**.

## 13. Wenn etwas nicht klappt

| Symptom | Ursache und Abhilfe |
|---|---|
| Nichts klingt | Transport läuft? Slot leer („Slot n: leer“)? *Erzeugen* drücken. Stimme *Stumm*? Instrument nach dem Plugin? |
| Es klingt erst nach einem Takt | Normal: Wechsel greifen zum nächsten Takt. |
| Synth-Spur schweigt in Live | Monitoring der Synth-Spur auf *In*, „MIDI From“ auf die MidiMaid-Spur (Setup-Hilfe). |
| „Hub nicht gefunden“ | Hub fehlt oder läuft in einem anderen Prozess/anderer Variante: Setup-Hilfe. |
| KI-Fehler | Statuszeile lesen; *Einstellungen → Verbindung testen*. |
| „Verfeinern braucht eine KI“ | Anbieter in den Einstellungen wählen. |
| Hängende Note | Soll nie vorkommen: bitte mit Host, Version und Schritten melden. |
