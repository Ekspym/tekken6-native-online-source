# Assembles the player's builder (package\builder) from the llvm-mingw build of the staged project (build_mingw.cmd):
#   python tools\make_builder.py <package dir>
# Nothing of the game goes in: the prebuilt parts are the runtime (SDK), the netplay code and GGPO, compiled without
# any generated game code; the player's build_native.py makes the game code from his own Default.xex.
import json, os, pathlib, re, shlex, shutil, subprocess, sys

DEV = os.path.dirname(os.path.dirname(os.path.abspath(__file__))).replace('\\', '/')  # (the project folder)
PKG = os.environ.get('T6_PKG_DIR', os.path.dirname(DEV) + '/t6pkg').replace('\\', '/')  # (packages + staged build)
SRC = PKG + '/src'
BLD = PKG + '/build'
TC = DEV + '/toolchain/llvm-mingw'
PY_EMBED = DEV + '/toolchain/python-embed'
SDK_OUT = SRC + '/third_party/rexglue-sdk/out/win-amd64'
VERSION = pathlib.Path(DEV + '/builder/build.version').read_text().strip()

pkg = os.path.abspath(sys.argv[1]).replace('\\', '/')
B = pkg + '/builder'
os.makedirs(B, exist_ok=True)


def copytree(a, b):
    if os.path.isdir(b):
        shutil.rmtree(b)
    shutil.copytree(a, b)


# 1. builder script, version, project files (codegen config + patch tools)
shutil.copy(DEV + '/builder/build_native.py', B)
shutil.copy(DEV + '/builder/build.version', B)
os.makedirs(B + '/project/tools', exist_ok=True)
shutil.copy(DEV + '/tekken6_config.toml', B + '/project')
for t in ('pagemap_patch', 'rawstore_patch', 'sndcall_patch', 'faststore_patch'):
    shutil.copy(DEV + f'/tools/{t}.py', B + '/project/tools')

# 2. Python (embeddable, the PS3 package's) and the compiler (x86_64 only)
if not os.path.isdir(B + '/python'):
    copytree(PY_EMBED, B + '/python')
T = B + '/llvm-mingw'
if not os.path.isdir(T):
    os.makedirs(T + '/bin')
    for f in os.listdir(TC + '/bin'):
        if (f in ('clang.exe', 'clang++.exe', 'clang-23.exe', 'clang-target-wrapper.exe', 'ld.lld.exe', 'lld.exe', 'llvm-ar.exe')
                or f.endswith('.dll') or f.startswith('x86_64-w64') or f in ('x86_64-w64-windows-gnu.cfg', 'x86_64-pc-windows-gnu.cfg', 'mingw32-common.cfg')):
            shutil.copy(f'{TC}/bin/{f}', T + '/bin')
    for d in ('include', 'lib', 'x86_64-w64-mingw32'):
        shutil.copytree(f'{TC}/{d}', f'{T}/{d}')
    shutil.copy(TC + '/LICENSE.TXT', T)

# 3. tools: codegen (with the runtime it links) and the disc image reader
os.makedirs(B + '/bin', exist_ok=True)
for f in ('rexglue.exe', 'rexruntime.dll'):
    shutil.copy(f'{SDK_OUT}/{f}', B + '/bin')
for f in ('libc++.dll', 'libunwind.dll'):
    shutil.copy(f'{TC}/x86_64-w64-mingw32/bin/{f}', B + '/bin')
shutil.copy(BLD + '/tools/xdvdfs/xdvdfs_tool.exe', B + '/bin/xdvdfs.exe')

# 4. runtime next to the game exe
os.makedirs(B + '/runtime', exist_ok=True)
for f in ('rexruntime.dll', 'rexgpu-xenos.dll'):
    shutil.copy(f'{BLD}/{f}', B + '/runtime')
for f in ('libc++.dll', 'libunwind.dll'):
    shutil.copy(f'{TC}/x86_64-w64-mingw32/bin/{f}', B + '/runtime')
shutil.copy(DEV + '/build/mmjoystick.ini', B + '/runtime')

# 5. compile flags of the generated code (compile_commands.json), include dirs copied into builder\sdk
cc = json.loads(pathlib.Path(BLD + '/compile_commands.json').read_text())
cmd = next(c['command'] for c in cc if c['file'].replace('\\', '/').endswith('generated/default/tekken6_recomp.0.cpp'))
args = shlex.split(cmd.replace('\\', '/'), posix=True)[1:]
cxx, incs, skip = [], [], 0
for i, a in enumerate(args):
    if skip:
        skip -= 1
        continue
    if a in ('-o', '-c'):
        skip = 1
        continue
    if a == '-Xclang' and args[i + 1] in ('-include-pch', '-include'):
        skip = 3  # the PCH options: build_native.py puts its own
        continue
    if a == '-isystem':
        incs.append(('-isystem', args[i + 1]))
        skip = 1
        continue
    if a.startswith('-I'):
        incs.append(('-I', a[2:]))
        continue
    cxx.append(a)
os.makedirs(B + '/sdk', exist_ok=True)
inc_args = []
for n, (flag, d) in enumerate(incs):
    if d.startswith(SRC + '/generated') or d in (SRC, SRC + '/src'):
        continue  # (the generated code: the player's own; the project root: not needed)
    dst = f'{B}/sdk/{n}'
    copytree(d, dst)
    inc_args += [flag, '{BUILDER}/sdk/' + str(n)]
    # the SDK writes its build time into rex/version.h: a fixed value, else every package changes the headers and the
    # player's update compiles the whole game again instead of a relink (value of v0.1.2)
    vh = f'{dst}/rex/version.h'
    if os.path.exists(vh):
        with open(vh, newline='') as fh:
            t = re.sub(r'#define REXGLUE_BUILD_TIMESTAMP "[^"]*"', '#define REXGLUE_BUILD_TIMESTAMP "20261012_1036"', fh.read())
        pathlib.Path(vh).write_text(t, newline='\n')
inc_args += ['-I', '{GEN}']
pch_h = '{OBJ}/tekken6_pch.pch'
compile_cfg = {
    'pch': [*cxx, *inc_args, '-Winvalid-pch', '-fpch-instantiate-templates', '-x', 'c++-header',
            '{GEN}/tekken6_pch.h', '-o', pch_h],
    'cxx': [*cxx, *inc_args, '-Winvalid-pch', '-include-pch', pch_h],
}

# 6. link: every object / library of the exe except the generated ones, packed into builder\lib
ninja = pathlib.Path(BLD + '/build.ninja').read_text(encoding='utf-8')
m = re.search(r'^build tekken6\.exe: CXX_EXECUTABLE_LINKER__tekken6_Release (.*?)(?: \|\| .*)?\n((?:  .*\n)+)', ninja, re.M)
inputs = m.group(1).split(' | ')[0].split()
vars_ = dict(re.findall(r'^  (\w+) = (.*)$', m.group(2), re.M))
objs = [o for o in inputs if 'tekken6_recomp.dir' not in o and o.endswith('.obj')]
os.makedirs(B + '/lib', exist_ok=True)
ar = B + '/lib/libt6prebuilt.a'
if os.path.exists(ar):
    os.remove(ar)
rsp = BLD + '/t6prebuilt.rsp'
pathlib.Path(rsp).write_text('\n'.join(os.path.join(BLD, o).replace('\\', '/') for o in objs))
subprocess.check_call([TC + '/bin/llvm-ar.exe', 'rcs', ar, '@' + rsp])
libs = []
for a in vars_['LINK_LIBRARIES'].split():
    if a.startswith('-l'):
        libs.append(a)
    else:
        p = a if os.path.isabs(a) else os.path.join(BLD, a)
        shutil.copy(p, B + '/lib')
        libs.append('{BUILDER}/lib/' + os.path.basename(p))
link_flags = vars_['LINK_FLAGS'].split()
if '-mwindows' not in link_flags:
    link_flags.append('-mwindows')  # GUI program: no console window next to the game
compile_cfg['link'] = [*vars_['FLAGS'].split(), *link_flags,
                       '-Wl,--whole-archive', '{BUILDER}/lib/libt6prebuilt.a', '-Wl,--no-whole-archive',
                       '@{RSP}', *libs]
pathlib.Path(B + '/compile.json').write_text(json.dumps(compile_cfg, indent=1))

# prebuilt.sha: the parts a relink takes (libraries, runtime DLLs, link line). The launcher compares it with
# native\prebuilt.sha of the player's last build: different = 'build_native.py --relink' (seconds, no ISO)
import hashlib
h = hashlib.sha256(json.dumps(compile_cfg['link']).encode())
for d in ('lib', 'runtime'):
    for f in sorted(os.listdir(f'{B}/{d}')):
        h.update(f'{d}/{f}'.encode())
        h.update(pathlib.Path(f'{B}/{d}/{f}').read_bytes())
pathlib.Path(B + '/prebuilt.sha').write_text(h.hexdigest())
print(f'builder {VERSION}: {len(objs)} prebuilt objects, {len(incs)} include dirs ->', B)
