# Release package of Tekken 6 Native Online (like the PS3 package\release_vX):
#   python tools\make_package.py <version>   -> <T6_PKG_DIR>/release_v<version>/  (T6_PKG_DIR: default t6pkg next to the project)
#       Tekken6NativeOnline\                     the folder the player extracts
#       tekken6_native_online.zip                the whole package
#       tekken6_native_online_patch.zip          the update (launcher's updater): everything but the compiler and Python
# Nothing of the game: no game\, native\, ISO; no *.ini, runs\, server keys. The player builds the game from his ISO.
import os, shutil, subprocess, sys, zipfile


def need(ok, why):
    """A check of the release (not assert: python -O would drop it)."""
    if not ok:
        sys.exit('ERROR: ' + why)


DEV = os.path.dirname(os.path.dirname(os.path.abspath(__file__))).replace('\\', '/')  # (the project folder)
PKG = os.environ.get('T6_PKG_DIR', os.path.dirname(DEV) + '/t6pkg').replace('\\', '/')  # (packages + staged build)
ver = sys.argv[1]
OUT = f'{PKG}/release_v{ver}'
P = OUT + '/Tekken6NativeOnline'
if os.path.isdir(P):
    shutil.rmtree(P)
os.makedirs(P)

# launcher + its Qt runtime (windeployqt output in launcher\build). An allow list (2026-10-18 review: every exe / dll /
# folder of launcher\build went in - a test exe, an ASAN build or debug files left there would have been released):
# the two exes, the dlls next to them and the dlls of the Qt plugin folders; an unknown exe stops the package.
L = DEV + '/launcher/build'
EXES = ('Tekken6 Native Online.exe', 'vc_redist.x64.exe')
PLUGINS = ('generic', 'iconengines', 'imageformats', 'networkinformation', 'platforms', 'styles', 'tls')
for f in os.listdir(L):
    src = f'{L}/{f}'
    if os.path.isdir(src):
        if f in PLUGINS:
            shutil.copytree(src, f'{P}/{f}', ignore=lambda d, names: [n for n in names if not n.endswith('.dll')])
    elif f.endswith('.exe'):
        need(f in EXES, f'{f}: not a file of the package (the developer launcher / a test build must never be in it)')
        shutil.copy(src, P)
    elif f.endswith('.dll'):
        shutil.copy(src, P)
need(all(os.path.isfile(f'{P}/{e}') for e in EXES), 'launcher exe or vc_redist missing in launcher/build')

# builder (compiler, Python, prebuilt runtime - no game code)
subprocess.check_call([sys.executable, '-I', DEV + '/tools/make_builder.py', P])

# files the launcher / game use at run time, and the reference profile (same for every player: it is in the state hash)
os.makedirs(P + '/tools')
for f in ('menu_vs.bin', 'netplay_exclude.txt'):
    shutil.copy(f'{DEV}/tools/{f}', P + '/tools')
shutil.copytree(DEV + '/user', P + '/profile')
# shader storage the launcher adds to the player's (tools\shader_cache.py merge): no pipeline is first met in a match
need(os.path.getsize(DEV + '/tools/shader_cache/4E4D07FC.rov.d3d12.xpso') > 12, 'tools/shader_cache is empty')
shutil.copytree(DEV + '/tools/shader_cache', P + '/tools/shader_cache')

# python tools\make_package.py <version> --test: a TEST copy (file test_channel.txt next to the launcher, in the package
# and in the patch): its launcher follows the pre-releases too. <version> like 2.0.3-test.1 = kVersion of that build;
# publish it on GitHub as a PRE-release with the tag v<version> (never as the latest release).
if '--test' in sys.argv[2:]:
    need('-test' in ver, 'a test package needs a version like 2.0.3-test.1')
    with open(P + '/test_channel.txt', 'w') as f:
        f.write('test copy: this launcher also updates to test versions (pre-releases)\n')
else:
    need('-test' not in ver, 'a version with -test needs --test')

for bad in ('game', 'native', 'runs'):
    need(not os.path.exists(f'{P}/{bad}'), bad)
need(not any(f.endswith('.ini') for f in os.listdir(P)), 'ini in the package root')


PATCH_SKIP = ('builder/llvm-mingw', 'builder/python')  # (the compiler and Python: only in the whole package)


def package_files(skip=()):
    """(path, name inside the package) of every file of the package folder, without the folders in skip."""
    for root, _, files in os.walk(P):
        rel = os.path.relpath(root, P).replace('\\', '/')
        if any(rel == s or rel.startswith(s + '/') for s in skip):
            continue
        for f in files:
            yield os.path.join(root, f), f if rel == '.' else f'{rel}/{f}'


def pack(name, skip=()):
    z = f'{OUT}/{name}'
    with zipfile.ZipFile(z, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        for path, arc in package_files(skip):
            zf.write(path, 'Tekken6NativeOnline/' + arc if name.endswith('online.zip') else arc)
    print(name, f'{os.path.getsize(z) / 1e6:.0f} MB')


pack('tekken6_native_online.zip')
# The patch is ALWAYS the whole package without the compiler and Python - never only the files changed since the last
# release. The launcher updates straight to the latest release: with "changed files only" patches (v0.1.2 - v2.0.2,
# 12-14 MB instead of 67) a player who skipped v0.1.9 or v2.0 kept the old libggpo / libSDL3 / librexruntime.dll.a /
# rexgpu-xenos.dll, got another game fingerprint than everybody else with the same launcher version ("The opponent
# has a different version of the game", no pairing in the queues), and a rebuild could not repair it (found
# 2026-10-13 from two players' build logs).
pack('tekken6_native_online_patch.zip', skip=PATCH_SKIP)
with zipfile.ZipFile(f'{OUT}/tekken6_native_online_patch.zip') as zf:
    have = set(zf.namelist())
for _, arc in package_files(PATCH_SKIP):
    need(arc in have, f'{arc} is missing in the patch')
print('patch complete:', len(have), 'files')
