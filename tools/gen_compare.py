# Compares two generated code trees function by function (the partition into tekken6_recomp.N.cpp files may differ
# between codegen runs):  gen_compare.py <generated/default A> <generated/default B>
# Prints functions only in one tree, functions whose bodies differ (first few), and whether the pch headers agree.
import glob, pathlib, re, sys

pat = re.compile(r'^DEFINE_REX_FUNC\((\w+)\)')


def funcs(d):
    out, name, body = {}, None, []
    for p in sorted(glob.glob(d + '/tekken6_recomp.*.cpp')):
        with open(p, encoding='utf-8', errors='replace') as fh:
            for line in fh:
                m = pat.match(line)
                if m:
                    if name:
                        out[name] = ''.join(body)
                    name, body = m.group(1), [line]
                elif name:
                    body.append(line)
                    if line.startswith('}'):
                        out[name] = ''.join(body)
                        name, body = None, []
    return out


a, b = funcs(sys.argv[1]), funcs(sys.argv[2])
only_a, only_b = sorted(set(a) - set(b)), sorted(set(b) - set(a))
diff = [k for k in sorted(set(a) & set(b)) if a[k] != b[k]]
print(f'functions: {len(a)} / {len(b)}, only in A {len(only_a)}, only in B {len(only_b)}, different {len(diff)}')
for k in (only_a[:5] + only_b[:5]):
    print('  only in one tree:', k)
for k in diff[:5]:
    la, lb = a[k].splitlines(), b[k].splitlines()
    i = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
    print(f'  {k}: line {i}: A {la[i] if i < len(la) else "<end>"!r} | B {lb[i] if i < len(lb) else "<end>"!r}')
ha = pathlib.Path(sys.argv[1] + '/tekken6_pch.h').read_text(encoding='utf-8').replace('\r\n', '\n')
hb = pathlib.Path(sys.argv[2] + '/tekken6_pch.h').read_text(encoding='utf-8').replace('\r\n', '\n')
print('pch:', 'EQUAL' if ha == hb else 'DIFFERENT')
