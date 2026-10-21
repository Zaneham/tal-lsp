# tal-lsp

A language server for TAL, pTAL and TACL, the languages that have been quietly moving your money since 1974.

Every time you withdraw cash from an ATM, buy shares on a stock exchange, or tap your card at a terminal, there's a decent chance a NonStop system is somewhere in that chain running TAL code that was written by someone who is now retired, started from a TACL macro that was written by someone who is now also retired. The machines are fault-tolerant. The code is not going anywhere. The developers maintaining it have been navigating with CTRL-F and prayer for fifty years.

This is the first LSP for TAL, and as far as I can tell the first for TACL as well. Autocomplete, hover, go-to-definition, diagnostics. The things every other language has had for a decade. Better late than never.

## What It Does

**TAL and pTAL**

- **Diagnostics**, errors that say what went wrong in words, not token numbers. Unused variable warnings for the dead code nobody dares touch. In pTAL mode the five statements that didn't survive the port (`CODE`, `STACK`, `STORE`, `USE`, `DROP`) get pointed out, and in TAL mode the pTAL-only words do.
- **Autocomplete** for keywords, every symbol in the file with its type, the standard `$` functions, and the Guardian procedure calls. All 380 of them, `READUPDATEX` to `FILE_OPEN_`, with the TAL calling syntax from the manual, so you can stop keeping the Procedure Calls Reference open in another window.
- **Hover**, what a thing is, what type it is, where it was declared. For DEFINEs, the full macro expansion. For a Guardian procedure, the parameter list with the `! i,o` annotations exactly as HP printed them.
- **Go to Definition**, click a name, jump to where it lives.
- **Document Symbols**, the outline view. PROCs, STRUCTs, LITERALs, DEFINEs at a glance.

**TACL**

- **Diagnostics** for the things TACL itself only tells you about at 3am. Unbalanced `[ ]`, a `{` comment that forgot its `}`, a `|label|` outside the four functions that take one, `|ELSE|` before `|THEN|`, `|OTHERWISE|` not last, a `#LOOP` with its labels in the wrong order, `?TACL` not on line one, a `~` escaping something that isn't a metacharacter, a line over 239 characters, a `#name` that isn't a built-in, and a `#PUSH`ed variable nobody ever reads.
- **Autocomplete** that knows where it is. Type `#` and you get the 229 built-in functions and variables. Type `|` and you get the labels. Type `?` at the start of a line and you get the four directives. Inside `[#ARGUMENT ...]` you get the alternatives (`NUMBER`, `FILENAME`, `KEYWORD`, `END`...), after `?SECTION name` or `#DEF name` you get the seven variable types, and anywhere else you get the `:UTILS:TACL` commands plus everything you've pushed, defined or sectioned in the file.
- **Hover** on any built-in or command for its one-line description from the reference manual, and on your own variables for what they are and where you declared them.
- **Go to Definition** and **Document Symbols** for `?SECTION`s, `#DEF`s and STRUCT items.

## What TAL Looks Like

```tal
?SOURCE $SYSTEM.SYSTEM.EXTDECS0 (READUPDATEX, WRITEREADX)
LITERAL max_msg = 4096;
DEFINE check_status(s) = IF s < 0 THEN CALL ABEND #;
INT .buf[0:max_msg - 1], fnum, len, count := 0;

PROC server^main MAIN;
  BEGIN
    WHILE 1 DO
      BEGIN
        CALL READUPDATEX(fnum, buf, max_msg, len);
        IF > THEN RETURN;
        count := count '+' 1;
        CALL WRITEREADX(fnum, buf, len, max_msg);
      END;
  END;
```

Yes, that caret in `server^main` is a valid identifier character. Yes, `:=` is assignment. Yes, `IF >` is a legal test, it reads the hardware condition code the last call left behind. Yes, `#` terminates DEFINE bodies because semicolons were already taken. Yes, `'+'` with the quotes is unsigned addition, and `'<<'` is an unsigned shift, and `':='` is a block move, because why have one of anything when the apostrophe key is right there.

TAL sits somewhere between C and Pascal with hardware-level features for fault-tolerant transaction processing. It was designed by Tandem Computers for their NonStop architecture, machines built in pairs so that if one dies the other picks up without dropping a single transaction. The language reflects that philosophy: practical, cautious, and deeply suspicious of anything that might crash. pTAL is the portable dialect that came with the move off the original stack machines; same language, fewer ways to touch the registers.

## What TACL Looks Like

```tacl
?TACL ROUTINE
== Day of the week, from the reference manual.
#FRAME
#PUSH days timestamp rslt
#SET rslt [#ARGUMENT /VALUE timestamp/ NUMBER END]
[#CASE [rslt]
  |1| SINK [#ARGUMENT END]
  |2| #SET timestamp [#TIMESTAMP]
]
#SET days [#COMPUTE [timestamp]/(24*60*60*100)]
#RESULT [#CASE [#COMPUTE days - ((days/7) * 7)]
  |0| Tuesday
  |1| Wednesday
  |OTHERWISE| Some other day
]
#UNFRAME
```

TACL is the command language of the NonStop, the thing you type at after you log on, and also a programming language with macros, routines, structs and its own text editor built in as a function. Square brackets mean "evaluate this now". Two equals signs start a comment. Vertical bars make labels. A tilde escapes. Every variable is a stack. It is the shell, the batch language and the glue, and every site has a hundred thousand lines of it that nobody has read since the Clinton administration.

## Files Without Extensions

Guardian doesn't have file extensions. A TAL source is called `TALSRC`, a TACL macro is called `PURGEM`, and the only way to tell them apart is to look inside. So the extension looks inside.

- A file whose first line is `?TACL MACRO`, `?TACL ROUTINE` or `?SECTION name TYPE` opens as TACL.
- A file whose first line is a `?SOURCE`, `?SECTION name` or similar compiler directive, a `!` comment banner, or a `PROC`, `NAME`, `LITERAL`, `INT`, `STRING`, `STRUCT` or `DEFINE` declaration opens as TAL.
- Anything else, or anything it got wrong, you fix with a right click. The context menu in the editor and the explorer has a **NonStop** submenu with **Treat as TAL**, **Treat as pTAL** and **Treat as TACL**. Picking one changes the language and writes the file name into the workspace's `files.associations`, so the next time your SFTP sync pulls `TALSRC` down it already knows. Turn `tal.rememberAssociations` off if you'd rather it didn't.

`.tal`, `.ptal` and `.tacl` work too, for people who have escaped.

## Building

```bash
make
```

GCC with C99 support. That's the dependency list. The Guardian procedure table and the TACL built-in tables are generated from the manuals and checked in as C, so the build never needs the PDFs.

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

Drives the server over stdio the way an editor would, across TAL, pTAL and TACL documents, and checks what comes back.

## Reference

Built from the TAL Reference Manual (526371-001, September 2003), the TAL Programmer's Guide (096254, September 1993), the pTAL Reference Manual (523746-009, February 2012), the TACL Reference Manual (429513-017, August 2013), the TACL Programming Guide (107365, December 1994) and the Guardian Procedure Calls Reference Manual (522629-030). If you can find copies, they're genuinely well-written technical documents. HP knew how to write a manual, and Tandem before them knew it better.

## Related Projects

If you've enjoyed providing tooling for financial infrastructure that could, at any moment, be processing something terribly important, you might also like:

- **[talc](https://github.com/Zaneham/talc)**, a compiler for TAL. Because once you've written the parser twice you may as well generate code with it.
- **[Skyhawk](https://github.com/Zaneham/Skyhawk)**, JOVIAL J73 compiler. The language that flies fighter jets, compiled from scratch in C99.
- **[BarraCUDA](https://github.com/Zaneham/BarraCUDA)**, CUDA compiler targeting AMD, NVIDIA, and Tenstorrent GPUs. What happens when you look at NVIDIA's walled garden and think "how hard can it be?"
- **[JOVIAL LSP](https://github.com/Zaneham/Jovial-LSP)**, language server for F-15 avionics code. Same idea, different era of computing.

## Contact

**Zane Hambly**, zanehambly@gmail.com, [github.com/Zaneham](https://github.com/Zaneham)

Based in New Zealand, where it's already tomorrow and the ATMs work just fine.

## License

Apache 2.0, (c) 2026 ZH
