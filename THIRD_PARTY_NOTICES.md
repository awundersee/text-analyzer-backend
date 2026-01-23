# Third-Party Notices

This project makes use of third-party open-source components.
All listed components are licensed under permissive licenses and are used
exclusively for infrastructure, API handling, or testing purposes.

The core logic of the text analysis (tokenization, stopword filtering,
word and bigram analysis, aggregation, and top-k selection) was implemented
independently as part of this project.

---

## CivetWeb — Embedded HTTP Server

- **Purpose:**  
  Embedded HTTP server used for the API layer of the application.

- **Usage in this project:**  
  CivetWeb is used to provide HTTP endpoints (e.g. `/health`, `/analyze`)
  and to handle incoming requests and responses.

- **License:**  
  MIT License

- **Source:**  
  https://github.com/civetweb/civetweb

---

## yyjson — JSON Parser and Serializer

- **Purpose:**  
  High-performance JSON parsing and serialization library written in C.

- **Usage in this project:**  
  yyjson is used exclusively for parsing incoming JSON requests and
  generating JSON responses in the API layer.

- **License:**  
  MIT License

- **Source:**  
  https://github.com/ibireme/yyjson

---

## Unity Test Framework

- **Purpose:**  
  Unit testing framework for the C programming language.

- **Usage in this project:**  
  Unity is used to implement automated unit tests as part of a
  test-driven development (TDD) approach.  
  The framework is only used during development and testing and is not
  required at runtime.

- **License:**  
  MIT License

- **Source:**  
  https://github.com/ThrowTheSwitch/Unity

---

## License Notice

All third-party components listed above are distributed under the MIT License.
Each component remains the property of its respective authors.

This project does not modify the licenses of the listed components and
complies with all applicable license terms.
