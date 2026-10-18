# Makes launcher/sounds/match_found.wav: the launcher's built-in "match found" gong (our own synthesis; chosen by the
# user 2026-10-14 after many tries: dense metallic modes sounded distorted, this is 6 clean plate modes, 160 Hz):
#   python -I tools/make_match_sound.py launcher/sounds/match_found.wav 160 6
# Clean gong: a few plate modes (circular plate ratios), each a pure decaying tone - no noise, no close pairs that
# beat, no pitch drift; a soft 3 ms mallet; two strikes 0.2 s apart (2nd at 85 %).  clean_gong.py out.wav f0 nmodes
import math, pathlib, struct, sys
rate = 44100; dur = 2.2; n = int(rate * dur)
f0 = float(sys.argv[2]); nm = int(sys.argv[3])
ratios = [1.0, 1.59, 2.14, 2.30, 2.65, 2.92, 3.16, 3.50, 3.60, 4.06, 4.15, 4.60, 4.85, 5.13, 5.40, 5.95, 6.20, 6.70][:nm]
one = [0.0] * n
for k, r in enumerate(ratios):
    hz = f0 * r
    if hz > 6000:
        continue
    amp = r ** -1.4
    decay = 1.4 + hz / 1500
    w = 2 * math.pi * hz / rate
    for i in range(n):
        t = i / rate
        env = 0.4 * math.exp(-t * 12) + 0.6 * math.exp(-t * decay)
        one[i] += amp * env * math.sin(w * i)
for i in range(n):
    t = i / rate
    one[i] *= 0.5 - 0.5 * math.cos(math.pi * min(1.0, t / 0.003))  # soft mallet onset
gap = int(rate * 0.2)
out = [one[i] + (0.85 * one[i - gap] if i >= gap else 0.0) for i in range(n)]
out = [x * min(1.0, (dur - i / rate) / 0.3) for i, x in enumerate(out)]
peak = max(abs(x) for x in out)
pcm = b''.join(struct.pack('<h', int(x / peak * 0.5 * 32767)) for x in out)
hdr = b'RIFF' + struct.pack('<I', 36 + len(pcm)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, 1, rate, rate * 2, 2, 16) + b'data' + struct.pack('<I', len(pcm))
pathlib.Path(sys.argv[1]).write_bytes(hdr + pcm)
