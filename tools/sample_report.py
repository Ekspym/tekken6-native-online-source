# Names the samples of T6_NET_SAMPLE (samples.txt: "<set> <lr> <count>"). lr is the return address of the last call the
# thread made: the generated code holds "ctx.lr = 0x<lr>;" right before that call, so each lr maps to the function that
# was called there (the code running when the sample was taken, or its caller's code after it returned).
# Usage: sample_report.py <samples.txt> [top n]   -> per set: share of samples by called function and by calling function
import collections, glob, re, sys

root = __file__.replace('\\', '/').rsplit('/tools/', 1)[0]
site = {}   # lr -> (caller function, callee)
fn = None
pat_fn = re.compile(r'^DEFINE_REX_FUNC\((\w+)\)')
pat_lr = re.compile(r'ctx\.lr = 0x([0-9A-Fa-f]{8});')
pat_call = re.compile(r'^\s*(\w+)\(ctx, base\);')
for path in glob.glob(root + '/generated/default/tekken6_recomp.*.cpp'):
    pending = None
    with open(path, encoding='utf-8', errors='ignore') as fh:
        for line in fh:
            m = pat_fn.match(line)
            if m:
                fn = m.group(1)
                continue
            m = pat_lr.search(line)
            if m:
                pending = int(m.group(1), 16)
                continue
            if pending is not None:
                m = pat_call.match(line)
                if m:
                    site[pending] = (fn, m.group(1))
                    pending = None
                elif 'PPC_CALL_INDIRECT' in line or 'ctr' in line and '(' in line:
                    site[pending] = (fn, 'indirect')
                    pending = None
top = int(sys.argv[2]) if len(sys.argv) > 2 else 25
sets = collections.defaultdict(list)
total = {}
with open(sys.argv[1]) as fh:
    for line in fh:
        w = line.split()
        if line.startswith('#'):
            print(line.strip())
        elif len(w) == 3 and w[1] == 'total':
            total[w[0]] = int(w[2])
        elif len(w) == 3:
            sets[w[0]].append((int(w[1], 16), int(w[2])))
for name, rows in sets.items():
    n = total.get(name) or sum(c for _, c in rows)
    print('== %s: %d samples = %.1f s' % (name, n, n / 5000.0))
    by_callee, by_caller = collections.Counter(), collections.Counter()
    for lr, c in rows:
        caller, callee = site.get(lr, ('?%08X' % lr, '?'))
        by_callee[callee] += c
        by_caller[caller] += c
    print('  by called function:')
    for k, c in by_callee.most_common(top):
        print('    %5.1f %%  %s' % (100.0 * c / n, k))
    print('  by calling function:')
    for k, c in by_caller.most_common(top):
        print('    %5.1f %%  %s' % (100.0 * c / n, k))
