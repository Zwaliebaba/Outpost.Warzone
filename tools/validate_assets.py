#!/usr/bin/env python3
"""Validate GameData/datasets.json against the filesystem and the code.

Checks, in order:
  1. datasets.json parses, and every file a unit lists exists on disk.
  2. Every unit entry's type string is registered in Outpost/Data.cpp.
  3. Every dataset's unit references resolve, its .gam exists, its base
     dataset and camchange partner are declared before it, and it has at
     most LEVEL_MAXFILES slots.
  4. Duplicate (type, filename) entries within one unit are reported.
  5. The audio tree indexes the way AudioSystem::Init indexes it: no two
     WAVs may share a bare filename, and every name audio.json or
     AudioID.cpp spells must be findable in it.
  6. The texturePages groups resolve: their files exist, each page offers a
     variant for either translucency setting, every TEXSET names a real
     group, and every page id a shipped .pie names is bound by one.

Resolution is case-insensitive, because that is what the game does on NTFS.
A name that only resolves case-insensitively is a warning; a name that does
not resolve at all is an error.  Exit code is non-zero if any error was
found.

Run from the repository root:  python tools/validate_assets.py
"""

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMEDATA = os.path.join(ROOT, 'GameData')
LEVEL_MAXFILES = 9

errors = []
warnings = []

# Missing files the owner has not yet ruled on.  These are reported as
# warnings, not errors, so CI stays green while the decision is open.
# SUB_3_3 / SUB_3_3S: the mission's .vlo files were never in the tree, its
# brief is commented out, and no mission .slo exists - half-built content.
KNOWN_MISSING = {
    'script\\data\\cam3-3.vlo',
    'script\\data\\cam3-3s.vlo',
}


def resolve(rel):
    """Resolve rel (with \\ or / separators) under GameData, the way the game
    would: exact first, then case-insensitively per component.
    Returns (resolved_relpath or None, case_matched_exactly)."""
    parts = [p for p in re.split(r'[\\/]', rel) if p]
    cur = GAMEDATA
    exact = True
    out = []
    for part in parts:
        cand = os.path.join(cur, part)
        if os.path.exists(cand):
            out.append(part)
            cur = cand
            continue
        try:
            entries = os.listdir(cur)
        except (FileNotFoundError, NotADirectoryError):
            return None, False
        match = next((e for e in entries if e.lower() == part.lower()), None)
        if match is None:
            return None, False
        exact = False
        out.append(match)
        cur = os.path.join(cur, match)
    return '/'.join(out), exact


def registered_types():
    """The resource types the game registers, read from Outpost/Data.cpp."""
    src = open(os.path.join(ROOT, 'Outpost', 'Data.cpp'), encoding='latin-1').read()
    m = re.search(r'ResourceTypes\[\]\s*=\s*\{(.*?)\n\};', src, flags=re.S)
    if not m:
        errors.append('Outpost/Data.cpp: could not find the ResourceTypes[] table')
        return set()
    body = re.sub(r'/\*.*?\*/', '', m.group(1), flags=re.S)
    body = re.sub(r'//[^\n]*', '', body)
    return set(re.findall(r'\{\s*"([A-Z_0-9]+)"', body))


DATASET_KINDS = {
    'level', 'campaign', 'camstart', 'camchange', 'expand', 'between',
    'miss_keep', 'miss_clear', 'expand_limbo', 'miss_keep_limbo', 'multi',
}


def check_units(doc, types):
    units = doc.get('units')
    if not isinstance(units, dict):
        errors.append('datasets.json: no unit table')
        return {}
    for name, entries in units.items():
        if not isinstance(entries, list):
            errors.append(f'unit {name}: not a list')
            continue
        seen = set()
        for entry in entries:
            d, t, f = entry.get('d'), entry.get('t'), entry.get('f')
            if not isinstance(t, str) or not isinstance(f, str) or not isinstance(d, str):
                errors.append(f'unit {name}: malformed entry {entry}')
                continue
            # TEXSET names a texturePages group, not a file or a resource
            # type; check_texture_pages owns it.
            if t == 'TEXSET':
                continue
            if t not in types:
                errors.append(f'unit {name}: unknown resource type {t} for "{f}"')
            key = (t, f.lower())
            if key in seen:
                warnings.append(f'unit {name}: duplicate entry {t} "{f}"')
            seen.add(key)
            ref = (d + '\\' + f) if d else f
            resolved, exact = resolve(ref)
            if resolved is None:
                if ref.lower() in KNOWN_MISSING:
                    warnings.append(f'unit {name}: known-missing (owner decision open): {ref}')
                else:
                    errors.append(f'unit {name}: referenced file not on disk: {ref}')
            elif not exact:
                warnings.append(f'unit {name}: case mismatch: {ref} -> {resolved}')
    return units


def check_datasets(doc, units):
    datasets = doc.get('datasets')
    if not isinstance(datasets, list):
        errors.append('datasets.json: no dataset list')
        return
    declared = {}
    for ds in datasets:
        name, kind = ds.get('name'), ds.get('kind')
        if not isinstance(name, str) or kind not in DATASET_KINDS:
            errors.append(f'dataset {name}: missing name or unknown kind {kind}')
            continue
        if kind == 'multi' and not isinstance(ds.get('type'), int):
            errors.append(f'dataset {name}: kind multi without a numeric type')
        if kind == 'camchange':
            if declared.get(name) != 'camstart':
                errors.append(f'dataset {name}: camchange without an earlier camstart')
        base = ds.get('base')
        if base is not None and base not in declared:
            errors.append(f'dataset {name}: base {base} not declared before it')
        slots = ds.get('slots')
        if not isinstance(slots, list) or len(slots) > LEVEL_MAXFILES:
            errors.append(f'dataset {name}: bad slot list')
            slots = []
        for slot in slots:
            if 'unit' in slot:
                if slot['unit'] not in units:
                    errors.append(f'dataset {name}: references unknown unit {slot["unit"]}')
            elif 'game' in slot:
                resolved, _exact = resolve(slot['game'])
                if resolved is None:
                    errors.append(f'dataset {name}: scenario file not on disk: {slot["game"]}')
            else:
                errors.append(f'dataset {name}: slot is neither unit nor game: {slot}')
        # first declaration wins for name lookups, as in the game
        declared.setdefault(name, kind)


def check_texture_pages(doc, units):
    """The texturePages table replaced the TEXPAGE resource entries: a group
    is an ordered list of page bindings, optionally extending another group,
    and a TEXSET unit entry names one.  Checks every file exists, every
    page offers a usable variant whichever way translucency is set, every
    TEXSET resolves, and every page id a shipped .pie names is bound."""
    table = doc.get('texturePages')
    if not isinstance(table, dict) or not isinstance(table.get('groups'), dict):
        errors.append('datasets.json: no texturePages table')
        return
    groups = table['groups']

    def flatten(name, depth=0):
        """A group's bindings as {page id: [files]}, base group first."""
        if depth > 8:
            errors.append(f'texturePages: group {name} extends itself')
            return {}
        group = groups.get(name)
        if not isinstance(group, dict):
            errors.append(f'texturePages: no group named {name}')
            return {}
        out = dict(flatten(group['extends'], depth + 1)) if group.get('extends') else {}
        for n, page in enumerate(group.get('pages') or []):
            if not isinstance(page.get('id'), str) or not isinstance(page.get('files'), list) or not page['files']:
                errors.append(f'texturePages: group {name} page {n} is malformed')
                continue
            out[page['id']] = page['files']
        return out

    for name in groups:
        for pid, files in flatten(name).items():
            for ref in files:
                resolved, exact = resolve(ref)
                if resolved is None:
                    errors.append(f'texturePages: group {name} page {pid}: file not on disk: {ref}')
                elif not exact:
                    warnings.append(f'texturePages: group {name} page {pid}: case mismatch: {ref} -> {resolved}')
            # the loader picks one file per page per translucency setting;
            # a page that resolves to none would fatal on load
            for additive, unwanted in ((True, 'soft'), (False, 'hard')):
                if not [f for f in files if unwanted not in f.lower()]:
                    errors.append(f'texturePages: group {name} page {pid} has no file for '
                                  f'{"additive" if additive else "compatible"} translucency')

    # every TEXSET entry names a real group, and every page a model wants is bound
    bound = set()
    for unit, entries in units.items():
        for entry in entries:
            if entry.get('t') != 'TEXSET':
                continue
            if entry['f'] not in groups:
                errors.append(f'unit {unit}: TEXSET names an unknown group {entry["f"]}')
            else:
                bound |= set(flatten(entry['f']))

    for pid in sorted(pie_page_ids()):
        if pid not in bound:
            errors.append(f'texturePages: .pie models reference {pid}, which no texture group binds')


def pie_page_ids():
    """The page ids shipped models name, reduced the way IMDLoad reduces
    them: a TEXTURE directive's name truncated at the first non-digit after
    "page-".

    Also checks the extension.  IMDLoad requires .dds; the models said .pcx
    until tools/convert_pie_textures.py rewrote them, and a model that says
    anything else fails to load.
    """
    ids = set()
    for dirpath, _dirs, files in os.walk(GAMEDATA):
        for name in files:
            if not name.lower().endswith('.pie'):
                continue
            path = os.path.join(dirpath, name)
            with open(path, encoding='latin-1') as handle:
                for line in handle:
                    m = re.match(r'\s*TEXTURE\s+\d+\s+(\S.*?)\.(\w+)\s+\d+\s+\d+\s*$', line, flags=re.I)
                    if not m:
                        if re.match(r'\s*TEXTURE\b', line, flags=re.I):
                            errors.append(f'{os.path.relpath(path, ROOT)}: unparsable TEXTURE directive: {line.strip()}')
                        continue
                    if m.group(2).lower() != 'dds':
                        errors.append(f'{os.path.relpath(path, ROOT)}: TEXTURE names a .{m.group(2)}, '
                                      f'but IMDLoad requires .dds')
                    page = re.match(r'(page-\d+)', m.group(1).lower())
                    if page:
                        ids.add(page.group(1))
    return ids


def audio_index():
    """Every WAV under GameData/audio, keyed by lower-cased bare filename -
    the same index Neuron::AudioSystem builds at Init, built the same way so
    a duplicate name fails here instead of on a Windows box.  Audio is no
    longer a resource type, so this index, not the manifest, is what says
    whether a sound the game names can be found."""
    root = os.path.join(GAMEDATA, 'audio')
    index = {}
    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            if not name.lower().endswith('.wav'):
                continue
            key = name.lower()
            path = os.path.relpath(os.path.join(dirpath, name), GAMEDATA)
            if key in index:
                errors.append(f'audio: two files share the bare name {name}: {index[key]} and {path}')
                continue
            index[key] = path
    return index


def check_audio(wavs):
    """audio.json is read once at AudioSystem::Init and registers every track
    the game starts with; AudioID.cpp maps fixed sound ids to WAV names."""
    resolved, _exact = resolve('audio\\audio.json')
    if resolved is None:
        errors.append('audio: audio/audio.json is missing')
    else:
        config = json.load(open(os.path.join(GAMEDATA, resolved), encoding='utf-8'))
        if not isinstance(config, list):
            errors.append('audio: audio.json is not a JSON array')
            config = []
        for n, row in enumerate(config):
            missing = [k for k in ('file', 'loop', 'volume', 'priority', 'radius') if k not in row]
            if missing:
                errors.append(f'audio.json row {n}: missing {", ".join(missing)}')
                continue
            if row['file'].lower() not in wavs:
                errors.append(f'audio.json row {n}: names a WAV not on disk: {row["file"]}')

    # A fixed id whose WAV was never shipped cannot register, so the sound is
    # silent - a content gap rather than a manifest error, and one that
    # predates the audio index (VtolMove.wav is the known example).
    audio_src = open(os.path.join(ROOT, 'Outpost', 'AudioID.cpp'), encoding='latin-1').read()
    for name in sorted({m for m in re.findall(r'"([^"]+\.wav)"', audio_src, flags=re.I)}):
        if name.lower() not in wavs:
            warnings.append(f'AudioID.cpp names {name}, which is not on disk - that sound is silent')


def load_table(name):
    resolved, _exact = resolve('Stats\\' + name)
    if resolved is None:
        return None
    return json.load(open(os.path.join(GAMEDATA, resolved), encoding='utf-8'))


def check_stats(doc, wavs):
    """Cross-reference checks over the JSON stats tables: every name one
    table uses to point at another must resolve, every model a table names
    must be a unit entry the manifests load, and every sound must be both in
    the AudioID.cpp table and on disk."""
    weapons = load_table('Weapons.json')
    if weapons is None:
        return  # tables not converted yet

    units = doc.get('units', {})
    imds = {e['f'].lower() for u in units.values() for e in u if e.get('t') == 'IMD'}

    # Sound references resolve against the AudioID.cpp name table at load
    # time; the WAV behind the name is found through the audio index, so a
    # name in the table with no file on disk is silent at runtime.
    audio_src = open(os.path.join(ROOT, 'Outpost', 'AudioID.cpp'), encoding='latin-1').read()
    audio_ids = {m.lower() for m in re.findall(r'"([^"]+\.wav)"', audio_src)}

    def check_sound(table, row, field, value):
        if value == '-1':
            return
        if value.lower() not in audio_ids:
            errors.append(f'Stats/{table} row {row}: {field} names a sound missing from AudioID.cpp: {value}')
        elif value.lower() not in wavs:
            warnings.append(f'Stats/{table} row {row}: {field} sound {value} is in AudioID.cpp but not on disk - silent at runtime')

    def names(table):
        rows = load_table(table) or []
        return {r['name'] for r in rows}

    def check_ref(table, row, field, value, pool, kind, none=('0',)):
        if value in none:
            return
        if value.lower() not in pool:
            errors.append(f'Stats/{table} row {row}: {field} names a missing {kind}: {value}')

    def check_name(table, row, field, value, pool, target):
        if value == '0':
            return
        if value not in pool:
            errors.append(f'Stats/{table} row {row}: {field} names an unknown {target}: {value}')

    body_names = names('Body.json')
    prop_names = names('Propulsion.json')
    weapon_names = {r['name'] for r in weapons}
    template_names = names('Templates.json')
    structure_names = names('Structures.json')
    sensor_names = names('Sensor.json')
    ecm_names = names('ECM.json')
    brain_names = names('Brain.json')
    construct_names = names('Construction.json')
    repair_names = names('Repair.json')
    proptype_names = names('PropulsionType.json')

    model_fields = {
        'Weapons.json': ['model', 'mountModel', 'muzzleModel', 'flightModel',
                         'hitModel', 'missModel', 'waterModel', 'trailModel'],
        'Body.json': ['model', 'flameModel'],
        'Propulsion.json': ['model'],
        'Sensor.json': ['model', 'mountModel'],
        'ECM.json': ['model', 'mountModel'],
        'Repair.json': ['model', 'mountModel'],
        'Construction.json': ['model', 'mountModel'],
        'Structures.json': ['model', 'baseModel'],
        'Features.json': ['model'],
        'BodyPropulsionIMD.json': ['leftModel', 'rightModel'],
    }
    for table, fields in model_fields.items():
        for n, row in enumerate(load_table(table) or []):
            for field in fields:
                check_ref(table, n, field, row[field], imds, 'model')

    for n, row in enumerate(load_table('WeaponSounds.json') or []):
        check_name('WeaponSounds.json', n, 'weapon', row['weapon'], weapon_names, 'weapon')
        for field in ('fireSound', 'impactSound'):
            check_sound('WeaponSounds.json', n, field, row[field])
    for n, row in enumerate(load_table('PropulsionSounds.json') or []):
        check_name('PropulsionSounds.json', n, 'propulsion', row['propulsion'], proptype_names, 'propulsion type')
        for field in ('start', 'idle', 'moveOff', 'move', 'hiss', 'shutDown'):
            check_sound('PropulsionSounds.json', n, field, row[field])
    for n, row in enumerate(load_table('WeaponModifier.json') or []):
        check_name('WeaponModifier.json', n, 'propulsion', row['propulsion'], proptype_names, 'propulsion type')
    for n, row in enumerate(load_table('BodyPropulsionIMD.json') or []):
        check_name('BodyPropulsionIMD.json', n, 'body', row['body'], body_names, 'body')
        check_name('BodyPropulsionIMD.json', n, 'propulsion', row['propulsion'], prop_names, 'propulsion')
    for n, row in enumerate(load_table('Templates.json') or []):
        check_name('Templates.json', n, 'body', row['body'], body_names, 'body')
        check_name('Templates.json', n, 'brain', row['brain'], brain_names, 'brain')
        check_name('Templates.json', n, 'construct', row['construct'], construct_names, 'construct')
        check_name('Templates.json', n, 'ecm', row['ecm'], ecm_names, 'ecm')
        check_name('Templates.json', n, 'propulsion', row['propulsion'], prop_names, 'propulsion')
        check_name('Templates.json', n, 'repair', row['repair'], repair_names, 'repair')
        check_name('Templates.json', n, 'sensor', row['sensor'], sensor_names, 'sensor')
    for n, row in enumerate(load_table('AssignWeapons.json') or []):
        check_name('AssignWeapons.json', n, 'template', row['template'], template_names, 'template')
        check_name('AssignWeapons.json', n, 'weapon', row['weapon'], weapon_names, 'weapon')
    for n, row in enumerate(load_table('Structures.json') or []):
        check_name('Structures.json', n, 'ecm', row['ecm'], ecm_names, 'ecm')
        check_name('Structures.json', n, 'sensor', row['sensor'], sensor_names, 'sensor')
    for n, row in enumerate(load_table('StructureWeapons.json') or []):
        check_name('StructureWeapons.json', n, 'structure', row['structure'], structure_names, 'structure')
        check_name('StructureWeapons.json', n, 'weapon', row['weapon'], weapon_names, 'weapon')
    for n, row in enumerate(load_table('StructureFunctions.json') or []):
        check_name('StructureFunctions.json', n, 'structure', row['structure'], structure_names, 'structure')


def main():
    path = os.path.join(GAMEDATA, 'datasets.json')
    try:
        doc = json.load(open(path, encoding='utf-8'))
    except (OSError, json.JSONDecodeError) as err:
        print(f'error: {path}: {err}')
        return 1
    types = registered_types()
    if types:
        units = check_units(doc, types)
        check_datasets(doc, units)
        check_texture_pages(doc, units)
        wavs = audio_index()
        check_audio(wavs)
        check_stats(doc, wavs)
    for w in warnings:
        print(f'warning: {w}')
    for e in errors:
        print(f'error: {e}')
    print(f'{len(errors)} error(s), {len(warnings)} warning(s)')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
