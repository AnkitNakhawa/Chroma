#!/usr/bin/env python3
"""Independently verify a full showcase encode and cache-equivalent scene frames."""
import argparse
import array
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def capture(command):
    return subprocess.check_output(command)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--video',type=Path,required=True)
    p.add_argument('--composition',type=Path,required=True)
    p.add_argument('--chroma',type=Path,required=True)
    p.add_argument('--ffmpeg',default='ffmpeg')
    p.add_argument('--ffprobe',default='ffprobe')
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    info=json.loads(capture([args.ffprobe,'-v','error','-count_frames','-show_streams','-show_format','-of','json',str(args.video)]))
    videos=[s for s in info['streams'] if s['codec_type']=='video']
    audios=[s for s in info['streams'] if s['codec_type']=='audio']
    assert len(videos)==len(audios)==1
    video,audio=videos[0],audios[0]
    assert (video['codec_name'],video['width'],video['height'],video['pix_fmt'],video['avg_frame_rate'],video['nb_read_frames']) == ('h264',1920,1080,'yuv420p','30/1','540')
    assert audio['codec_name']=='aac' and int(audio['sample_rate'])==48000 and audio['channels']==2
    assert abs(float(video['duration'])-18)<1e-6 and abs(float(audio['duration'])-18)<1/48000
    timestamps=json.loads(capture([args.ffprobe,'-v','error','-select_streams','v:0','-show_frames',
                                  '-show_entries','frame=best_effort_timestamp_time','-of','json',str(args.video)]))['frames']
    assert len(timestamps)==540
    assert all(abs(float(frame['best_effort_timestamp_time'])-i/30)<1e-6 for i,frame in enumerate(timestamps))
    # Decode all video frames independently, fail on decoder errors, retain a content digest.
    decoded=capture([args.ffmpeg,'-hide_banner','-loglevel','error','-xerror','-i',str(args.video),'-map','0:v:0','-an','-f','framemd5','-'])
    assert len([line for line in decoded.splitlines() if not line.startswith(b'#')])==540
    pcm=capture([args.ffmpeg,'-hide_banner','-loglevel','error','-xerror','-i',str(args.video),'-map','0:a:0','-t','18','-f','s16le','-'])
    samples=array.array('h',pcm)
    if sys.byteorder!='little': samples.byteswap()
    assert len(samples)==864000*2
    peak=max(abs(x) for x in samples)/32768
    rms=lambda values:(sum(x*x for x in values)/len(values))**.5/32768
    body_rms=rms(samples[48000*2:48000*4])
    tail_rms=rms(samples[-4800*2:])
    assert .05 < peak < .9 and body_rms>.02 and tail_rms<body_rms*.15
    assert any(samples[i]!=samples[i+1] for i in range(0,len(samples),2))
    hashes={}
    encoded_frame_rgb_mae={}
    with tempfile.TemporaryDirectory(prefix='chroma-showcase-verify-') as temporary:
        for frame in (72,240,390,510):
            paths=[Path(temporary)/f'{frame}-{variant}.ppm' for variant in ('cached','uncached')]
            for index,path in enumerate(paths):
                command=[str(args.chroma.resolve()),'frame',str(args.composition),'--frame',str(frame),'--output',str(path)]
                if index: command += ['--image-cache-bytes','0','--text-cache-bytes','0']
                subprocess.run(command,check=True,stdout=subprocess.DEVNULL)
            first,second=[path.read_bytes() for path in paths]
            assert first==second, f'cache changed pixels at frame {frame}'
            hashes[str(frame)]=hashlib.sha256(first).hexdigest()
            header=first.split(b'\n',3)
            assert header[:3]==[b'P6',b'1920 1080',b'255']
            rgb=capture([args.ffmpeg,'-hide_banner','-loglevel','error','-xerror','-i',str(args.video),
                         '-vf',f'select=eq(n\\,{frame})','-frames:v','1','-pix_fmt','rgb24','-f','rawvideo','-'])
            assert len(rgb)==len(header[3])==1920*1080*3
            mae=sum(abs(a-b) for a,b in zip(rgb,header[3]))/len(rgb)
            assert mae<3, f'encoded frame {frame} differs from native frame: {mae}'
            encoded_frame_rgb_mae[str(frame)]=mae
    result=dict(video=dict(width=1920,height=1080,frames=540,fps=30,seconds=18,codec='h264',timestamp_check='every frame'),
                audio=dict(codec='aac',rate=48000,channels=2,samples=864000,peak=peak,body_rms=body_rms,tail_rms=tail_rms),
                encoded_frame_rgb_mae=encoded_frame_rgb_mae,decoded_video_framemd5_sha256=hashlib.sha256(decoded).hexdigest(),cache_equivalent_frame_sha256=hashes,
                composition_sha256=hashlib.sha256(args.composition.read_bytes()).hexdigest(),
                video_sha256=hashlib.sha256(args.video.read_bytes()).hexdigest())
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__': main()
