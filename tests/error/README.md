# Error / Edge-Case Suite (CLI + API)

## Struktur
- `data/`      Test-Inputs
- `scripts/`   Runner + Generator
- `results/`   CSV + Summary (wird automatisch geschrieben)
- `manifest.json` Erwartungen pro Datei

## CMake-Aufruf (robust)
Wichtig ist, dass du `CMAKE_BINARY_DIR` an den Runner durchreichst, damit er das Binary sicher findet.

```cmake
add_custom_target(test_error
  COMMAND ${CMAKE_COMMAND} -E env
          CMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}
          API_URL=http://127.0.0.1:8080/analyze
          bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/error/scripts/run_suite.sh
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Running error suite (CLI + API)"
)
```

## Optional: >10MB Body generieren
```bash
cd scripts
./gen_big_payload_over_10mb.sh 11 > ../data/GEN_api_over_10mb.json
```
