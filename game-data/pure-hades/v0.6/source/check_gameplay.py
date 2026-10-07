"""Bounded tests through unmodified Chocolate Doom demos and game-written saves."""
from pathlib import Path
import json,struct
import test_gameplay as game
from test_gameplay import tic
from walk_tests import Walk
from bfg_helpers import make_case,VAULT,SUPPORTS,ScriptWalk
ROOT=Path(__file__).resolve().parents[1]
results={'maps':[],'scope':'Unmodified Chocolate Doom 3.1.1; ordinary recorded inputs and engine-written saves. No cheats, teleports or gameplay patches.'}
weapons={1:8,2:4,3:5,4:8,5:8}
ammo_slot={1:1,2:3,3:2,4:1,5:1}
for number in range(1,6):
 game.MAPNO=number
 commands=[tic(forward=25)]*20+[tic()]*35
 before=game.run(f'map{number}-weapon-collected',commands)['save']['players'][0]
 wanted=weapons[number]
 assert before['weapons'][wanted] and before['readyweapon']==wanted and before['cheats']==0,(number,before)
 after=game.run(f'map{number}-weapon-fired',commands+[tic(buttons=1)]*85+[tic()]*20)['save']['players'][0]
 assert after['ammo'][ammo_slot[number]]<before['ammo'][ammo_slot[number]],(number,after)
 # Four real engine player slots with normal deathmatch spawning in both modes.
 starts=[]
 for dm in (1,2):
  save=game.run(f'map{number}-dm{dm}-four-starts',[tic(forward=10)*4]*4+[tic()*4]*10,deathmatch=dm,players=4)['save']
  assert len(save['players'])==4 and all(p['health']==100 and p['cheats']==0 for p in save['players'])
  assert all(p['weapons'][weapons[number] if number!=1 and number!=4 else 2] for p in save['players'])
  starts.append({'mode':dm,'four_players_spawned':True,'positions':[[p['x'],p['y']] for p in save['players']]})
 # Walk, use the hidden exit and advance through the normal intermission.
 w=ScriptWalk(f'map{number}-exit');w.move(0,-384);w.move(0,896);w.move(-96,1056);w.use(90,wait=10)
 w.commands += [tic()]*200+[tic(buttons=1)]+[tic()]*30+[tic(buttons=1)]+[tic()]*200
 exited=game.run(f'map{number}-hidden-exit',w.commands)['save']
 assert exited['map']==number+1,(number,exited)
 # No obsolete BFG mechanics in finale, including all four wall-use positions.
 if number==5:
  check=game.run('finale-old-switches-inert',make_case(15))['save']
  assert not check['players'][0]['weapons'][6]
  assert all(check['sectors'][i]['floor']==96 for i in [VAULT,*SUPPORTS])
  assert all(check['sectors'][i]['special']==0 for i in [VAULT,*SUPPORTS])
 results['maps'].append({'map':f'MAP{number:02}','weapon_collected_and_fired':wanted,'ammo_before':before['ammo'],'ammo_after':after['ammo'],'deathmatch_starts':starts,'hidden_exit_next_map_vanilla':exited['map'],'finale_old_switches_inert':number==5})
 print('PASS gameplay MAP'+str(number),flush=True)
# Every chaos weapon can be collected and used; the BFG still needs all switches.
game.MAPNO=4
chaos=[]
for title,coords,weapon,ammo in [('chainsaw',(-416,-416),7,None),('chaingun',(0,960),3,0),('rocket',(-224,-224),4,3),('plasma',(0,-288),5,2)]:
 w=ScriptWalk(title);w.move(*coords)
 r=game.run('chaos-'+title+'-pickup',w.commands)['save']['players'][0]
 assert r['weapons'][weapon],(title,r)
 select=4|((0 if weapon==7 else weapon)<<3);fire=w.commands+[tic(buttons=select)]+[tic()]*35+[tic(buttons=1)]*70+[tic()]*5
 s=game.run('chaos-'+title+'-fire',fire)['save']['players'][0]
 assert s['readyweapon']==weapon and s['cheats']==0,(title,s)
 if ammo is not None:assert s['ammo'][ammo]<r['ammo'][ammo]
 chaos.append({'weapon':title,'owned':True,'selected_and_fired':True})
for mask in (0,1,3,7,15):
 s=game.run(f'chaos-bfg-{mask}',make_case(mask))['save']
 assert bool(s['players'][0]['weapons'][6])==(mask==15)
 assert s['sectors'][VAULT]['floor']==(0 if mask==15 else 96)
results['chaos_extra_weapons']=chaos;results['chaos_bfg_gate_masks']=[0,1,3,7,15]
sg_commands=[tic(forward=10)]*3+[tic()]*40
sg_before=game.run('chaos-single-shotgun-pickup',sg_commands)['save']['players'][0]
sg_after=game.run('chaos-single-shotgun-fire',sg_commands+[tic(buttons=1)]*70+[tic()]*5)['save']['players'][0]
assert sg_before['readyweapon']==2 and sg_after['ammo'][1]<sg_before['ammo'][1]
bfg_commands=make_case(15)
bfg_before=game.run('chaos-bfg-before-fire',bfg_commands)['save']['players'][0]
bfg_after=game.run('chaos-bfg-fired',bfg_commands+[tic(buttons=4|(6<<3))]+[tic()]*40+[tic(buttons=1)]*120+[tic()]*10)['save']['players'][0]
assert bfg_after['readyweapon']==6 and bfg_after['ammo'][2]<bfg_before['ammo'][2] and bfg_after['cheats']==0
results['chaos_single_shotgun_fired']=True
results['chaos_bfg_fired']={'cells_before':bfg_before['ammo'][2],'cells_after':bfg_after['ammo'][2]}
results['result']='PASS';(ROOT/'validation').mkdir(exist_ok=True);(ROOT/'validation/gameplay.json').write_text(json.dumps(results,indent=2)+'\n')
print('PASS all gameplay checks',flush=True)
