#!/usr/bin/env python3
"""Build the isolated core as RISC-V and enforce the real cartridge import gate."""
from pathlib import Path
import importlib.util,json,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
SRC=ROOT/'.tools/wacky-p4-port';OUT=ROOT/'build-host/wacky-riscv';OUT.mkdir(exist_ok=True)
cache=(ROOT/'apps/console_os/build-tab5/CMakeCache.txt').read_text()
gcc=Path(re.search(r'^CMAKE_C_COMPILER_AR:FILEPATH=(.*)-gcc-ar$',cache,re.M)[1]+'-gcc')
FLAGS=['-std=gnu17','-Os','-march=rv32imafc_zicsr_zifencei','-mabi=ilp32f','-fPIC','-ffunction-sections','-fdata-sections','-fvisibility=hidden','-fno-jump-tables','-fno-builtin','-fstack-usage','-Werror=implicit-function-declaration','-Werror=incompatible-pointer-types','-Werror=frame-larger-than=4096',f'-I{SRC}',f'-I{ROOT}/components/p4_game_api/include',f'-I{ROOT}/components/p4_cp437/include',f'-I{ROOT}/components/p4_midi/include']
PORTABLE=['-D'+name+'=ww_p4_'+name for name in ['strlen','strncpy','strcpy','strtol','toupper','snprintf','abs']]
files=list(SRC.glob('*.c'))+[ROOT/'components/p4_game_api/runtime/cartridge_main.c',ROOT/'components/p4_game_api/src/game_runtime.c',ROOT/'components/p4_game_api/src/draw.c',ROOT/'components/p4_cp437/src/cp437.c',ROOT/'components/p4_midi/src/midi.c']
objs=[]
with (OUT/'build.log').open('w') as log:
    for f in files:
        obj=OUT/(f.stem+'.o');cmd=[str(gcc),*FLAGS,'-DWW_P4_EMBEDDED']
        if f.name.startswith('ww_'):
            cmd+=['-include',str(SRC/'port_support.h'),'-Dmalloc=ww_p4_malloc','-Dcalloc=ww_p4_calloc','-Dfree=ww_p4_free',*PORTABLE]
        if f.name in ['port.c','front.c']:cmd+=PORTABLE
        cmd+=['-DP4_GAME_ENTRY_SYMBOL=p4_wacky_probe_game','-c',str(f),'-o',str(obj)]
        subprocess.run(cmd,check=True,stdout=log,stderr=log);objs.append(obj)
    subprocess.run([str(gcc),*FLAGS,'-shared','-nostdlib','-nostartfiles','-static-libgcc','-Wl,--gc-sections','-Wl,--allow-shlib-undefined','-Wl,--build-id=none','-Wl,-e,app_main',*[str(o) for o in objs],'-lgcc','-o',str(OUT/'wacky.elf')],check=True,stdout=log,stderr=log)
sys.path.insert(0,str(ROOT/'scripts'))
spec=importlib.util.spec_from_file_location('package',ROOT/'scripts/build-game-package.py')
package=importlib.util.module_from_spec(spec);spec.loader.exec_module(package)
package.validate_elf_imports(OUT/'wacky.elf',gcc)
subprocess.run([str(gcc.with_name('riscv32-esp-elf-strip')),'--strip-unneeded','--remove-section=.comment','--remove-section=.riscv.attributes',str(OUT/'wacky.elf'),'-o',str(OUT/'wacky-stripped.elf')],check=True)
frames=[]
for p in OUT.glob('*.su'):
    for line in p.read_text().splitlines():
        a=line.split('\t');frames.append((int(a[1]),a[0]))
print('P4 cross-compile and frozen cartridge import allowlist: PASS')
print('ELF bytes:',(OUT/'wacky-stripped.elf').stat().st_size)
print('Largest individual stack frames:')
for n,name in sorted(frames,reverse=True)[:6]: print(n,name.rsplit(':',1)[-1])
subprocess.run([str(gcc.with_name('riscv32-esp-elf-nm')),'-u',str(OUT/'wacky.elf')])

subprocess.run([str(ROOT/'build-host/wacky-p4/wacky_validate_elf'),str(OUT/'wacky-stripped.elf')],check=True)
