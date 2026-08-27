import os
import re
import sys

import rtconfig
import SCons.Tool.cc
import SCons.Tool.compilation_db
from SCons.Builder import ListEmitter

REPOSITORY_ROOT = os.path.abspath(Dir('#').abspath)
RTT_ROOT = os.path.join(REPOSITORY_ROOT, 'vendor', 'rt-thread')
sys.path.insert(0, os.path.join(REPOSITORY_ROOT, 'tools'))
from firmware_manifest import (
    load_manifest,
    manifest_group_settings,
    manifest_sources,
    validate_manifest,
)

sys.path.append(os.path.join(RTT_ROOT, 'tools'))
from building import *


def bsp_pkg_check():
    required = [
        'vendor/cmsis-core',
        'vendor/rt-thread-stm32-drivers',
        'vendor/stm32f1-cmsis',
        'vendor/stm32f1-hal',
    ]
    if not all(os.path.isdir(os.path.join(REPOSITORY_ROOT, path)) for path in required):
        print('Vendored dependency package is missing; restore the pinned source snapshot.')
        Exit(1)


def dependency_declaration_check():
    empty_dependency = re.compile(r"depend\s*=\s*\[\s*['\"]['\"]\s*\]")
    violations = []
    source_root = os.path.join(REPOSITORY_ROOT, 'src')
    for directory, _, files in os.walk(source_root):
        if 'SConscript' not in files:
            continue
        if os.path.relpath(directory, source_root).startswith('platform' + os.sep + 'board'):
            continue
        path = os.path.join(directory, 'SConscript')
        with open(path, encoding='utf-8') as stream:
            for line_number, line in enumerate(stream, start=1):
                if empty_dependency.search(line):
                    violations.append('%s:%d' % (
                        os.path.relpath(path, REPOSITORY_ROOT), line_number
                    ))
    if violations:
        print('Empty SCons dependency declarations are not allowed:')
        for violation in violations:
            print('  ' + violation)
        Exit(1)


def centralize_object_target(target, source, env):
    target_path = os.path.relpath(target[0].abspath, REPOSITORY_ROOT)
    object_root = os.path.join('build', 'scons', 'firmware', 'objects')
    canonical_marker = object_root + os.sep
    if canonical_marker in target_path:
        target_path = target_path[target_path.index(canonical_marker):]
        target[0] = env.File(os.path.join(REPOSITORY_ROOT, target_path))
        return target, source

    legacy_marker = os.path.join('build', 'firmware') + os.sep
    if legacy_marker in target_path:
        target_path = target_path[target_path.index(legacy_marker) + len(legacy_marker):]
    elif target_path.startswith('build' + os.sep):
        target_path = target_path[len('build' + os.sep):]
    target[0] = env.File(os.path.join(REPOSITORY_ROOT, object_root, target_path))
    return target, source


def redirect_object_builders(environment):
    object_builder = environment['BUILDERS']['StaticObject']
    for suffix, emitter in object_builder.emitter.items():
        object_builder.emitter[suffix] = ListEmitter([
            emitter,
            centralize_object_target,
        ])


_cc_generate = SCons.Tool.cc.generate


def _cc_generate_with_centralized_objects(environment):
    _cc_generate(environment)
    redirect_object_builders(environment)


SCons.Tool.cc.generate = _cc_generate_with_centralized_objects


_cdb_generate = SCons.Tool.compilation_db.generate


def _cdb_generate_with_scons_output(environment, **kwargs):
    _cdb_generate(environment, **kwargs)
    builder = environment['BUILDERS']['CompilationDatabase']
    original_emitter = builder.emitter

    def emit_scons_database(target, source, env):
        target, source = original_emitter(target, source, env)
        target[0] = env.File(
            os.path.join(REPOSITORY_ROOT, 'build', 'scons', 'compile_commands.json')
        )
        return target, source

    builder.emitter = emit_scons_database


SCons.Tool.compilation_db.generate = _cdb_generate_with_scons_output


def verify_manifest_sources(objects):
    expected = set(manifest_sources(manifest))
    actual = set()
    flattened = []
    for item in objects:
        if isinstance(item, list):
            flattened.extend(item)
        else:
            flattened.append(item)
    for item in flattened:
        sources = getattr(item, 'sources', None) or [item]
        for source in sources:
            if not hasattr(source, 'srcnode'):
                continue
            source_path = os.path.relpath(
                source.srcnode().abspath,
                REPOSITORY_ROOT,
            )
            if os.path.splitext(source_path)[1] in {'.c', '.s', '.S'}:
                actual.add(source_path.replace(os.sep, '/'))
    if actual != expected:
        missing = sorted(expected - actual)
        unexpected = sorted(actual - expected)
        print('SCons source set differs from project/firmware-manifest.json:')
        if missing:
            print('  missing: ' + ', '.join(missing))
        if unexpected:
            print('  unexpected: ' + ', '.join(unexpected))
        Exit(1)


RegisterPreBuildingAction(bsp_pkg_check)
dependency_declaration_check()

MANIFEST_PATH = os.path.join(REPOSITORY_ROOT, 'project', 'firmware-manifest.json')
manifest = load_manifest(MANIFEST_PATH)
validate_manifest(REPOSITORY_ROOT, manifest)
board_settings = manifest_group_settings(manifest, 'board', root=REPOSITORY_ROOT)

FIRMWARE_DIR = os.path.join('build', 'scons', 'firmware')
TARGET = os.path.join(FIRMWARE_DIR, 'huokong.' + rtconfig.TARGET_EXT)
BIN_TARGET = os.path.join(FIRMWARE_DIR, 'huokong.bin')
MAP_TARGET = os.path.join(FIRMWARE_DIR, 'huokong.map')

SConsignFile('build/scons/firmware.dblite')
DefaultEnvironment(tools=[])
env = Environment(
    tools=['mingw'],
    AS=rtconfig.AS,
    ASFLAGS=rtconfig.AFLAGS,
    CC=rtconfig.CC,
    CFLAGS=rtconfig.CFLAGS,
    AR=rtconfig.AR,
    ARFLAGS='-rc',
    CXX=rtconfig.CXX,
    CXXFLAGS=rtconfig.CXXFLAGS,
    LINK=rtconfig.LINK,
    LINKFLAGS=rtconfig.LFLAGS,
    OBJCOPY=rtconfig.OBJCPY,
    CPPPATH=board_settings['include_paths'],
    CPPDEFINES=board_settings['defines'],
)
env.PrependENVPath('PATH', rtconfig.EXEC_PATH)
env.AppendUnique(LIBS=['m'])
object_builder = env['BUILDERS']['StaticObject']
redirect_object_builders(env)

Export('env')
Export('RTT_ROOT')
Export('rtconfig')
Export('manifest')

objs = PrepareBuilding(env, RTT_ROOT, has_libcpu=False)
objs.extend(SConscript('vendor/rt-thread-stm32-drivers/SConscript',
                       variant_dir='build/scons/vendor/rt-thread-stm32-drivers', duplicate=0))

direct_hal = [
    'vendor/stm32f1-hal/Src/stm32f1xx_hal_spi.c',
    'vendor/stm32f1-hal/Src/stm32f1xx_hal_tim.c',
    'vendor/stm32f1-hal/Src/stm32f1xx_hal_tim_ex.c',
]
hal_settings = manifest_group_settings(manifest, 'hal', root=REPOSITORY_ROOT)
hal_env = env.Clone(BUILDERS={})
hal_env.Tool('cc')
hal_env.Replace(
    CFLAGS=env['CFLAGS'],
    CPPPATH=hal_settings['include_paths'],
    CPPDEFINES=hal_settings['defines'],
)
if GetOption('cdb'):
    hal_env.Tool('compilation_db')
hal_objects = []
for source in direct_hal:
    hal_objects.extend(hal_env.Object(
        target='#build/scons/firmware/objects/' + source[:-2],
        source=source,
    ))
objs.extend(DefineGroup(
    'Direct HAL',
    hal_objects,
    depend=[],
    direct_dependencies=['STM32F1-HAL'],
))

verify_manifest_sources(objs)
DoBuilding(TARGET, objs)
program = env['target']
binary = env.Command(BIN_TARGET, program,
                     '$OBJCOPY -O binary $SOURCE $TARGET')
env.SideEffect(MAP_TARGET, program)
env.Clean(program, [BIN_TARGET, MAP_TARGET])
Default(program, binary)
if GetOption('cdb'):
    Default('build/scons/compile_commands.json')
