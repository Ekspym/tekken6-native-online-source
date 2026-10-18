# Averages of the netplay log windows of a run:  run_stats.py <run> [<run> ...]   (runs\<run>_p1, _p2; session frames
# 600+ so the loading and the intro do not count). State equality of the two sides: tools\state_diff.py.
import re, sys

root = __file__.replace('\\', '/').rsplit('/tools/', 1)[0] + '/runs/'
pats = {
    'game': r'average frame of the last 120: game frame ([\d.]+)',
    'audio': r'average frame of the last 120: .*?audio ([\d.]+)',
    'adv': r'advance \(save \+ rollbacks\) ([\d.]+)',
    'rollbacks': r'last 120 frames: (\d+) rollbacks',
    'resim': r'rollbacks, (\d+) re-simulated',
    'step': r'game step of a re-simulated frame ([\d.]+)',
    'load': r're-simulated frame [\d.]+ ms, load ([\d.]+)',
    'save': r'ms, save ([\d.]+) ms',
    'rb avg': r'whole rollback avg ([\d.]+)',
    'rb max': r'whole rollback avg [\d.]+ max ([\d.]+)',
    'period max': r'period avg [\d.]+ max ([\d.]+)',
    '>17.5': r'over 17\.5 ms (\d+)',
    '>25': r'over 25 ms (\d+)',
    'reserve': r'reserve ([\d.]+)',
}
frame = re.compile(r'T6 net: frame (\d+):')
for run in sys.argv[1:]:
    for side in ('p1', 'p2'):
        acc = {k: [] for k in pats}
        last = 0
        with open(root + f'{run}_{side}/tekken6.log', encoding='utf-8', errors='ignore') as fh:
            for line in fh:
                m = frame.search(line)
                if not m or int(m.group(1)) < 600:
                    continue
                last = int(m.group(1))
                for k, p in pats.items():
                    if (v := re.search(p, line)):
                        # (windows without a rollback hold zeros for the rollback values)
                        if float(v.group(1)) or k not in ('step', 'load', 'rb avg'):
                            acc[k].append(float(v.group(1)))
        out = [f'{k} {sum(v) / len(v):.2f}' if k != 'rb max' else f'{k} {max(v):.1f}' for k, v in acc.items() if v]
        print(f'{run} {side} (to frame {last}): ' + ', '.join(out))
