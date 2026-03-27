# tal-lsp

Language server for TAL (Transaction Application Language) — the language that runs every stock exchange you've never thought about and every ATM withdrawal you've taken for granted. HP NonStop, née Tandem, since 1974.

## Features

- Diagnostics with unused variable warnings
- Autocomplete — keywords and symbols with type detail
- Hover — type info, declaration site, DEFINE macro expansion
- Go to definition
- Document symbols / outline view
- Syntax highlighting (TextMate grammar)
- Code folding, bracket matching, auto-indent

## Building

```bash
make
```

## VS Code Extension

```bash
cd vscode
npm install && npm run compile
npx @vscode/vsce package --allow-missing-repository
code --install-extension tal-lsp-*.vsix
```

Or grab it from the [Marketplace](https://marketplace.visualstudio.com/items?itemName=ZaneHambly.tal-lsp).

## Testing

```bash
python3 test_lsp.py
```

## What TAL Looks Like

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

## Reference

Built from the TAL Reference Manual (526371-001, September 2003) and the TAL Programmer's Guide (096254, September 1993).

## Contact

**Zane Hambly** — zanehambly@gmail.com — [github.com/Zaneham](https://github.com/Zaneham)

## License

Apache 2.0 — (c) 2026 ZH
