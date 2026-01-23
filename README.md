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