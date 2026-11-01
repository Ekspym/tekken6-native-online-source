#!/usr/bin/env python3
"""Patch build copies of the supplied ReXGlue menu functions; originals stay intact."""
import argparse
import pathlib
import re
import hashlib

OWNERS = {
 '82430830':'r3', '82430870':'r31', '82430A38':'r28',
 '82430B88':'r31', '82430BF8':'r31', '82430C50':'r31',
 '82432398':'r31', '82432CD8':'r27', '82433128':'r31', '82433560':'r31',
}
TARGETS = set(OWNERS) | {'824306D8','82430788','822FEDA8','8239E4F8'}
BANK = {2996:0,18676:1,-31168:2}
EXPECTED = {'822FEDA8': '34d3a19990a9529d69470e534af440f0616dee08ee19666af7e950144aa76e71', '8239E4F8': 'b4393d1c0b485ec8ea2ca0653e0a0952f404a2b6bce93750947d47c78f8197a0', '824306D8': 'e53c6b2e8b1baf175c3e4194b0e7b4e97bc271fe826c58d22b222bf111432831', '82430788': '3599d67d7e9d544fd6c58ae72fc0c8104bbbe04a7f4a354c4743236b887a1546', '82430830': 'bed740969b1638f68ad7e4151f6a7551812f188e71666e5cee3e5994c63375ad', '82430870': 'd7030ecee6347274303ef06e66f63aeb3d0921dc380d055aed24caf237d95a72', '82430A38': '41a3c38f79e668720897efff969b71934ff4b5c54fa05698590712e6d4b27d68', '82430B88': 'c0178438fa076e1d0e9d77c711f36380a6391793e3a372842605799459d4ce26', '82430BF8': '62ea1db095fd40928132264f9a7a80123aff2a48015ad2c4f019836d9e37ab85', '82430C50': 'bf25cb96a352acc81036665685cb8791f6881eee6a5db2e8797d523c28ea9510', '82432398': '8e0b857a1390024c5e3eee9d1d3ad3156846b752d42c69b8f8f0bdab958d3aef', '82432CD8': 'e0146f8ff7fe14fa8acc474ea16da6da1b97fc32c1334b8a3c4afd3ed165c6d0', '82433128': '81a73bcd59d3b4093ca7b1e415433a5dd1b85209bc1dce735304d9de112e0205', '82433560': '85df31ef374c0652430dd7ff9e8682c60208bcc7612da7a905572b8ba0c51a67', '82436D58': '5d4bcc70d280515871e1fe3885b024187c9bc00928f0aefac4cf6d7bfe5948fd', '82436E28': 'c04c25b8c0ce0bf0dfa4a6de70cc4c5f2b968329ff0a5296ef906aff5ee75d67'}

def patch_function(name, text):
    # BEGIN QUICK SELECT UI GUARD V5
    if name == '822FA058':
        instructions = '\n'.join(re.findall(r'^\t// ([^\n]*)', text, re.M))
        if hashlib.sha256(instructions.encode()).hexdigest() != 'a052a2222126a2838073d9adde99e12227601aa53390d33d07cb2460f76ae6ec':
            raise ValueError('Unsupported selection UI renderer; refusing render guard')
        needle = '\tREX_FUNC_PROLOGUE();\n'
        if text.count(needle) != 1:
            raise ValueError('Unexpected selection UI renderer prologue')
        guard = ('\textern bool QuickSelectHideSelectionUI(uint8_t*, uint32_t);\n'
                 '\tif (QuickSelectHideSelectionUI(base, ctx.r3.u32)) return;\n')
        return text.replace(needle, needle + guard, 1), 1
    # END QUICK SELECT UI GUARD V5
    changes = 0
    if name in EXPECTED:
        instructions = '\n'.join(re.findall(r'^\t// ([^\n]*)', text, re.M))
        if hashlib.sha256(instructions.encode()).hexdigest() != EXPECTED[name]:
            raise ValueError('Unsupported instruction layout in sub_' + name + '; refusing partial patch')
    if name == '82436E28':
        if text.count('\n\treturn;') != 1: raise ValueError('Unexpected menu constructor returns')
        text = text.replace('\n\treturn;', '\n\tT6RowsInitialize(ctx, base, ctx.r3.u32);\n\treturn;')
        return text, 1
    if name == '82436D58':
        text,n = re.subn(r'(\tuint32_t ea\{\};\n)', r'\1\tT6RowsDestroy(ctx, base, ctx.r3.u32);\n', text, count=1)
        if n != 1: raise ValueError('Unexpected menu destructor prologue')
        return text, 1
    if name not in TARGETS: return text, 0
    prologue = '\tREX_FUNC_PROLOGUE();\n'
    if text.count(prologue) != 1: raise ValueError('Unexpected owner capture in sub_' + name)
    text = text.replace(prologue, prologue + '\t[[maybe_unused]] const uint32_t t6RowsOwner = ctx.r3.u32; // OWNER-SCOPED MENU ROWS FIX\n', 1)
    blocks = re.split(r'(?=^\t// )', text, flags=re.M)
    result = []
    for block in blocks:
        m = re.match(r'\t// ([^\n]*)\n', block)
        if not m:
            result.append(block); continue
        instruction = m[1]
        add = re.fullmatch(r'addi (r\d+),(r\d+),(-?\d+)', instruction)
        if add:
            dest,src,n = add.groups(); n=int(n)
            if n == 2972:
                # Redirect the complete byte-map base, including indexed accesses.
                statement = f'\tctx.{dest}.u64 = T6RowsSlotMap(ctx.{dest}.u32, t6RowsOwner);\n'
                p = re.compile(r'(\tctx\.'+dest+r'\.s64 = [^;]+;\n)')
                block,count = p.subn(lambda x:x[1]+statement,block,count=1)
                if count != 1: raise ValueError(name+': unexpected slot-map base '+instruction)
                changes += 1
            elif n in BANK:
                statement = f'\tctx.{dest}.u64 = T6RowsMap(ctx.{dest}.u32, {BANK[n]}, t6RowsOwner); // expanded row address\n'
                # Insert after the instruction's original assignment, before labels.
                p = re.compile(r'(\tctx\.'+dest+r'\.s64 = [^;]+;\n)')
                block,count = p.subn(lambda x:x[1]+statement,block,count=1)
                if count != 1: raise ValueError(name+': unexpected addi '+instruction)
                changes += 1
            elif n in (1120,320,15680,-15680):
                if n in (1120,320):
                    rhs=f'T6RowsAdvance(ctx.{src}.u32, {n}, t6RowsOwner)'
                else:
                    rhs=f'T6RowsRelated(ctx.{src}.u32, {0 if n < 0 else 1}, t6RowsOwner)'
                p = re.compile(r'\tctx\.'+dest+r'\.s64 = [^;]+;')
                block,count=p.subn(f'\tctx.{dest}.u64 = {rhs};',block,count=1)
                if count != 1: raise ValueError(name+': unexpected row increment '+instruction)
                changes += 1
        load=re.fullmatch(r'lwz (r\d+),(-?\d+)\((r\d+)\)',instruction)
        if load:
            dest,off,src=load.groups();off=int(off)
            if off in (2996,18676,15680,-15680):
                rhs=(f'T6RowsMap(ctx.{src}.u32 + {off}, {BANK[off]}, t6RowsOwner)'
                     if off in BANK else f'T6RowsRelated(ctx.{src}.u32, {0 if off<0 else 1}, t6RowsOwner)')
                p=re.compile(r'REX_LOAD_U32\(ctx\.'+src+r'\.u32 \+ '+str(off)+r'\)')
                block,count=p.subn(f'REX_LOAD_U32({rhs})',block,count=1)
                if count!=1:raise ValueError(name+': unexpected row load '+instruction)
                changes+=1
        slot_access = re.fullmatch(r'(lbz|stb) (r\d+),2972\((r\d+)\)',instruction)
        if slot_access:
            op,reg,src=slot_access.groups()
            macro='REX_LOAD_U8' if op=='lbz' else 'REX_STORE_U8'
            needle=f'{macro}(ctx.{src}.u32 + 2972'
            replacement=f'{macro}(T6RowsSlotMap(ctx.{src}.u32 + 2972, t6RowsOwner)'
            if block.count(needle)!=1:raise ValueError(name+': unexpected slot-map access '+instruction)
            block=block.replace(needle,replacement,1)
            changes+=1
        # Indicator byte 113 is read through an indexed offset of 34368+113.
        if name=='82430C50' and instruction=='lbzx r6,r11,r7':
            block,count=re.subn(r'REX_LOAD_U8\(ctx\.r11\.u32 \+ ctx\.r7\.u32\)',
                               'REX_LOAD_U8(T6RowsMap(ctx.r11.u32 + ctx.r7.u32, 2, t6RowsOwner))',block,count=1)
            if count!=1:raise ValueError('Unexpected indexed indicator flag load')
            changes+=1
        if name in OWNERS:
            capacity=f'T6RowsCapacity(ctx.{OWNERS[name]}.u32)'
            if re.fullmatch(r'cmp(?:l)?wi cr6,r\d+,14',instruction):
                block,count=re.subn(r'(\.compare<(?:u?int32_t)>\(ctx\.r\d+\.(?:s32|u32), )14(, ctx\.xer\);)',
                                    lambda x:x[1]+capacity+x[2],block,count=1)
                if count!=1:raise ValueError(name+': unexpected capacity comparison')
                changes+=1
            if instruction=='li r10,14':
                block,count=re.subn(r'ctx\.r10\.s64 = 14;',f'ctx.r10.s64 = {capacity};',block,count=1)
                if count!=1:raise ValueError(name+': unexpected slot-map reset')
                changes+=1
        result.append(block)
    return ''.join(result),changes

MARKER = '// ADVANCED PRACTICE ROW FALLBACK V1'

def transform(source):
    if MARKER in source:
        return source, 0
    if 'OWNER-SCOPED MENU ROWS FIX' in source:
        raise ValueError('Already adapted by another row patch; regenerate guest code before migrating')
    count=0
    parts=re.split(r'(?=^DEFINE_REX_FUNC\()',source,flags=re.M)
    for i,part in enumerate(parts):
        m=re.match(r'DEFINE_REX_FUNC\(sub_([0-9A-F]+)\)',part)
        if not m: continue
        changed,n=patch_function(m[1],part)
        if not n: continue
        name=m[1]
        # Preserve an exact original body for online and rollback execution.
        original=part.replace('DEFINE_REX_FUNC(sub_'+name+')',
                              'DEFINE_REX_FUNC(T6Original_'+name+')',1)
        prologue='\tREX_FUNC_PROLOGUE();\n'
        if changed.count(prologue)!=1: raise ValueError('Ambiguous function prologue')
        guard=('\textern bool T6AdvancedGuestAvailable();\n'
               '\tif (!T6AdvancedGuestAvailable()) { T6Original_'+name+'(ctx,base); return; }\n')
        parts[i]=original+'\n'+changed.replace(prologue,prologue+guard,1)
        count+=n
    output=''.join(parts)
    if count: output=MARKER+'\n#include "tekken6_expanded_rows.h"\n'+output
    return output,count

def main():
    p=argparse.ArgumentParser();p.add_argument('--input');p.add_argument('--output');p.add_argument('--validate-dir');p.add_argument('--in-place-dir')
    args=p.parse_args()
    if args.in_place_dir:
        files=sorted(pathlib.Path(args.in_place_dir).glob('tekken6_recomp.*.cpp'))
        if not files: raise ValueError('No generated guest source files')
        # Validate every file before touching any output.
        pending=[(file,transform(file.read_text(encoding='utf-8'))) for file in files]
        for file,(out,count) in pending:
            if count: file.write_text(out,encoding='utf-8')
        print('Offline practice row adaptations checked')
        return
    if args.validate_dir:
        seen=set(); total=0
        for file in pathlib.Path(args.validate_dir).glob('tekken6_recomp.*.cpp'):
            source=file.read_text(encoding='utf-8')
            for name in re.findall(r'^DEFINE_REX_FUNC\(sub_([0-9A-F]+)\)',source,re.M):
                if name in EXPECTED: seen.add(name)
            _,n=transform(source);total+=n
        if seen != set(EXPECTED):
            raise ValueError('Missing audited menu functions: '+', '.join(sorted(set(EXPECTED)-seen)))
        print(f'Validated {len(seen)} menu functions; {total} storage/capacity adaptations')
        return
    if not args.input or not args.output: p.error('--input and --output are required')
    src=pathlib.Path(args.input);dst=pathlib.Path(args.output)
    out,count=transform(src.read_text(encoding='utf-8'))
    dst.parent.mkdir(parents=True,exist_ok=True)
    if not dst.exists() or dst.read_text(encoding='utf-8')!=out:dst.write_text(out,encoding='utf-8')
    # The custom-command output must be newer than its prerequisites even if bytes match.
    dst.touch()
    if count:print(f'{src.name}: {count} row-storage adaptations')
if __name__=='__main__':main()
