"""CI gate: exact native pixels, independent encoded fidelity, timing and negative controls."""
import argparse
import json
import hashlib
from pathlib import Path
import traceback

from quality_fixtures import fixtures, run
from quality_media import (encoded_case, negative_controls, RGB_MAE_LIMIT, RGB_PSNR_MIN,
                           AUDIO_RMSE_LIMIT, AUDIO_CORRELATION_MIN)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--chroma', required=True)
    parser.add_argument('--verifier', required=True)
    parser.add_argument('--ffmpeg', required=True)
    parser.add_argument('--ffprobe', required=True)
    parser.add_argument('--font', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = dict(passed=False, cases=[], thresholds=dict(rgb_mae_max=RGB_MAE_LIMIT,
        rgb_psnr_min_db=RGB_PSNR_MIN, audio_rmse_max=AUDIO_RMSE_LIMIT, audio_correlation_min=AUDIO_CORRELATION_MIN))
    def save():
        (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    save()
    try:
        for name, composition, snapshots in fixtures(args.output, args.ffmpeg, args.font):
            case = dict(name=name, composition_sha256=hashlib.sha256(composition.read_bytes()).hexdigest(), stage='native')
            report['cases'].append(case)
            save()
            native = args.output / f'{name}-native.json'
            run([args.verifier, composition, native, '--reference-mode', 'reference'])
            data = json.loads(native.read_text())
            if data['reference_mode'] != 'reference' or data['frames'] != json.loads(composition.read_text())['canvas']['duration_frames']:
                raise AssertionError('incomplete native reference comparison')
            case['native'] = data
            case['stage'] = 'encoded'
            save()
            case['encoded'] = encoded_case(args, name, composition, snapshots)
            case['stage'] = 'passed'
            save()
        report['negative_controls'] = negative_controls(args)
        report['passed'] = True
    except Exception:
        report['error'] = traceback.format_exc()
        raise
    finally:
        save()


if __name__ == '__main__':
    main()
