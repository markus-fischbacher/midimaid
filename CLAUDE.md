# CLAUDE.md – Arbeitsanweisungen für Claude Code

> Projekt: **MidiMaid** von Klirrwerk – KI-gestützter MIDI-Generator für Techno-Melodien und Basslines.
> Formate v1.0: VST3 und AU (MIDI-FX-Variante als AU für Logic), Standalone für Entwicklung; Architektur für
> CLAP, LV2, AAX und Linux vorbereitet (SPEC §2.4).

## Pflichtlektüre vor jeder Aufgabe

1. `docs/SPEC.md` – vollständige Spezifikation (die verbindliche Quelle)
2. `docs/ROADMAP.md` – Phasen, Aufgaben, Abnahmekriterien
3. `docs/DECISIONS.md` – getroffene und offene Entscheidungen
4. `docs/STYLES.md` – Stilprofile, Archetypen, Kick-Raster (für alles in `core/` und den Prompts)
5. `docs/REVIEW.md` – Begründung vieler Regeln (Kritik aus Reviews, Musiktheorie); bei Fragen zum „Warum“
6. `docs/FEATURES.md` – Bewertung und Priorität jeder Funktion; bei Zielkonflikten gewinnt die höhere Priorität

**Release-Umfang:** Umgesetzt wird **nur v1.0**. Abschnitte mit [v1.1], [Backlog] oder [Gestrichen] nicht
implementieren; [Gestrichen] auch nicht vorbereiten. Für v1.1
die vorgesehenen Datenfelder und reservierten Parameter-IDs anlegen (SPEC §3.13, §5) und keine
Architekturentscheidung treffen, die v1.1 oder die Modulation (v1.2-Kandidat) verbaut.

Wenn die Spezifikation unklar, widersprüchlich oder lückenhaft ist: **nicht raten, sondern nachfragen.**
Offene Punkte in `docs/DECISIONS.md` unter „Offen“ eintragen.

## Sprachen
- **Code, Bezeichner, Kommentare, Commit-Nachrichten, PR-Texte: Englisch**
- **Dokumentation (`docs/`, `CLAUDE.md`): Deutsch**; Änderungen an der Doku ebenfalls auf Deutsch
- **UI-Texte:** nur über die Übersetzungstabelle (v1.0: `de.json`)
- **Kommunikation mit dem Entwickler** (Pläne, Rückfragen, Zusammenfassungen): Deutsch

## Arbeitsweise

- Immer genau **eine** Roadmap-Aufgabe bearbeiten. Vorher kurz den Plan nennen, dann umsetzen.
- Kleine, überprüfbare Schritte. Nach jedem Schritt muss das Projekt bauen.
- Eine Aufgabe ist erst erledigt, wenn alle Punkte erfüllt sind:
  - Build läuft ohne Warnungen (Warnungen gelten als Fehler).
  - Alle Unit-Tests sind grün (`ctest`).
  - Bei Änderungen am Plugin: pluginval (Strictness 10) besteht, auf macOS zusätzlich `auval`.
  - Die Checkbox in `docs/ROADMAP.md` ist abgehakt.
  - Neue Entscheidungen stehen in `docs/DECISIONS.md`.
- Keine neuen Abhängigkeiten ohne Rückfrage.
- `git push` nur nach Rückfrage (siehe `.claude/settings.json`); nie auf `main` direkt, immer per Feature-Branch und PR.
- Bestehende Tests nie abschwächen oder löschen, damit etwas grün wird.
- Commits: klein und thematisch, Format `bereich: kurze beschreibung` (z. B. `core: add scale quantizer`).

## Harte Regeln (nicht verhandelbar)

### Echtzeit / Audio-Thread (`processBlock`)
- Keine Speicherallokation (`new`, `std::vector::push_back` mit Wachstum, `std::string`-Bau usw.).
- Keine Locks (`std::mutex`, `CriticalSection`), kein I/O, kein Logging, keine Netzwerkzugriffe.
- Pattern-Übergabe nur lock-free (siehe SPEC §6.3). Kein `std::atomic<std::shared_ptr>`.
- Jeder Note-On braucht ein garantiertes Note-Off: bei Stop, Loop-Sprung, Pattern-Wechsel und Bypass.

### Formate und Plattformen
- Kein formatspezifischer Code außerhalb von `HostCapabilities` (SPEC §2.4); keine Abfragen von
  `wrapperType` verstreut im Code.
- Formate nur über `MIDIMAID_FORMATS` in CMake aktivieren; State und Parameter-IDs formatunabhängig.
- Kein Plattformcode außerhalb von `platform/`; das VST3-Plugin muss in der CI auch unter Linux kompilieren.

### Hub & Voices
- Zwischen Instanzen werden nur unveränderliche Patterns über die Registry auf dem Message-Thread
  ausgetauscht, nie MIDI-Events und nie aus dem Audio-Thread (SPEC §6.5).
- Stimmenzahl nie fest verdrahten: immer über `Pattern::voices` iterieren (D-52).
- Ausnahme vom Registry-Grundsatz: geplante Wechsel (PPQ-Zeitstempel) über einen lock-freien,
  prozessweiten Kanal, den Audio-Threads lesen dürfen (SPEC §6.5).

### Ausgabestufe
- Groove, Transposition, Slide-Überlappung und Kick-Freiraum strikt in der Reihenfolge aus SPEC §4.2a.
- Host-Parameter nie aus dem Audio-Thread setzen (`setValueNotifyingHost` nur auf dem Message-Thread).

### Threading
- KI-Anfragen, Dateizugriffe und MIDI-Analyse laufen ausschließlich in Hintergrund-Threads.
- UI-Updates nur auf dem Message-Thread (`juce::MessageManager::callAsync`).
- Jeder Hintergrundjob muss abbrechbar sein (Plugin-Destruktor wartet sauber).

### Sicherheit & Datenschutz
- API-Keys **nur** im Schlüsselbund des Betriebssystems (SPEC §8.5).
- Keys nie im Plugin-State, in Logs, Fehlermeldungen oder Testdaten.
- Keine Telemetrie.

### Code
- C++20, `clang-format` (Konfig im Repo), Namespace `mm::` mit Unterbereichen (`mm::core`, `mm::ai` …).
- Kein rohes `new`/`delete`; RAII, `std::unique_ptr`.
- `source/core/` ist **frei von JUCE-GUI und -Audio** (nur Standardbibliothek). So bleibt es testbar.
- Zufall in `core` nur über den eigenen PRNG; keine `std::*_distribution`, kein `-ffast-math` (SPEC §4.3).
- Wahrscheinlichkeiten bei der Wiedergabe nur per Hash aus Seed, Noten-ID und Durchlauf, nie mit
  laufendem Zufallszustand im Audio-Thread (SPEC §3.19).
- Tonhöhen im Code immer als MIDI-Nummern, Notennamen nur in der UI.
- Öffentliche Funktionen in `core` und `ai` bekommen Unit-Tests.
- Keine festen UI-Texte im Code: alles über die Übersetzungstabelle (SPEC §8.2).

## Entwicklungsumgebung
- **macOS** (ab 15): Xcode bzw. Xcode-Kommandozeilenwerkzeuge, CMake ≥ 3.25, Ninja, CLion, `gh`, pluginval
- **Windows** (10/11, x64): Visual Studio 2022 Build Tools (C++), CMake ≥ 3.25, Ninja, CLion, `gh`, pluginval
- In Phase 0 prüft Claude Code die vorhandenen Werkzeuge und dokumentiert die Versionen von macOS,
  Windows, Live, Logic und Compilern in `DECISIONS.md`
- `.claude/settings.json` erlaubt Build-, Test- und lokale Git-Befehle ohne Rückfrage; Push und
  destruktive Befehle bleiben gesperrt bzw. fragen nach

## Build (CLion / CMake)

```bash
# macOS
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug

# Windows
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug

# Linux
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

CLion liest `CMakePresets.json` direkt. Für jede Plattform gibt es Debug- und Release-Presets.

### Validierung
```bash
pluginval --strictness-level 10 --validate-in-process --validate <Pfad zum .vst3/.component>
auval -v aumi <PLUGIN_CODE> <MANUFACTURER_CODE>   # MIDI-FX-Variante (Logic)
auval -v aumu <PLUGIN_CODE> <MANUFACTURER_CODE>   # Instrument-Variante
```

### Debugging
- Bevorzugt mit **Standalone** oder dem **JUCE AudioPluginHost** debuggen. Logic und Live laufen mit
  Hardened Runtime, ein Debugger lässt sich dort nicht ohne Weiteres anhängen.
- In echten DAWs über die Logdatei arbeiten (SPEC §10).
- macOS AU-Cache nach Neubau leeren: `killall -9 AudioComponentRegistrar`, danach die DAW neu starten.

## Verzeichnisstruktur

```
CMakeLists.txt, CMakePresets.json, .clang-format
cmake/                 CPM, Hilfsfunktionen
source/
  core/                Musiktheorie, Pattern-Modell, Generatoren, Variationen, Constraints (ohne JUCE)
  ai/                  Provider-Schnittstelle, Anthropic/OpenAI/Ollama/OpenAI-kompatibel, Schema, Prompts
  engine/              PatternPlayer, Transport-Sync, Note-Off-Tracking (Echtzeit)
  platform/            Keychain (macOS), Credential Manager (Windows), libsecret (Linux)
  ui/                  Editor, Piano-Roll, Bibliothek, Einstellungen
  plugin/              AudioProcessor-Varianten (Instrument, MIDI-FX), State
resources/prompts/v1/  Versionierte Prompt-Vorlagen (als BinaryData eingebettet)
resources/styles/      Stilprofile als JSON (aus docs/STYLES.md)
resources/grooves/     Groove-Vorlagen als JSON
resources/i18n/        de.json, en.json
resources/config/      models.json, pricing.json (Standardwerte)
tests/                 Catch2-Tests (core, ai mit Mock-Provider, engine)
tools/mmgen/           Kommandozeilen-Werkzeug für Hörtests: erzeugt .mid pro Stil, Archetyp, Seed (nutzt nur core)
docs/                  SPEC, STYLES, ROADMAP, DECISIONS, REVIEW
```
