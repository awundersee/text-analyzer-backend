# Text Analyzer Backend (C)

Backend-Komponente des Textanalyse-Tools zur wort- und wortpaarbasierten Analyse
von Textinhalten aus Webseiten.

Das Frontend der Anwendung ist über folgenden Link erreichbar:
[wp-analyzer.wundersee.dev](https://wp-analyzer.wundersee.dev)

## Ziel
- Analyse von Textdaten (Wortfrequenzen, Wortpaare)
- Implementierung in **C**
- **Testgetriebene Entwicklung (TDD)**
- Containerisierte Ausführung mit **Docker**
- Klare Trennung zwischen Analyse-Logik (Core) und API-Schicht

## Build & Tests
Voraussetzungen:
- C Compiler (gcc / clang)
- CMake ≥ 3.16

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build

## Externe Abhängigkeiten

Für die API-, Infrastruktur- und Test-Schicht werden bewusst etablierte,
permissiv lizenzierte Open-Source-Bibliotheken eingesetzt. Diese Komponenten
werden **nicht** für die eigentliche Textanalyse verwendet, sondern ausschließlich
für technische Aufgaben wie HTTP-Kommunikation, JSON-Verarbeitung und automatisierte Tests.

Die Kernlogik der Textanalyse (Tokenisierung, Stoppwortfilterung, Wort- und Wortpaaranalyse,
Aggregation sowie Top-K-Auswahl) wurde vollständig eigenständig implementiert und
testgetrieben entwickelt.

### Verwendete Bibliotheken

- **CivetWeb** (MIT-Lizenz)  
  Eingesetzt als eingebetteter HTTP-Server für die API-Schicht.  
  Die Bibliothek ist weit verbreitet, stabil und auf Performance optimiert.

- **yyjson** (MIT-Lizenz)  
  Eingesetzt für das Parsen und Serialisieren von JSON-Daten.  
  yyjson ist eine sehr performante JSON-Bibliothek in C und wird ausschließlich
  für die Ein- und Ausgabe der API verwendet.

- **Unity Test Framework** (MIT-Lizenz)  
  Eingesetzt für automatisierte Unit-Tests im Rahmen der testgetriebenen Entwicklung (TDD).  
  Unity ermöglicht strukturierte, reproduzierbare Tests der Analyse-Komponenten
  und wird ausschließlich in der Testumgebung verwendet (keine Laufzeitabhängigkeit).

Die Verwendung dieser Bibliotheken erfolgt bewusst, um sich im Projekt auf die Entwicklung
und Optimierung der Analysealgorithmen zu konzentrieren und bewährte, gut getestete Lösungen
für infrastrukturelle und unterstützende Aufgaben zu nutzen.

Weitere Lizenz- und Herkunftsinformationen sind in der Datei `THIRD_PARTY_NOTICES.md`
dokumentiert.

## API & Docker

### API

Die Analysefunktionalität wird über eine schlanke HTTP-API bereitgestellt.  
Die API fungiert ausschließlich als Schnittstelle und ruft die im Core implementierte
Analyse-Logik auf.

Der Server stellt aktuell folgende Endpunkte bereit:

- `GET /health`  
  Einfacher Health-Check zur Überprüfung der Erreichbarkeit.

- `POST /analyze`  
  Führt eine Textanalyse durch und liefert die Top-K-Wörter sowie Wortpaare (Bigrams).

Die API erwartet ein JSON-Objekt mit einer Liste von Texten sowie optional der gewünschten
Anzahl der Top-Ergebnisse.

---

### Docker

Das Backend kann vollständig containerisiert ausgeführt werden.  
Das Docker-Setup verwendet ein Multi-Stage-Build, sodass das finale Image ausschließlich
das kompilierte API-Binary und notwendige Laufzeitdateien enthält.

Die Anwendung wird im Container als eigenständiger HTTP-Service betrieben und kann
lokal oder auf einem Server über Port `8080` angesprochen werden.

Zur Optimierung des Docker-Build-Kontexts wird eine `.dockerignore`-Datei verwendet,
die Build-Artefakte und Versionsverwaltungsdaten ausschließt.