# Machine-readable composition schema

Development `main` supplies a self-contained JSON Schema, Draft 2020-12, for
composition `schema_version: 1`. This feature is newer than the published v0.1.0
archives. The canonical file is
[`schemas/composition-v1.schema.json`](../schemas/composition-v1.schema.json).
The installed SDK includes it at `share/chroma/schemas/composition-v1.schema.json`.
All references are local `$defs`; validation needs no network access.

Discover the schema from any working directory, even with a standalone CLI:

```sh
chroma schema > composition-v1.schema.json
```

Stdout contains only the schema. Additional arguments return usage exit code 2.
Shell redirection writes to the path you choose. The CLI embeds the canonical
file at build time, so a missing SDK resource directory cannot break discovery.

## Agent validation pipeline

1. Validate the generated document against the schema to catch missing fields,
   unsupported types, invalid enums, array lengths, numeric limits, animation
   source conflicts, and text layout constraints before invoking Chroma.
2. Run `chroma validate scene.json --json` for native semantic checks. Exit 0
   means valid; exit 3 means invalid. This command does not open assets.
3. Run `chroma preflight scene.json --json` to check files, fonts, media support,
   and timeline compatibility before rendering. See [input preflight](preflight.md).
4. Render with progress and a [terminal report](render-reports.md) to observe the job.

Example with Python's optional `jsonschema` package (install `jsonschema>=4.18,<5`
in your authoring environment):

```python
import json
import subprocess
from pathlib import Path
from jsonschema import Draft202012Validator

schema = json.loads(subprocess.check_output(["chroma", "schema"], text=True))
Draft202012Validator.check_schema(schema)
validator = Draft202012Validator(schema)
scene = json.loads(Path("scene.json").read_text())
errors = sorted(validator.iter_errors(scene), key=lambda error: str(error.json_path))
for error in errors:
    print(error.json_path, error.message)
if errors:
    raise SystemExit(1)
subprocess.run(["chroma", "validate", "scene.json", "--json"], check=True)
subprocess.run(["chroma", "preflight", "scene.json", "--json"], check=True)
```

The validator is an authoring/test dependency. The CLI, C library, and Python
wrapper require no JSON Schema library. C/Python applications can use the same
schema file or CLI discovery, then their existing composition loading and
preflight APIs for native validation.

## Scope and compatibility

The schema covers all five layer types, all nine effects, animation keyframes,
asset declarations, audio tracks, and text layout. Unknown properties remain
accepted wherever the native parser ignores them; text layout rejects unknown
properties. This preserves extension metadata and permits an optional editor
`$schema` annotation without changing parser behavior. It does not catch every
misspelled optional property.

Standard JSON Schema cannot express asset reference/type resolution, uniqueness
of IDs by property, strictly increasing keyframes, comparisons between timeline
fields, or the 67,108,864-pixel canvas budget. Native limits on IDs and text count
UTF-8 bytes; JSON Schema `maxLength` counts characters. JSON Schema considers
`1.0` an integer; Chroma requires integer JSON tokens for frame counts, dimensions,
and schema version. Agents should serialize those fields as integers and always
run native validation after structural validation. Finite numeric bounds in the
schema reflect Chroma's double precision parser.

Schema success alone does not guarantee a renderable composition. Preflight
checks initial media data rather than scanning every frame, and later I/O or
resource failures can still interrupt rendering. The existing version 1 format
remains experimental; incompatible future formats should use a new schema version.

## Regression coverage

`tests/schema_tests.py` uses a standards validator and the native CLI against all
composition examples, generated templates, positive/negative field cases, and
explicit semantic boundary cases. CI requires this test on Linux and macOS.
Local CTest includes it when the optional test dependency is installed:

```sh
python3 -m pip install -r tests/requirements-schema.txt
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build -R chroma_schema_tests --output-on-failure
```

Clean-runtime package smoke tests compare CLI output byte-for-byte with the
installed schema, without Python or an external validator.
