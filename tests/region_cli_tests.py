"""Exercise graphics-mode dispatch, pixel equivalence and telemetry through the CLI."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

chroma=sys.argv[1]
with tempfile.TemporaryDirectory(prefix='chroma-regions-') as temporary:
    root=Path(temporary)
    doc=dict(schema_version=1,canvas=dict(width=128,height=72,frame_rate=dict(numerator=30,denominator=1),duration_frames=4),layers=[
        dict(id='background',type='solid',start_frame=0,end_frame=4,color=[.1,.2,.3,1]),
        dict(id='shape',type='shape',shape='ellipse',bounds=[7.25,8.5,22.5,18.75],fill=[.8,.2,.5,.7],start_frame=0,end_frame=4,
             effects=[dict(type='translation',keyframes=[dict(frame=0,value=[0,0]),dict(frame=3,value=[8.25,-2.5])]),dict(type='opacity',value=.7)])])
    if len(sys.argv)>2:
        doc['assets']=[dict(id='font',type='font',path=sys.argv[2])]
        doc['layers'].append(dict(id='text',type='text',font='font',text='ffi Á',font_size=16,position=[40.5,42.25],color=[1,.7,.2,.8],
                                  start_frame=0,end_frame=4,effects=[dict(type='translation',value=[.2,-.35])]))
    composition=root/'scene.json';composition.write_text(json.dumps(doc))
    samples={}
    for mode in ('bounded','full-frame'):
        for frame in range(4):
            out=root/f'{mode}-{frame}.ppm'
            subprocess.run([chroma,'frame',str(composition),'--frame',str(frame),'--output',str(out),'--graphics-mode',mode],check=True,stdout=subprocess.DEVNULL)
        profile=root/f'{mode}.json'
        subprocess.run([chroma,'benchmark',str(composition),'--iterations','1','--output',str(profile),'--graphics-mode',mode],check=True,stdout=subprocess.DEVNULL)
        samples[mode]=json.loads(profile.read_text())['iterations'][0]['graphics']
    assert all((root/f'bounded-{f}.ppm').read_bytes()==(root/f'full-frame-{f}.ppm').read_bytes() for f in range(4))
    assert samples['bounded']['bounded_operations']>0 and samples['bounded']['full_frame_operations']==0
    assert samples['full-frame']['bounded_operations']==0 and samples['full-frame']['full_frame_operations']>0
    for key in ('source_pixels','transform_pixels','composite_pixels'):
        assert 0 < samples['bounded'][key] < samples['full-frame'][key]/2
    for options in (['--graphics-mode','bad'],['--graphics-mode','bounded','--graphics-mode','full-frame'],['--graphics-mode']):
        result=subprocess.run([chroma,'frame',str(composition),'--frame','0','--output',str(root/'invalid.ppm'),*options],capture_output=True)
        assert result.returncode==2
print('Graphics CLI pixels, dispatch, counters and invalid options passed')
