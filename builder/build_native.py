"""Tekken 6 Native Online: builds the game (native\\tekken6.exe) on the player's PC from his OWN copy of Tekken 6 for
Xbox 360 (disc image .iso or an extracted folder). Nothing of the game is shipped with the package: the game's code
(Default.xex) is translated to C++ here (rexglue codegen), the netplay patches are applied (the same tools as the
development build) and the result is compiled with the bundled compiler and linked against the prebuilt runtime.
The game's data files are extracted to native\\game (the game reads them from there).

usage:  python build_native.py <iso or folder>          build (progress lines below, log native\\build.log)
        python build_native.py --update                 after an update of the package: the game data already in
                                                        native\\game, only changed files are compiled (no ISO)
        python build_native.py --relink                 only the prebuilt parts changed: link again (seconds)
        python build_native.py --check <iso or folder>  only check that it is the supported version
The objects stay in native\\obj (keys in obj\\keys.json): an update compiles only the files whose source, flags
or precompiled header changed; a relink needs no compiler run at all.
Output lines for the launcher:  STEP <n>/<N> <text>, PROGRESS <0-100>, DONE <exe>, ERROR <text>; exit 0 / 1.
Supported: Tekken 6 (USA, Europe) Rev 1 - title 4E4D07FC, Default.xex SHA-256 below."""
import concurrent.futures
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))     # builder
ROOT = os.path.dirname(HERE)                          # package (launcher)
OUT = os.path.join(ROOT, 'native')
GAME = os.path.join(OUT, 'game')
WORK = os.path.join(OUT, 'work')
GEN = os.path.join(WORK, 'generated', 'default')
OBJ = os.path.join(OUT, 'obj')  # kept between builds (outside work\\, which codegen starts fresh)
KEYS = os.path.join(OBJ, 'keys.json')
RSP = os.path.join(OUT, 'objects.rsp')
EXE = os.path.join(OUT, 'tekken6.exe')
TOOLCHAIN = os.path.join(HERE, 'llvm-mingw', 'bin')
XEX_SHA256 = {'ee138a4fe8d32a0edc131451377f4377041a49626153fa992db24f5420a7f15c': 'Tekken 6 (USA, Europe) Rev 1'}
PATCH_TOOLS = ('pagemap_patch', 'rawstore_patch', 'sndcall_patch', 'faststore_patch')  # this order (faststore last)
STEPS = 6
# Every tool runs without a console window (the launcher starts this script without one; each compiler / codegen
# process would otherwise open its own black window)
NO_WINDOW = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
log_file = None


def say(msg):
    print(msg, flush=True)
    if log_file:
        log_file.write(msg + '\n')
        log_file.flush()


def fail(msg):
    say('ERROR ' + msg)
    sys.exit(1)


def step(n, text):
    say(f'STEP {n}/{STEPS} {text}')


def run(args, what, cwd=None):
    """Runs a tool; its output goes to the log. A failure ends the build."""
    t0 = time.time()
    p = subprocess.run(creationflags=NO_WINDOW, args=args, cwd=cwd or WORK, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
    if log_file:
        log_file.write(p.stdout)
    if p.returncode:
        tail = '\n'.join(p.stdout.strip().splitlines()[-5:])
        fail(f'{what} failed (native\\build.log): {tail}')
    if log_file:
        log_file.write(f'{what}: {time.time() - t0:.1f} s\n')


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def supported(digest, what):
    """(version name, None) for the supported Default.xex, else (None, the error text)."""
    if digest in XEX_SHA256:
        return XEX_SHA256[digest], None
    return None, (f'this {what} is not the supported version (Tekken 6 USA/Europe, Rev 1, Xbox 360); '
                  f'Default.xex SHA-256 {digest[:16]}...')


def find_xex(folder):
    for name in os.listdir(folder):
        if name.lower() == 'default.xex':
            return os.path.join(folder, name)
    return None


def check_folder(folder):
    xex = find_xex(folder)
    if not xex:
        return None, 'no Default.xex in the folder - pick the folder of the extracted disc or the .iso file'
    if not any(n.upper().startswith('DATA') for n in os.listdir(folder)):
        return None, 'the game data (DATA*.BIN) is missing next to Default.xex'
    name, error = supported(sha256(xex), 'copy')
    return (xex, name) if name else (None, error)


def xdvdfs(*args):
    return [os.path.join(HERE, 'bin', 'xdvdfs.exe'), *args]


def check_iso(iso):
    """Reads Default.xex out of the image (small) to check it before extracting 7 GB."""
    tmp = os.path.join(OUT, 'check')
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    # (names in the image are case sensitive: take Default.xex's spelling from the listing)
    listing = os.path.join(tmp, 'list.txt')
    subprocess.run(creationflags=NO_WINDOW, args=xdvdfs('list', iso, listing), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    names = []
    if os.path.exists(listing):
        names = [l.split('	', 1)[1].strip() for l in open(listing, encoding='utf-8', errors='replace') if '	' in l]
    name = next((n for n in names if n.lower() == 'default.xex'), 'Default.xex')
    p = subprocess.run(creationflags=NO_WINDOW, args=xdvdfs('extract', iso, name, os.path.join(tmp, 'Default.xex')),
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
    xex = os.path.join(tmp, 'Default.xex')
    if p.returncode or not os.path.exists(xex):
        return None, 'the file is not an Xbox 360 disc image (or the image is damaged)'
    digest = sha256(xex)
    shutil.rmtree(tmp, ignore_errors=True)
    name, error = supported(digest, 'image')
    return (iso, name) if name else (None, error)


def check(src):
    if not os.path.exists(src):
        return None, f'not found: {src}'
    return check_folder(src) if os.path.isdir(src) else check_iso(src)


def game_ready():
    """native\\game holds an extracted copy of the supported version (left from an earlier build)."""
    return os.path.isdir(GAME) and check_folder(GAME)[0] is not None


def has_fma3():
    """FMA3 on this CPU: Windows reports AVX2 (PF_AVX2_INSTRUCTIONS_AVAILABLE, every AVX2 CPU has FMA3); an older
    Windows or CPU says no - the game is then compiled without it (slower float math, otherwise the same)."""
    try:
        import ctypes
        return bool(ctypes.windll.kernel32.IsProcessorFeaturePresent(40))
    except Exception:
        return False


def compile_all(cfg):
    """Compiles the generated game code (pch first, then every file) on all cores."""
    os.makedirs(OBJ, exist_ok=True)
    if not has_fma3():
        say('  this CPU has no FMA3 (AVX2): the game is compiled without it')
        cfg = dict(cfg, pch=[a for a in cfg['pch'] if a != '-mfma'], cxx=[a for a in cfg['cxx'] if a != '-mfma'])
    sub = lambda s: s.replace('{BUILDER}', HERE).replace('{GEN}', GEN).replace('{OBJ}', OBJ)
    clang = os.path.join(TOOLCHAIN, 'clang++.exe')
    # The same exe on every PC (the launcher pairs players by a hash of it): no path of this folder in the output
    repro = [f'-ffile-prefix-map={ROOT}=T6', f'-ffile-prefix-map={ROOT.replace(os.sep, "/")}=T6', '-g0']
    cfg = dict(cfg, pch=[*cfg['pch'], *repro], cxx=[*cfg['cxx'], *repro])
    pch = [sub(a) for a in cfg['pch']]
    files = sorted(f for f in os.listdir(GEN) if f.endswith('.cpp'))  # recomp.N, init, register
    # Key of the precompiled header: the flags, every generated header and the builder's SDK headers;
    # key of a file: its text + the pch key. Same key and the object exists = not compiled again.
    h = hashlib.sha256(json.dumps([cfg['pch'], cfg['cxx']]).encode())
    for base in (GEN, os.path.join(HERE, 'sdk')):
        for dp, dns, fns in os.walk(base):
            dns.sort()
            for fn in sorted(fns):
                if not fn.endswith('.cpp'):
                    h.update(os.path.relpath(os.path.join(dp, fn), base).encode())
                    h.update(open(os.path.join(dp, fn), 'rb').read())
    pch_key = h.hexdigest()
    try:
        old = json.load(open(KEYS, encoding='utf-8'))
    except (OSError, ValueError):
        old = {}
    keys = {f: hashlib.sha256((pch_key + f).encode() + open(os.path.join(GEN, f), 'rb').read()).hexdigest() for f in files}
    todo = [f for f in files if old.get(f) != keys[f] or not os.path.exists(os.path.join(OBJ, f + '.obj'))]
    say(f'  {len(todo)} of {len(files)} files to compile')
    if todo:
        json.dump({}, open(KEYS, 'w'))  # a build that fails half way leaves no stale keys
        run([clang, *pch], 'precompiled header')
    done, total = 0, max(1, len(todo))
    jobs = max(1, (os.cpu_count() or 2) - 1)

    def one(f):
        args = [clang, *[sub(a) for a in cfg['cxx']], '-c', os.path.join(GEN, f), '-o', os.path.join(OBJ, f + '.obj')]
        p = subprocess.run(creationflags=NO_WINDOW, args=args, cwd=WORK, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
        return f, p.returncode, p.stdout

    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        for f, code, out in ex.map(one, todo):
            if code:
                log_file.write(out)
                fail(f'compiling {f} failed (native\\build.log)')
            done += 1
            say(f'PROGRESS {20 + 70 * done // total}')
    for f in os.listdir(OBJ):  # objects of files that are gone
        if f.endswith('.obj') and f[:-4] not in keys:
            os.remove(os.path.join(OBJ, f))
    json.dump(keys, open(KEYS, 'w'), indent=0)
    return [os.path.join(OBJ, f + '.obj') for f in files]


def link(cfg, objs):
    """Links native\\tekken6.exe from the game objects and the builder's prebuilt parts, copies the runtime."""
    with open(RSP, 'w') as r:
        r.write('\n'.join('"' + o.replace('\\', '/') + '"' for o in objs))
    sub = lambda s: s.replace('{BUILDER}', HERE).replace('{OBJ}', OBJ).replace('{OUT}', OUT).replace('{RSP}', RSP)
    tmp_exe = os.path.join(OUT, 'tekken6.new.exe')
    run([os.path.join(TOOLCHAIN, 'clang++.exe'), *[sub(a) for a in cfg['link']], '-Wl,--no-insert-timestamp', '-s', '-o', tmp_exe], 'link')
    for f in os.listdir(os.path.join(HERE, 'runtime')):
        shutil.copy(os.path.join(HERE, 'runtime', f), OUT)
    os.replace(tmp_exe, EXE)
    for f in ('build.version', 'prebuilt.sha'):
        if os.path.exists(os.path.join(HERE, f)):
            shutil.copy(os.path.join(HERE, f), os.path.join(OUT, f))


def main():
    global log_file
    args = sys.argv[1:]
    if len(args) == 2 and args[0] == '--check':
        what, info = check(args[1])
        print(f'OK {info}' if what else f'ERROR {info}', flush=True)
        return 0 if what else 1
    relink = args == ['--relink']
    if args == ['--update']:
        args = [GAME]  # the data the first build extracted; no ISO needed
    if relink:
        args = [GAME]
    if len(args) != 1:
        print(__doc__)
        return 1
    src = os.path.abspath(args[0])
    os.makedirs(OUT, exist_ok=True)
    log_file = open(os.path.join(OUT, 'build.log'), 'w', encoding='utf-8')
    t0 = time.time()
    say(f'version {open(os.path.join(HERE, "build.version")).read().strip()}, source {src}')
    cfg = json.load(open(os.path.join(HERE, 'compile.json'), encoding='utf-8'))
    if relink:
        objs = [l.strip().strip('"') for l in open(RSP)] if os.path.exists(RSP) else []
        objs = [o for o in objs if o]
        if not objs or not os.path.exists(KEYS) or not all(os.path.exists(o) for o in objs):
            fail('no objects of an earlier build - build the game from your copy')
        step(6, 'Linking the updated parts')
        link(cfg, objs)
        say('PROGRESS 100')
        say(f'  linked in {time.time() - t0:.0f} s')
        say(f'DONE {EXE}')
        return 0

    step(1, 'Checking the game copy')
    what, info = check(src)
    if not what:
        fail(info)
    say(f'  {info}')
    say('PROGRESS 2')

    step(2, 'Copying the game data')
    if os.path.normcase(src) != os.path.normcase(GAME) and not game_ready():  # same version already extracted: kept
        shutil.rmtree(GAME, ignore_errors=True)
        if os.path.isdir(src):
            shutil.copytree(src, GAME)
        else:
            run(xdvdfs('extract-all', src, GAME), 'extracting the disc image', cwd=OUT)
    if not game_ready():
        fail('the game data could not be prepared in native\\game')
    say('PROGRESS 10')

    step(3, 'Translating the game code')
    shutil.rmtree(WORK, ignore_errors=True)
    os.makedirs(GEN)
    for f in ('tekken6_config.toml',):
        shutil.copy(os.path.join(HERE, 'project', f), WORK)
    with open(os.path.join(WORK, 'tekken6_manifest.toml'), 'w') as m:
        m.write('[project]\nname = "tekken6"\nsdk_version = "0.10.0"\ngame_root = "../game"\n\n'
                '[entrypoint]\nfile_path = "../game/' + os.path.basename(find_xex(GAME)) + '"\n'
                'out_directory_path = "generated/default"\nincludes = [\n  "tekken6_config.toml",\n]\n')
    run([os.path.join(HERE, 'bin', 'rexglue.exe'), '-f', 'codegen', os.path.join(WORK, 'tekken6_manifest.toml')], 'codegen')
    say('PROGRESS 15')

    step(4, 'Applying the netplay patches')
    for t in PATCH_TOOLS:
        run([sys.executable, '-I', os.path.join(HERE, 'project', 'tools', t + '.py'), GEN], t)
    say('PROGRESS 20')

    step(5, 'Compiling (this takes a while)')
    objs = compile_all(cfg)

    step(6, 'Linking')
    link(cfg, objs)
    say('PROGRESS 100')
    say(f'  built in {time.time() - t0:.0f} s')
    say(f'DONE {EXE}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
