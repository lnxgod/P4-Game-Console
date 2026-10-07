#!/usr/bin/env python3
"""Check all sixteen four-switch combinations in the actual Doom engine."""
import json, hashlib
from test_gameplay import run,tic,TESTS,PWAD
from walk_tests import Walk
manifest=json.loads((PWAD.parent/'build/base-geometry.json').read_text())
VAULT=manifest['vault'];SUPPORTS=manifest['supports']

class ScriptWalk(Walk):
    """Build inputs without relaunching the engine at every intermediate point."""
    def check(self,note):
        return {'players':[dict(self.player)]}

def switch_route(w,i):
    if i==0:
        points=[(0,128),(0,1056)];back=[(0,128),(128,128)];angle=90
    elif i==1:
        points=[(128,0),(1056,0)];back=[(128,0),(128,128)];angle=0
    elif i==2:
        points=[(128,-128),(0,-128),(0,-1056)]
        back=[(0,-128),(128,-128),(128,128)];angle=270
    else:
        points=[(-128,128),(-128,0),(-1056,0)]
        back=[(-128,0),(-128,128),(128,128)];angle=180
    for x,y in points:w.move(x,y)
    w.use(angle,wait=120)
    # Repeated presses cannot lower any other support or bypass the gate.
    for _ in range(3):w.use(angle,wait=8)
    for x,y in back:w.move(x,y)

def make_case(mask,order=None,player=None):
    w=ScriptWalk(f'bfg-mask-{mask:02d}')
    if player:w.player=dict(player)
    # Reach a safe arena hub without cutting through the closed central vault.
    x,y=w.player['x'],w.player['y']
    if abs(x)>704:
        w.move(128 if x>0 else -128,0)
        w.move(128 if x>0 else -128,128)
    elif abs(y)>704:
        w.move(0,128 if y>0 else -128)
        w.move(128,128 if y>0 else -128)
    w.move(128,128)
    for i in (order if order is not None else range(4)):
        if mask&(1<<i):switch_route(w,i)
    approach=next((i for i in range(4) if mask&(1<<i)),0)
    if approach==0:
        w.move(0,128);w.move(0,80);angle=270
    elif approach==1:
        w.move(128,0);w.move(80,0);angle=180
    elif approach==2:
        w.move(128,-128);w.move(0,-128);w.move(0,-80);angle=90
    else:
        w.move(-128,128);w.move(-128,0);w.move(-80,0);angle=0
    w.use(angle,wait=140)
    w.avoid_vault=False
    w.move(0,0,collision=True)
    return w.commands

