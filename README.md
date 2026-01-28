# Text Analyzer Backend (C)

Backend-Komponente des Textanalyse-Tools zur wort- und wortpaarbasierten Analyse
von Textinhalten aus Webseiten.

Das Frontend der Anwendung ist erreichbar unter:
[textanalyse.wundersee.dev](https://textanalyse.wundersee.dev)

---

## Zielsetzung

Ziel dieses Projekts ist die Entwicklung eines performanten, robusten und modularen
Textanalyse-Backends in **C**, das sowohl über eine CLI als auch über eine HTTP-API
angesprochen werden kann.

Schwerpunkte des Projekts sind:

* Analyse von Textdaten (Wortfrequenzen, Wortpaare / Bigrams)
* Implementierung der Kernlogik in **C**
* **Testgetriebene Entwicklung (TDD)**
* Klare Trennung zwischen Analyse-Core und API-Schicht
* Vergleich und Bewertung unterschiedlicher Analyse-Pipelines
* Containerisierte Ausführung mit **Docker**

---

## Architektur & Analyse-Pipelines

Die Textanalyse ist in zwei unterschiedliche Pipelines unterteilt, die je nach
Eingabedaten und Anwendungsfall eingesetzt werden:

### String-basierte Pipeline

* Direkte Verarbeitung der Texte als Strings
* Tokenisierung, Stoppwortfilterung und Aggregation erfolgen unmittelbar auf
  String-Ebene
* Vorteil: geringe Komplexität, geringer Overhead bei kleinen bis mittleren
  Textmengen
* Nachteil: höherer Speicherbedarf bei großen Datenmengen durch redundante Strings

### ID-basierte Pipeline

* Tokens werden frühzeitig auf numerische IDs abgebildet
* Interne Verarbeitung (Zählung, Aggregation, Sortierung) erfolgt ausschließlich
  auf ID-Basis
* Vorteil: deutlich geringerer Speicherverbrauch und bessere Skalierbarkeit
* Nachteil: höherer Initialaufwand (Mapping / Lookup)

Beide Pipelines liefern **funktional identische Ergebnisse** und werden ausschließlich
intern unterschieden. Die Auswahl der Pipeline erfolgt deterministisch anhand
vordefinierter Kriterien (z. B. Eingabegröße / Seitenanzahl).

---

## Build & Tests

### Voraussetzungen

* C Compiler (gcc oder clang)
* CMake ≥ 3.16

### Build

```bash
cmake -S . -B build
cmake --build build
```

### Tests ausführen

```bash
ctest --test-dir build
```

---

## Externe Abhängigkeiten

Für API-, Infrastruktur- und Test-Schichten werden bewusst etablierte,
permissiv lizenzierte Open-Source-Bibliotheken eingesetzt. Diese Abhängigkeiten
werden **nicht** für die eigentliche Textanalyse verwendet.

Die Kernlogik (Tokenisierung, Stoppwortfilterung, Wort- und Bigram-Analyse,
Aggregation, Sortierung, Top-K-Auswahl) wurde vollständig eigenständig
implementiert und testgetrieben entwickelt.

### Verwendete Bibliotheken

* **CivetWeb** (MIT)

  * Eingebetteter HTTP-Server für die API-Schicht

* **yyjson** (MIT)

  * Performantes JSON Parsing und Serialisierung

* **Unity Test Framework** (MIT)

  * Unit-Tests im Rahmen der TDD-Strategie
  * Nur in der Testumgebung eingesetzt

Weitere Lizenz- und Herkunftsinformationen sind in `THIRD_PARTY_NOTICES.md`
dokumentiert.

---

## CLI Batch-Analyse

Die CLI unterstützt neben der Analyse einzelner JSON-Dateien auch einen
**Batch-Modus**.

Erwartete Projektstruktur:

```
data/
├─ batch_in/    # Eingabe: JSON-Requests
└─ batch_out/   # Ausgabe: Analyse-Ergebnisse (*.result.json)
```

### Batch-Ausführung

```bash
./build/analyze_cli batch
```

Alternativ mit expliziten Pfaden:

```bash
./build/analyze_cli batch --in data/batch_in --out data/batch_out
```

### Hinweis

Der Batch-Modus erzeugt **vollständige Analyse-Ergebnisse ohne Top-K-Limit**.
Die Top-K-Begrenzung wird ausschließlich für API- und UI-Ausgaben verwendet.

---

## API

Die HTTP-API dient ausschließlich als dünne Schnittstelle zur Analyse-Logik.
Die API hat aber verschiedene Grenzen: es werden nur bis zu 100 Seiten analysiert
und die Datei darf maximal 10MB groß sein.

### Endpunkte

* `GET /health`

  * Health-Check zur Überprüfung der Erreichbarkeit

* `POST /analyze`

  * Führt eine Textanalyse durch
  * Liefert Top-K-Wörter und Bigrams pro Seite und für das gesamte Dokument

Die API validiert eingehende Requests strikt (JSON-Schema, Datentypen,
Grenzwerte) und antwortet bei Fehlern deterministisch mit geeigneten
HTTP-Statuscodes.

---

## Docker

Das Backend kann vollständig containerisiert betrieben werden.

* Multi-Stage-Build für minimale Image-Größe
* Finales Image enthält ausschließlich das kompilierte API-Binary
* Standard-Port: `8080`

```bash
docker-compose up -d --build
```

Eine `.dockerignore`-Datei reduziert den Build-Kontext auf notwendige Dateien.

---

## Teststrategie

### Unit-Tests

* Testgetriebene Entwicklung der Analyse-Komponenten
* Tests liegen unter `tests/unit`
* Fokus auf deterministische und reproduzierbare Ergebnisse

### API-Tests

* Automatisierte Tests der HTTP-Schnittstelle
* Ausführung über GitHub Actions
* Lokal testbar über:

```bash
cmake --build build --target test_api
```

Bei Änderungen an API-Testdaten können die erwarteten Ergebnisse neu erzeugt werden:

```bash
chmod +x tests/api/scripts/regen_expected.sh
API_URL=http://localhost:8080 tests/api/scripts/regen_expected.sh
```

---

## Einrichtung

Initiale Einrichtung der Submodule und Abhängigkeiten:

```bash
git submodule update --init --recursive
```

Start des Backends im Docker-Container:

```bash
docker-compose up -d --build
```