"""Native quality gate; encoded-media checks are added in the next checkpoint."""
import argparse
import json
from pathlib import Path
import traceback

from quality_fixtures import fixtures, run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--chroma', required=True)
    parser.add_argument('--verifier', required=True)
    parser.add_argument('--ffmpeg', required=True)
    parser.add_argument('--font', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = dict(passed=False, cases=[])
    try:
        for name, composition, snapshots in fixtures(args.output, args.ffmpeg, args.font):
            native = args.output / f'{name}-native.json'
            run([args.verifier, composition, native, '--reference-mode', 'reference'])
            data = json.loads(native.read_text())
            if data['reference_mode'] != 'reference' or data['frames'] != json.loads(composition.read_text())['canvas']['duration_frames']:
                raise AssertionError('incomplete native reference comparison')
            report['cases'].append(dict(name=name, native=data))
        report['passed'] = True
    except Exception:
        report['error'] = traceback.format_exc()
        raise
    finally:
        (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
