#!/usr/bin/env python3
"""One-shot converter: the comma-separated stats tables -> JSON arrays.

Field names and order are transcribed from the sscanf calls in the C++
loaders; the JSON carries exactly the cell values the .txt carried (numbers
as numbers, strings verbatim, including the "0" / "-1" no-asset sentinels),
so the loaders' downstream semantics do not change.  Array order is
preserved - it derives the REF_* reference numbers.

Verification: --check re-emits every line from the JSON and compares it
byte-for-byte (modulo CR stripping) with the original .txt, proving the
conversion lost nothing.  A column the C parser skipped (%*d) is only
dropped from the JSON if it is constant across the file; the constant is
recorded here so --check can re-emit it.

Run from the repository root:  python tools/convert_stats.py [--check]
"""

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMEDATA = os.path.join(ROOT, 'GameData')

S = 's'  # string cell
I = 'i'  # integer cell
DROP = 'drop'  # cell the C parser skipped; must be constant to drop


# Ordered column specs, one per table, transcribed from the loaders.
# (field, kind) - kind 'drop' names no field and requires a constant value.
TABLES = {
    'Stats/Weapons.txt': [
        ('name', S), ('techLevel', S), ('buildPower', I), ('buildPoints', I),
        ('weight', I), ('hitPoints', I), ('systemPoints', I), ('body', I),
        ('model', S), ('mountModel', S), ('muzzleModel', S), ('flightModel', S),
        ('hitModel', S), ('missModel', S), ('waterModel', S), ('trailModel', S),
        ('shortRange', I), ('longRange', I), ('shortHit', I), ('longHit', I),
        ('firePause', I), ('numExplosions', I), ('numRounds', I),
        ('reloadTime', I), ('damage', I), ('radius', I), ('radiusHit', I),
        ('radiusDamage', I), ('incenTime', I), ('incenDamage', I),
        ('incenRadius', I), ('directLife', I), ('radiusLife', I),
        ('flightSpeed', I), ('indirectHeight', I), ('fireOnMove', S),
        ('weaponClass', S), ('weaponSubClass', S), ('movement', S),
        ('weaponEffect', S), ('rotate', I), ('maxElevation', I),
        ('minElevation', I), ('facePlayer', S), ('faceInFlight', S),
        ('recoilValue', I), ('minRange', I), ('lightWorld', S),
        ('effectSize', I), ('surfaceToAir', I), ('numAttackRuns', I),
        ('designable', I),
    ],
    'Stats/Body.txt': [
        ('name', S), ('techLevel', S), ('size', S), ('buildPower', I),
        ('buildPoints', I), ('weight', I), ('body', I), ('model', S),
        ('systemPoints', I), ('weaponSlots', I), ('powerOutput', I),
        ('armourKinetic', I), ('armourHeat', I), ('flameModel', S),
        ('designable', I),
    ],
    'Stats/Brain.txt': [
        ('name', S), ('techLevel', S), ('buildPower', I), ('buildPoints', I),
        ('weight', I), ('hitPoints', I), ('systemPoints', I), ('weapon', S),
        ('progCap', I),
    ],
    'Stats/Propulsion.txt': [
        ('name', S), ('techLevel', S), ('buildPower', I), ('buildPoints', I),
        ('weight', I), ('hitPoints', I), ('systemPoints', I), ('body', I),
        ('model', S), ('type', S), ('maxSpeed', I), ('designable', I),
    ],
    'Stats/Sensor.txt': [
        ('name', S), ('techLevel', S), ('buildPower', I), ('buildPoints', I),
        ('weight', I), ('hitPoints', I), ('systemPoints', I), ('body', I),
        ('model', S), ('mountModel', S), ('range', I), ('location', S),
        ('type', S), ('time', I), ('power', I), ('designable', I),
    ],
    'Stats/ECM.txt': [
        ('name', S), ('techLevel', S), ('buildPower', I), ('buildPoints', I),
        ('weight', I), ('hitPoints', I), ('systemPoints', I), ('body', I),
        ('model', S), ('mountModel', S), ('location', S), ('power', I),
        ('designable', I),
    ],
    'Stats/Repair.txt': [
        ('name', S), ('techLevel', S), ('buildPower', I), ('buildPoints', I),
        ('weight', I), ('hitPoints', I), ('systemPoints', I),
        ('repairArmour', I), ('location', S), ('model', S), ('mountModel', S),
        ('repairPoints', I), ('time', I), ('designable', I),
    ],
    'Stats/Construction.txt': [
        ('name', S), ('techLevel', S), ('buildPower', I), ('buildPoints', I),
        ('weight', I), ('hitPoints', I), ('systemPoints', I), ('body', I),
        ('model', S), ('mountModel', S), ('constructPoints', I),
        ('designable', I),
    ],
    'Stats/PropulsionType.txt': [
        ('name', S), ('flight', S), ('multiplier', I),
    ],
    'Stats/TerrainTable.txt': [
        ('terrainType', I), ('propulsionType', I), ('speedFactor', I),
    ],
    'Stats/SpecialAbility.txt': [
        ('name', S), ('id', I),
    ],
    'Stats/BodyPropulsionIMD.txt': [
        ('body', S), ('propulsion', S), ('leftModel', S), ('rightModel', S),
        ('dummy', I),
    ],
    'Stats/WeaponSounds.txt': [
        ('weapon', S), ('fireSound', S), ('impactSound', S), ('dummy', I),
    ],
    'Stats/WeaponModifier.txt': [
        ('effect', S), ('propulsion', S), ('modifier', I),
    ],
    'Stats/PropulsionSounds.txt': [
        ('propulsion', S), ('start', S), ('idle', S), ('moveOff', S),
        ('move', S), ('hiss', S), ('shutDown', S), ('dummy', I),
    ],
    'Stats/Structures.txt': [
        ('name', S), ('type', S), ('techLevel', S), ('strength', S),
        ('terrainType', I), ('baseWidth', I), ('baseBreadth', I),
        ('foundation', S), ('buildPoints', I), ('height', I), ('armour', I),
        ('bodyPoints', I), ('repairSystem', I), ('powerToBuild', I),
        ('minimumPower', I), ('resistance', I), ('quantityLimit', I),
        ('sizeModifier', I), ('ecm', S), ('sensor', S), ('weaponSlots', I),
        ('model', S), ('baseModel', S), ('numFuncs', I), ('numWeaps', I),
    ],
    'Stats/StructureWeapons.txt': [
        ('structure', S), ('weapon', S), ('dummy', I),
    ],
    'Stats/StructureFunctions.txt': [
        ('structure', S), ('function', S), ('dummy', I),
    ],
    'Stats/StructureModifier.txt': [
        ('effect', S), ('strength', S), ('modifier', I),
    ],
    'Stats/Features.txt': [
        ('name', S), ('width', I), ('breadth', I), ('damageable', I),
        ('armour', I), ('body', I), ('model', S), ('type', S),
        ('tileDraw', I), ('allowLOS', I), ('visibleAtStart', I),
    ],
    'Stats/Templates.txt': [
        ('name', S), ('id', I), ('body', S), ('brain', S), ('construct', S),
        ('ecm', S), ('player', I), ('propulsion', S), ('repair', S),
        ('type', S), ('sensor', S), ('numWeaps', I),
    ],
    'Stats/AssignWeapons.txt': [
        ('template', S), ('weapon', S), ('player', I),
    ],
}

# The research tables ship once per campaign plus multiplayer.
RESEARCH_DIRS = ['Stats/Research/Cam1', 'Stats/Research/Cam2',
                 'Stats/Research/Cam3', 'Stats/Research/multiPlayer']

RESEARCH_TABLES = {
    'Research.txt': [
        ('name', S), ('techLevel', S), ('subGroupIcon', S), ('techCode', I),
        ('iconID', S), ('model', S), ('model2', S), ('message', S),
        ('structure', S), ('component', S), ('componentType', S),
        ('researchPoints', I), ('keyTopic', I), ('numPRRequired', I),
        ('numFunctions', I), ('numStructures', I), ('numRedStructs', I),
        ('numStructResults', I), ('numRedArtefacts', I),
        ('numArteResults', I),
    ],
    'PRResearch.txt': [
        ('research', S), ('requires', S), ('dummy', I),
    ],
    'RedComponents.txt': [
        ('research', S), ('component', S), ('componentType', S),
        ('dummy', I),
    ],
    'ResultComponent.txt': [
        ('research', S), ('component', S), ('componentType', S),
        ('replacedComponent', S), ('replacedComponentType', S), ('dummy', I),
    ],
    'ResearchStruct.txt': [
        ('research', S), ('structure', S), ('dummy', I),
    ],
    'RedStructure.txt': [
        ('research', S), ('structure', S), ('dummy', I),
    ],
    'ResultStructure.txt': [
        ('research', S), ('structure', S), ('dummy', I), ('dummy2', I),
    ],
    'ResearchFunctions.txt': [
        ('research', S), ('function', S), ('dummy', I),
    ],
}

# Functions.txt: the first cell picks the record shape. Field lists per
# type, transcribed from the per-type loaders in Outpost/Function.cpp.
FUNCTION_TYPES = {
    'Production': [('bodySize', S), ('productionOutput', I)],
    'Production Upgrade': [('factory', I), ('cyborg', I), ('vtol', I),
                           ('outputModifier', I)],
    'Research': [('researchPoints', I)],
    'ReArm': [('reArmPoints', I)],
    'Research Upgrade': [('modifier', I)],
    'Power Upgrade': [('modifier', I)],
    'Repair Upgrade': [('modifier', I)],
    'VehicleRepair Upgrade': [('modifier', I)],
    'VehicleECM Upgrade': [('modifier', I)],
    'VehicleConst Upgrade': [('modifier', I)],
    'ReArm Upgrade': [('modifier', I)],
    'VehicleBody Upgrade': [('modifier', I), ('body', I),
                            ('armourKinetic', I), ('armourHeat', I),
                            ('droid', I), ('cyborg', I)],
    'VehicleSensor Upgrade': [('modifier', I), ('range', I)],
    'Weapon Upgrade': [('weaponSubClass', S), ('firePause', I),
                       ('shortHit', I), ('longHit', I), ('damage', I),
                       ('radiusDamage', I), ('incenDamage', I),
                       ('radiusHit', I)],
    'Structure Upgrade': [('armour', I), ('body', I), ('resistance', I)],
    'WallDefence Upgrade': [('armour', I), ('body', I)],
    'Power Generator': [('powerOutput', I), ('powerMultiplier', I),
                        ('criticalMassChance', I), ('criticalMassRadius', I),
                        ('criticalMassDamage', I), ('radiationDecayTime', I)],
    'Resource': [('maxPower', I)],
    'Repair Droid': [('repairPoints', I)],
    'Wall Function': [('structure', S), ('dummy', I)],
}

# recorded constants for dropped columns, per table (filled by convert)
CONSTANTS_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              'stats_dropped_columns.json')

errors = []
skipped = []


def find_file(rel):
    """Case-insensitive resolve under GameData (the shipped names vary)."""
    parts = rel.split('/')
    cur = GAMEDATA
    for part in parts:
        entries = os.listdir(cur)
        match = next((e for e in entries if e.lower() == part.lower()), None)
        if match is None:
            return None
        cur = os.path.join(cur, match)
    return cur


def read_rows(path, ncols):
    """Rows exactly as the C loaders saw them: one record per '\n'."""
    raw = open(path, 'rb').read().decode('latin-1')
    body = raw.split('\n')
    # numCR counted every '\n'; a trailing chunk with no '\n' was never read
    lines = body[:-1]
    rows = []
    for n, line in enumerate(lines):
        line = line.rstrip('\r')
        if line.strip() == '':
            errors.append(f'{path}: line {n + 1} is blank - the C parser read garbage here')
            continue
        cells = line.split(',')
        if len(cells) < ncols:
            errors.append(f'{path}: line {n + 1} has {len(cells)} cells, spec wants {ncols}')
            continue
        rows.append(cells[:ncols])
    return rows


def convert_table(rel, spec, out_path):
    path = find_file(rel)
    if path is None:
        errors.append(f'{rel}: file not found')
        return None
    rows = read_rows(path, len(spec))
    dropped = {}
    records = []
    for cells in rows:
        record = {}
        drop_index = 0
        for (field, kind), cell in zip(spec, cells):
            if kind == DROP:
                dropped.setdefault(drop_index, set()).add(cell.strip())
                drop_index += 1
                continue
            if kind == I:
                try:
                    record[field] = int(cell.strip())
                except ValueError:
                    errors.append(f'{rel}: non-integer cell for {field}: {cell!r}')
                    record[field] = 0
            else:
                record[field] = cell
        records.append(record)
    consts = []
    for index in sorted(dropped):
        values = dropped[index]
        if len(values) != 1:
            errors.append(f'{rel}: dropped column has varying values {sorted(values)!r} - keep it instead')
            consts.append(None)
        else:
            consts.append(next(iter(values)))
    with open(out_path, 'w', newline='\n') as f:
        json.dump(records, f, indent=1)
        f.write('\n')
    return {'source': rel, 'rows': len(records), 'dropped': consts,
            'original': path}


def check_table(rel, spec, json_path, consts, original):
    """Re-emit each line from the JSON and diff against the original."""
    records = json.load(open(json_path, encoding='utf-8'))
    raw = open(original, 'rb').read().decode('latin-1')
    lines = [l.rstrip('\r') for l in raw.split('\n')[:-1] if l.strip() != '']
    if len(lines) != len(records):
        errors.append(f'{rel}: row count mismatch json={len(records)} txt={len(lines)}')
        return
    for n, (record, line) in enumerate(zip(records, lines)):
        cells = []
        drop_iter = iter(consts)
        for field, kind in spec:
            if kind == DROP:
                cells.append(next(drop_iter))
            elif kind == I:
                cells.append(str(record[field]))
            else:
                cells.append(record[field])
        original_cells = line.split(',')[:len(spec)]
        for i, (cell, orig) in enumerate(zip(cells, original_cells)):
            if spec[i][1] == I:
                ok = int(orig.strip()) == int(cell)
            else:
                ok = cell == orig
            if not ok:
                errors.append(f'{rel}: line {n + 1} field {i} does not round-trip: txt={orig!r} json={cell!r}')


def read_lines(path):
    raw = open(path, 'rb').read().decode('latin-1')
    return [l.rstrip('\r') for l in raw.split('\n')[:-1]]


def convert_functions(check):
    """Functions.txt: cell 1 picks the record shape."""
    path = find_file('Stats/Functions.txt')
    if check and path is None:
        skipped.append('Stats/Functions.txt')
        return
    out = os.path.splitext(path)[0] + '.json'
    records = json.load(open(out)) if check else []
    for n, line in enumerate(read_lines(path)):
        if line.strip() == '':
            errors.append(f'Functions.txt: line {n + 1} is blank')
            continue
        cells = line.split(',')
        ftype = cells[0]
        spec = FUNCTION_TYPES.get(ftype)
        if spec is None:
            errors.append(f'Functions.txt: line {n + 1} has unknown type {ftype!r}')
            continue
        if len(cells) < 2 + len(spec):
            errors.append(f'Functions.txt: line {n + 1} has {len(cells)} cells, wants {2 + len(spec)}')
            continue
        if check:
            record = records[n]
            rebuilt = [record['type'], record['name']]
            for field, kind in spec:
                rebuilt.append(str(record[field]) if kind == I else record[field])
            for i, (orig, new) in enumerate(zip(cells, rebuilt)):
                ok = int(orig) == int(new) if i >= 2 and spec[i - 2][1] == I else orig == new
                if not ok:
                    errors.append(f'Functions.txt: line {n + 1} field {i}: txt={orig!r} json={new!r}')
        else:
            record = {'type': ftype, 'name': cells[1]}
            for (field, kind), cell in zip(spec, cells[2:]):
                record[field] = int(cell.strip()) if kind == I else cell
            records.append(record)
    if check:
        if len(records) != len([l for l in read_lines(path) if l.strip()]):
            errors.append('Functions.txt: row count mismatch')
    else:
        with open(out, 'w', newline='\n') as f:
            json.dump(records, f, indent=1)
            f.write('\n')


def convert_research(check):
    for rdir in RESEARCH_DIRS:
        for name, spec in sorted(RESEARCH_TABLES.items()):
            rel = rdir + '/' + name
            path = find_file(rel)
            if path is None:
                if check:
                    skipped.append(rel)
                else:
                    errors.append(f'{rel}: file not found')
                continue
            out = os.path.splitext(path)[0] + '.json'
            if check:
                check_table(rel, spec, out, [], path)
            else:
                convert_table(rel, spec, out)


def smsg_files():
    doc = json.load(open(os.path.join(GAMEDATA, 'datasets.json')))
    refs = set()
    for unit in doc['units'].values():
        for entry in unit:
            if entry['t'] == 'SMSG':
                ref = (entry['d'] + '\\' + entry['f']) if entry['d'] else entry['f']
                if ref.lower().endswith('.json'):
                    ref = ref[:-5] + '.txt'
                refs.add(ref)
    return sorted(refs)


def parse_viewdata_line(line, rel, n):
    cells = line.split(',')
    pos = 0

    def take():
        nonlocal pos
        if pos >= len(cells):
            raise ValueError(f'{rel}: line {n + 1}: ran out of cells at {pos}')
        cell = cells[pos]
        pos += 1
        return cell

    record = {'name': take()}
    num_text = int(take())
    record['textIds'] = [take() for _ in range(num_text)]
    vtype = int(take())
    record['type'] = vtype
    if vtype == 0:  # VIEW_RES
        record['model'] = take()
        record['model2'] = take()
        record['sequence'] = take()
        record['audio'] = take()
        record['numFrames'] = int(take())
    elif vtype in (1, 3):  # VIEW_RPL / VIEW_RPLX
        num_seq = int(take())
        seqs = []
        for _ in range(num_seq):
            seq = {'name': take()}
            if vtype == 3:
                seq['flag'] = int(take())
            seq_text = int(take())
            seq['textIds'] = [take() for _ in range(seq_text)]
            seq['audio'] = take()
            seq['numFrames'] = int(take())
            seqs.append(seq)
        record['sequences'] = seqs
    elif vtype == 2:  # VIEW_PROX
        record['x'] = int(take())
        record['y'] = int(take())
        record['z'] = int(take())
        record['audio'] = take()
        record['proxType'] = int(take())
    else:
        raise ValueError(f'{rel}: line {n + 1}: unknown view type {vtype}')
    if pos != len(cells):
        raise ValueError(f'{rel}: line {n + 1}: {len(cells) - pos} unconsumed cells')
    return record


def emit_viewdata_line(record):
    cells = [record['name'], str(len(record['textIds']))]
    cells += record['textIds']
    cells.append(str(record['type']))
    if record['type'] == 0:
        cells += [record['model'], record['model2'], record['sequence'],
                  record['audio'], str(record['numFrames'])]
    elif record['type'] in (1, 3):
        cells.append(str(len(record['sequences'])))
        for seq in record['sequences']:
            cells.append(seq['name'])
            if record['type'] == 3:
                cells.append(str(seq['flag']))
            cells.append(str(len(seq['textIds'])))
            cells += seq['textIds']
            cells += [seq['audio'], str(seq['numFrames'])]
    else:
        cells += [str(record['x']), str(record['y']), str(record['z']),
                  record['audio'], str(record['proxType'])]
    return cells


def convert_messages(check):
    for rel in smsg_files():
        resolved, _exact = None, None
        path = find_file(rel.replace('\\', '/'))
        if path is None:
            if check:
                skipped.append(rel)
            else:
                errors.append(f'{rel}: SMSG file not found')
            continue
        out = os.path.splitext(path)[0] + '.json'
        lines = [l for l in read_lines(path) if l.strip() != '']
        if check:
            records = json.load(open(out))
            if len(records) != len(lines):
                errors.append(f'{rel}: row count mismatch')
                continue
            for n, (record, line) in enumerate(zip(records, lines)):
                rebuilt = emit_viewdata_line(record)
                original = line.split(',')
                ints = {i for i, c in enumerate(rebuilt)}
                if len(rebuilt) != len(original):
                    errors.append(f'{rel}: line {n + 1} arity: txt={len(original)} json={len(rebuilt)}')
                    continue
                for orig, new in zip(original, rebuilt):
                    if orig != new and not (orig.strip().isdigit() and new.isdigit()
                                            and int(orig) == int(new)):
                        errors.append(f'{rel}: line {n + 1}: txt={orig!r} json={new!r}')
        else:
            records = []
            for n, line in enumerate(lines):
                try:
                    records.append(parse_viewdata_line(line, rel, n))
                except ValueError as err:
                    errors.append(str(err))
            with open(out, 'w', newline='\n') as f:
                json.dump(records, f, indent=1)
                f.write('\n')


def main():
    check = '--check' in sys.argv
    manifest = {}
    for rel, spec in sorted(TABLES.items()):
        out = os.path.splitext(find_file(rel) or os.path.join(GAMEDATA, rel))[0] + '.json'
        if check:
            if find_file(rel) is None:
                skipped.append(rel)
                continue
            recorded = json.load(open(CONSTANTS_PATH))
            entry = recorded[rel]
            check_table(rel, spec, out, entry['dropped'], find_file(rel))
        else:
            entry = convert_table(rel, spec, out)
            if entry is not None:
                manifest[rel] = {'dropped': entry['dropped'], 'rows': entry['rows']}
    convert_functions(check)
    convert_research(check)
    convert_messages(check)
    if not check:
        with open(CONSTANTS_PATH, 'w', newline='\n') as f:
            json.dump(manifest, f, indent=1)
            f.write('\n')
    if skipped:
        print(f'note: {len(skipped)} table(s) skipped: source .txt already '
              'deleted from the tree (round-trip was proven before removal)')
    for e in errors:
        print(f'error: {e}')
    print(('check ' if check else 'convert ') + ('FAILED' if errors else 'ok'))
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
