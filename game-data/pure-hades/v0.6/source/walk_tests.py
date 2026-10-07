#!/usr/bin/env python3
"""Replay genuine movement/use inputs and verify engine-written save states."""
from test_gameplay import run,tic,TESTS,RUNTIME
import json,math

class Walk:
    def __init__(self,label,extra_args=()):
        self.label=label;self.commands=[];self.number=0;self.extra_args=extra_args
        self.player={'x':-384.,'y':-384.,'angle':45.}
        self.avoid_vault=True
        self.records=[]
    def check(self,note):
        self.number+=1
        r=run(f'{self.label}-{self.number:02d}-{note}',self.commands,extra_args=self.extra_args)
        self.last=r['save'];self.player=self.last['players'][0] if self.last['players'] else None
        self.records.append(r['label'])
        print(r['label'],json.dumps(self.player or self.last),flush=True)
        return self.last
    def face(self,angle):
        turn=round((angle-self.player['angle'])*256/360)
        turn=(turn+128)%256-128
        self.commands.append(tic(turn=turn));self.player['angle']=angle
    def move(self,x,y,note='move',collision=False):
        def leg(axis,target):
            d=target-self.player[axis]
            if abs(d)<.6:return
            angle=(0 if d>0 else 180) if axis=='x' else (90 if d>0 else 270)
            self.face(angle)
            budget=round(abs(d)*3+2)
            count,remain=divmod(budget,50)
            self.commands += [tic(forward=50)]*count
            if remain:self.commands.append(tic(forward=remain))
            self.commands += [tic()]*70
            self.player[axis]=target
        for axis,target in [('x',x),('y',y)]:
            other='y' if axis=='x' else 'x'
            start=self.player[axis];cross=self.player[other]
            if self.avoid_vault and not collision and abs(cross)<80 and ((start>80 and target<-80) or (start<-80 and target>80)):
                pre=128 if start>0 else -128
                leg(axis,pre);leg(other,128);leg(axis,-pre);leg(other,cross)
            leg(axis,target)
        result=self.check(note)
        if not collision:
            assert math.hypot(self.player['x']-x,self.player['y']-y)<3,(note,self.player,(x,y))
        return result
    def use(self,angle,wait=70,note='use'):
        self.face(angle);self.commands += [tic(buttons=2)]+[tic()]*wait
        return self.check(note)
    def idle(self,ticks,note='wait'):
        self.commands += [tic()]*ticks
        return self.check(note)
