# MiniRelDB

A small relational database engine written in **C11** for Compiler Design and DBMS coursework. It tokenizes and parses SQL itself, validates typed values, executes full table scans, and persists records in CSV files. No external libraries or database engines are used.

## Build and run

Requires GCC or Clang and GNU Make.

Linux / macOS:

```sh
make
./minireldb
make test
```

Windows PowerShell with MinGW GCC and Make on PATH:

```powershell
mingw32-make
.\minireldb.exe
mingw32-make test
```

To use Clang, run `make CC=clang`. `make clean` removes the application and test binaries; it leaves stored data intact. The default build enables C11, optimization, and strict compiler warnings.

Run a script without interactive prompts:

```sh
./minireldb examples/demo.sql
./minireldb < examples/demo.sql
```

```powershell
.\minireldb.exe examples\demo.sql
Get-Content -Raw examples\demo.sql | .\minireldb.exe
```

Script errors go to stderr. The engine continues with subsequent statements and exits with a nonzero status if any statement failed. A final statement without a semicolon is rejected. `.help` and `.exit` are terminal commands on their own lines, without semicolons. Interactive mode prints prompts; redirected input does not.

## SQL examples

```sql
CREATE TABLE students (id INT, name STRING, cgpa FLOAT, active BOOL);
INSERT INTO students VALUES (1, 'Aman', 8.5, true);
INSERT INTO students VALUES (2, 'O''Brien, "Alex"', 7.2, false);
SELECT * FROM students;
SELECT name, cgpa FROM students WHERE cgpa >= 8;
SELECT name FROM students WHERE active = true;
UPDATE students SET cgpa = 9.2 WHERE id = 1;
DELETE FROM students WHERE id = 2;
DROP TABLE students;
```

The six supported statements are CREATE TABLE, INSERT INTO ... VALUES, SELECT ... FROM, UPDATE ... SET, DELETE FROM, and DROP TABLE. SELECT supports `*` or a comma-separated list of column names. UPDATE accepts one assignment. SELECT, UPDATE, and DELETE accept one optional WHERE condition using `=`, `!=`, `<`, `>`, `<=`, or `>=`. **UPDATE and DELETE without WHERE affect all rows.** Rows are returned in their stored order.

SQL keywords and identifiers are case-insensitive; identifiers are stored in lowercase. Names follow `[A-Za-z_][A-Za-z0-9_]*`, and reserved keywords cannot be used as names. Quoted identifiers, SQL comments, NULL, primary keys, joins, AND/OR, ordering, indexes, and transactions are outside this project's scope. Duplicate column names in CREATE are rejected; row values need not be unique.

Strings use single quotes, preserve their contents, and escape apostrophes by doubling them: `'O''Brien'`. Backslashes have no SQL escape meaning. Strings and statements may contain actual newlines; semicolons inside strings do not terminate statements. Results escape control characters, backslashes, and non-ASCII bytes (`\n`, `\r`, `\t`, `\\`, `\xHH`) to keep terminal columns aligned. Stored string bytes are preserved, including UTF-8; this engine does not perform Unicode collation or character counting.

### Types and comparisons

| Type | Values and validation |
| --- | --- |
| INT | Signed 32-bit integer, from -2147483648 to 2147483647 |
| FLOAT | Finite `double`; signed decimal and exponent notation are accepted |
| STRING | Up to 255 bytes, including empty strings |
| BOOL | `true` or `false`, without quotes |

INT literals are accepted in FLOAT columns. Other insert/update conversions are rejected, including FLOAT to INT and numeric values to BOOL. WHERE permits INT/FLOAT comparisons, bytewise STRING comparisons, and BOOL equality/inequality. Incompatible comparisons are errors even when a table is empty. FLOAT comparisons use exact double comparisons; no epsilon is applied. Overflow and conversions reporting range errors (including underflow) are rejected. An integer literal must fit INT even when targeting a FLOAT column; use decimal/exponent notation for larger floating-point values.

## Architecture and compiler concepts

```text
SQL input -> lexer -> tokens -> parser -> Query -> executor -> schema / CSV
```

- `main.c`: reads statements, tracks quoted strings, provides prompts and terminal commands, reports errors, and controls program lifetime.
- `lexer.c` / `lexer.h`: lexical analysis recognizes keywords, identifiers, numeric/string/boolean literals, operators, and punctuation. Each token retains its byte position for diagnostics.
- `parser.c` / `parser.h`: a handwritten recursive-descent/structured parser checks command grammar and builds a typed `Query`. Syntax checking happens before execution; the parser performs no storage I/O.
- `database.c` / `database.h`: defines typed values using enums/unions, loads and validates schemas and rows, checks query types and column names, evaluates WHERE, executes CRUD, and formats result tables.
- `csv.c` / `csv.h`: reads CSV with a character-based state machine and serializes fields with proper quoting. Embedded commas, double quotes, CR/LF, and empty fields round-trip. Doubled double quotes escape quotes in CSV.
- `tests.c`: dependency-free C tests for lexical analysis, parsing, CSV round trips, query execution, corruption handling, and limits.

This separates compiler stages: lexical analysis identifies the vocabulary, parsing validates its arrangement, and semantic checks in execution resolve identifiers/types against the loaded schema.

## Storage and limits

Database files live in `database/` relative to the current working directory. The directory is created automatically. Each table has a `.schema` file containing one `name:TYPE` line per column and a `.csv` file whose first record is the column-name header. Data survives restarts; the working directory selects the database.

Every access loads and validates the schema and CSV. Wrong headers, invalid field counts/types, NUL bytes, malformed quoting, and oversized fields are rejected. INSERT, UPDATE, and DELETE validate before writing, then serialize to an exclusively created `.csv.tmp` and replace the original after a successful close. Windows uses `MoveFileExA`; POSIX uses `rename`. A pre-existing temporary file blocks a write so recovery data is not overwritten. After an interrupted run, inspect the `.csv` and `.csv.tmp` files before manually removing a stale temporary file.

| Limit | Default |
| --- | --- |
| Columns per table / projection | 32 |
| Identifier length | 63 ASCII bytes |
| STRING length | 255 bytes |
| SQL statement length | 65,536 bytes, including semicolon |
| Rows per table | 10,000 |
| Database directory path | 511 bytes |

Limits are defined in `database.h`. Full table scans and in-memory rows keep the implementation understandable; a full-width table near the row limit may use roughly 85 MB. This is a single-process educational engine: no locking, concurrent writers, transaction recovery, or full power-loss durability guarantees. CREATE/DROP span two files and are not atomic. DROP validates the table first and reports a partial failure if CSV removal succeeds but schema removal fails. Keep backups of data you care about.

## Testing

Run `make test` (or `mingw32-make test`). Tests create isolated directories under ignored `test-output/` and never use the application's `database/` directory. The suite covers all commands and types, all WHERE operators, projection, quoted strings, empty results, malformed SQL, type/column errors, duplicate names, numeric and size limits, malformed schema/CSV, and preservation of original files after rejected mutations or a blocked temporary-file write. Subprocess tests launch the actual engine with redirected input, verify persistence between separate launches, exercise quoted semicolons/newlines and terminal commands, and check recovery after invalid or oversized statements. The test harness uses the platform command shell to redirect subprocess input/output; no scripting language is required.

For an end-to-end smoke test, run `examples/demo.sql`. It creates `students`, inserts rows, selects, updates, deletes, and drops the table. Use a database without an existing `students` table. `examples/errors.sql` intentionally produces errors and a nonzero exit status while cleaning up its `validation` table.

To verify persistence manually, start the engine, create and insert into a table, use `.exit`, start the engine again in the same directory, and SELECT from that table.

The project tracks source, examples, and the empty database directory. Runtime data, build products, and test output are excluded from Git.
