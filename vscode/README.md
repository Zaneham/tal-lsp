# TAL and TACL for NonStop

A language server for TAL, pTAL and TACL on HPE NonStop.

## What It Does

**TAL and pTAL**

- **Diagnostics** that say what went wrong in words. Unused variable warnings. pTAL mode flags `CODE`, `STACK`, `STORE`, `USE` and `DROP`, TAL mode flags the pTAL-only words.
- **Autocomplete** for keywords, every symbol in the file with its type, the `$` standard functions and all 383 Guardian procedure calls.
- **Hover** for what a thing is, its type and where it was declared. DEFINEs show their expansion. Guardian procedures show the TAL calling syntax from the manual, `! i,o` annotations included.
- **Go to Definition** and **Document Symbols**.

![Hover on WRITEX](https://raw.githubusercontent.com/Zaneham/tal-lsp/main/screenshots/hover-guardian.png)

**TACL**

- **Diagnostics** for unbalanced `[ ]`, a `{` comment with no `}`, a `|label|` outside `#IF`, `#CASE`, `#LOOP` or `#DEF`, `|ELSE|` before `|THEN|`, `|OTHERWISE|` not last, `#LOOP` labels out of order, `?TACL` not on line one, a `~` before a non-metacharacter, lines over 239 characters, a `#name` that isn't a built-in, and `#PUSH`ed variables never used.
- **Autocomplete** that follows context. `#` gives the 229 built-ins, `|` the labels, `?` the directives, `[#ARGUMENT` its alternatives, `?SECTION name` the variable types, and anywhere else the `:UTILS:TACL` commands plus your own variables.
- **Hover** for every built-in and command with its description from the reference manual.
- **Go to Definition** and **Document Symbols** for `?SECTION`s, `#DEF`s and STRUCT items.

![TACL diagnostics](https://raw.githubusercontent.com/Zaneham/tal-lsp/main/screenshots/diagnostics-tacl.png)

**Syntax highlighting** for all three. The TAL and pTAL grammars cover keywords, types, the quoted operators, standard functions, `%H` hex, `%B` binary and `%` octal literals, compiler directives and both comment forms (`!` and `--`). The TACL grammar covers the metacharacters, built-ins, labels, directives and `==` and `{ }` comments.

## TAL

![Peek definition in a $RECEIVE server](https://raw.githubusercontent.com/Zaneham/tal-lsp/main/screenshots/peek-definition.png)

Yes, that caret in `sum^words` is a valid identifier character. Yes, `:=` is assignment. Yes, `IF <` tests the hardware condition code the last call left behind. Yes, `#` terminates DEFINE bodies because semicolons were already taken. Yes, `'+'` with the quotes is unsigned addition, because why have one addition when you can have two.

## pTAL

![pTAL diagnostics](https://raw.githubusercontent.com/Zaneham/tal-lsp/main/screenshots/diagnostics-ptal.png)

pTAL is the portable dialect, the same language with fewer ways to touch the registers.

## TACL

![TACL completion in a library file](https://raw.githubusercontent.com/Zaneham/tal-lsp/main/screenshots/completion-tacl.png)

Square brackets evaluate, `==` starts a comment, vertical bars make labels, a tilde escapes, and every variable is a stack.

## Files Without Extensions

Guardian has no file extensions, so the extension reads the first line. `?TACL` or `?SECTION name TYPE` opens as TACL. A compiler directive, a `!` comment or a `PROC`, `NAME`, `LITERAL`, `INT`, `STRING`, `STRUCT` or `DEFINE` declaration opens as TAL. For anything else, right-click the file and pick **NonStop > Treat as TAL / pTAL / TACL**. The choice is written to the workspace's `files.associations` so it sticks; turn `tal.rememberAssociations` off if you'd rather it didn't. `.tal`, `.ptal` and `.tacl` work too.

![SYSINFO with no extension](https://raw.githubusercontent.com/Zaneham/tal-lsp/main/screenshots/sysinfo.png)

## Vibecoding Ya Switch

The language server is not only for people. Cursor and GitHub Copilot read what it reports, so the model gets the same diagnostics, definitions and hover text you do. I tried this on Cursor with the free credits. Without the LSP, Grok knew nothing about TACL. It skipped the `?TACL ROUTINE` line and threw in C features. With the LSP running it knew what it was doing. Pretty please check with your company's policies on AI before you go chucking an LLM into a NonStop computer. I would like my debit card to work :-)

## Installation

Install from the Marketplace and open a file. The server binary is bundled.

To build it yourself:

```bash
git clone https://github.com/Zaneham/tal-lsp
cd tal-lsp && make
cd vscode && npm install && npm run compile
npx @vscode/vsce package --allow-missing-repository
code --install-extension tal-lsp-*.vsix
```

## Configuration

| Setting | Default | Description |
|---------|---------|-------------|
| `tal.serverPath` | (bundled) | Path to the `tal-lspd` binary |
| `tal.rememberAssociations` | `true` | Write right-click language choices to the workspace's `files.associations` |
| `tal.trace.server` | `off` | Protocol trace level (`off`, `messages`, `verbose`) |

## Reference

Built from the TAL Reference Manual (526371-001), the TAL Programmer's Guide (096254), the pTAL Reference Manual (523746-009), the TACL Reference Manual (429513-017), the TACL Programming Guide (107365) and the Guardian Procedure Calls Reference Manual (522629-030).

## Contact

**Zane Hambly**, my contact information is available at zanehambly.com :-)

## License

Apache 2.0, (c) 2026 ZH
