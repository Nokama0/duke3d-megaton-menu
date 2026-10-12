# Splits src/duke3dmod/test_host.cpp (once one main() of ~18,000 lines) into one translation unit per area with
# identical behaviour: the same blocks run in the same order over the same globals and the pass count does not move.

# Usage: python tools/split_tests.py --analyze (structure only, writes nothing) | --write (emit tests/, rewrite
# test_host.cpp) | --table (assignment table from BASE) | --verify (the replay check). --analyze reads the working tree;
# --table and --verify read the original out of git at BASE, so they still work after the split.

# THE REPLAY CHECK is the acceptance gate: it re-derives from the ORIGINAL test_host.cpp at BASE what the split should
# contain (main()'s body cut into units, in order, documented rewrites applied) and compares it with the emitted files
# re-read from disk, not the emitter's bookkeeping. It prints REPRODUCED or the first differing line.

# Unit rule: main() holds 67 brace blocks AND 213 loose statements in 13 CONTIGUOUS RUNS (the config and ui/fov groups
# are written flat). A brace block is one unit and so is a maximal run of consecutive loose statements; order and
# grouping are untouched.

# Hoisted fixtures: nine variables (tmp, f, r, cur, a, b, p, q, s) cross units. Accessors would RECOMPUTE the panel
# rects per use instead of keeping main's single evaluation, and cannot handle an inner `p` shadowing main's. File-scope
# variables assigned at the original position are exact (only the declaration's type is dropped).

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src', 'duke3dmod', 'test_host.cpp')
OUTDIR = os.path.join(ROOT, 'src', 'duke3dmod', 'tests')
BASE = '8865703'


# A brace/string/comment aware view of the source.

def is_continuation(text, i, end):
    """A backslash that ends its line -- CRLF included -- splices the next one."""
    nl = text.find('\n', i, end)
    if nl < 0:
        return False
    return text[i + 1:nl].strip() == ''


def strip_code(text):
    """text with comments, strings and char literals blanked (same length, so
    every offset stays valid) -- everything a brace scan must not see."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            while i < n and text[i] != '\n':
                # a line comment can be continued with a trailing backslash
                if text[i] == '\\' and is_continuation(text, i, n):
                    nl = text.index('\n', i)
                    for k in range(i, nl):
                        out[k] = ' '
                    i = nl + 1
                    continue
                out[i] = ' '
                i += 1
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            out[i] = out[i + 1] = ' '
            i += 2
            while i < n and not (text[i] == '*' and i + 1 < n and text[i + 1] == '/'):
                if text[i] != '\n':
                    out[i] = ' '
                i += 1
            if i < n:
                out[i] = out[i + 1] = ' '
                i += 2
        elif c in '"\'':
            q = c
            out[i] = ' '
            i += 1
            while i < n:
                if text[i] == '\\':
                    out[i] = ' '
                    if i + 1 < n and text[i + 1] != '\n':
                        out[i + 1] = ' '
                    i += 2
                    continue
                if text[i] == q:
                    out[i] = ' '
                    i += 1
                    break
                if text[i] != '\n':
                    out[i] = ' '
                i += 1
        else:
            i += 1
    return ''.join(out)


def line_of(text, off):
    return text.count('\n', 0, off) + 1


def find_main(code):
    m = re.search(r'^int main\(\)\s*\{', code, re.M)
    if not m:
        raise SystemExit('no int main() found')
    open_brace = code.index('{', m.start())
    depth = 0
    for i in range(open_brace, len(code)):
        if code[i] == '{':
            depth += 1
        elif code[i] == '}':
            depth -= 1
            if depth == 0:
                return m.start(), open_brace, i
    raise SystemExit('unbalanced main()')


def split_statements(code, start, end):
    """[start, end) cut into top-level statements. A statement ends at a ';' at
    depth 0, or just after the '}' that returns depth to 0 when no ';' follows
    (a function or type definition). Returns [[kind, s, e], ...] with kind
    'block' for a statement that begins with '{', 'pp' for a preprocessor
    line, 'stmt' otherwise."""
    stmts = []
    i = start
    while i < end:
        while i < end and code[i].isspace():
            i += 1
        if i >= end:
            break
        s = i
        if code[i] == '#':
            while i < end:
                if code[i] == '\\' and is_continuation(code, i, end):
                    i = code.index('\n', i) + 1
                    continue
                if code[i] == '\n':
                    break
                i += 1
            stmts.append(['pp', s, i])
            continue
        kind = 'block' if code[i] == '{' else 'stmt'
        depth = 0
        saw_brace = False
        while i < end:
            c = code[i]
            if c in '([':
                depth += 1
            elif c in ')]':
                depth -= 1
            elif c == '{':
                depth += 1
                saw_brace = True
            elif c == '}':
                depth -= 1
                if depth == 0:
                    i += 1
                    j = i
                    while j < end and code[j].isspace():
                        j += 1
                    if j < end and code[j] == ';':
                        i = j + 1
                    break
            elif c == ';' and depth == 0:
                i += 1
                break
            i += 1
        else:
            raise SystemExit('unterminated statement at offset %d' % s)
        if saw_brace and kind == 'stmt' and code[s:i].lstrip().startswith('{'):
            kind = 'block'
        stmts.append([kind, s, i])
    return stmts


def spanned(stmts, region_start):
    """Give every statement the whole text from the previous statement's end,
    so no comment and no blank line between statements can be dropped."""
    out = []
    prev = region_start
    for k, s, e in stmts:
        out.append({'kind': k, 'span': prev, 'code': s, 'end': e})
        prev = e
    return out


# main(): units.

def build_units(text, code):
    _, brace, close = find_main(code)
    body_s, body_e = brace + 1, close
    stmts = spanned(split_statements(code, body_s, body_e), body_s)
    units, run = [], []
    for st in stmts:
        if st['kind'] == 'block':
            if run:
                units.append({'kind': 'run', 'stmts': run})
                run = []
            units.append({'kind': 'block', 'stmts': [st]})
        else:
            run.append(st)
    if run:
        units.append({'kind': 'run', 'stmts': run})
    for u in units:
        u['span'] = u['stmts'][0]['span']
        u['code'] = u['stmts'][0]['code']
        u['end'] = u['stmts'][-1]['end']
    return units, body_s, body_e


SLUG_STOP = set('''the a an of to for and or is are it its that this with in on at
by as from be no not any all one two each per its'''.split())


def slug_for(text, unit, used):
    lead = text[unit['span']:unit['code']]
    words = []
    for line in lead.splitlines():
        t = line.strip()
        if not t.startswith('//'):
            continue
        t = t[2:].strip(' -=*')
        if not t:
            continue
        words = re.findall(r'[A-Za-z0-9_]+', t)
        break
    if not words:
        words = re.findall(r'[A-Za-z0-9_]+', ' '.join(text[unit['code']:unit['end']].split())[:80])
    keep = [w.lower() for w in words if w.lower() not in SLUG_STOP][:5]
    if not keep:
        keep = [w.lower() for w in words[:5]]
    s = '_'.join(keep) or 'block'
    if not s[0].isalpha():
        s = 'b_' + s
    n, base = 2, s
    while s in used:
        s = '%s_%d' % (base, n)
        n += 1
    used.add(s)
    return s


AREAS = [
    ('platform', ['log', 'init', 'deferred', 'gamecfg', 'install', 'fontpack', 'atlas']),
    ('freecam', ['camera', 'freecam', 'nearsect', 'nocull', 'teleport', 'clamp']),
    ('target', ['target', 'aim', 'silent', 'accuracy', 'lead', 'hitscan', 'shoot']),
    ('esp', ['esp', 'project', 'secret', 'item', 'explodable', 'switch', 'name',
             'label', 'marker']),
    ('menu', ['menu', 'panel', 'row', 'tab', 'slider', 'capture', 'bind', 'sweep',
              'scale', 'rail']),
    ('config', ['config', 'ini', 'cfg', 'migrate']),
]


def classify(text, unit):
    head = text[unit['span']:unit['code']].lower()
    body = text[unit['code']:unit['end']].lower()
    for area, keys in AREAS:
        for k in keys:
            if re.search(r'\b%s' % re.escape(k), head):
                return area, 'header:' + k
    for area, keys in AREAS:
        for k in keys:
            if re.search(r'\b%s' % re.escape(k), body):
                return area, 'body:' + k
    return 'misc', '-'


# The hoisted main-scope fixtures.

DECL_RE = re.compile(
    r'^(\s*(?:const\s+|static\s+|unsigned\s+|signed\s+|struct\s+)*'
    r'[A-Za-z_][A-Za-z0-9_:<>,\s\*&]*?\s+\*?\s*)([A-Za-z_][A-Za-z0-9_]*)\s*(?==|\[)')

NOT_A_TYPE = set('''if else for while do switch case return break continue goto
CHECK sizeof new delete true false nullptr'''.split())


def find_hoists(text, code, units):
    """Declarations made at main scope in one unit and named in another. Both
    ways of being wrong here are safe: a name hoisted that did not need to be
    is simply shadowed by the local declaration that already existed, and a
    name missed is an undeclared-identifier error at compile time."""
    hoists = []
    for idx, u in enumerate(units):
        if u['kind'] != 'run':
            continue
        for st in u['stmts']:
            stmt = code[st['code']:st['end']]
            if stmt.lstrip().split('(')[0].strip() in NOT_A_TYPE:
                continue
            m = DECL_RE.match(stmt)
            if not m:
                continue
            name = m.group(2)
            prefix = m.group(1)
            if prefix.split()[0] in NOT_A_TYPE or name in NOT_A_TYPE:
                continue
            pat = re.compile(r'\b%s\b' % re.escape(name))
            if not any(pat.search(code[v['code']:v['end']])
                       for j, v in enumerate(units) if j != idx):
                continue
            decl = '%s %s' % (' '.join(prefix.split()), name)
            if decl.startswith('const ') and '*' not in decl:
                raise SystemExit(
                    'hoisting a top-level const (%s) would need the const dropped; '
                    'classify it by hand' % decl)
            hoists.append({'name': name, 'decl': decl, 'unit': idx,
                           'stmt_start': st['code'], 'name_at': st['code'] + m.start(2),
                           'line': line_of(text, st['code'])})
    return hoists


def apply_hoists(text, hoists):
    """The one text transform: a hoisted declaration loses its type and becomes
    an assignment to the file-scope variable. Applied to the ORIGINAL text, so
    the replay check compares transformed-original against emitted."""
    out = text
    for h in sorted(hoists, key=lambda h: h['stmt_start'], reverse=True):
        out = out[:h['stmt_start']] + out[h['name_at']:]
    return out


def shift(off, hoists):
    """An original offset, in the coordinates of the transformed text."""
    d = 0
    for h in hoists:
        if h['name_at'] <= off:
            d += h['name_at'] - h['stmt_start']
    return off - d


# The prelude.

def build_prelude(text, code, main_start, main_body):
    """Every top-level entity above main(), classified into what belongs in
    test_common.h and what belongs in test_common.cpp."""
    ents = spanned(split_statements(code, 0, main_start), 0)
    for e in ents:
        c = code[e['code']:e['end']]
        e['text'] = text[e['span']:e['end']]
        e['decl'] = text[e['code']:e['end']]
        e['lead'] = text[e['span']:e['code']]
        if e['kind'] == 'pp':
            e['what'] = 'pp'
            continue
        brace = c.find('{')
        head = c[:brace] if brace >= 0 else c
        if re.match(r'^\s*(struct|enum|union|class)\b', c):
            e['what'] = 'type'
        elif brace >= 0 and '(' in head and re.search(r'\)\s*$', head.strip()):
            e['what'] = 'func'
            e['sig'] = re.sub(r'^\s*static\s+', '', text[e['code']:e['code'] + brace]).rstrip()
            e['body'] = re.sub(r'^\s*static\s+', '', text[e['code']:e['end']])
            e['name'] = re.findall(r'([A-Za-z_][A-Za-z0-9_]*)\s*\(', head)[-1]
        else:
            e['what'] = 'var'
            e['names'] = re.findall(r'([A-Za-z_][A-Za-z0-9_]*)\s*(?==|\[|,|;)', c)
            e['static'] = bool(re.match(r'^\s*static\b', c))
            # Only a const OBJECT may be copied into the header, where internal linkage gives every TU an identical
            # value. `const void* q_stub_last_addr` is a MUTABLE pointer to const: copied per TU, the stub's write lands
            # in test_common.cpp's copy while the reader sees its own null. A '*' or '&' in the declaration tells which.
            e['const'] = (bool(re.search(r'\bconst\b', head))
                          and '*' not in head and '&' not in head)
            e['used_in_main'] = any(re.search(r'\b%s\b' % re.escape(n), main_body)
                                    for n in e['names'])
    return ents


def reroute_includes(chunk):
    """tests/ is one level below the headers it uses."""
    return re.sub(r'(#\s*include\s+")(?!\.\./)', r'\1../', chunk)


# Emit.

BANNER = '''// GENERATED by tools/split_tests.py from the single main() that
// src/duke3dmod/test_host.cpp used to be. Do not re-run the splitter over the
// split: it reads one 18,000-line main(). To ADD a check, append a tb_ function
// to the area file it belongs in, declare it in test_blocks.h, and call it from
// main() in test_host.cpp at the position that keeps the order rule -- after
// whatever it depends on, which is normally the end.
'''


def plan():
    with open(SRC, 'r', encoding='utf-8', newline='') as fh:
        text = fh.read()
    code = strip_code(text)
    main_start, brace, close = find_main(code)
    units, body_s, body_e = build_units(text, code)

    # the tail -- the summary print and main's return -- stays in main()
    tail = None
    for i, u in enumerate(units):
        if re.search(r'\breturn\b', code[u['code']:u['end']]):
            if i != len(units) - 1:
                raise SystemExit('a `return` at main scope in unit %d, not the last' % i)
            tail = units.pop(i)
            break
    if tail is None:
        raise SystemExit('no trailing return found in main()')

    hoists = find_hoists(text, code, units)
    used = set()
    for i, u in enumerate(units):
        u['n'] = i + 1
        u['slug'] = slug_for(text, u, used)
        u['fn'] = 'tb_%04d_%s' % (u['n'], u['slug'])
        u['area'], u['why'] = classify(text, u)
    prelude = build_prelude(text, code, main_start, code[body_s:body_e])
    return text, code, units, tail, hoists, prelude, body_s, body_e


def emit():
    with open(SRC, 'r', encoding='utf-8') as fh:
        if 'test_blocks.h' in fh.read():
            raise SystemExit(
                'test_host.cpp is already the split call list. The splitter reads the '
                'ONE BIG main(); re-running it over its own output would swallow the '
                'call list as the new prelude. Restore test_host.cpp from %s first.'
                % BASE)
    text, code, units, tail, hoists, prelude, body_s, body_e = plan()
    tx = apply_hoists(text, hoists)

    if not os.path.isdir(OUTDIR):
        os.makedirs(OUTDIR)

    # ---- test_common.h / test_common.cpp -----------------------------------
    hdr = ['#pragma once', BANNER.rstrip(), '',
           '// The suite\'s shared prelude: the includes every area file needs, the',
           '// CHECK macro and its counters, the fixtures main() used to declare in its',
           '// own body, and every helper the old single file kept above main().', '']
    cpp = ['#include "test_common.h"', '',
           '// The prelude\'s definitions. Everything here was `static` in the old',
           '// single-file suite; it keeps the same behaviour with external linkage so',
           '// the area files can share one copy of the state it holds.', '']
    moved_to_common = []
    for e in prelude:
        c = code[e['code']:e['end']]
        if e['what'] == 'pp':
            if re.match(r'^\s*#\s*include', c):
                hdr.append(reroute_includes(e['text'].strip('\n')))
            else:
                hdr.append(e['text'].strip('\n'))
        elif e['what'] == 'type':
            hdr.append(e['text'].strip('\n'))
        elif e['what'] == 'func':
            hdr.append((e['lead'] + e['sig'] + ';').strip('\n'))
            cpp.append(e['body'].strip('\n'))
            moved_to_common.append(e['name'])
        elif e['what'] == 'var':
            if e['names'] == ['passed', 'failed']:
                hdr.append((e['lead'] + 'extern int passed, failed;').strip('\n'))
                continue
            if not e['used_in_main']:
                cpp.append(e['text'].strip('\n'))          # stays file-static
            elif e['const']:
                hdr.append(e['text'].strip('\n'))          # internal linkage, one value
            else:
                base = re.sub(r'^\s*static\s+', '', e['decl'])
                hdr.append((e['lead'] + 'extern ' + re.sub(r'\s*=.*;\s*$', ';', base,
                                                           flags=re.S)).strip('\n'))
                cpp.append(base.strip('\n'))
                moved_to_common.append(e['names'][0])
    hdr.append('')
    hdr.append('// Fixtures main() used to declare in its own body. They are assigned at')
    hdr.append('// the same point in the order as before, so every use sees the value it')
    hdr.append('// saw then; a block that declares its own shadows these, as it did then.')
    for h in hoists:
        hdr.append('extern %s;' % h['decl'])
        cpp.append('%s;' % h['decl'])
    moved_to_common.extend(h['name'] for h in hoists)

    write(os.path.join(OUTDIR, 'test_common.h'), '\n'.join(hdr) + '\n')
    write(os.path.join(OUTDIR, 'test_common.cpp'), '\n'.join(cpp) + '\n')

    # ---- test_blocks.h ------------------------------------------------------
    bl = ['#pragma once', BANNER.rstrip(), '',
          '// Every block of the suite, in the order main() runs them.', '']
    for u in units:
        bl.append('void %s();' % u['fn'])
    write(os.path.join(OUTDIR, 'test_blocks.h'), '\n'.join(bl) + '\n')

    # ---- the area files -----------------------------------------------------
    areas = {}
    for u in units:
        lead = tx[shift(u['span'], hoists):shift(u['code'], hoists)]
        body = tx[shift(u['code'], hoists):shift(u['end'], hoists)]
        lead = '\n'.join(l[2:] if l.startswith('  ') else l for l in lead.split('\n'))
        if u['kind'] == 'block':
            fn = '%svoid %s() %s' % (lead.lstrip('\n'), u['fn'], body)
        else:
            fn = '%svoid %s() {\n%s\n}' % (lead.lstrip('\n'), u['fn'], body)
        areas.setdefault(u['area'], []).append(fn)
    for area, fns in sorted(areas.items()):
        out = ['#include "test_common.h"', '#include "test_blocks.h"', '', BANNER.rstrip(), '']
        out.extend(fns)
        write(os.path.join(OUTDIR, 'test_%s.cpp' % area), '\n\n'.join(out) + '\n')

    # ---- test_host.cpp ------------------------------------------------------
    host = ['''// The Nuke Panel pure-function suite: one translation unit per area.
//
// This file is the ORDER. Every check lives in a tb_ function in one of the
// tests/test_<area>.cpp files, declared in tests/test_blocks.h; main() calls
// them in exactly the order the one big main() used to run them in, over the
// same globals, so a block that leaves g_cfg or the menu state a certain way
// still hands that state to the block that followed it.
//
// TO ADD A CHECK: append a tb_ function to the area file it belongs in, declare
// it in tests/test_blocks.h, and add the call below at the position that keeps
// the order rule -- after whatever it depends on, which is normally the end.
//
// Shared fixtures, helpers and the CHECK macro are in tests/test_common.h.
// tools/split_tests.py generated this layout and can replay the split.

#include "tests/test_common.h"
#include "tests/test_blocks.h"

int passed = 0, failed = 0;

int main() {''']
    for u in units:
        host.append('  %s();' % u['fn'])
    host.append(tx[shift(tail['span'], hoists):shift(tail['end'], hoists)].strip('\n'))
    host.append('}')
    write(SRC, '\n'.join(host) + '\n')

    print('%d units -> %d area files' % (len(units), len(areas)))
    for area in sorted(areas):
        print('  test_%-10s %d blocks' % (area + '.cpp', len(areas[area])))
    print('hoisted fixtures: %s' % ', '.join('%s (%s, L%d)' % (h['name'], h['decl'], h['line'])
                                             for h in hoists))
    print('helpers moved to test_common: %d' % len(moved_to_common))
    return units, hoists, moved_to_common


def write(path, content):
    with open(path, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write(content)


# The replay check.

def norm(s):
    return re.sub(r'\s+', ' ', s).strip()


def verify():
    orig = subprocess.run(['git', 'show', '%s:src/duke3dmod/test_host.cpp' % BASE],
                          cwd=ROOT, capture_output=True, text=True, check=True).stdout
    code = strip_code(orig)
    main_start, brace, close = find_main(code)
    units, body_s, body_e = build_units(orig, code)
    tail = units.pop()
    hoists = find_hoists(orig, code, units)
    used = set()
    for i, u in enumerate(units):
        u['n'] = i + 1
        u['slug'] = slug_for(orig, u, used)
        u['fn'] = 'tb_%04d_%s' % (u['n'], u['slug'])
    tx = apply_hoists(orig, hoists)

    # what main() must call, in order
    with open(SRC, 'r', encoding='utf-8', newline='') as fh:
        host = fh.read()
    calls = re.findall(r'^\s*(tb_\d{4}_\w+)\(\);', host, re.M)
    want = [u['fn'] for u in units]
    if calls != want:
        for i, (a, b) in enumerate(zip(calls + [None] * len(want), want + [None] * len(calls))):
            if a != b:
                print('NOT REPRODUCED: main() call %d is %s, expected %s' % (i + 1, a, b))
                return 1
        print('NOT REPRODUCED: main() calls %d blocks, expected %d' % (len(calls), len(want)))
        return 1

    # every block's body, read back off disk, against the transformed original
    bodies = {}
    for fn in sorted(os.listdir(OUTDIR)):
        if not fn.startswith('test_') or not fn.endswith('.cpp') or fn == 'test_common.cpp':
            continue
        with open(os.path.join(OUTDIR, fn), 'r', encoding='utf-8', newline='') as fh:
            t = fh.read()
        c = strip_code(t)
        for m in re.finditer(r'^void (tb_\d{4}_\w+)\(\)\s*\{', c, re.M):
            ob = c.index('{', m.start())
            d = 0
            for i in range(ob, len(c)):
                if c[i] == '{':
                    d += 1
                elif c[i] == '}':
                    d -= 1
                    if d == 0:
                        bodies[m.group(1)] = t[ob + 1:i]
                        break
    missing = [u['fn'] for u in units if u['fn'] not in bodies]
    if missing:
        print('NOT REPRODUCED: %d block functions not found on disk, first %s'
              % (len(missing), missing[0]))
        return 1

    for u in units:
        want_body = tx[shift(u['code'], hoists):shift(u['end'], hoists)]
        if u['kind'] == 'block':
            want_body = want_body.strip()
            want_body = want_body[1:-1]         # its own outer braces are the function's
        got = bodies[u['fn']]
        if norm(want_body) != norm(got):
            a = norm(want_body).split(' ')
            b = norm(got).split(' ')
            for i in range(max(len(a), len(b))):
                x = a[i] if i < len(a) else '<end>'
                y = b[i] if i < len(b) else '<end>'
                if x != y:
                    print('NOT REPRODUCED: %s differs at token %d: original %r, emitted %r'
                          % (u['fn'], i, ' '.join(a[max(0, i - 6):i + 6]),
                             ' '.join(b[max(0, i - 6):i + 6])))
                    return 1
            print('NOT REPRODUCED: %s differs' % u['fn'])
            return 1

    # and the whole body, concatenated, against the transformed original
    joined = norm(''.join(
        tx[shift(u['span'], hoists):shift(u['code'], hoists)] +
        tx[shift(u['code'], hoists):shift(u['end'], hoists)] for u in units))
    whole = norm(tx[shift(body_s, hoists):shift(tail['span'], hoists)])
    if joined != whole:
        print('NOT REPRODUCED: the units do not cover main()\'s body')
        return 1

    print('REPRODUCED: %d units, %d CHECK( in the original main body, %d hoisted fixtures (%s)'
          % (len(units), orig[brace:close].count('CHECK('), len(hoists),
             ', '.join(h['name'] for h in hoists)))
    return 0


def table():
    """The assignment table, re-derived from the ORIGINAL at BASE -- so it can
    still be regenerated after the split has replaced test_host.cpp."""
    orig = subprocess.run(['git', 'show', '%s:src/duke3dmod/test_host.cpp' % BASE],
                          cwd=ROOT, capture_output=True, text=True, check=True).stdout
    code = strip_code(orig)
    units, body_s, body_e = build_units(orig, code)
    units.pop()
    used = set()
    print('| # | orig line | kind | file | function | matched |')
    print('|---|-----------|------|------|----------|---------|')
    for i, u in enumerate(units):
        fn = 'tb_%04d_%s' % (i + 1, slug_for(orig, u, used))
        area, why = classify(orig, u)
        print('| %d | %d | %s | test_%s.cpp | `%s` | %s |'
              % (i + 1, line_of(orig, u['code']), u['kind'], area, fn, why))


def analyze():
    text, code, units, tail, hoists, prelude, body_s, body_e = plan()
    print('%d units (%d brace blocks, %d runs of loose statements)'
          % (len(units), sum(1 for u in units if u['kind'] == 'block'),
             sum(1 for u in units if u['kind'] == 'run')))
    print('main body lines %d..%d; tail kept in main(): %s'
          % (line_of(text, body_s), line_of(text, body_e),
             ' '.join(text[tail['code']:tail['end']].split())))
    print()
    print('%-4s %-6s %-6s %-11s %-46s %s' % ('#', 'line', 'kind', 'file', 'function', 'why'))
    for u in units:
        print('%-4d %-6d %-6s %-11s %-46s %s'
              % (u['n'], line_of(text, u['code']), u['kind'], u['area'], u['fn'], u['why']))
    print()
    print('hoisted fixtures:')
    for h in hoists:
        print('  %-6s %-14s declared L%d in unit %d' % (h['name'], h['decl'], h['line'], h['unit'] + 1))
    print()
    counts = {}
    for u in units:
        counts[u['area']] = counts.get(u['area'], 0) + 1
    print('per area: %s' % counts)
    kinds = {}
    for e in prelude:
        kinds[e['what']] = kinds.get(e['what'], 0) + 1
    print('prelude entities: %s' % kinds)


if __name__ == '__main__':
    if '--analyze' in sys.argv:
        analyze()
    elif '--write' in sys.argv:
        emit()
    elif '--table' in sys.argv:
        table()
    elif '--verify' in sys.argv:
        sys.exit(verify())
    else:
        print('usage: split_tests.py --analyze | --write | --table | --verify')
