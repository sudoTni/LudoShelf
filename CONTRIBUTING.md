# Contributing

Build and run the test suite before opening a change:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Keep persistence changes migration-safe, add regression tests for bug fixes,
and avoid committing ROMs, BIOS files, personal library exports, or generated
build directories. Changes that accept files, archives, URLs, or process
configuration must document input limits and failure behavior.
