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

Diese Teststrategie beschreibt die eingesetzten Testarten, deren Zielsetzung sowie die lokale Ausführung der Tests. Ziel ist es, Korrektheit, Stabilität und Performance der Textanalyse reproduzierbar sicherzustellen.

### Unit-Tests

* Testgetriebene Entwicklung der Analyse-Komponenten
* Tests liegen unter `tests/unit`
* Fokus auf kleine, isolierte Funktionseinheiten (z. B. Tokenizer, Stoppwortfilter, Aggregation)
* Deterministische und reproduzierbare Ergebnisse ohne externe Abhängigkeiten

Die Unit-Tests dienen primär der funktionalen Absicherung und Regressionserkennung bei Refactorings oder algorithmischen Änderungen.

### API-Tests

* Automatisierte End-to-End-Tests der HTTP-Schnittstelle
* Validierung von Request-Parsing, Validierung, Analysepipeline und Response-Struktur
* Vergleich der tatsächlichen API-Antworten mit versionierten Erwartungsdaten

Lokale Ausführung:

```bash
cmake --build build --target test_api
```

Bei Änderungen an API-Testdaten können die erwarteten Ergebnisse neu erzeugt werden:

```bash
chmod +x tests/api/scripts/regen_expected.sh
API_URL=http://localhost:8080 tests/api/scripts/regen_expected.sh
```

### Performance-Tests

Die Performance-Tests dienen der Analyse von Laufzeit- und Speicherverhalten unter verschiedenen realistischen und synthetischen Lastszenarien.

Getestet werden unter anderem:

* Reine Analyse-Laufzeit (```analyze```)
* Gesamtlaufzeit inklusive Parsing und Serialisierung (```total```)
* Unterschiede zwischen CLI- und API-Ausführung
* Speicherverbrauch (Peak RSS)
* Parallele API-Anfragen (Concurrency-Tests)
* Vergleich der Analysepipelines (```string``` vs. ```id```)

Die Tests ermöglichen es, algorithmische Änderungen oder neue Speicherstrategien objektiv zu bewerten und über mehrere Versionen hinweg vergleichbar zu halten.

Lokale Ausführung:

#### Pipeline-Performance

Misst die Laufzeit der einzelnen Analyse-Pipelines (z. B. freq, id) über verschiedene Eingabedateien. Ziel ist der Vergleich der End-to-End-Laufzeit der Kernverarbeitung unabhängig von Netzwerk- oder API-Overhead.

```bash
cmake --build build --target test_perf_pipeline
```

#### Speicherverbrauch

Ermittelt den Speicherverbrauch der Analyse bei unterschiedlich großen Eingaben. Mit ```INCLUDE_LARGE=1``` werden zusätzlich sehr große Testdateien einbezogen, um Speichergrenzen und Skalierungseffekte sichtbar zu machen.

```bash
INCLUDE_LARGE=1 cmake --build build --target test_perf_mem
```

#### API-Baseline vs. Delta

Vergleicht eine API-Baseline-Ausführung mit inkrementellen Änderungen (Delta). Der Fokus liegt auf der reinen Verarbeitungszeit, nicht auf Netzwerk- oder Serialisierungskosten.

```bash
cmake --build build --target test_perf_api_baseline_delta
```

#### API-Concurrency

Testet die API unter paralleler Last. Ziel ist es, Auswirkungen von gleichzeitigen Requests auf Laufzeit und Ressourcenverbrauch zu analysieren. Große Eingaben können optional über ```INCLUDE_LARGE=1``` aktiviert werden.

```bash
INCLUDE_LARGE=1 cmake --build build --target test_perf_api_concurrency
```

#### Top-K Laufzeitenmessung (Detailanalyse)

Da die Sortierphase als zentraler Flaschenhals identifiziert wurde, existiert eine dedizierte Messung für die Top-K-Selektion.

Dabei werden in topk.c gezielt Zeitstempel an mehreren Stellen erfasst:

* Kopieren der Kandidaten
* Sortierung
* Erzeugung der Ergebnisliste

Die Messung erfolgt getrennt für:

* Wort-Frequenzen (words)
* Wortpaare (bigrams)

Beispielaufruf für Top-20 (entspricht dem API-Default):

```bash
K=20 cmake --build build --target test_topk_measure
```

```bash
cmake --build build --target test_topk_measure_perf
```

Die Auswertung erfolgt statistisch über alle Top-K-Aufrufe hinweg. Entscheidend ist dabei nicht die Herkunft der Daten (Datei oder Seite), sondern ausschließlich die Anzahl der zu selektierenden Elemente (```n```). Dadurch lässt sich die Laufzeitentwicklung der Top-K-Selektion unabhängig vom Eingabeformat analysieren.

### Stress-Tests

Die Stress-Tests prüfen das Systemverhalten bei großen und sehr großen Eingabedaten.
Unterschieden wird insbesondere zwischen:

* Einseitigen und mehrseitigen Dokumenten
* Stark skalierten Textmengen (z. B. durch Vervielfältigung)
* Grenzfällen hinsichtlich Speicher- und Laufzeitlimits

Ziel der Stress-Tests ist es, das Verhalten unter realitätsnahen Extrembedingungen zu analysieren, potenzielle Engpässe frühzeitig zu erkennen und die Robustheit der Implementierung für die vorgesehene Zielarchitektur abzusichern.

Lokale Ausführung:

```bash
cmake --build build --target test_stress_single
```

```bash
cmake --build build --target test_stress_multi
```

### Hinweise

* Alle Tests sind für lokale Ausführung vorgesehen
* Ergebnisse werden als CSV-Dateien abgelegt und eignen sich zur Weiterverarbeitung (z. B. Liniendiagramme)
* Die CLI-Tests werden auch im Container ausgeführt; produktiv läuft jedoch ausschließlich die API

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