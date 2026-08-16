#!/usr/bin/env python3
"""One-shot converter: GameDesc.lev + GameData/WRF/*.wrf -> GameData/datasets.json.

The output is a faithful merge of the two legacy formats, per
Docs/AssetPipeline.md section 6.2: every WRF becomes a named unit whose
entries keep their original order, directory strings and type strings; every
.lev dataset becomes a dataset record whose slots reference units (or name
the .gam scenario file) in declaration order.  The in-game loader replays a
unit as the same resLoadFile calls the WRF parser used to make, so the
conversion is behaviour-preserving by construction.

Deliberately mirrored .lev parser behaviour:
  - data/game paths are lowercased (levParse called resToLower on them);
  - a camchange dataset shares its name with the camstart it modifies;
  - slot order is load order, and the game slot's index matters.

Run from the repository root:  python tools/convert_manifests.py
The output is deterministic, so re-running produces an identical file.
"""

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMEDATA = os.path.join(ROOT, 'GameData')
OUT = os.path.join(GAMEDATA, 'datasets.json')

LEV_KINDS = {
    'level': 'level', 'campaign': 'campaign', 'camstart': 'camstart',
    'camchange': 'camchange', 'expand': 'expand', 'between': 'between',
    'miss_keep': 'miss_keep', 'miss_clear': 'miss_clear',
    'expand_limbo': 'expand_limbo', 'miss_keep_limbo': 'miss_keep_limbo',
}


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), text, flags=re.S)
    text = re.sub(r'//[^\n]*', '', text)
    return text


def unit_name(path):
    """wrf\\Cam1\\Cam1A.wrf -> wrf/cam1/cam1a (the manifest unit key)."""
    name = path.replace('\\', '/').lower()
    if name.endswith('.wrf'):
        name = name[:-4]
    return name


def parse_lev():
    text = strip_comments(open(os.path.join(GAMEDATA, 'GameDesc.lev'), encoding='latin-1').read())
    tokens = re.findall(r'"[^"]*"|\S+', text)
    datasets = []
    cur = None
    pending = None  # keyword waiting for its value
    for tok in tokens:
        if tok in LEV_KINDS:
            cur = {'name': None, 'kind': LEV_KINDS[tok], 'slots': []}
            datasets.append(cur)
            pending = 'name'
        elif tok in ('players', 'type', 'dataset', 'data', 'game'):
            pending = tok
        elif tok.startswith('"'):
            value = tok[1:-1].lower()
            if pending == 'data':
                cur['slots'].append({'unit': unit_name(value)})
            elif pending == 'game':
                cur['slots'].append({'game': value.replace('\\', '/')})
            else:
                raise SystemExit(f'unexpected string {tok} after {pending}')
            pending = 'wait'
        elif pending == 'name':
            cur['name'] = tok
            pending = 'wait'
        elif pending == 'dataset':
            cur['base'] = tok
            pending = 'wait'
        elif pending == 'players':
            cur['players'] = int(tok)
            pending = 'wait'
        elif pending == 'type':
            cur['kind'] = 'multi'
            cur['type'] = int(tok)
            pending = 'wait'
        else:
            raise SystemExit(f'unexpected token {tok} (after {pending})')
    return datasets


def parse_wrfs():
    units = {}
    statement = re.compile(r'(directory|file)\s+(?:([A-Za-z][-\w]*)\s+)?"([^"]*)"')
    for dirpath, _dirnames, filenames in os.walk(os.path.join(GAMEDATA, 'WRF')):
        for f in sorted(filenames):
            if not f.lower().endswith('.wrf'):
                continue
            path = os.path.join(dirpath, f)
            rel = os.path.relpath(path, GAMEDATA)
            entries = []
            curdir = ''
            for kind, typ, name in statement.findall(
                    strip_comments(open(path, encoding='latin-1').read())):
                if kind == 'directory':
                    curdir = name
                elif not typ:
                    raise SystemExit(f'{rel}: file entry with no type: "{name}"')
                else:
                    entries.append({'d': curdir, 't': typ, 'f': name})
            units[unit_name(rel)] = entries
    return units


def main():
    datasets = parse_lev()
    units = parse_wrfs()

    missing = sorted({s['unit'] for d in datasets for s in d['slots'] if 'unit' in s} - set(units))
    if missing:
        raise SystemExit(f'datasets reference units with no .wrf on disk: {missing}')

    doc = {
        'units': {k: units[k] for k in sorted(units)},
        'datasets': datasets,
    }
    with open(OUT, 'w', newline='\n') as f:
        json.dump(doc, f, indent=1, sort_keys=False)
        f.write('\n')
    entries = sum(len(v) for v in units.values())
    print(f'{OUT}: {len(units)} units ({entries} entries), {len(datasets)} datasets')
    return 0


if __name__ == '__main__':
    sys.exit(main())
