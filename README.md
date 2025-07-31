# dd-native

Repository structure:

- `include-c/` contains public headers for the C API.
- `include-cpp/` contains public headers for the C++ API.
- `src/c/` implements the C API, binding it to `src/impl/`.
- `src/cpp/` implements the C++ API, binding it to `src/impl/`.
- `src/impl/` implements the core business logic of the library.
- `examples/` demonstrates usage of both C and C++ APIs.

To build and run examples:

```
mkdir build && cd build
cmake -DDD_BUILD_EXAMPLES=ON .. && cmake --build .
examples/dd_native_c_example
examples/dd_native_cpp_example
```
