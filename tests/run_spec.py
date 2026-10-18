"""Runs the CommonMark spec examples against build/render_cli.exe and reports the pass rate per section.

Usage: python run_spec.py [spec.json] [--fails SECTION]
spec.json: https://spec.commonmark.org/0.31.2/spec.json
"""
import sys
sys.stdout.reconfigure(encoding='utf-8')
import json, subprocess, sys, os, re, collections

here = os.path.dirname(os.path.abspath(__file__))
spec = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith('--') else os.path.join(here, 'spec.json')
show = sys.argv[sys.argv.index('--fails') + 1] if '--fails' in sys.argv else None
exe = os.path.join(here, '..', 'build', 'render_cli.exe')

cases = json.load(open(spec, encoding='utf-8'))
data = b'\0'.join(c['markdown'].encode('utf-8') for c in cases)
out = subprocess.run([exe], input=data, capture_output=True, timeout=120).stdout.split(b'\0')

def norm(h):
    h = h.replace('\r', '')
    h = re.sub(r'\s*\n\s*', '\n', h).strip()
    # whitespace around block-level tags is not significant
    h = re.sub(r'\n?(</?(?:li|ul|ol|p|blockquote|h[1-6]|hr|table|thead|tbody|tr|td|th)\b[^>]*>)\n?', r'\1', h)
    h = re.sub(r'\n(?=</li>)', '', h)
    h = re.sub(r'<(br|hr|img[^>]*?)\s*/>', r'<\1>', h)   # <br /> == <br>
    return h

stats = collections.defaultdict(lambda: [0, 0])
fails = []
for c, o in zip(cases, out):
    ok = norm(o.decode('utf-8', 'replace')) == norm(c['html'])
    stats[c['section']][0] += ok
    stats[c['section']][1] += 1
    if not ok:
        fails.append((c, o.decode('utf-8', 'replace')))

tp = sum(v[0] for v in stats.values()); tt = sum(v[1] for v in stats.values())
for k, (p, t) in stats.items():
    print(f'{p:4}/{t:<4} {k}')
print(f'\nTOTAL {tp}/{tt} = {100 * tp / tt:.1f}%')
if show:
    for c, o in fails:
        if show.lower() in c['section'].lower():
            print(f"\n--- example {c['example']} ({c['section']})\nmd:   {c['markdown']!r}\nwant: {c['html']!r}\ngot:  {o!r}")
