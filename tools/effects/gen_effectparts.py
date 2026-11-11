# Converts data/EffectTable.json (roBrowser style, JS object syntax) to data/effectparts.json:
# { "<effect id>": [ {type, file, ...simple params...}, ... ] }.  FUNC parts and anything with code are skipped.
import re, json, sys

src = open('data/EffectTable.json', encoding='latin-1').read()

def strip_comments(t):
    out = []; i = 0; q = None
    while i < len(t):
        c = t[i]
        if q:
            out.append(c)
            if c == '\\': out.append(t[i+1]); i += 1
            elif c == q: q = None
        elif c in '\'"': q = c; out.append(c)
        elif t.startswith('//', i):
            while i < len(t) and t[i] != '\n': i += 1
            continue
        elif t.startswith('/*', i):
            i = t.find('*/', i) + 2; continue
        else: out.append(c)
        i += 1
    return ''.join(out)

def dec(s): return re.sub(r'\\x([0-9a-fA-F]{2})', lambda m: chr(int(m.group(1), 16)), s)

def split_top(s, sep=','):
    parts = []; depth = 0; q = None; cur = []
    i = 0
    while i < len(s):
        c = s[i]
        if q:
            cur.append(c)
            if c == '\\': cur.append(s[i+1]); i += 1
            elif c == q: q = None
        elif c in '\'"': q = c; cur.append(c)
        elif c in '{[(': depth += 1; cur.append(c)
        elif c in '}])': depth -= 1; cur.append(c)
        elif c == sep and depth == 0: parts.append(''.join(cur)); cur = []
        else: cur.append(c)
        i += 1
    if ''.join(cur).strip(): parts.append(''.join(cur))
    return parts

def parse_val(v):
    v = v.strip()
    if v in ('true', 'false'): return v == 'true'
    m = re.fullmatch(r"'((?:[^'\\]|\\.)*)'", v) or re.fullmatch(r'"((?:[^"\\]|\\.)*)"', v)
    if m: return dec(m.group(1))
    try: return float(v) if ('.' in v or 'e' in v.lower()) else int(v)
    except ValueError: pass
    if v.startswith('[') and v.endswith(']'):
        items = [parse_val(x) for x in split_top(v[1:-1])]
        if all(isinstance(x, (int, float)) for x in items): return items
    return None

def parse_obj(body):
    d = {}
    for piece in split_top(body):
        m = re.match(r'\s*([A-Za-z_][A-Za-z0-9_]*)\s*:(.*)$', piece, re.S)
        if not m: continue
        v = parse_val(m.group(2))
        if v is not None: d[m.group(1)] = v
    return d

text = strip_comments(src)
out = {}
for m in re.finditer(r'^\s{8}(-?\d+)\s*:\s*\[', text, re.M):
    eid = m.group(1)
    i = m.end(); depth = 1
    while depth and i < len(text):
        c = text[i]
        if c in '[{': depth += 1
        elif c in ']}': depth -= 1
        i += 1
    body = text[m.end():i-1]
    parts = []
    for obj in split_top(body):
        obj = obj.strip()
        if not (obj.startswith('{') and obj.endswith('}')): continue
        d = parse_obj(obj[1:-1])
        if d.get('type') in ('STR', 'SPR', '2D', '3D', 'CYLINDER') and (d.get('file') or d.get('textureName') or d.get('type') == '3D'):
            parts.append(d)
    if parts: out[eid] = parts
json.dump(out, open('data/effectparts.json', 'w'), separators=(',', ':'))
from collections import Counter
print(len(out), 'effects,', sum(len(v) for v in out.values()), 'parts', Counter(p['type'] for v in out.values() for p in v))
print(out.get('47'), out.get('10'), str(out.get('1'))[:300])
