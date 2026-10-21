"""Exercise project creation through the actual CLI, including no-clobber behavior."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import wave

p = argparse.ArgumentParser()
p.add_argument('--chroma', required=True)
p.add_argument('--render', action='store_true')
a = p.parse_args()
cli = str(Path(a.chroma).resolve())


def run(*args, status=0, cwd=None):
    result = subprocess.run([cli, *map(str,args)], capture_output=True, text=True, cwd=cwd, timeout=90)
    assert result.returncode == status, (args,result.returncode,result.stdout,result.stderr)
    return result


with tempfile.TemporaryDirectory(prefix='chroma init ') as folder:
    root = Path(folder)
    assert 'title-sequence' in run('init','--list-templates').stdout
    run('init','--help')
    for args in [[], ['--list-templates','extra'], [root/'bad','--template','unknown'],
                 [root/'bad','--accent','#gg2233'], [root/'bad','--accent','112233'],
                 [root/'bad','--title',''], [root/'bad','--title'],
                 [root/'bad','--title','one','--title','two'], [root/'bad','--unknown','value'],
                 [root/'bad','--music','anything.wav','--no-music']]:
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
    run('init',project,'--title',title,'--subtitle','Your idea, in motion.','--accent','#Ae39fF')
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
        run('render',moved/'scene.json','--output',moved/'video.mp4','--progress','json')
        assert (moved/'video.mp4').stat().st_size>1000
    # Two competing initializations must never remove or merge the winner's project.
    race = root/'race'
    first = subprocess.Popen([cli,'init',str(race)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    second = subprocess.Popen([cli,'init',str(race)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    first.communicate(timeout=30); second.communicate(timeout=30)
    assert sorted([first.returncode,second.returncode])==[0,4]
    run('validate',race/'scene.json')
print('PASS: init options, assets, relocation, rendering, and no-clobber races')
