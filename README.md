# Text Analyzer Backend (C)

Backend-Komponente des Textanalyse-Tools zur wort- und wortpaarbasierten Analyse
von Textinhalten aus Webseiten.

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
