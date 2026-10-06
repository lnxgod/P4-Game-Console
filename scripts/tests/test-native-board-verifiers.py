#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise exact current native-only verifier gates without firmware or devices."""
import ast,copy,json,sys,tempfile
from pathlib import Path
from types import SimpleNamespace
BASE=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
passed=0

def check(value,message):
    if not value:raise ValueError(message)

def gate(calls,label,environment,expected=True):
    global passed
    matches=[node for node in calls if len(node.args)>1 and isinstance(node.args[1],ast.Constant) and node.args[1].value==label]
    assert len(matches)==1,(label,len(matches))
    code=ast.fix_missing_locations(ast.Module(body=[ast.Expr(copy.deepcopy(matches[0]))],type_ignores=[]))
    try:exec(compile(code,'<exact-proposed-gate>','exec'),dict(environment,require=check))
    except ValueError:
        if expected:raise
    else:
        if not expected:raise AssertionError('Gate wrongly accepted: '+label)
    passed+=1

for name in ('verify-console-os.py','verify-console-os-olimex.py','verify-console-os-waveshare.py'):
    source=(BASE/name).read_text()
    tree=ast.parse(source)
    calls=[node for node in ast.walk(tree) if isinstance(node,ast.Call) and isinstance(node.func,ast.Name) and node.func.id=='require']
    legacy={'retired':True,'game_manager_visible':False,'runtime_implemented':False,'execution_enabled':False,'seed_carts':[]}
    gate(calls,'native-only retirement metadata differs',dict(legacy=legacy))
    for key,value in (('retired',False),('retired',None),('game_manager_visible',True),('runtime_implemented',True),('execution_enabled',True),('seed_carts',['P4/GAMES/OLD.P4CART']),('seed_cart','P4/GAMES/OLD.P4CART')):
        gate(calls,'native-only retirement metadata differs',dict(legacy={**legacy,key:value}),False)
    gate(calls,'retired Lua runtime component linked',dict(components={'p4_game_api','p4_frame_scheduler'}))
    for component in ('p4_lua_runtime','p4_script_renderer','p4_script_audio','lua'):
        gate(calls,'retired Lua runtime component linked',dict(components={component}),False)
    gate(calls,'retired Lua VM symbol linked',dict(symbols_result=SimpleNamespace(stdout='40000000 T p4_tick_scheduler_init\n40000004 T platform_game_loader_run\n')))
    for symbol in ('lua_newstate','luaL_loadbufferx','luaopen_base','p4_lua_runtime_load'):
        gate(calls,'retired Lua VM symbol linked',dict(symbols_result=SimpleNamespace(stdout='40000000 T '+symbol+'\n')),False)
    with tempfile.TemporaryDirectory() as temporary:
        root=Path(temporary);bundle=root/'game-storage-seed';bundle.mkdir()
        label='retired Lua cartridge in native seed bundle' if name=='verify-console-os.py' else 'retired Lua cartridge in native SD bundle'
        gate(calls,label,dict(build=root,bundle=bundle))
        cart=bundle/'P4/GAMES/old.p4cart';cart.parent.mkdir(parents=True);cart.write_bytes(b'old bytes')
        gate(calls,label,dict(build=root,bundle=bundle),False)
        assert cart.read_bytes()==b'old bytes'
    if name=='verify-console-os.py':
        tokens=['CONSOLE_PAGE_GAMES','native_format=p4-native-elf-v1','platform_game_loader_run']
        gate(calls,'Game Manager/cartridge route is absent from Console OS',dict(shell_main=' '.join(tokens)))
        for missing in tokens:
            gate(calls,'Game Manager/cartridge route is absent from Console OS',dict(shell_main=' '.join(t for t in tokens if t!=missing)),False)
        with tempfile.TemporaryDirectory() as temporary:
            volume=Path(temporary)
            for name in ('DOOM1.WAD','GAMES','README.TXT','UPDATE'):(volume/name).mkdir()
            gate(calls,'generated FAT root contents differ',dict(volume=volume))
            (volume/'P4').mkdir()
            gate(calls,'generated FAT root contents differ',dict(volume=volume),False)
            (volume/'P4').rmdir();(volume/'DOOM1.WAD').rmdir()
            gate(calls,'generated FAT root contents differ',dict(volume=volume),False)
    if name=='verify-console-os-waveshare.py':
        tokens=['content.state != P4_CONTENT_TRANSFER_IDLE','file.state != P4_FILE_TRANSFER_IDLE','platform_game_storage_set_usb_mode(false)','stop_usb_input_for_role_switch()']
        environment=dict(source=' '.join(tokens),storage_source='reason=host-not-ejected')
        gate(calls,'H1 role transition or eject gate is not fail-closed',environment)
        for missing in tokens:
            gate(calls,'H1 role transition or eject gate is not fail-closed',{**environment,'source':' '.join(t for t in tokens if t!=missing)},False)
        gate(calls,'H1 role transition or eject gate is not fail-closed',{**environment,'storage_source':''},False)
print(json.dumps({'result':'PASS','native_verifier_gate_cases':passed,'device_io':False,'production_changes':False}))
