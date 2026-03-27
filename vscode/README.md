# TAL for NonStop

Language server for TAL (Transaction Application Language), the language that has been quietly moving your money since 1974.

Every time you withdraw cash from an ATM, buy shares on a stock exchange, or tap your card at a terminal, there's a decent chance a NonStop system is somewhere in that chain running TAL code written by someone who is now retired. The machines are fault-tolerant. The code is not going anywhere. The developers maintaining it have been navigating with CTRL-F and prayer for fifty years.

This is the first LSP for TAL. Better late than never.

## Features

### Diagnostics
Error messages that say what went wrong in actual words. Unused variable warnings for the dead code nobody dares touch.

### Autocomplete
All 61 TAL keywords plus every symbol in the file — PROCs, variables, LITERALs, DEFINEs — with type and kind information. Start typing, the editor does the rest.

### Hover
Mouse over any identifier. See what it is, what type it is, where it was declared. For DEFINEs, see the full macro expansion. No more grepping through TACL scripts at 3am to figure out what `MSG^BUF` resolves to.

### Go to Definition
Click a name, jump to its declaration. The feature every TAL developer has been simulating with CTRL-F since the EDIT days.

### Document Symbols
Outline view in the sidebar. PROCs, STRUCTs, LITERALs, DEFINEs at a glance. Navigate a TAL file without scrolling through 2000 lines of MOVE statements hoping to spot a procedure boundary by indentation alone.

### Syntax Highlighting
TextMate grammar covering keywords, types, operators, standard functions (`$LEN`, `$CARRY`), numeric literals (`%H` hex, `%B` binary, `%` octal), compiler directives, and both comment forms (`!` and `--`).

## What TAL Looks Like

For the uninitiated:

```tal
?SOURCE SYSLIB
LITERAL max_msg = 4096;
DEFINE check_status(s) = IF s < 0 THEN CALL ABEND #;

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

Yes, that caret in `server^main` is a valid identifier character. Yes, `:=` is assignment. Yes, `$CARRY` checks a hardware flag. Yes, `#` terminates DEFINE bodies because semicolons were already taken. Welcome to NonStop.

## Installation

1. Install from the VS Code marketplace
2. Open any `.tal` file
3. Congratulations, you now have tooling that the financial system should have had twenty years ago

### Manual Setup

```bash
git clone https://github.com/Zaneham/tal-lsp
cd tal-lsp && make
cd vscode && npm install && npm run compile
code --install-extension tal-lsp-*.vsix
```

## Configuration

| Setting | Default | Description |
|---------|---------|-------------|
| `tal.serverPath` | (bundled) | Path to `tal-lspd` binary |
| `tal.trace.server` | `off` | Protocol trace level |

## Reference

Built from the TAL Reference Manual (526371-001, September 2003) and the TAL Programmer's Guide (096254, September 1993).

## Contact

Found a bug? Have war stories about TAL codebases older than you are? Just want to tell someone you still write TAL and have them believe you?

**Zane Hambly** — zanehambly@gmail.com — [github.com/Zaneham](https://github.com/Zaneham)

Based in New Zealand, where it's already tomorrow and the ATMs work just fine.

## License

Apache 2.0 — (c) 2026 ZH
