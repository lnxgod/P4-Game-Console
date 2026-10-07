#!/usr/bin/env python3
"""Exercise the actual component CMake against pinned IDF early expansion and Ninja.

The normal configure boundary substitutes idf_component_register only to avoid
an unrelated firmware build. The production custom command and dependency
list are evaluated by CMake; the early-expansion pass is the actual SDK script.
"""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[3]
BASE=Path(os.environ.get('P4_TRUSTED_INPUT_ROOT',ROOT)).resolve()
CMAKE=BASE/'.tools/host-venv/bin/cmake';NINJA=BASE/'.tools/host-venv/bin/ninja'
IDF=BASE/'.tools/esp-idf-v5.5.3'
DEPENDENCIES=('scripts/doom/arena-trusted-digests.py','scripts/doom/arena-content.py','third_party/game-data.json','components/platform_game_storage/include/platform/doom_arena_content.h','local-data/doom/arena-compact-v1/ARENA2.WAD','game-data/pure-hades/v0.6/PUREHADES.WAD','local-data/doom/arena-inbox/dwango5/DWANGO5.WAD')
PROJECT=r'''
cmake_minimum_required(VERSION 3.16)
project(trusted_metadata_graph LANGUAGES C)
function(idf_build_get_property result property)
  if(property STREQUAL "P4_BOARD_PROFILE")
    set(${result} "${BOARD}" PARENT_SCOPE)
  elseif(property STREQUAL "PYTHON")
    set(${result} "@PYTHON@" PARENT_SCOPE)
  else()
    message(FATAL_ERROR "Unexpected build property ${property}")
  endif()
endfunction()
function(idf_component_register)
  cmake_parse_arguments(C "" "" "SRCS;INCLUDE_DIRS;PRIV_INCLUDE_DIRS;REQUIRES" ${ARGN})
  set(chosen "")
  foreach(source IN LISTS C_SRCS)
    if(source MATCHES "arena_trusted_digests.c$")
      list(APPEND chosen "${source}")
    endif()
  endforeach()
  if(NOT chosen)
    set(chosen src/game_storage_model.c)
  endif()
  add_library(metadata_graph STATIC ${chosen})
  set(COMPONENT_LIB metadata_graph PARENT_SCOPE)
endfunction()
add_subdirectory("@COMPONENT@" component)
'''
class BuildTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        output=os.environ.get('P4_TRUSTED_BUILD_OUT')
        if output:cls.work=Path(output).resolve();cls.work.mkdir(parents=True,exist_ok=True)
        else:cls.tmp=tempfile.TemporaryDirectory(prefix='p4-trusted-build-');cls.addClassCleanup(cls.tmp.cleanup);cls.work=Path(cls.tmp.name)
        cls.source=(ROOT/'components/platform_game_storage/CMakeLists.txt').read_text();cls.records=[]
    @classmethod
    def tearDownClass(cls):
        (cls.work/'commands.json').write_text(json.dumps(cls.records,indent=2)+'\n')
    def component(self,name,text):
        root=self.work/name/'root';component=root/'components/platform_game_storage';component.mkdir(parents=True,exist_ok=True)
        (component/'CMakeLists.txt').write_text(text);(component/'src').mkdir(exist_ok=True)
        shutil.copyfile(ROOT/'components/platform_game_storage/src/game_storage_model.c',component/'src/game_storage_model.c')
        return root,component
    def run_command(self,cmd,output):
        r=subprocess.run([str(x) for x in cmd],capture_output=True,text=True,timeout=30)
        output.write_text(r.stdout+r.stderr);self.records.append({'command':[str(x) for x in cmd],'exit_code':r.returncode,'log':str(output),'log_sha256':hashlib.sha256(output.read_bytes()).hexdigest()});return r
    def early(self,name,text):
        root,component=self.component(name,text);w=root.parent;t='___idf_platform_game_storage'
        (w/'build.cmake').write_text(f'set(IDF_PATH "{IDF}")\nset(P4_BOARD_PROFILE "m5stack-tab5")\nset(PYTHON "{sys.executable}")\nset(__COMPONENT_TARGETS "{t}")\n')
        props={'COMPONENT_DIR':str(component),'COMPONENT_NAME':'platform_game_storage','COMPONENT_ALIAS':'idf::platform_game_storage','COMPONENT_SOURCE':'project_extra_components'}
        (w/'components.cmake').write_text(''.join(f'set(__component_{t}_{k} "{v}")\n' for k,v in props.items()))
        return self.run_command([CMAKE,f'-DBUILD_PROPERTIES_FILE={w}/build.cmake',f'-DCOMPONENT_PROPERTIES_FILE={w}/components.cmake',f'-DCOMPONENT_REQUIRES_FILE={w}/requirements.cmake','-P',IDF/'tools/cmake/scripts/component_get_requirements.cmake'],w/'early.log')
    def graph(self,name,text,board='m5stack-tab5'):
        root,component=self.component(name,text);w=root.parent
        (w/'CMakeLists.txt').write_text(PROJECT.replace('@PYTHON@',sys.executable).replace('@COMPONENT@',str(component)))
        result=self.run_command([CMAKE,'-S',w,'-B',w/'build','-G','Ninja',f'-DCMAKE_MAKE_PROGRAM={NINJA}',f'-DBOARD={board}'],w/'configure.log')
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        return root,(w/'build/build.ninja').read_text()
    def check_dependencies(self,root,graph):
        line=next(x for x in graph.splitlines() if x.startswith('build component/arena-digests/arena_trusted_digests.c') and 'CUSTOM_COMMAND' in x)
        for dependency in DEPENDENCIES:self.assertIn(str(root/dependency),line)
        self.assertIn('arena_trusted_digests.json',line)
        self.assertIn('arena_trusted_digests.c.o:',graph)
        self.assertIn('--root '+str(root),graph)
    def test_actual_idf_early_pass(self):
        result=self.early('early-valid',self.source);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    def test_missing_early_guard_reproduces_sdk_error(self):
        mutant=self.source.replace(' AND\n       NOT CMAKE_BUILD_EARLY_EXPANSION','')
        self.assertNotEqual(mutant,self.source);result=self.early('early-mutant',mutant)
        self.assertNotEqual(result.returncode,0);self.assertIn('add_custom_command command is not scriptable',result.stderr)
    def test_normal_graph_binds_every_exact_input_and_receipt(self):
        root,graph=self.graph('graph-valid',self.source);self.check_dependencies(root,graph)
    def test_missing_wad_dependency_is_rejected(self):
        mutant=self.source.replace('                "${P4_ARENA_ROOT}/local-data/doom/arena-compact-v1/ARENA2.WAD"\n','')
        self.assertNotEqual(mutant,self.source);root,graph=self.graph('graph-mutant',mutant)
        with self.assertRaises(AssertionError):self.check_dependencies(root,graph)
    def test_legacy_graph_has_no_trusted_generation(self):
        _,graph=self.graph('graph-legacy',self.source,'olimex-esp32-p4-pc');self.assertNotIn('arena_trusted_digests',graph)
if __name__=='__main__':unittest.main()
