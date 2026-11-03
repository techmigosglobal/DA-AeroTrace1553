"""Compare generated PNGs with immutable reference images (no image rewriting)."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageChops, ImageStat

parser = argparse.ArgumentParser()
parser.add_argument('render_directory', type=Path)
parser.add_argument('--output', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
results = []
for index, reference in enumerate(sorted((root / 'UI_Screens').glob('*.png')), 1):
    render = args.render_directory / f'{index:02}.png'
    result = {'reference': reference.name, 'render': str(render)}
    if not render.exists():
        result['status'] = 'missing'
    else:
        with Image.open(reference) as expected, Image.open(render) as actual:
            result['reference_size'] = list(expected.size)
            result['render_size'] = list(actual.size)
            if actual.size != expected.size:
                result['status'] = 'size_mismatch'
            else:
                difference = ImageChops.difference(expected.convert('RGB'), actual.convert('RGB'))
                result['mean_absolute_channel_error_0_255'] = round(sum(ImageStat.Stat(difference).mean) / 3, 4)
                result['status'] = 'identical' if difference.getbbox() is None else 'different'
    results.append(result)
report = json.dumps({'note': 'Raw pixel comparison, no masking or resizing. Dynamic text/data contributes to differences. Render success is not visual acceptance.', 'screens': results}, indent=2)
if args.output:
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(report + '\n', encoding='utf-8')
    print(f'{len(results)} screens compared; {sum(r["status"] == "identical" for r in results)} identical. Report: {args.output}')
else:
    print(report)
