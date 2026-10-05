#!/usr/bin/env python3
"""Mirror only dice sources for upstream CMake's no-spaces requirement."""
import hashlib
import json
from pathlib import Path
import shutil
import sys
root, stage = map(Path,sys.argv[1:3])
expected = Path('/tmp/p4-dice-build-'+hashlib.sha256(str(root).encode()).hexdigest()[:12])
if stage != expected or stage.is_symlink():
    raise SystemExit('Unexpected dice staging directory')
marker=stage/'.p4-source-root'
if stage.exists() and (not marker.is_file() or marker.read_text()!=str(root)):
    raise SystemExit('Staging directory belongs to another source')
stage.mkdir(mode=0o700,exist_ok=True)
marker.write_text(str(root))
if '--collect' in sys.argv:
    output=root/'apps/dice_core2/build-core2'
    output.mkdir(exist_ok=True)
    for name in ['p4_dice_core2.bin','p4_dice_core2.elf','p4_dice_core2.map','sdkconfig','flasher_args.json','project_description.json','bootloader/bootloader.bin','partition_table/partition-table.bin']:
        target=output/name;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(stage/'build'/name,target)
    shutil.copy2(stage/'apps/dice_core2/dependencies.lock',root/'apps/dice_core2/dependencies.lock')
    artifact=output/'p4_dice_core2.bin'
    (output/'artifact.json').write_text(json.dumps({'board':'m5stack-core2','chip':'esp32','idf':'5.5.3','bytes':artifact.stat().st_size,'sha256':hashlib.sha256(artifact.read_bytes()).hexdigest(),'staging_directory':str(stage)},indent=2)+'\n')
    print('Core2 artifact:',artifact)
else:
    for name in ['apps/dice_core2','components/p4_multiplayer','components/p4_game_api/include']:
        shutil.copytree(root/name,stage/name,dirs_exist_ok=True,
            ignore=shutil.ignore_patterns('build*','managed_components','__pycache__'))

    # The wrapper owns this generated configuration. Re-apply checked-in defaults
    # when they change; an old sdkconfig must not silently disable new hardware.
    defaults=root/'apps/dice_core2/sdkconfig.defaults'
    digest=hashlib.sha256(defaults.read_bytes()).hexdigest()
    stamp=stage/'.sdkconfig-defaults-sha256'
    if not stamp.exists() or stamp.read_text()!=digest:
        for name in ('sdkconfig','sdkconfig.old'):
            generated=stage/'build'/name
            if generated.exists(): generated.unlink()
        stamp.write_text(digest)
