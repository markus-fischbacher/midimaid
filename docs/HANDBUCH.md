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
| **Größe** | Fenstergröße von 75 bis 200 % (Hub oben rechts, Stimme unten rechts); wird je Instanz im Projekt gespeichert |
| **Statuszeile** | Was gerade passiert, auch Fehler und Hinweise |

Eine **Stimme** zeigt nur ihre eigene Spur: Stumm, Oktave, Griff zum Herausziehen der Stimme, Hilfe zum Aufnehmen.

## 4. Erzeugen, Variation, Neu

- **Erzeugen** füllt den gewählten Slot neu. **Gesperrte** Stimmen bleiben, mit ihnen Tonart, Harmonie und Länge
  („Fertig: gesperrte Stimmen, Tonart und Länge bleiben“). Sind alle gesperrt, gibt es nichts zu erzeugen.
- **Variation** (je Stimme) ändert ein Pattern; wie stark, bestimmt der Regler **Stärke** (im Experten-Modus, Standard 30).
  **Variation (alle)** (Experte) wirkt auf alle Stimmen. Die Harmonie bleibt.
- **Neu** (je Stimme) erzeugt nur diese Stimme neu; Harmonie, Kick-Raster und die anderen Stimmen bleiben.
- **Duplizieren** (je Stimme) verdoppelt die **gewählten Noten** der Piano-Roll; **Rückgängig** und **Wiederholen** gelten für
  jede Änderung.
- **Verlauf** (◀ ▶, „Verlauf 2/5“): jeder Slot behält seine letzten Ergebnisse. Vorher gab es nichts, was du verlieren
  könntest.

**Slots kopieren, tauschen, leeren:** Ein Rechtsklick auf ein Feld der Slot-Leiste öffnet das Menü *Slot kopieren*, *Slot einfügen*, *Slot leeren* und *Tauschen mit …*. Du kannst auch ein Feld auf ein anderes **ziehen**: Das tauscht die beiden Slots, mit gehaltener **Alt-Taste** kopiert es. Einfügen ist ein Ergebnis wie *Erzeugen* (◀ und *Rückgängig* gehen zurück). **Leeren und Tauschen** lassen sich nicht rückgängig machen, weil sie auch Verlauf, Name und Farbe mitnehmen.

**Sperren:** Der Knopf *Sperren* an einer Stimme hält sie fest, bis du sie wieder freigibst. Erzeugen, Variation und
Verfeinern lassen sie in Ruhe.

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

## 6. Noten bearbeiten (Piano-Roll)

- **Klick** wählt eine Note, **Ziehen im Leeren** zieht einen Auswahlrahmen, **Doppelklick** setzt eine Note oder löscht
  die getroffene. Eine Note **ziehen** verschiebt die Auswahl, ihren **rechten Rand** ziehen ändert die Länge.
  **Cmd/Strg + Mausrad** zoomt, das Mausrad scrollt (Umschalt: seitwärts).
- Die **Anschlagstärke** stellst du in der Leiste unter den Noten ein: Balken ziehen; mit Alt setzt ein Strich über
  leere Leiste alle Balken, die er kreuzt.
- **Raster**, **Triolen** und **Einrasten** (nur im Experten-Modus sichtbar): Das Raster bestimmt die Schrittweite,
  **Einrasten: Skala** hält dich in der Tonleiter, **Einrasten: Chromatisch** nicht.
- **Akzent**, **Slide** und **Löschen** wirken auf die gewählten Noten.
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
MIDI ab, stellst du sie in den Einstellungen unter *Drum-Belegung* ein.

**Referenzen (dein Stil):** Mit *Referenzen* öffnest du ein Fenster je Stil. Ziehe `.mid`-Dateien oder Ordner hinein: MidiMaid
liest Tonart, Skala, Rolle (Bass, Melodie, Akkorde, Schlagzeug) und Akkordfolge. Je Eintrag kannst du *an/aus* schalten,
das *Gewicht* ändern und *Entfernen*. Es wirkt in dieser Form:

- Ab **5 aktiven Einträgen** mischt MidiMaid deren Akkordfolgen in die des Stils (höchstens zur Hälfte).
- **Kopierschutz:** Ein Ergebnis, das einem aktiven Bass- oder Melodie-Eintrag zu ähnlich ist, wird gekennzeichnet
  und nach unten gewertet.
- An eine **Cloud-KI** gehen deine Beispiele nur mit deiner **Zustimmung für diesen Anbieter**
  (*Einstellungen → Datenschutz*, Standard: aus). Lokale Anbieter brauchen keine.

## 8. Bibliothek

*Bibliothek* öffnet die Liste aller gespeicherten Einträge. **Slot speichern** legt das Pattern des Slots ab, **Set
speichern** alle belegten Slots. **Laden** bringt es zurück, mit Name und Farbe; das alte Pattern bleibt über ◀
erreichbar. **Als .mid …** exportiert, **.mid importieren …** legt eine Datei als Eintrag ab. **Löschen** fragt einmal
nach. Die Bibliothek fasst 5000 Einträge und liegt als Dateien im Datenordner (Abschnitt 12).

## 9. MIDI herausziehen und aufnehmen

Jede Stimme hat einen **Griff**, den du in die DAW ziehst: Das ergibt eine `.mid`-Datei dieser Stimme im Tempo der DAW,
mit der Oktave der Instanz. Zum **Aufnehmen** auf die Synth-Spur siehe `docs/SETUP-HILFE.md` (Routing, Monitoring,
Aufnahme, Freeze, Arrangement).

## 10. Automation

Die Host-Parameter heißen **Slot** (1–16) und **Mute 1 … Mute 8**. Damit schaltest du Patterns und Stimmen im
Arrangement um. In einer Gruppe automatisierst du den **Slot im Hub**; eine Stimme kann auf *Slot: eigener* gestellt
werden, wenn sie ihren Slot selbst automatisieren soll (Bounce, Freeze). Die Wechsel greifen zum nächsten Takt.

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
