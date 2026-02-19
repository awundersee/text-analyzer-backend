# Text Analyzer Backend (C)

Die Backend-Komponente des Textanalyse-Tools zur wort- und wortpaarbasierten Analyse
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
* Implementierung der Kernlogik in C
* Testgetriebene Entwicklung (TDD)
* Klare Trennung zwischen einzelnen Komponenten
* Vergleich und Bewertung unterschiedlicher Analyse-Pipelines
* Containerisierte Ausführung mit Docker

---

## Externe Abhängigkeiten

Die Kernlogik ist selbst implementiert. Für API-Zugriffe, JSON-Parsing und Tests werden 
etablierte Open-Source-Bibliotheken eingesetzt. Diese Abhängigkeiten
werden nicht für die eigentliche Textanalyse verwendet.

Die Kernlogik (Tokenisierung, Stoppwortfilterung, Wort- und Bigram-Analyse,
Aggregation, Sortierung, Top-K-Auswahl) wurde eigenständig
implementiert und testgetrieben entwickelt.

### Verwendete Bibliotheken

* **CivetWeb** (MIT)

  * Eingebetteter HTTP-Server für die API-Schicht

* **yyjson** (MIT)

  * Performantes JSON Parsing und Serialisierung

* **Unity Test Framework** (MIT)

  * Unit-Tests im Rahmen der TDD-Strategie

Weitere Lizenz- und Herkunftsinformationen sind in `THIRD_PARTY_NOTICES.md`
dokumentiert.

### Initiale Einrichtung der Submodule und Abhängigkeiten

```bash
git submodule update --init --recursive
```

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
intern unterschieden. Die Auswahl der Pipeline erfolgt anhand
der Dateigröße: ab 1MB Input-Größe wird die ID-Pipeline verwendet.

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

## Einzelanalyse einer JSON-Datei

Neben dem Batch-Modus kann die CLI auch eine einzelne JSON-Datei direkt analysieren.

### Aufruf

```bash
./build/analyze_cli data/batch_in/request.json --out result.json
```

### Beschreibung

* Der erste Parameter ist der Pfad zur JSON-Request-Datei
* Mit `--out` kann optional eine Ausgabedatei angegeben werden
* Wird kein `--out` gesetzt, erfolgt die Ausgabe ausschließlich auf `stdout`

### Ausgabe-Verhalten

Auch wenn `--out` verwendet wird, wird das vollständige Analyse-Ergebnis
zusätzlich in der Kommandozeile (`stdout`) ausgegeben.


### Optionale Parameter

```bash
--pipeline auto|string|id
--topk K
```

* `--pipeline` erzwingt eine bestimmte Analysevariante
* `--topk` begrenzt die Anzahl der ausgegebenen Top-Wörter und -Wortpaare

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

---

## API

Die HTTP-API dient als Schnittstelle zur Analyse-Logik und hat 
verschiedene Grenzen: es werden nur bis zu 100 Seiten analysiert
und die Datei darf maximal 10MB groß sein.

### Endpunkte

* `GET /health`

  * Health-Check zur Überprüfung der Erreichbarkeit

* `POST /analyze`

  * Führt eine Textanalyse durch
  * Liefert Top-K-Wörter und Bigrams pro Seite und für das gesamte Dokument

Die API validiert eingehende Requests strikt (JSON-Schema, Datentypen,
Grenzwerte) und antwortet bei Fehlern mit geeigneten
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

Die Teststrategie beschreibt die eingesetzten Testarten, deren Zielsetzung 
sowie die lokale Ausführung der Tests. Ziel ist es, Korrektheit, Stabilität und 
Performance der Textanalyse reproduzierbar sicherzustellen.

### Unit-Tests

* Für die testgetriebene Entwicklung der Analyse-Komponenten
* Tests liegen unter `tests/unit`
* Fokus auf kleine, isolierte Funktionseinheiten (z. B. Tokenizer, Stoppwortfilter, Aggregation)

Die Unit-Tests dienen primär der funktionalen Absicherung und Regressionserkennung 
bei Refactorings oder algorithmischen Änderungen.

### API-Tests

* Automatisierte End-to-End-Tests der HTTP-Schnittstelle
* Validierung von Request-Parsing, Validierung, Analysepipeline und Response-Struktur
* Vergleich der tatsächlichen API-Antworten mit Erwartungsdaten

Lokale Ausführung:

```bash
cmake --build build --target test_api
```

Bei Änderungen an API-Testdaten können die Ergebnisse neu erzeugt werden:

```bash
chmod +x tests/api/scripts/regen_expected.sh
API_URL=http://localhost:8080 tests/api/scripts/regen_expected.sh
```

Dabei muss beachtet werden, dass dadurch mögliche Fehler im Programmcode
in die erwarteten Ergebnisse integriert werden. Eine manuelle Prüfung wird 
daher empfohlen.

### Performance-Tests

Die Performance-Tests dienen der Analyse von Laufzeit- und Speicherverhalten 
unter verschiedenen realistischen und synthetischen Lastszenarien.

Getestet werden unter anderem:

* Reine Analyse-Laufzeit (```runtimeMsAnalyze```)
* Gesamtlaufzeit inklusive Parsing und Serialisierung (```runtimeMsTotal```)
* Unterschiede zwischen CLI- und API-Ausführung
* Speicherverbrauch (Peak RSS)
* Parallele API-Anfragen (Concurrency-Tests)
* Vergleich der Analysepipelines (```string``` vs. ```id```)

Die Tests ermöglichen es, algorithmische Änderungen oder neue Speicherstrategien 
objektiv zu bewerten und über mehrere Versionen hinweg vergleichbar zu halten.

Lokale Ausführung:

#### Pipeline-Performance

Misst die Laufzeit der einzelnen Analyse-Pipelines (```string``` und  ```id```). 
Ziel ist der Vergleich der End-to-End-Laufzeit der Kernverarbeitung.

```bash
cmake --build build --target test_perf_pipeline
```

Die Tests werden lokal in der CLI und API durchgeführt. Für die API muss das Programm mit 
Docker laufen und über ```http://127.0.0.1:8080/analyze``` aufrufbar sein. Für die CLI muss ein lokaler Build vorhanden
sein.

#### Speicherverbrauch

Ermittelt den Speicherverbrauch der Analyse bei unterschiedlich großen Eingaben. Über Cmake standardmäßig
ohne die großen Testdateien, die von der API abgelehnt werden (wegen Dateigröße und Seitenanzahl). 
Mit ```INCLUDE_LARGE=1``` werden auch die großen Testdateien (```tests/performance/data/large/```) 
einbezogen, um Speichergrenzen und Skalierungseffekte sichtbar zu machen.

```bash
INCLUDE_LARGE=1 cmake --build build --target test_perf_mem
```

Die Tests werden lokal in der CLI und API durchgeführt. Für die API muss das Programm mit 
Docker laufen und über ```http://127.0.0.1:8080/analyze``` aufrufbar sein. Für die CLI muss ein lokaler Build vorhanden
sein.

#### Parallele API-Requests

Testet die API unter paralleler Last. Ziel ist es, Auswirkungen von gleichzeitigen Requests auf Laufzeit 
und Ressourcenverbrauch zu analysieren. Große Eingaben können optional über ```INCLUDE_LARGE=1``` aktiviert werden.

```bash
INCLUDE_LARGE=1 cmake --build build --target test_perf_api_concurrency
```

Die Tests werden lokal in der CLI und API durchgeführt. Für die API muss das Programm mit 
Docker laufen und über ```http://127.0.0.1:8080/analyze``` aufrufbar sein. Für die CLI muss ein lokaler Build vorhanden
sein.

#### Top-K Laufzeitenmessung

Die Sortierphase ist ein zentraler Flaschenhals und es gibt daher dedizierte Messungen für die Berechnung 
der Top-Wörter und Top-Wortpaare.

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

Die Messung der erfolgt über wiederholte CLI-Aufrufe. Bewertet wird ausschließlich die Laufzeit des 
Selektionsalgorithmus in Abhängigkeit von der Anzahl der zu sortierenden Elemente (```n```). 
Dadurch kann das Laufzeitverhalten der Top-K-Operation isoliert analysiert werden.

#### API Produktions-Performance

Es werden alle Testdateien unter ```/data/``` in der Zielumgebung analysiert und der Durchschnitt aus
fünf Durchläufen jeweils für

* `meta.runtimeMsAnalyze` – reine Analysezeit
* `meta.runtimeMsTotal` – Gesamtverarbeitungszeit (inkl. Overhead)
* `meta.peakRssKiB` – maximaler Speicherverbrauch pro Lauf

als CSV-Datei abgespeichert.

Beispielaufruf:

```bash
cmake --build build --target test_perf_api_prod_avg5
```

Die Tests werden in der Zielarchitektur durchgeführt, daher muss das Programm unter 
```https://textanalyse.wundersee.dev/analyze``` aufrufbar sein.

### Stress-Tests

Die Stress-Tests prüfen das Systemverhalten bei großen und sehr großen Eingabedaten.
Unterschieden wird insbesondere zwischen:

* Einseitigen und mehrseitigen Dokumenten (```/tests/stress/data/single-page/```und ```/tests/stress/data/multi-page/```)
* Grenzfälle hinsichtlich Speicher- und Laufzeitlimits ermitteln

Ziel der Stress-Tests ist es, das Verhalten unter Extrembedingungen zu analysieren, potenzielle Engpässe 
frühzeitig zu erkennen und die Robustheit der Implementierung für die vorgesehene Zielarchitektur abzusichern.

Lokale Ausführung:

```bash
cmake --build build --target test_stress_single
```

```bash
cmake --build build --target test_stress_multi
```

Die Tests werden lokal in der CLI durchgeführt, dafür muss ein vollständiger Build vorhanden sein.

#### Große Testdaten erzeugen

Um große Testdaten zu erzeugen befinden sich in den Ordner für mehr- und einseitige JSON-Dateien
jeweils vier verschiedene JSON-Basis-Dateien, die über die Skripte in den Ordner vervielfacht werden können.

Bitte beachten: es existieren Abhängigkeiten, die ggf. lokal installiert werden müssen.

### Hinweise

* Alle Tests sind für die lokale Ausführung vorgesehen
* Ergebnisse werden als CSV-Dateien abgelegt (jeweils in den Unterordnern ```/results/```) und eignen sich zur Weiterverarbeitung (z. B. Liniendiagramme)

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