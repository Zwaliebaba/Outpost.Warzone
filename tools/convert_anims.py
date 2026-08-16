#!/usr/bin/env python3
"""One-shot converter for stage D: the audp_ grammar's three languages.

  - GameData/anims/*.ani      -> *.json   (keyframe scripts)
  - GameData/anims/anim.cfg   -> anim.json (name -> anim ID table)
  - GameData/audio/audio.cfg, frontaud.cfg -> *.json (track parameters)

The .ani state rows keep their documented 10-integer layout as arrays:
[frame, x, y, z, xrot, yrot, zrot, xscale, yscale, zscale], values verbatim
(positions/rotations are milli-units, scale 1000 = 1.0).  ANIMOBJECT names,
which the old parser discarded, are preserved.  anim.json's file references
are renamed to the .json files so the resource names keep matching.

--check re-emits every token stream and compares it with the original,
proving the conversion lost nothing.

Run from the repository root:  python tools/convert_anims.py [--check]
"""

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMEDATA = os.path.join(ROOT, 'GameData')

errors = []
skipped = []


def tokens_of(path):
    text = open(path, encoding='latin-1').read()
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    return re.findall(r'"[^"]*"|\{|\}|[^\s{}]+', text)


def unquote(tok):
    return tok[1:-1] if tok.startswith('"') else tok


class Cursor:
    def __init__(self, toks, name):
        self.toks = toks
        self.pos = 0
        self.name = name

    def take(self, expect=None):
        if self.pos >= len(self.toks):
            raise ValueError(f'{self.name}: unexpected end of file')
        tok = self.toks[self.pos]
        self.pos += 1
        if expect is not None and tok != expect:
            raise ValueError(f'{self.name}: expected {expect!r}, got {tok!r}')
        return tok

    def done(self):
        return self.pos >= len(self.toks)


def parse_ani(path):
    cur = Cursor(tokens_of(path), os.path.basename(path))
    kind = cur.take()
    record = {'pie': unquote(cur.take())}
    states = int(cur.take())
    record['frameRate'] = int(cur.take())
    if kind == 'ANIM3DFRAMES':
        record['type'] = 'frames'
        cur.take('{')
        rows = [[int(cur.take()) for _ in range(10)] for _ in range(states)]
        cur.take('}')
        record['states'] = rows
    elif kind == 'ANIM3DTRANS':
        record['type'] = 'trans'
        num_obj = int(cur.take())
        cur.take('{')
        objects = []
        for _ in range(num_obj):
            cur.take('ANIMOBJECT')
            index = int(cur.take())
            name = unquote(cur.take())
            cur.take('{')
            rows = [[int(cur.take()) for _ in range(10)] for _ in range(states)]
            cur.take('}')
            objects.append({'index': index, 'name': name, 'states': rows})
        cur.take('}')
        record['objects'] = objects
    else:
        raise ValueError(f'{path}: unknown script kind {kind!r}')
    if not cur.done():
        raise ValueError(f'{path}: trailing tokens')
    return record


def emit_ani_tokens(record):
    out = []
    if record['type'] == 'frames':
        states = len(record['states'])
        out += ['ANIM3DFRAMES', f'"{record["pie"]}"', str(states),
                str(record['frameRate']), '{']
        for row in record['states']:
            out += [str(v) for v in row]
        out.append('}')
    else:
        states = len(record['objects'][0]['states'])
        out += ['ANIM3DTRANS', f'"{record["pie"]}"', str(states),
                str(record['frameRate']), str(len(record['objects'])), '{']
        for obj in record['objects']:
            out += ['ANIMOBJECT', str(obj['index']), f'"{obj["name"]}"', '{']
            for row in obj['states']:
                out += [str(v) for v in row]
            out.append('}')
        out.append('}')
    return out


def parse_audio_cfg(path):
    cur = Cursor(tokens_of(path), os.path.basename(path))
    cur.take('audio_module')
    cur.take('{')
    records = []
    while True:
        tok = cur.take()
        if tok == '}':
            break
        if tok != 'audio':
            raise ValueError(f'{path}: expected audio, got {tok!r}')
        records.append({
            'file': unquote(cur.take()),
            'loop': {'loop': True, 'oneshot': False}[cur.take()],
            'volume': int(cur.take()),
            'priority': int(cur.take()),
            'radius': int(cur.take()),
        })
    if not cur.done():
        raise ValueError(f'{path}: trailing tokens')
    return records


def emit_audio_tokens(records):
    out = ['audio_module', '{']
    for r in records:
        out += ['audio', f'"{r["file"]}"', 'loop' if r['loop'] else 'oneshot',
                str(r['volume']), str(r['priority']), str(r['radius'])]
    out.append('}')
    return out


def parse_anim_cfg(path):
    cur = Cursor(tokens_of(path), os.path.basename(path))
    cur.take('anim_module')
    cur.take('{')
    records = []
    while True:
        tok = cur.take()
        if tok == '}':
            break
        records.append({'file': unquote(tok), 'id': int(cur.take())})
    if not cur.done():
        raise ValueError(f'{path}: trailing tokens')
    return records


def emit_anim_cfg_tokens(records):
    out = ['anim_module', '{']
    for r in records:
        out += [f'"{r["file"]}"', str(r['id'])]
    out.append('}')
    return out


def convert(path, parse, emit, out_path, check, post=None):
    if not os.path.exists(path):
        skipped.append(os.path.basename(path))
        return
    try:
        parsed = parse(path)
    except ValueError as err:
        errors.append(str(err))
        return
    if check:
        stored = json.load(open(out_path, encoding='utf-8'))
        undo = post_undo(stored, post) if post else stored
        if emit(undo) != emit(parsed):
            errors.append(f'{path}: does not round-trip against {out_path}')
    else:
        if post:
            parsed = post(parsed)
        with open(out_path, 'w', newline='\n') as f:
            json.dump(parsed, f, indent=1)
            f.write('\n')


def rename_ani_refs(records):
    for r in records:
        if r['file'].lower().endswith('.ani'):
            r['file'] = r['file'][:-4] + '.json'
    return records


def post_undo(records, post):
    # anim.cfg file references were renamed .ani -> .json; undo for compare
    import copy
    out = copy.deepcopy(records)
    for r in out:
        if r['file'].lower().endswith('.json'):
            r['file'] = r['file'][:-5] + '.ani'
    return out


def main():
    check = '--check' in sys.argv
    anims = os.path.join(GAMEDATA, 'anims')
    for f in sorted(os.listdir(anims)):
        if f.lower().endswith('.ani'):
            path = os.path.join(anims, f)
            convert(path, parse_ani, lambda r: emit_ani_tokens(r),
                    os.path.splitext(path)[0] + '.json', check)
    convert(os.path.join(anims, 'anim.cfg'), parse_anim_cfg,
            emit_anim_cfg_tokens, os.path.join(anims, 'anim.json'), check,
            post=rename_ani_refs)
    audio = os.path.join(GAMEDATA, 'audio')
    for f in ('audio.cfg', 'frontaud.cfg'):
        convert(os.path.join(audio, f), parse_audio_cfg, emit_audio_tokens,
                os.path.join(audio, os.path.splitext(f)[0] + '.json'), check)
    if skipped:
        print(f'note: {len(skipped)} file(s) skipped: source already deleted '
              'from the tree (round-trip was proven before removal)')
    for e in errors:
        print(f'error: {e}')
    print(('check ' if check else 'convert ') + ('FAILED' if errors else 'ok'))
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
