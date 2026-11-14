"""Apply a saved UE scene export to the derived world input, preserving unrelated source bytes."""
import argparse
import copy
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re
import sys

import DemoContent as content
import DemoWorld
from CatalogTools import encode
from SyncDemoContentData import _ProtectedFile


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def _json(value):
    if isinstance(value, Decimal):
        _require(value.is_finite(), "non-finite exported coordinate")
        return format(value, 'f')
    if isinstance(value, dict):
        return '{' + ', '.join(json.dumps(k) + ': ' + _json(v) for k, v in value.items()) + '}'
    if isinstance(value, list):
        return '[' + ', '.join(_json(v) for v in value) + ']'
    return json.dumps(value, ensure_ascii=False, allow_nan=False)


def _spans(text):
    decoder = json.JSONDecoder(parse_float=Decimal)
    spans = {}
    def space(i):
        while i < len(text) and text[i].isspace():
            i += 1
        return i
    def node(i, path):
        i = space(i)
        start = i
        if text[i] == '{':
            i = space(i + 1)
            while text[i] != '}':
                key, i = decoder.raw_decode(text, i)
                i = space(i)
                _require(text[i] == ':', 'malformed object')
                i = space(node(i + 1, path + (key,)))
                if text[i] != ',':
                    break
                i = space(i + 1)
            _require(text[i] == '}', 'malformed object end')
            i += 1
        elif text[i] == '[':
            i, index = space(i + 1), 0
            while text[i] != ']':
                i = space(node(i, path + (index,)))
                index += 1
                if text[i] != ',':
                    break
                i = space(i + 1)
            _require(text[i] == ']', 'malformed array end')
            i += 1
        else:
            _, i = decoder.raw_decode(text, i)
        spans[path] = (start, i)
        return i
    _require(space(node(0, ())) == len(text), 'trailing source data')
    return spans


def patch_bytes(original, before, after):
    text = original.decode('utf-8')
    newline = '\r\n' if '\r\n' in text else '\n'
    spans, edits = _spans(text), []
    def walk(a, b, path):
        if a == b:
            return
        if type(a) is dict and type(b) is dict and a.keys() == b.keys():
            for key in a:
                walk(a[key], b[key], path + (key,))
        elif type(a) is list and type(b) is list and len(a) == len(b):
            for index, (av, bv) in enumerate(zip(a, b)):
                walk(av, bv, path + (index,))
        elif type(a) is list and type(b) is list and len(b) > len(a) and all(
            type(old) is dict and type(new) is dict and old.get('id') == new.get('id')
            for old, new in zip(a, b)
        ):
            for index, (av, bv) in enumerate(zip(a, b)):
                walk(av, bv, path + (index,))
            start, end = spans[path]
            close = end - 1
            line_start = text.rfind(newline, start, close)
            line_end = line_start + len(newline)
            if line_start >= 0 and not text[line_end:close].strip():
                insert_at = line_start
                indent = text[line_end:close] + '    '
            else:
                insert_at = close
                indent = ' '
            additions = ''.join(
                (',' + newline if index or a else newline) + indent + _json(row)
                for index, row in enumerate(b[len(a):])
            )
            edits.append((insert_at, insert_at, additions))
        else:
            edits.append((*spans[path], _json(b)))
    walk(before, after, ())
    for start, end, replacement in sorted(edits, reverse=True):
        text = text[:start] + replacement + text[end:]
    return text.encode('utf-8')


def _rows(rows, allowed, label):
    _require(type(rows) is list, f'{label}: expected array')
    result = {}
    for row in rows:
        _require(type(row) is dict and set(row) <= allowed and 'id' in row, f'{label}: unsupported fields')
        key = row['id']
        _require(type(key) is str and key not in result, f'{label}: duplicate/invalid id')
        result[key] = row
    return result


def build_candidate(original, exported):
    before = content.parse_json(original, 'world source')
    export = content.parse_json(exported, 'saved scene export')
    _require(set(export) == {'schema_version', 'source_sha256', 'surfaces', 'plateaus', 'ramps', 'blockers', 'anchors'}, 'unsupported scene export fields')
    _require(type(export['schema_version']) is int and export['schema_version'] == 1, 'unsupported scene export version')
    _require(export['source_sha256'] == hashlib.sha256(original).hexdigest(), 'world source changed since the saved scene definition; export refused')
    after = copy.deepcopy(before)
    def update(rows, incoming, fields, label):
        data = _rows(incoming, {'id', *fields}, label)
        _require(set(data) == {row['id'] for row in rows}, f'{label}: identities cannot be added or removed')
        for row in rows:
            value = data[row['id']]
            _require(set(value) == {'id', *fields}, f'{label}: missing fields')
            for field in fields:
                row[field] = copy.deepcopy(value[field])
    geometry = after['geometry']
    update(geometry['surfaces'], export['surfaces'], ['min_cm', 'max_cm'], 'surfaces')
    update(geometry['elevation']['plateaus'], export['plateaus'], ['polygon_cm'], 'plateaus')
    update(geometry['elevation']['ramps'], export['ramps'], ['polygon_cm'], 'ramps')
    allowed = {'id', 'shape', 'surface_id', 'center_cm', 'radius_cm', 'height_cm', 'blocks_movement', 'blocks_sight', 'polygon_cm'}
    blockers = _rows(export['blockers'], allowed, 'blockers')
    for row in blockers.values():
        fields = {'center_cm', 'radius_cm'} if row.get('shape') == 'cylinder' else {'polygon_cm'}
        _require(set(row) == {'id', 'shape', 'surface_id', 'height_cm', 'blocks_movement', 'blocks_sight', *fields}, 'blocker: missing or unsupported fields')
    old_ids = [row['id'] for row in geometry['blockers']]
    geometry['blockers'] = [blockers[key] for key in old_ids if key in blockers]
    geometry['blockers'] += [blockers[key] for key in sorted(set(blockers) - set(old_ids))]
    anchors = _rows(export['anchors'], {'id', 'kind', 'surface_id', 'position_cm', 'members'}, 'anchors')
    source_anchors = {row['id'] for row in after['anchors']}
    final_ids = [*(f'final_spawn_{slot}' for slot in range(6)), 'final_capture']
    exported_ids = set(anchors)
    new_ids = exported_ids - source_anchors
    missing_ids = source_anchors - exported_ids
    _require(not missing_ids, 'layout point identities cannot be removed')
    _require(new_ids <= set(final_ids) and (not new_ids or new_ids == set(final_ids)),
             'only the complete final-arena anchor set can be added')
    for row in after['anchors']:
        value = anchors[row['id']]
        _require(set(value) == {'id', 'kind', 'surface_id', 'position_cm', 'members'}, 'anchor: missing fields')
        _require(value['kind'] == row['kind'] and value['surface_id'] == row['surface_id'],
                 f"anchor: kind or surface changed for {row['id']}")
        row['position_cm'] = copy.deepcopy(value['position_cm'])
        if 'members' in row:
            update(row['members'], value['members'], ['position_cm'], 'camp homes')
        else:
            _require(value['members'] == [], 'only camps have home points')
    if new_ids:
        _require(after['schema_version'] == 3 and after['projection_revision'] == 3,
                 'final-arena anchors can only be introduced by upgrading world schema 3')
        _require([row['id'] for row in export['anchors'] if row['id'] in new_ids] == final_ids,
                 'final-arena anchors must be exported in canonical order')
        for identity in final_ids:
            value = anchors[identity]
            kind = 'FinalCapture' if identity == 'final_capture' else 'FinalSpawn'
            _require(set(value) == {'id', 'kind', 'surface_id', 'position_cm', 'members'}
                     and value['kind'] == kind and value['surface_id'] == after['geometry']['surfaces'][0]['id']
                     and value['members'] == [],
                     f'final-arena anchor has an invalid kind, surface, or member list: {identity}')
            after['anchors'].append({
                'id': identity, 'kind': kind, 'surface_id': value['surface_id'],
                'position_cm': copy.deepcopy(value['position_cm']),
            })
        after['schema_version'] = DemoWorld.SCHEMA_VERSION
        after['projection_revision'] = DemoWorld.PROJECTION_REVISION
    if after != before:
        after['layout_revision'] += 1
        after['geometry_revision'] += 1
    return patch_bytes(original, before, after)


def apply_export(root, export_path):
    root = Path(root)
    source = root / content.WORLD_SOURCE
    protected = _ProtectedFile(source, writable=True)
    original = protected.read()
    try:
        candidate = build_candidate(original, Path(export_path).read_bytes())
        world = DemoWorld.project_author(content.parse_json(candidate, 'derived world'), candidate, (root / content.WORLD_SCHEMA_SOURCE).read_bytes())
        payload = encode(world)
        DemoWorld.validate_payload(payload)
        author = content.compile_author(root)
        DemoWorld._binding_report(world, payload, author.catalog.data)
        for path, raw in author.inputs.items():
            _require((root / path).read_bytes() == raw, f'input changed while validating scene: {path}')
        if candidate != original:
            backup = Path(export_path).with_name('WorldSourceBefore.json')
            backup.write_bytes(original)
            try:
                protected.write(candidate)
                _require(protected.read() == candidate, 'derived source write verification failed')
            except BaseException:
                protected.write(original)
                raise
        return candidate != original
    finally:
        protected.close()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('export', type=Path)
    args = parser.parse_args(argv)
    try:
        changed = apply_export(content.ROOT, args.export)
    except (OSError, ValueError) as error:
        print(f'FAIL scene export: {error}', file=sys.stderr)
        return 2
    print('PASS scene export: ' + ('derived world updated' if changed else 'world bytes unchanged'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
