#!/bin/sh
# Only Python/venv/pip are needed; wheel installation is offline.
set -eu
package=$(cd "$1" && pwd)
fixture=$(cd "$2" && pwd)
python=${3:-python3}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"$python" -m venv "$work/venv"
"$work/venv/bin/python" -m pip install --no-index --no-deps "$package"/share/chroma/python/*.whl
case "$(uname -s)" in Darwin) library=libchroma.dylib ;; *) library=libchroma.so ;; esac
cd "$work"
env -i PATH=/usr/bin:/bin HOME="$work" LC_ALL=C CHROMA_LIBRARY_PATH="$package/lib/$library" \
  "$work/venv/bin/python" "$fixture/python-smoke.py" "$package" "$fixture"
# Test SDK-relative source discovery separately, without a library override.
env -i PATH=/usr/bin:/bin HOME="$work" PYTHONPATH="$package/share/chroma/python" \
  "$work/venv/bin/python" -c 'from chroma_render import Engine; e = Engine(); print(e.version); e.close()'
