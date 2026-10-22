# Validation record

Executed on 22 October 2026 on Windows with MinGW-w64 GCC 16.1.0 and GNU Make 4.4.1.

- `mingw32-make`: builds the C11 engine with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`; no compiler warnings.
- `mingw32-make test`: **600 checks, 0 failures**. Includes lexer/parser/CSV/executor tests and actual subprocess tests for restart persistence, redirected SQL, quoted semicolons and newlines, type errors, EOF, oversized and NUL input recovery, terminal commands, and both example scripts.
- GCC `-fanalyzer` compilation of all six translation units (`main`, `lexer`, `parser`, `csv`, `database`, and `tests`): no diagnostics.
- `git diff --cached --check`: no whitespace errors.

An additional UndefinedBehaviorSanitizer build was attempted but could not link because this MinGW installation lacks `libubsan`. No sanitizer run is claimed. Linux/macOS builds have not been executed in this environment.

To reproduce the primary checks:

```powershell
mingw32-make
mingw32-make test
```

Tests use isolated directories under ignored `test-output/`. Application data and binaries are not committed.
