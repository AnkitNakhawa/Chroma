"""Terminal render reports: success, failures, cancellation and safe file publication."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile

p=argparse.ArgumentParser()
p.add_argument('--chroma',required=True)
p.add_argument('--media',action='store_true')
a=p.parse_args(); cli=str(Path(a.chroma).resolve())


def run(*args,status=0):
 r=subprocess.run([cli,*map(str,args)],capture_output=True,text=True,timeout=40)
 assert r.returncode==status,(args,r.returncode,r.stdout,r.stderr)
 return r


with tempfile.TemporaryDirectory(prefix='chroma report ') as folder:
 root=Path(folder); scene=root/'scene.json'; output=root/'output.mp4'; report=root/'report.json'
 doc=dict(schema_version=1,canvas=dict(width=32,height=18,frame_rate=dict(numerator=30,denominator=1),duration_frames=6),layers=[dict(id='bg',type='solid',start_frame=0,end_frame=6,color=[.1,.3,.5,1])])
 scene.write_text(json.dumps(doc)); original=scene.read_bytes()
 def render(path=scene,**kw):
  return run('render',path,'--output',output,'--report',report,**kw)
 def load(status):
  d=json.loads(report.read_text()); assert d['schema_version']==1 and d['kind']=='chroma.render-report'
  assert d['status']==status
  assert d['timings']['wall_nanoseconds']>=0
  assert d['memory']['scope']=='process_lifetime'
  assert d['memory']['process_peak_rss_bytes'] is None or d['memory']['process_peak_rss_bytes']>0
  assert not list(root.glob('.chroma-report-*'))
  return d
 render(root/'missing.json',status=4); d=load('failed')
 assert d['diagnostics'][0]['code']=='composition.unreadable' and not d['output']['published']
 bad=root/'bad.json'; bad.write_text('{')
 render(bad,status=3); assert load('failed')['diagnostics']
 for alias in [scene,output]:
  run('render',scene,'--output',output,'--report',alias,status=2)
 assert scene.read_bytes()==original
 link=root/'alias'; link.symlink_to(scene)
 run('render',scene,'--output',output,'--report',link,status=2)
 hard=root/'hard'; os.link(scene,hard)
 run('render',scene,'--output',output,'--report',hard,status=2)
 assert scene.read_bytes()==original
 run('render',scene,'--output',output,'--report',root/'absent/report.json',status=4)
 assert not output.exists()
 run('render',scene,'--output',output,'--report',report,'--report',report,status=2)
 run('render',scene,'--output',output,'--report','-',status=2)
 if not a.media:
  render(status=4); d=load('failed'); assert d['profile'] is None and d['diagnostics'][0]['code']=='render.failed'
 else:
  r=run('render',scene,'--output',output,'--report',report,'--progress','json')
  assert json.loads(r.stdout.splitlines()[-1])['phase']=='completed'
  d=load('succeeded'); assert d['output']['published'] and d['output']['size_bytes']==output.stat().st_size
  assert d['output']['metadata']['width']==32 and d['output']['metadata']['duration']=={'numerator':1,'denominator':5}
  assert d['profile']['frames_encoded']==6 and d['cache']['usage'] is not None
  assert d['timings']['stages_complete'] and d['timings']['milliseconds']['total']>=0
  unused=json.loads(scene.read_text()); unused['assets']=[dict(id='unused',type='image',path='absent.png')]
  bad.write_text(json.dumps(unused)); render(bad)
  assert load('succeeded')['diagnostics'][0]['severity']=='warning'
  marker=b'existing'; output.write_bytes(marker)
  missing=json.loads(scene.read_text()); missing['assets']=[dict(id='missing',type='image',path='missing.png')]
  missing['layers']=[dict(id='image',type='image',asset='missing',start_frame=0,end_frame=6)]
  bad.write_text(json.dumps(missing)); render(bad,status=4); d=load('failed')
  assert d['phase']=='preflight' and d['diagnostics'][0]['code']=='asset.missing'
  assert d['diagnostics'][0]['asset_id']=='missing' and d['diagnostics'][0]['suggested_action']
  assert d['profile'] is None and d['cache']['usage'] is None and not d['output']['published']
  assert output.read_bytes()==marker
  # Reports cannot overwrite an input asset, including an unused one.
  missing['assets'][0]['path']=str(report); bad.write_text(json.dumps(missing))
  previous=report.read_bytes(); render(bad,status=2); assert report.read_bytes()==previous
  asset=root/'owned-image.ppm'; asset.write_bytes(b'owned input')
  missing['assets'][0]['path']=str(asset); bad.write_text(json.dumps(missing))
  run('render',bad,'--output',output,'--report',report,'--cache-report',asset,status=2)
  assert asset.read_bytes()==b'owned input' and report.read_bytes()==previous
  # A side-report failure after video commit must disclose output publication.
  run('render',scene,'--output',output,'--report',report,'--cache-report',root/'absent/cache.json',status=4)
  d=load('failed'); assert d['output']['published'] and d['profile'] is not None
  assert any(item['code']=='job.failed' for item in d['diagnostics'])
  for sig,exit_code in [(signal.SIGINT,130),(signal.SIGTERM,143)]:
   doc['canvas']['duration_frames']=100000;doc['layers'][0]['end_frame']=100000
   scene.write_text(json.dumps(doc)); output.write_bytes(marker)
   process=subprocess.Popen([cli,'render',str(scene),'--output',str(output),'--report',str(report),'--progress','json'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
   first=json.loads(process.stdout.readline()); assert first['phase']=='preparing'
   process.send_signal(sig); process.communicate(timeout=20)
   assert process.returncode==exit_code
   d=load('cancelled'); assert not d['output']['published'] and output.read_bytes()==marker
   assert d['progress']['frames_encoded']<100000
  # A closed progress consumer is cancellation, but the terminal file still arrives.
  output.write_bytes(marker)
  process=subprocess.Popen([cli,'render',str(scene),'--output',str(output),'--report',str(report),'--progress','json'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  assert json.loads(process.stdout.readline())['phase']=='preparing'
  process.stdout.close(); process.wait(timeout=20); process.stderr.close()
  assert process.returncode==130 and load('cancelled')['status']=='cancelled'
  assert output.read_bytes()==marker
 assert not list(root.glob('.chroma-render-*'))
print('PASS: structured render reports and safe terminal outcomes')
