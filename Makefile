CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
SOURCES = lexer.c parser.c csv.c database.c
HEADERS = lexer.h parser.h csv.h database.h
ifeq ($(OS),Windows_NT)
EXE = .exe
else
EXE =
endif

.PHONY: all test clean
all: minireldb$(EXE)

minireldb$(EXE): main.c $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) main.c $(SOURCES) -o $@

tests$(EXE): tests.c $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) tests.c $(SOURCES) -o $@

test: minireldb$(EXE) tests$(EXE)
	./tests$(EXE)

clean:
ifeq ($(OS),Windows_NT)
	-powershell -NoProfile -Command "Remove-Item -LiteralPath 'minireldb.exe','tests.exe' -ErrorAction SilentlyContinue"
else
	rm -f minireldb tests
endif
