"""Exercise the published schema with a standards validator and the native parser."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from jsonschema import Draft202012Validator

ROOT = Path(__file__).resolve().parents[1]
CLI = Path(sys.argv[1]).resolve()
canonical = (ROOT / 'schemas/composition-v1.schema.json').read_text()
schema = json.loads(canonical)
Draft202012Validator.check_schema(schema)
validator = Draft202012Validator(schema)
assert subprocess.check_output([CLI, 'schema'], text=True) == canonical
assert subprocess.run([CLI, 'schema', '--unknown'], capture_output=True).returncode == 2
count = 0
with tempfile.TemporaryDirectory() as temp:
    scene = Path(temp) / 'scene.json'

    def check(document, structural=True, native=None):
        global count
        if native is None:
            native = structural
        assert validator.is_valid(document) == structural, list(validator.iter_errors(document))
        scene.write_text(json.dumps(document))
        result = subprocess.run([CLI, 'validate', scene, '--json'], capture_output=True, text=True)
        assert (result.returncode == 0) == native, result.stdout + result.stderr
        count += 1

    for example in sorted((ROOT / 'examples').rglob('*.json')):
        document = json.loads(example.read_text())
        if isinstance(document, dict) and 'schema_version' in document:
            check(document)

    for template in ('title-sequence', 'product-promo', 'captioned-footage'):
        project = Path(temp) / template
        subprocess.run([CLI, 'init', project, '--template', template], check=True, capture_output=True)
        check(json.loads((project / 'scene.json').read_text()))

    base = {'schema_version': 1, 'canvas': {'width': 1280, 'height': 720,
            'frame_rate': {'numerator': 30, 'denominator': 1}, 'duration_frames': 60},
            'layers': [{'id': 'bg', 'type': 'solid', 'start_frame': 0, 'end_frame': 60,
                        'color': [0, 0, 0, 1]}]}
    check(base)
    def mutation(path, value, structural=False, native=None):
        document = copy.deepcopy(base)
        target = document
        for key in path[:-1]:
            target = target[key]
        target[path[-1]] = value
        check(document, structural, native)

    for key in ('schema_version', 'canvas', 'layers'):
        document = copy.deepcopy(base)
        del document[key]
        check(document, False)
    for path, value in [(['schema_version'], 2), (['canvas', 'width'], 32769),
                        (['canvas', 'height'], 0), (['canvas', 'duration_frames'], -1),
                        (['canvas', 'frame_rate', 'denominator'], 0),
                        (['layers', 0, 'color'], [0, 0, 0]),
                        (['layers', 0, 'color'], [0, 0, 0, 1.1]),
                        (['layers', 0, 'start_frame'], True),
                        (['layers', 0, 'id'], ''), (['layers', 0, 'type'], 'unknown')]:
        mutation(path, value)
    valid_effects = [
        {'type': 'transform_2d', 'matrix': [1, 0, 0, 0, 1, 0]},
        {'type': 'crop', 'x': -1, 'y': 0, 'width': 10, 'height': 10},
        {'type': 'mask', 'shape': 'ellipse', 'bounds': [0, 0, 10, 10], 'inverted': True},
        {'type': 'gaussian_blur', 'radius': 0},
        {'type': 'color_matrix', 'matrix': [0] * 20}]
    for kind, value in [('opacity', 0.5), ('translation', [1, 2]), ('scale', [1, -1]), ('rotation', 90)]:
        valid_effects += [{'type': kind, 'value': value}, {'type': kind, 'keyframes': [
            {'frame': 0, 'value': value}, {'frame': 10, 'value': value, 'easing': 'ease_in_out'}]}]
    for effect in valid_effects:
        mutation(['layers', 0, 'effects'], [effect], True)
        bad = dict(effect, type='bogus')
        mutation(['layers', 0, 'effects'], [bad])
    for effect in [
        {'type': 'translation', 'value': [0, 0], 'origin': [0, 0]},
        {'type': 'scale', 'value': [1, 1], 'keyframes': []},
        {'type': 'rotation'}, {'type': 'opacity', 'keyframes': []},
        {'type': 'opacity', 'value': 2}, {'type': 'gaussian_blur', 'radius': 4097},
        {'type': 'crop', 'x': 0, 'y': 0, 'width': 0, 'height': 1},
        {'type': 'mask', 'shape': 'ellipse', 'bounds': [0, 0, -1, 1]},
        {'type': 'color_matrix', 'matrix': [0] * 19}]:
        mutation(['layers', 0, 'effects'], [effect])
    mutation(['layers', 0, 'effects'], [{'type': 'opacity', 'value': 1}] * 65)
    mutation(['layers', 0, 'effects'], [{'type': 'opacity', 'keyframes': [{'frame': 0, 'value': 1}] * 1025}])
    text = dict(base, assets=[{'id': 'font', 'type': 'font', 'path': 'missing.ttf'}], layers=[{
        'id': 'text', 'type': 'text', 'start_frame': 0, 'end_frame': 60, 'font': 'font',
        'text': 'Hello', 'position': [0, 20], 'font_size': 24, 'color': [1, 1, 1, 1]}])
    for layout in [{}, {'width': 100, 'align': 'center', 'overflow': 'ellipsis'},
                   {'height': 50, 'wrap': 'none', 'line_spacing': 1.5, 'max_lines': 2}]:
        document = copy.deepcopy(text)
        document['layers'][0]['layout'] = layout
        check(document)
    for layout in [{'align': 'right'}, {'overflow': 'ellipsis'}, {'width': 0},
                   {'width': 65537}, {'line_spacing': 101}, {'max_lines': 0}, {'typo': 1}]:
        document = copy.deepcopy(text)
        document['layers'][0]['layout'] = layout
        check(document, False)
    # Deliberate standard-schema limits: semantics remain native validation's job.
    mutation(['layers', 0, 'end_frame'], 61, True, False)
    mutation(['layers', 0, 'end_frame'], 1, True)
    mutation(['layers', 0, 'start_frame'], 60, True, False)
    mutation(['canvas', 'width'], 1.0, True, False)
    mutation(['layers', 0, 'id'], 'é' * 128, True, False)
    document = copy.deepcopy(base)
    document['canvas'].update(width=32768, height=32768)
    check(document, True, False)
    document = copy.deepcopy(base)
    document['layers'] *= 2
    check(document, True, False)
    document = copy.deepcopy(text)
    document['layers'][0]['font'] = 'unknown'
    check(document, True, False)
    mutation(['layers', 0, 'effects'], [{'type': 'rotation', 'keyframes': [
        {'frame': 1, 'value': 0}, {'frame': 0, 'value': 90}]}], True, False)
    # Native parser permits extension metadata, except inside text layout.
    document = dict(base, **{'$schema': schema['$id'], 'agent_metadata': {'job': 1}})
    check(document)
print(f'{count} schema/native conformance cases passed')
