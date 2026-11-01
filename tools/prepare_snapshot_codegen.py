"""Apply the packaged builder's snapshot instrumentation to CMake codegen outputs.

Runs inside the codegen custom command, before its stamp and before compilation.
The existing patchers are idempotent. Fail the build if any function is untracked
instead of producing an executable that can save an incomplete task heap.
"""
import pathlib
import re
import subprocess
import sys

TOOLS = ('pagemap_patch', 'rawstore_patch', 'sndcall_patch', 'faststore_patch')


def validate(directory):
    pch = (directory / 'tekken6_pch.h').read_text(encoding='utf-8-sig')
    for marker in ('T6 pagemap diagnostic', 'T6 virtual timebase',
                   'T6 sound boundary', 'T6 cheap stores / loads'):
        if marker not in pch:
            raise ValueError(f'Missing {marker} in generated PCH')
    count = 0
    paths = sorted(directory.glob('tekken6_recomp.*.cpp'))
    if not paths:
        raise ValueError('No generated guest translation units found')
    for path in paths:
        text = path.read_text(encoding='utf-8-sig')
        for marker in ('T6 raw stores marked', 'T6 stack stores'):
            if marker not in text:
                raise ValueError(f'Missing {marker} in {path.name}')
        # Check every prologue, including units with partially applied old patches.
        for match in re.finditer(r'DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\)\s*\{\s*REX_FUNC_PROLOGUE\(\);', text):
            prologue = text[match.end():match.end()+150]
            if not re.match(r'\s*T6_CAPFN\([01]\);\s*T6_KIND\(\d+\);', prologue):
                raise ValueError(f'Incomplete snapshot tracking: {path.name} sub_{match[1]}')
            count += 1
    if not count:
        raise ValueError('No instrumented guest functions found')
    print(f'Snapshot instrumentation verified: {count} functions in {len(paths)} units.', flush=True)


def main():
    directory = pathlib.Path(sys.argv[1]).resolve()
    tools = pathlib.Path(__file__).resolve().parent
    for name in TOOLS:
        subprocess.run([sys.executable, '-I', str(tools / (name + '.py')), str(directory)], check=True)
    validate(directory)


if __name__ == '__main__':
    main()
