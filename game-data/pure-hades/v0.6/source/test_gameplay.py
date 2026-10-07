#!/usr/bin/env python3
"""Exercise the unmodified engine through its documented demo/save formats.

No engine patches, map substitutions, teleports, or collision cheats are used.
Demo and save serialization follows Chocolate Doom 3.1.1 g_game.c/p_saveg.c.
"""
from pathlib import Path
import json, os, struct, subprocess, hashlib

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / 'build'
ENGINE = os.environ.get('PURE_HADES_ENGINE', 'chocolate-doom')
IWAD = Path(os.environ['PURE_HADES_IWAD'])
PWAD = ROOT / 'PUREHADES.WAD'
TESTS = RUNTIME / 'gameplay-tests'
TESTS.mkdir(parents=True,exist_ok=True)
MAPNO = 1

def wad_lumps(path=PWAD):
    b = path.read_bytes()
    _, n, o = struct.unpack_from('<4sii', b)
    out = {}; active = False
    for i in range(n):
        a,s,name = struct.unpack_from('<ii8s',b,o+16*i)
        name = name.rstrip(b'\0').decode()
        if name.startswith('MAP') and name[3:].isdigit():active = name == f'MAP{MAPNO:02}'
        if active:out[name] = b[a:a+s]

    return out

def read_save(path):
    data = path.read_bytes()
    assert data[24:40].rstrip(b'\0') == b'version 109'
    active = list(data[43:47])
    out = {'episode':data[41], 'map':data[42],
           'leveltime':int.from_bytes(data[47:50],'big'), 'players':[],
           'sectors':[], 'objects':[], 'file':str(path.relative_to(ROOT))}
    if out['episode'] != 1 or out['map'] != MAPNO:
        return out
    offset = 50
    for i, enabled in enumerate(active):
        if not enabled: continue
        offset = (offset+3)&~3
        integers = struct.unpack_from('<70i', data, offset)
        p = {'index':i, 'health':integers[8], 'armor':integers[9],
             'readyweapon':integers[28], 'weapons':list(integers[30:39]),
             'ammo':list(integers[39:43]), 'cheats':integers[49],
             'kills':integers[51], 'items':integers[52], 'secrets':integers[53]}
        out['players'].append(p)
        offset += 280
    lumps = wad_lumps()
    for i in range(len(lumps['SECTORS'])//26):
        floor, ceil, fp, cp, light, special, tag = struct.unpack_from('<7h', data, offset)
        out['sectors'].append({'index':i,'floor':floor,'ceiling':ceil,'special':special})
        offset += 14
    for l in struct.iter_unpack('<7H', lumps['LINEDEFS']):
        offset += 6 + 10*(1+(l[6]!=65535))
    while data[offset] != 0:
        assert data[offset] == 1, (offset,data[offset])
        offset = (offset+4)&~3
        ints = struct.unpack_from('<35i',data,offset)
        spawn = struct.unpack_from('<5h',data,offset+140)
        ob = {'x':ints[3]/65536,'y':ints[4]/65536,'z':ints[5]/65536,
              'angle':(ints[8]&0xffffffff)*360/2**32,'type':ints[22],
              'health':ints[27], 'player':ints[33], 'spawn':list(spawn),
              'momx':ints[18]/65536,'momy':ints[19]/65536}
        out['objects'].append(ob)
        if ob['player']:
            p = next(p for p in out['players'] if p['index']==ob['player']-1)
            p.update({k:ob[k] for k in ('x','y','z','angle','momx','momy')})
        offset += 154
    return out

def tic(forward=0, side=0, turn=0, buttons=0):
    return struct.pack('<bbbB',forward,side,turn,buttons)

def run(label, commands, *, deathmatch=0, players=1, headless=True, timeout=45, extra_args=()):
    folder = TESTS / label
    folder.mkdir(exist_ok=True)
    (folder/'default.cfg').write_text('')
    (folder/'chocolate.cfg').write_text('show_endoom 0\nfullscreen 0\ngrabmouse 0\nstartup_delay 0\n')
    # Last save is an actual in-game action, followed by enough ticks to flush.
    frames = list(commands)
    if players == 1:
        frames += [tic(buttons=0x82),tic(),tic()]
    else:
        frames += [tic(buttons=0x82)+tic()*(players-1),tic()*players,tic()*players]
    header = bytes([109,2,1,MAPNO,deathmatch,0,0,1,0]+[int(i<players) for i in range(4)])
    demo = folder/'check.lmp'
    demo.write_bytes(header+b''.join(frames)+b'\x80')
    cmd = [ENGINE,'-iwad',str(IWAD),'-file',str(PWAD),'-config',str(folder/'default.cfg'),
           '-extraconfig',str(folder/'chocolate.cfg'),'-savedir',str(folder),'-nosound',
           '-window','-geometry','640x480','-nograbmouse','-nomouse','-noautoload','-timedemo',str(demo)]
    cmd += list(extra_args)
    env = os.environ.copy()
    if headless:
        env.update(SDL_VIDEODRIVER='dummy',SDL_RENDER_DRIVER='software')
        cmd += ['-noblit']
    result = subprocess.run(cmd,cwd=folder,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                            env=env,timeout=timeout)
    log = result.stdout.decode(errors='replace')
    (folder/'engine.log').write_text(log)
    record = {'label':label,'command':cmd,'exit_code':result.returncode,
              'headless':headless,'ticks':len(frames),'demo_sha256':hashlib.sha256(demo.read_bytes()).hexdigest(),
              'wad_sha256':hashlib.sha256(PWAD.read_bytes()).hexdigest(),
              'completed': ' gametics in ' in log,'log':str((folder/'engine.log').relative_to(ROOT))}
    save = folder/'doomsav0.dsg'
    if save.exists(): record['save'] = read_save(save)
    (folder/'result.json').write_text(json.dumps(record,indent=2)+'\n')
    if not record['completed']:
        raise RuntimeError(log[-2500:])
    return record

if __name__=='__main__':
    result = run('initial-load', [tic()]*2+[tic(turn=2)]*128)
    print(json.dumps(result,indent=2))
