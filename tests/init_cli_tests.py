"""Exercise project creation through the actual CLI, including no-clobber behavior."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import wave

from quality_fixtures import scale_scene

p = argparse.ArgumentParser()
p.add_argument('--chroma', required=True)
p.add_argument('--render', action='store_true')
p.add_argument('--render-scale', type=float, choices=[0.25,1.0], default=1.0)
a = p.parse_args()
cli = str(Path(a.chroma).resolve())


def run(*args, status=0, cwd=None):
    result = subprocess.run([cli, *map(str,args)], capture_output=True, text=True, cwd=cwd, timeout=90)
    assert result.returncode == status, (args,result.returncode,result.stdout,result.stderr)
    return result


def rendering_scene(project):
    scene = project/'scene.json'
    if a.render_scale == 1:
        return scene
    # Debug CI exercises the same timelines, assets and operations at draft size.
    # Release CTest and relocated package smoke retain all 180 frames at 720p.
    preview = project/'preview-scene.json'
    preview.write_text(json.dumps(scale_scene(json.loads(scene.read_text()),a.render_scale)))
    return preview


with tempfile.TemporaryDirectory(prefix='chroma init ') as folder:
    root = Path(folder)
    listing = run('init','--list-templates').stdout
    assert all(name in listing for name in ['title-sequence','product-promo','captioned-footage'])
    run('init','--help')
    for args in [[], [""], ['--list-templates','extra'], [root/'bad','--template','unknown'],
                 [root/'bad','--accent','#gg2233'], [root/'bad','--accent','112233'],
                 [root/'bad','--title',''], [root/'bad','--title'],
                 [root/'bad','--title','one','--title','two'], [root/'bad','--unknown','value'],
                 [root/'bad','--music','anything.wav','--no-music'],
                 [root/'bad','--image','file'], [root/'bad','--footage','file']]:
        run('init',*args,status=2)
        assert not (root/'bad').exists()
    for kind in ['directory','file','symlink','dangling']:
        target = root/kind
        if kind=='directory': target.mkdir(); (target/'keep').write_text('untouched')
        elif kind=='file': target.write_text('untouched')
        else: target.symlink_to(root/('file' if kind=='symlink' else 'absent'))
        run('init',target,status=4)
    assert (root/'directory/keep').read_text()=='untouched'
    assert (root/'file').read_text()=='untouched' and (root/'dangling').is_symlink()
    run('init',root/'bad','--font',root/'absent.ttf',status=4)
    assert not (root/'bad').exists()
    (root/'empty.ttf').touch()
    run('init',root/'bad','--font',root/'empty.ttf',status=4)
    assert not (root/'bad').exists()
    run('init',root/'missing-parent/project',status=4)
    assert not (root/'missing-parent').exists()
    project = root/'Café project with spaces'
    title = 'A "quoted" title\nwith café & a backslash \\.'
    run('init',project,'--title',title,'--subtitle','Your idea, in motion.','--accent','#Ae39fF',cwd=root)
    doc = json.loads((project/'scene.json').read_text())
    assert doc['canvas']['duration_frames']==180
    assert next(l for l in doc['layers'] if l['id']=='title')['text']==title
    assert next(l for l in doc['layers'] if l['id']=='accent-line')['fill']==[174/255,57/255,1,1]
    assert hashlib.sha256((project/'assets/font.ttf').read_bytes()).hexdigest()=='d636e4683231f931eda222d588e944d082bfd3bdba02f928bee461c0f185b251'
    assert 'SIL OPEN FONT LICENSE' in (project/'assets/FONT-LICENSE.txt').read_text()
    assert (project/'assets/CHROMA-LICENSE.txt').is_file()
    with wave.open(str(project/'assets/music.wav')) as audio:
        assert (audio.getnchannels(),audio.getsampwidth(),audio.getframerate(),audio.getnframes())==(2,2,48000,288000)
        assert any(audio.readframes(audio.getnframes()))
    custom = root/'custom'
    run('init',custom,'--font',project/'assets/font.ttf','--no-music')
    assert not (custom/'assets/FONT-LICENSE.txt').exists()
    assert not (custom/'assets/music.wav').exists()
    assert 'audio_tracks' not in json.loads((custom/'scene.json').read_text())
    supplied = root/'supplied'
    run('init',supplied,'--music',project/'assets/music.wav')
    assert (supplied/'assets/music.wav').read_bytes()==(project/'assets/music.wav').read_bytes()
    moved = root/'moved project'
    project.rename(moved)
    run('validate',moved/'scene.json')
    run('validate',custom/'scene.json')
    if a.render:
        assert json.loads(run('preflight',moved/'scene.json','--json').stdout)['ready']
        run('render',rendering_scene(moved),'--output',moved/'video.mp4','--progress','json')
        assert (moved/'video.mp4').stat().st_size>1000
    # Every preset is render-ready, with relative assets and editable text.
    product, captioned = root/'product', root/'captioned'
    run('init',product,'--template','product-promo')
    run('init',captioned,'--template','captioned-footage')
    image = root/'product image.ppm'
    image.write_bytes(b'P6\n24 24\n255\n' + bytes([80,180,210])*24*24)
    with_image, with_footage = root/'image project', root/'footage project'
    run('init',with_image,'--template','product-promo','--image',image,'--title','Your product')
    run('init',with_footage,'--template','captioned-footage','--footage',captioned/'assets/footage.mp4',
        '--subtitle','Your first caption')
    assert (with_image/'assets/product.ppm').read_bytes()==image.read_bytes()
    assert (with_footage/'assets/footage.mp4').read_bytes()==(captioned/'assets/footage.mp4').read_bytes()
    assert 'Your first caption' in (with_footage/'scene.json').read_text()
    for preset, flag in [('product-promo','--image'),('captioned-footage','--footage')]:
        run('init',root/'bad','--template',preset,flag,root/'missing-media',status=4)
        assert not (root/'bad').exists()
    for project in [product,captioned,with_image,with_footage]:
        run('validate',project/'scene.json')
        doc = json.loads((project/'scene.json').read_text())
        for asset in doc['assets']:
            assert not Path(asset['path']).is_absolute()
            assert (project/asset['path']).is_file()
        if a.render:
            assert json.loads(run('preflight',project/'scene.json','--json').stdout)['ready']
            result = run('render',rendering_scene(project),'--output',project/'video.mp4','--progress','json')
            events = [json.loads(line) for line in result.stdout.splitlines()]
            assert events[-1]['phase']=='completed' and events[-1]['frames_encoded']==180
            assert events[-1]['audio_samples_encoded']==288000
            info = json.loads(run('probe',project/'video.mp4').stdout)
            assert (info['width'],info['height'],info['frame_count'],info['duration'])==(round(1280*a.render_scale),round(720*a.render_scale),180,{'numerator':6,'denominator':1})
    if a.render:
        # Inspect all three caption cues and the animated title/product layouts.
        for project in [moved,product,captioned]:
            for frame in [35,95,155]:
                for mode in ['bounded','full-frame']:
                    run('frame',rendering_scene(project),'--frame',frame,'--output',project/(mode+'.ppm'),'--graphics-mode',mode)
                assert (project/'bounded.ppm').read_bytes()==(project/'full-frame.ppm').read_bytes(), (project,frame)
    # Two competing initializations must never remove or merge the winner's project.
    race = root/'race'
    first = subprocess.Popen([cli,'init',str(race)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    second = subprocess.Popen([cli,'init',str(race)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    first.communicate(timeout=30); second.communicate(timeout=30)
    assert sorted([first.returncode,second.returncode])==[0,4]
    run('validate',race/'scene.json')
print('PASS: init options, assets, relocation, rendering, and no-clobber races')
