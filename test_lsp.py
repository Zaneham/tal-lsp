#!/usr/bin/env python3
"""End-to-end LSP test harness for tal-lspd."""

import subprocess, json, sys

proc = subprocess.Popen(
    ['./tal-lspd.exe'],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE
)
fails = []

def send(msg):
    data = json.dumps(msg).encode()
    header = f'Content-Length: {len(data)}\r\n\r\n'.encode()
    proc.stdin.write(header + data)
    proc.stdin.flush()

def recv():
    line = b''
    while True:
        c = proc.stdout.read(1)
        if not c: return None
        line += c
        if line.endswith(b'\r\n\r\n'): break
    length = int(line.split(b':')[1].strip().split(b'\r')[0])
    body = proc.stdout.read(length)
    return json.loads(body)

def check(name, cond):
    status = 'PASS' if cond else 'FAIL'
    if not cond: fails.append(name)
    print(f'  [{status}] {name}')

# ---- INIT ----
send({'jsonrpc':'2.0','id':1,'method':'initialize','params':{'capabilities':{},'rootUri':None}})
r = recv()
caps = r['result']['capabilities']
check('init response has capabilities', 'textDocumentSync' in caps)
check('completion provider registered', 'completionProvider' in caps)
check('hover provider registered', caps.get('hoverProvider') == True)
check('definition provider registered', caps.get('definitionProvider') == True)
check('document symbol provider registered', caps.get('documentSymbolProvider') == True)

send({'jsonrpc':'2.0','method':'initialized','params':{}})

# ---- OPEN ----
tal_src = (
    "?SOURCE SYSLIB\n"
    "! Global configuration\n"
    "LITERAL max_buf = 4096;\n"
    "LITERAL debug_mode = 0;\n"
    "DEFINE check_overflow(val) = $CARRY OR $OVERFLOW #;\n"
    "DEFINE response_buf = .resp_area #;\n"
    "\n"
    "INT .resp_area[0:255];\n"
    "\n"
    "PROC server_loop(port, timeout) MAIN;\n"
    "  INT port;\n"
    "  INT timeout;\n"
    "  INT unused_flag;\n"
    "  BEGIN\n"
    "    INT req_count;\n"
    "    INT status;\n"
    "    req_count := 0;\n"
    "\n"
    "    WHILE req_count < max_buf DO\n"
    "    BEGIN\n"
    "      status := req_count + 1;\n"
    "      CALL PROCESS_REQ(port, req_count);\n"
    "      req_count := req_count + 1;\n"
    "    END;\n"
    "  END;\n"
    "\n"
    "PROC helper(x);\n"
    "  INT x;\n"
    "  BEGIN\n"
    "    RETURN x + 1;\n"
    "  END;\n"
)

send({'jsonrpc':'2.0','method':'textDocument/didOpen','params':{
    'textDocument':{'uri':'file:///server.tal','languageId':'tal','version':1,'text':tal_src}
}})
diag = recv()
diags = diag['params']['diagnostics']
print()
print('--- Diagnostics ---')
for d in diags:
    sev = {1:'ERR',2:'WARN',3:'INFO'}.get(d['severity'],'?')
    print(f"  L{d['range']['start']['line']+1} [{sev}] {d['message']}")

msgs = [d['message'] for d in diags]
check('unused_flag warned', any('unused_flag' in m for m in msgs))
check('used vars NOT warned (req_count)', not any('req_count' in m for m in msgs))
check('used vars NOT warned (port)', not any("'port'" in m for m in msgs))
check('used vars NOT warned (status)', not any("'status'" in m for m in msgs))

# ---- COMPLETION ----
print()
print('--- Completion ---')
send({'jsonrpc':'2.0','id':10,'method':'textDocument/completion','params':{
    'textDocument':{'uri':'file:///server.tal'},
    'position':{'line':15,'character':0}
}})
comp = recv()
items = comp['result']
labels = [i['label'] for i in items]
check('keywords present (PROC)', 'PROC' in labels)
check('keywords present (WHILE)', 'WHILE' in labels)
check('symbol: server_loop', 'server_loop' in labels)
check('symbol: helper', 'helper' in labels)
check('symbol: max_buf', 'max_buf' in labels)
check('symbol: check_overflow', 'check_overflow' in labels)
check('symbol: response_buf', 'response_buf' in labels)

detail_map = {i['label']: i.get('detail','') for i in items}
check('max_buf detail is literal INT', 'literal' in detail_map.get('max_buf','') and 'INT' in detail_map.get('max_buf',''))
check('server_loop detail is proc', detail_map.get('server_loop','') == 'proc')
check('check_overflow detail is define', 'define' in detail_map.get('check_overflow',''))

# ---- HOVER ----
print()
print('--- Hover ---')

# server_loop on line 10 (0-based 9)
send({'jsonrpc':'2.0','id':20,'method':'textDocument/hover','params':{
    'textDocument':{'uri':'file:///server.tal'},
    'position':{'line':9,'character':6}
}})
h = recv()
check('hover: server_loop shows PROC', h.get('result') and 'PROC' in h['result']['contents']['value'])

# max_buf usage at line 19 (0-based 18)
send({'jsonrpc':'2.0','id':21,'method':'textDocument/hover','params':{
    'textDocument':{'uri':'file:///server.tal'},
    'position':{'line':18,'character':24}
}})
h = recv()
check('hover: max_buf shows literal', h.get('result') and 'literal' in h['result']['contents']['value'])

# DEFINE check_overflow at line 5 (0-based 4)
send({'jsonrpc':'2.0','id':22,'method':'textDocument/hover','params':{
    'textDocument':{'uri':'file:///server.tal'},
    'position':{'line':4,'character':10}
}})
h = recv()
hval = h['result']['contents']['value'] if h.get('result') else ''
check('hover: DEFINE shows body with #', 'DEFINE' in hval and '#' in hval)
if hval:
    print(f'    -> {hval.split(chr(10))[1]}')

# port param (line 11, 0-based 10)
send({'jsonrpc':'2.0','id':23,'method':'textDocument/hover','params':{
    'textDocument':{'uri':'file:///server.tal'},
    'position':{'line':10,'character':6}
}})
h = recv()
hval = h['result']['contents']['value'] if h.get('result') else ''
check('hover: port param shows INT (type merge)', 'INT' in hval)

# ---- GO TO DEFINITION ----
print()
print('--- Go to Definition ---')

# req_count usage at line 23 (0-based 22)
send({'jsonrpc':'2.0','id':30,'method':'textDocument/definition','params':{
    'textDocument':{'uri':'file:///server.tal'},
    'position':{'line':22,'character':6}
}})
defn = recv()
check('goto-def: req_count resolves', defn.get('result') is not None)
if defn.get('result'):
    dl = defn['result']['range']['start']['line']
    check('goto-def: points to declaration (line 15)', dl == 14)

# max_buf usage
send({'jsonrpc':'2.0','id':31,'method':'textDocument/definition','params':{
    'textDocument':{'uri':'file:///server.tal'},
    'position':{'line':18,'character':24}
}})
defn = recv()
check('goto-def: max_buf resolves', defn.get('result') is not None)
if defn.get('result'):
    dl = defn['result']['range']['start']['line']
    check('goto-def: max_buf at line 3', dl == 2)

# ---- DOCUMENT SYMBOLS ----
print()
print('--- Document Symbols ---')
send({'jsonrpc':'2.0','id':40,'method':'textDocument/documentSymbol','params':{
    'textDocument':{'uri':'file:///server.tal'}
}})
syms = recv()
sym_names = [s['name'] for s in syms['result']]
print(f'  symbols: {sym_names}')
check('outline: server_loop', 'server_loop' in sym_names)
check('outline: helper', 'helper' in sym_names)
check('outline: max_buf', 'max_buf' in sym_names)
check('outline: debug_mode', 'debug_mode' in sym_names)
check('outline: check_overflow', 'check_overflow' in sym_names)
check('outline: resp_area', 'resp_area' in sym_names)
check('outline: no locals (req_count excluded)', 'req_count' not in sym_names)
check('outline: no params (port excluded)', 'port' not in sym_names)

# ---- LIVE EDITING ----
print()
print('--- Live Edit ---')
send({'jsonrpc':'2.0','method':'textDocument/didChange','params':{
    'textDocument':{'uri':'file:///server.tal','version':2},
    'contentChanges':[{'text':'PROC broken(\n'}]
}})
diag2 = recv()
errs = diag2['params']['diagnostics']
check('broken code produces errors', len(errs) > 0)
check('errors are human-readable', all('expected' in d['message'] for d in errs))
for d in errs:
    print(f"  -> {d['message']}")

# fix it
send({'jsonrpc':'2.0','method':'textDocument/didChange','params':{
    'textDocument':{'uri':'file:///server.tal','version':3},
    'contentChanges':[{'text':'PROC ok; BEGIN END;'}]
}})
diag3 = recv()
check('fixed code clears errors', len(diag3['params']['diagnostics']) == 0)

# ---- OPERATORS AND GUARDIAN ----
print()
print('--- Operators and Guardian procedures ---')
tal2 = (
    "?SOURCE $SYSTEM.SYSTEM.EXTDECS0 (READUPDATEX, FILE_OPEN_)\n"
    "LITERAL max_msg = 4096;\n"
    "INT .buf[0:max_msg - 1];\n"
    "INT fnum, err, len, count := 0;\n"
    "STRING ro = 'P' := \"hello\";\n"
    "PROC main^loop MAIN;\n"
    "  BEGIN\n"
    "    err := FILE_OPEN_(\"$RECEIVE\":8, fnum);\n"
    "    CALL READUPDATEX(fnum, buf, max_msg, len);\n"
    "    count := count '+' 1 '<<' 2;\n"
    "    len := $LEN(buf) '\\' 2;\n"
    "    CODE(NOP);\n"
    "  END;\n"
)
send({'jsonrpc':'2.0','method':'textDocument/didOpen','params':{
    'textDocument':{'uri':'file:///ops.tal','languageId':'tal','version':1,'text':tal2}
}})
d = recv()['params']['diagnostics']
for x in d: print(f"  L{x['range']['start']['line']+1} {x['message']}")
check("quoted operators '+' '<<' '\\' parse clean", not any(x['severity'] == 1 for x in d))
check('name:length parameter pair parses', not any("got ':'" in x['message'] for x in d))
send({'jsonrpc':'2.0','id':60,'method':'textDocument/hover','params':{
    'textDocument':{'uri':'file:///ops.tal'},'position':{'line':8,'character':10}}})
h = recv(); hv = h['result']['contents']['value'] if h.get('result') else ''
check('hover: READUPDATEX shows Guardian syntax', 'READUPDATEX' in hv and '! i' in hv)
send({'jsonrpc':'2.0','id':61,'method':'textDocument/hover','params':{
    'textDocument':{'uri':'file:///ops.tal'},'position':{'line':10,'character':12}}})
h = recv(); hv = h['result']['contents']['value'] if h.get('result') else ''
check('hover: $LEN shows standard function', '$LEN' in hv)
send({'jsonrpc':'2.0','id':62,'method':'textDocument/completion','params':{
    'textDocument':{'uri':'file:///ops.tal'},'position':{'line':8,'character':9}}})
labels = [i['label'] for i in recv()['result']]
check('completion: Guardian procedures offered', 'WRITEREADX' in labels and 'FILE_OPEN_' in labels)
check('completion: $ functions offered', '$LEN' in labels)
check('completion: pTAL words not offered for TAL', 'WADDR' not in labels)
check('completion: four names from one INT declaration', all(n in labels for n in ('fnum', 'err', 'len', 'count')))

# ---- pTAL ----
print()
print('--- pTAL ---')
send({'jsonrpc':'2.0','method':'textDocument/didOpen','params':{
    'textDocument':{'uri':'file:///ops.ptal','languageId':'ptal','version':1,'text':tal2}
}})
d = recv()['params']['diagnostics']
check('pTAL: CODE flagged as not available', any('CODE' in x['message'] and x['severity'] == 3 for x in d))
send({'jsonrpc':'2.0','id':63,'method':'textDocument/completion','params':{
    'textDocument':{'uri':'file:///ops.ptal'},'position':{'line':8,'character':9}}})
labels = [i['label'] for i in recv()['result']]
check('pTAL: WADDR offered', 'WADDR' in labels)

# ---- TACL ----
print()
print('--- TACL ---')
tacl_src = (
    "?TACL ROUTINE\n"
    "== day of the week, from the reference manual\n"
    "#FRAME\n"
    "#PUSH days timestamp rslt unused^one\n"
    "#SET rslt [#ARGUMENT /VALUE timestamp/ NUMBER END]\n"
    "[#CASE [rslt]\n"
    "|1| SINK [#ARGUMENT END]\n"
    "|2| #SET timestamp [#TIMESTAMP]\n"
    "|OTHERWISE| #OUTPUT Invalid argument\n"
    "]\n"
    "#SET days [#COMPUTE [timestamp]/(24*60*60*100) ]\n"
    "[#IF days > 3 |THEN|\n"
    "  #OUTPUT Late: [days] { a brace comment }\n"
    "|ELSE|\n"
    "  #OUTPUT Early: [days]\n"
    "]\n"
    "[#LOOP |WHILE| days < 7 |DO| #SET days [#COMPUTE days + 1] ]\n"
    "FILENAMES $SYSTEM.SYSTEM.TACL*\n"
    "#OUTPUT \"a string with ~[ brackets\" ~; #OUTPUT second\n"
    "#BOGUS this is not real\n"
    "[#IF days |ELSE| #OUTPUT bad order |THEN| ]\n"
    "[#OUTPUT this line never closes\n"
    "#UNFRAME\n"
)
send({'jsonrpc':'2.0','method':'textDocument/didOpen','params':{
    'textDocument':{'uri':'file:///dow','languageId':'tacl','version':1,'text':tacl_src}
}})
d = recv()['params']['diagnostics']
for x in d: print(f"  L{x['range']['start']['line']+1} {x['message']}")
msgs = [x['message'] for x in d]
check('tacl: unknown built-in flagged', any('not a TACL built-in' in m for m in msgs))
check('tacl: |ELSE| before |THEN| flagged', any('|ELSE| before |THEN|' in m for m in msgs))
check('tacl: unclosed [ flagged', any('never closed' in m for m in msgs))
check('tacl: pushed but unused flagged', any('unused^one' in m for m in msgs))
check('tacl: used variables not flagged', not any("'days'" in m or "'rslt'" in m for m in msgs))
check('tacl: no noise from ~[ ~; { } or ==', len(d) == 4)

send({'jsonrpc':'2.0','id':70,'method':'textDocument/hover','params':{
    'textDocument':{'uri':'file:///dow'},'position':{'line':3,'character':2}}})
h = recv(); hv = h['result']['contents']['value'] if h.get('result') else ''
check('tacl hover: #PUSH described', 'Creates new top level' in hv)
send({'jsonrpc':'2.0','id':71,'method':'textDocument/hover','params':{
    'textDocument':{'uri':'file:///dow'},'position':{'line':4,'character':6}}})
h = recv(); hv = h['result']['contents']['value'] if h.get('result') else ''
check('tacl hover: rslt is a pushed variable at line 4', 'pushed variable' in hv and 'line 4' in hv)
send({'jsonrpc':'2.0','id':72,'method':'textDocument/definition','params':{
    'textDocument':{'uri':'file:///dow'},'position':{'line':10,'character':6}}})
r = recv().get('result')
check('tacl goto-def: days -> line 4', r is not None and r['range']['start']['line'] == 3)

def labels_at(line, char):
    send({'jsonrpc':'2.0','id':73,'method':'textDocument/completion','params':{
        'textDocument':{'uri':'file:///dow'},'position':{'line':line,'character':char}}})
    return [i['label'] for i in recv()['result']]
l = labels_at(3, 2)
check('tacl completion after #: built-ins only', '#OUTPUT' in l and 'FILENAMES' not in l)
l = labels_at(11, 16)
check('tacl completion after |: labels', '|THEN|' in l and len(l) == 7)
l = labels_at(4, 40)
check('tacl completion in #ARGUMENT: alternatives', 'FILENAME' in l and 'NUMBER' in l)
l = labels_at(0, 2)
check('tacl completion after ?: directives', '?SECTION' in l and len(l) == 4)
l = labels_at(0, 6)
check('tacl completion after ?TACL: types', 'ROUTINE' in l and len(l) == 7)
l = labels_at(17, 1)
check('tacl completion at a command: commands, built-ins and variables', 'FILENAMES' in l and '#SET' in l and 'days' in l)

lib = (
    "?SECTION fn MACRO\n"
    "FILENAMES %1%\n"
    "?SECTION arrays STRUCT\n"
    "  BEGIN\n"
    "     INT array^1 (1:100);\n"
    "     CHAR array^4 (0:16) VALUE \"This is a \"\"VALUE\"\"\";\n"
    "  END;\n"
    "?SECTION s ALIAS\n"
    "STATUS\n"
)
send({'jsonrpc':'2.0','method':'textDocument/didChange','params':{
    'textDocument':{'uri':'file:///dow','version':2},'contentChanges':[{'text':lib}]}})
d = recv()['params']['diagnostics']
for x in d: print(f"  L{x['range']['start']['line']+1} {x['message']}")
check('tacl library: clean', len(d) == 0)
send({'jsonrpc':'2.0','id':75,'method':'textDocument/documentSymbol','params':{'textDocument':{'uri':'file:///dow'}}})
syms = {s['name']: s.get('detail') for s in recv()['result']}
check('tacl outline: ?SECTION fn MACRO', syms.get('fn') == 'MACRO')
check('tacl outline: ?SECTION arrays STRUCT', syms.get('arrays') == 'STRUCT')
check('tacl outline: ?SECTION s ALIAS', syms.get('s') == 'ALIAS')
check('tacl outline: struct items stay out of the outline', 'array^1' not in syms)
send({'jsonrpc':'2.0','id':76,'method':'textDocument/hover','params':{
    'textDocument':{'uri':'file:///dow'},'position':{'line':4,'character':10}}})
h = recv(); hv = h['result']['contents']['value'] if h.get('result') else ''
check('tacl hover: STRUCT item', 'STRUCT item' in hv)

for u in ('file:///ops.tal', 'file:///ops.ptal', 'file:///dow'):
    send({'jsonrpc':'2.0','method':'textDocument/didClose','params':{'textDocument':{'uri':u}}})
    recv()

# ---- CLOSE ----
send({'jsonrpc':'2.0','method':'textDocument/didClose','params':{
    'textDocument':{'uri':'file:///server.tal'}
}})
clear = recv()
check('close clears diagnostics', len(clear['params']['diagnostics']) == 0)

# ---- SHUTDOWN ----
send({'jsonrpc':'2.0','id':99,'method':'shutdown'})
recv()
send({'jsonrpc':'2.0','method':'exit'})
proc.stdin.close()
rc = proc.wait()
check('clean exit code 0', rc == 0)

# ---- SUMMARY ----
print()
total_checks = len(fails) + sum(1 for _ in open(__file__) if 'check(' in _) - 1  # rough
if not fails:
    print(f'ALL CHECKS PASSED')
else:
    print(f'{len(fails)} FAILED:')
    for f in fails:
        print(f'  - {f}')
    sys.exit(1)
