# vtable(s) of a class by its RTTI name, from image.bin (T6_HOLD_IMAGE=1: 0x82000000-0x82726000) and a data dump
# (hold_data_1.bin: 0x82726000-). Usage: vtable_of.py <run dir> <name part, e.g. tcb_BlurControl>
import pathlib, re, struct, sys
d, name = sys.argv[1], sys.argv[2].encode()
img = pathlib.Path(d + '/image.bin').read_bytes()
data = pathlib.Path(d + '/hold_data_1.bin').read_bytes()
def refs(v): return [m.start() + 0x82000000 for m in re.finditer(re.escape(struct.pack('>I', v)), img)]
for m in re.finditer(rb'\.\?AV[ -~]*' + re.escape(name) + rb'[ -~]*', data):
    td = 0x82726000 + m.start() - 8
    print(m.group().decode(), 'TD %08X' % td)
    for r in refs(td):
        col = r - 12
        for v in refs(col):
            vt = v + 4
            o = vt - 0x82000000
            print('  vtable %08X:' % vt, ' '.join('%08X' % x for x in struct.unpack('>14I', img[o:o + 56])))
