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
