#!/usr/bin/env python3
"""Reproduce the 18s Chroma showcase. Python standard library + FFmpeg only."""
import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import random
import shutil
import subprocess
import urllib.request
import wave

SOURCES = {
    'ocean-source.mp4': 'https://videos.pexels.com/video-files/4193133/4193133-uhd_2562_1440_24fps.mp4',
    'Lato-Bold.ttf': 'https://raw.githubusercontent.com/google/fonts/main/ofl/lato/Lato-Bold.ttf',
    'Lato-Regular.ttf': 'https://raw.githubusercontent.com/google/fonts/main/ofl/lato/Lato-Regular.ttf',
    'OFL.txt': 'https://raw.githubusercontent.com/google/fonts/main/ofl/lato/OFL.txt',
}


SOURCE_HASHES = {
    'ocean-source.mp4': 'c98fda4bea3ba86b3e1c4cb25c77bc4f7c1d31cc4db31bf6825ce52f81728b29',
    'Lato-Bold.ttf': '8a0aace75d33794eece4b28187bfc1df0bbd2888b5d8a56e01788c8d65d16be1',
    'Lato-Regular.ttf': 'd636e4683231f931eda222d588e944d082bfd3bdba02f928bee461c0f185b251',
    'OFL.txt': '74ba064d03f1f1c4a952da936c3eb71866c34404916734de3cae73b34357e59e',
}

def music(path):
    """Original deterministic 100-BPM ambient/electronic cue, with a resolved tail."""
    rate, seconds = 48000, 18
    rng = random.Random(1984)
    audio = array.array('h')
    chords = [(130.8128, 164.8138, 195.9977), (110, 130.8128, 164.8138),
              (87.3071, 110, 130.8128), (97.9989, 123.4708, 146.8324)]
    melody = [523.251, 659.255, 783.991, 659.255, 440, 523.251, 659.255, 523.251]
    peak = 0
    for i in range(rate * seconds):
        t = i / rate
        beat = t % .6
        chord = chords[min(int(t / 4.8), 3)]
        attack = min(t / .8, 1)
        release = min(max((seconds - t) / 1.5, 0), 1)
        pad = sum(math.sin(2 * math.pi * f * t) + .23 * math.sin(2 * math.pi * f * 2 * t)
                  for f in chord) * .042
        bass = math.sin(2 * math.pi * chord[0] / 2 * t) * .10 * math.exp(-beat * 4)
        kick = math.sin(2 * math.pi * (48 * beat + 9 * (1 - math.exp(-beat * 35)))) * math.exp(-beat * 23) * .20
        tick = (rng.random() * 2 - 1) * math.exp(-(t % .3) * 180) * .035
        note = melody[int(t / .3) % 8]
        pluck = (math.sin(2 * math.pi * note * t) + .25 * math.sin(2 * math.pi * note * 2 * t)) * math.exp(-(t % .3) * 10) * .042
        left = (pad + bass + kick + tick + pluck) * attack * release
        right = (pad + bass + kick - tick + pluck * .82) * attack * release
        for value in (left, right):
            sample = round(max(-1, min(1, value)) * 32767)
            peak = max(peak, abs(sample))
            audio.append(sample)
    if __import__('sys').byteorder != 'little':
        audio.byteswap()
    with wave.open(str(path), 'wb') as out:
        out.setparams((2, 2, rate, 0, 'NONE', 'not compressed'))
        out.writeframes(audio.tobytes())
    return peak / 32767


def composition():
    # Colors are authored in sRGB, then converted to Chroma's linear-light contract.
    def color(hex_value, alpha=1):
        values = [int(hex_value[i:i+2], 16) / 255 for i in (0, 2, 4)]
        return [(v / 12.92 if v <= .04045 else ((v + .055) / 1.055) ** 2.4) for v in values] + [alpha]
    ink, cream, mint, muted = '102D32', 'F1F0E9', 'B5F1CE', '527076'
    layers = []
    def add(id, kind, start, end, **fields):
        layer = dict(id=id, type=kind, start_frame=start, end_frame=end, **fields)
        layers.append(layer)
        return layer
    def motion(start, end, distance=32, delay=0):
        length = end-start
        return [dict(type='translation', keyframes=[
            dict(frame=delay, value=[0,distance], easing='ease_out'),
            dict(frame=delay+22, value=[0,0])]),
            dict(type='opacity', keyframes=[dict(frame=delay, value=0, easing='ease_out'),
                dict(frame=delay+18, value=1), dict(frame=max(delay+19,length-14), value=1),
                dict(frame=length-1, value=0)])]
    def text(id, value, x, y, size, start, end, shade=ink, bold=False, delay=0):
        return add(id,'text',start,end,font='bold' if bold else 'regular',text=value,
                   font_size=size,position=[x,y],color=color(shade),effects=motion(start,end,26,delay))
    def shape(id,bounds,start,end,shade,radius=0,alpha=1,effects=None):
        kw = dict(shape='rounded_rectangle' if radius else 'rectangle',bounds=bounds,fill=color(shade,alpha))
        if radius: kw['corner_radius']=radius
        if effects: kw['effects']=effects
        return add(id,'shape',start,end,**kw)
    def fade():
        return [dict(type='opacity',keyframes=[dict(frame=0,value=0,easing='ease_in_out'),dict(frame=14,value=1)])]
    # Act I: cinematic full-bleed opening.
    add('ocean-opening','video',0,162,asset='ocean',fit='cover')
    add('opening-grade','solid',0,162,color=color(ink,.48))
    text('opening-label','CHROMA  /  MOTION STUDIES',100,110,25,0,162,cream,True)
    text('opening-title-a','Make room',95,500,142,0,162,cream,True,6)
    text('opening-title-b','for wonder.',95,660,142,0,162,mint,True,12)
    text('opening-caption','A small film. A bigger possibility.',104,842,32,0,162,cream,False,20)
    shape('opening-rule',[104,900,240,4],0,162,mint,effects=motion(0,162,0,20))
    text('opening-number','01 — THE FEELING',1510,980,22,0,162,cream)
    # Act II: offset editorial footage card on ivory, with animated typography.
    add('editorial-background','solid',144,330,color=color(cream),effects=fade())
    text('editorial-label','THE ART OF SIMPLE',100,150,25,150,330,muted,True)
    text('editorial-title-a','Every frame.',95,395,96,150,330,ink,True,6)
    text('editorial-title-b','A feeling.',95,505,96,150,330,ink,True,12)
    text('editorial-description-a','Footage, type, and rhythm.',104,658,32,150,330,muted,False,18)
    text('editorial-description-b','Composed with intention.',104,706,32,150,330,muted,False,22)
    shape('editorial-card',[920,212,900,592],150,330,ink,22,effects=motion(150,330,45))
    add('editorial-ocean','video',150,330,asset='ocean',source_start_frame=150,fit='cover',effects=[
        dict(type='scale',value=[.45,.45],origin=[960,540]),
        dict(type='translation',keyframes=[dict(frame=0,value=[410,-18],easing='ease_out'),dict(frame=30,value=[410,-40])]),
        *fade()])
    text('editorial-card-title','A slower kind of beautiful.',957,780,28,150,330,cream,False,12)
    text('editorial-card-note','ATLANTIC / FIELD NOTES',941,860,22,150,330,muted,True,16)
    text('editorial-number','02 — THE DETAILS',1510,980,22,150,330,muted)
    # Act III: staggered feature cards, native shapes and text.
    add('system-background','solid',312,456,color=color(ink),effects=fade())
    text('system-label','IDEAS INTO MOTION',100,140,25,318,456,mint,True)
    text('system-title','Built to move.',95,345,124,318,456,cream,True,4)
    for index,(title,body,accent) in enumerate([
        ('Compose.','A clear, declarative scene.','B5F1CE'),
        ('Animate.','Timing with a human feel.','D4CCF2'),
        ('Deliver.','Picture and sound, together.','F2C5A3')]):
        x=100+580*index
        effects=motion(318,456,55,index*5+4)
        shape(f'feature-{index}',[x,470,550,370],318,456,cream,24,effects=effects)
        shape(f'feature-accent-{index}',[x+36,510,64,6],318,456,accent,effects=effects)
        text(f'feature-num-{index}',f'0{index+1}',x+36,592,24,318,456,muted,True,index*5+5)
        text(f'feature-title-{index}',title,x+32,680,56,318,456,ink,True,index*5+7)
        text(f'feature-body-{index}',body,x+36,758,27,318,456,muted,False,index*5+9)
    text('system-footnote','NATIVE RENDERING  /  FRAME-EXACT TIMING  /  OPEN SOURCE',104,976,22,318,456,cream)
    # Act IV: return to footage; final lockup stays readable at the last frame.
    add('ocean-closing','video',438,540,asset='ocean',source_start_frame=360,fit='cover',effects=fade())
    add('closing-grade','solid',438,540,color=color(ink,.67),effects=fade())
    text('closing-label','CHROMA',100,150,35,444,554,mint,True)
    text('closing-title-a','From idea',95,480,138,444,554,cream,True,4)
    text('closing-title-b','to motion.',95,638,138,444,554,cream,True,9)
    shape('closing-pill',[104,764,484,72],444,554,mint,36,effects=motion(444,554,20,14))
    text('closing-pill-text','MAKE SOMETHING MOVE',134,812,24,444,554,ink,True,14)
    text('closing-credit','Footage: Nino Souza / Pexels  •  Original score: Chroma study',104,989,22,444,554,cream,False,14)
    # Clamp final scene to the document while keeping its exit fade beyond the cut.
    for layer in layers: layer['end_frame']=min(layer['end_frame'],540)
    return dict(schema_version=1,canvas=dict(width=1920,height=1080,frame_rate=dict(numerator=30,denominator=1),duration_frames=540),
                assets=[dict(id='ocean',type='video',path='assets/ocean.mp4'),
                        dict(id='bold',type='font',path='assets/Lato-Bold.ttf'),
                        dict(id='regular',type='font',path='assets/Lato-Regular.ttf'),
                        dict(id='music',type='audio',path='assets/music.wav')],layers=layers,
                audio_tracks=[dict(id='original-score',asset='music',start_frame=0,end_frame=540,gain=.85)])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=Path('artifacts/showcase'))
    parser.add_argument('--ffmpeg',default='ffmpeg')
    args=parser.parse_args()
    assets=args.output/'assets'
    assets.mkdir(parents=True,exist_ok=True)
    for name,url in SOURCES.items():
        target=assets/name
        if not target.exists():
            print('Downloading',name,flush=True)
            with urllib.request.urlopen(url,timeout=90) as source, target.with_suffix(target.suffix+'.part').open('wb') as dest:
                shutil.copyfileobj(source,dest)
            target.with_suffix(target.suffix+'.part').replace(target)
        if hashlib.sha256(target.read_bytes()).hexdigest() != SOURCE_HASHES[name]:
            raise ValueError('Source checksum mismatch: ' + str(target))
    subprocess.run([args.ffmpeg,'-hide_banner','-loglevel','error','-y','-i',str(assets/'ocean-source.mp4'),
                    '-t','18','-vf','scale=1920:1080:force_original_aspect_ratio=increase,crop=1920:1080,fps=30',
                    '-an','-c:v','libx264','-preset','fast','-crf','18','-pix_fmt','yuv420p',str(assets/'ocean.mp4')],check=True)
    peak=music(assets/'music.wav')
    (args.output/'composition.json').write_text(json.dumps(composition(),indent=2)+'\n')
    manifest=dict(sources=SOURCES,music=dict(description='Original deterministic 100 BPM synthesized cue',seed=1984,peak=peak),
                  sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(assets.iterdir()) if p.is_file()})
    (args.output/'assets.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(args.output/'composition.json')

if __name__=='__main__': main()
