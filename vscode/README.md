# TAL Language Support for VS Code

**The language that runs the financial backbone of civilisation now has autocomplete.**

TAL (Transaction Application Language) has been silently processing your bank transfers, stock trades, and ATM withdrawals since 1974. It runs on HP NonStop (née Tandem) systems — machines so fault-tolerant they make "five nines" sound like amateur hour. The developers who maintain these systems have been navigating million-line codebases with CTRL-F and prayer for fifty years.

Until now.

## Features

### Diagnostics
Real error messages. Not `expected token 31, got 107`. Actual words that a human being can read at 3am when the transaction processor is acting up.

![diagnostics](https://img.shields.io/badge/squiggly_lines-red-red)

Includes unused variable warnings, because dead code in a financial system is the kind of thing that keeps auditors awake at night.

### Autocomplete
All 61 TAL keywords plus every symbol in your file. Types, kinds, the lot. The editor's fuzzy matcher handles the rest — just start typing and stop memorising the reference manual.

### Hover
Mouse over any identifier and see what it is, what type it is, and where it was declared. For DEFINEs, see the full macro expansion. No more grepping through TACL scripts to figure out what `MSG^BUF` actually resolves to.

### Go to Definition
Click a name. Jump to its declaration. The feature that NonStop developers have been simulating with CTRL-F since the EDIT days. Works for variables, procedures, parameters, literals, defines — everything the symbol table knows about.

### Document Symbols
Outline view in the sidebar. PROCs, SUBPROCs, STRUCTs, LITERALs, DEFINEs, global declarations — all at a glance. Navigate a TAL file without scrolling through 2000 lines of MOVE statements hoping you recognise a procedure boundary by indentation.

### Syntax Highlighting
TextMate grammar that knows the difference between keywords, types, operators, standard functions ($LEN, $CARRY, etc.), numeric literals (%H hex, %B binary, % octal), compiler directives, and comments (both `!` and `--` forms).

### Code Folding
Collapse BEGIN/END blocks, PROC bodies, STRUCT definitions. Because some procedures have been growing since 1987 and they're not getting shorter.

## What TAL Looks Like

For the uninitiated:

```tal
?SOURCE SYSLIB
LITERAL max_msg = 4096;

PROC server^main(cmd, cmd^len) MAIN;
  STRING .cmd;
  INT cmd^len;
  BEGIN
    INT status;
    status := 0;

    WHILE status >= 0 DO
    BEGIN
      CALL READUPDATE(recv^file, buffer, 512);
      IF $CARRY THEN
        CALL PROCESS^MSG(buffer, status)
      ELSE
        status := -1;
    END;
  END;
```

Yes, that caret in `server^main` is an identifier character. Yes, `:=` is the assignment operator. Yes, `$CARRY` checks a hardware flag. Welcome to NonStop.

## Installation

### From VSIX
```
code --install-extension tal-lsp-0.2.0.vsix
```

### From Source
```bash
# Build the language server
cd tal-lsp
make

# Build the extension
cd vscode
npm install
npm run compile
npx @vscode/vsce package --allow-missing-repository

# Install
code --install-extension tal-lsp-*.vsix
```

## Configuration

| Setting | Default | Description |
|---------|---------|-------------|
| `tal.serverPath` | (bundled) | Path to `tal-lspd` binary |
| `tal.trace.server` | `off` | Protocol trace level (`off`, `messages`, `verbose`) |

## Architecture

The language server is pure C99 with zero external dependencies. No cJSON, no jansson, no runtime, no garbage collector. The entire binary is 104KB. It hand-rolls its own JSON-RPC transport and JSON parser because the only thing worse than writing your own JSON parser is adding a dependency for one.

Single-threaded, blocking stdin/stdout. The way language servers were meant to be before everyone decided they needed async runtimes and fourteen layers of abstraction.

## What It Doesn't Do (Yet)

- **Cross-file analysis** — `?SOURCE` directives are parsed but not followed. Each file is its own island for now.
- **Find all references** — go-to-definition works, but the reverse doesn't. Yet.
- **Semantic tokens** — highlighting is TextMate-based, not semantic. It can't tell a type from a variable with the same name.
- **Rename symbol** — too dangerous to ship without cross-file support. Renaming half the references is worse than renaming none.

## The Language

TAL was created by Tandem Computers in 1974 for their NonStop architecture. It's a systems programming language — somewhere between C and Pascal, with hardware-level features for fault-tolerant transaction processing.

It has:
- Signed and unsigned arithmetic as separate operators (`+` vs `(+)`)
- Hardware carry/overflow flags accessible as expressions (`$CARRY`)
- Byte-addressable and word-addressable pointers (`.` prefix for byte pointers)
- A MOVE statement that does what memcpy wishes it could
- DEFINE macros terminated by `#` because semicolons were too mainstream
- Case-insensitive keywords but case-preserved identifiers, because NonStop programmers are a free people

NonStop systems process more transactions per day than any other platform on Earth. Every major stock exchange, most ATM networks, and significant chunks of global banking infrastructure run on TAL code that was written by people who are now retired and whose phone numbers are increasingly hard to find.

This LSP exists because those codebases aren't going anywhere, and the people maintaining them deserve better tools than a terminal emulator and a prayer.

## Built From

- TAL Reference Manual (526371-001, September 2003)
- TAL Programmer's Guide (096254, September 1993)
- The quiet desperation of NonStop developers everywhere

## Contact

Built by Zane Hambly. If you're a NonStop developer and this saved you from opening INSPECT at 3am, or if you have feature requests, bug reports, or war stories about TAL codebases older than you are — reach out.

**Email:** zanehambly@gmail.com
**GitHub:** [github.com/Zaneham](https://github.com/Zaneham)

## License

Apache 2.0. Because the financial system runs on this language and we believe the tools should be free.
