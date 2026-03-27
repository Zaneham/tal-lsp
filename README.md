# tal-lsp

A language server for TAL (Transaction Application Language), the language that has been quietly moving your money since 1974.

Every time you withdraw cash from an ATM, buy shares on a stock exchange, or tap your card at a terminal, there's a decent chance a NonStop system is somewhere in that chain running TAL code that was written by someone who is now retired. The machines are fault-tolerant. The code is not going anywhere. The developers maintaining it have been navigating with CTRL-F and prayer for fifty years.

This is the first LSP for TAL. Autocomplete, hover, go-to-definition, diagnostics — the things every other language has had for a decade. Better late than never.

## What It Does

- **Diagnostics** — errors that say what went wrong in words, not token numbers. Unused variable warnings for the dead code nobody dares touch.
- **Autocomplete** — keywords and every symbol in the file, with types.
- **Hover** — what a thing is, what type it is, where it was declared. For DEFINEs, the full macro expansion, because grepping through TACL scripts at 3am builds character but not productivity.
- **Go to Definition** — click a name, jump to where it lives.
- **Document Symbols** — the outline view. PROCs, STRUCTs, LITERALs, DEFINEs at a glance.

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

Yes, that caret in `server^main` is a valid identifier character. Yes, `:=` is assignment. Yes, `$CARRY` is a hardware carry flag you can use as an expression. Yes, `#` terminates DEFINE bodies because semicolons were already taken. Yes, `(+)` and `(-)` are unsigned arithmetic operators because why have one addition when you can have two.

TAL sits somewhere between C and Pascal with hardware-level features for fault-tolerant transaction processing. It was designed by Tandem Computers for their NonStop architecture — machines built in pairs so that if one dies the other picks up without dropping a single transaction. The language reflects that philosophy: practical, cautious, and deeply suspicious of anything that might crash.

## Building

```bash
make
```

GCC with C99 support. That's the dependency list.

## VS Code Extension

```bash
cd vscode
npm install && npm run compile
npx @vscode/vsce package --allow-missing-repository
code --install-extension tal-lsp-*.vsix
```

Or install from the [Marketplace](https://marketplace.visualstudio.com/items?itemName=ZaneHambly.tal-lsp).

## Testing

```bash
python3 test_lsp.py
```

## Reference

Built from the TAL Reference Manual (526371-001, September 2003) and the TAL Programmer's Guide (096254, September 1993). If you can find copies, they're genuinely well-written technical documents. HP knew how to write a manual.

## Related Projects

If you've enjoyed providing tooling for financial infrastructure that could, at any moment, be processing something terribly important, you might also like:

- **[Skyhawk](https://github.com/Zaneham/Skyhawk)** — JOVIAL J73 compiler. The language that flies fighter jets, compiled from scratch in C99.
- **[BarraCUDA](https://github.com/Zaneham/BarraCUDA)** — CUDA compiler targeting AMD, NVIDIA, and Tenstorrent GPUs. What happens when you look at NVIDIA's walled garden and think "how hard can it be?"
- **[JOVIAL LSP](https://github.com/Zaneham/Jovial-LSP)** — Language server for F-15 avionics code. Same idea, different era of computing.

## Contact

**Zane Hambly** — zanehambly@gmail.com — [github.com/Zaneham](https://github.com/Zaneham)

Based in New Zealand, where it's already tomorrow and the ATMs work just fine.

## License

Apache 2.0 — (c) 2026 ZH
