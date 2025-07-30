# dd-native

Repository structure:

- `include-c/` contains public headers for the C API.
- `include-cpp/` contains public headers for the C++ API.
- `src/c/` implements the C API, binding it to `src/impl/`.
- `src/cpp/` implements the C++ API, binding it to `src/impl/`.
- `src/impl/` implements the core business logic of the library.
- `examples/` demonstrates usage of both C and C++ APIs.
