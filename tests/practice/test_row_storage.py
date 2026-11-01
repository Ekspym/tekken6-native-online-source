"""Exercise the real expanded-row implementation with a minimal SDK allocator."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class RowStorageTest(unittest.TestCase):
    def test_eighteenth_row_and_slot_map(self):
        compiler = shutil.which('g++')
        if not compiler:
            self.skipTest('g++ is needed for the standalone SDK-stub check')
        with tempfile.TemporaryDirectory(prefix='t6-rows-') as temp:
            directory = pathlib.Path(temp)
            ppc = directory / 'rex/ppc'
            ppc.mkdir(parents=True)
            (ppc / 'context.h').write_text('''#pragma once
#include <cstdint>
struct PPCRegister {uint64_t u64=0;};
struct PPCContext {
 PPCRegister r3;
 struct {uint32_t csr=0;void restoreGuestBits(uint32_t value){csr=value;}} fpscr;
};
''')
            (directory / 'rex/runtime.h').write_text('''#pragma once
#include <cstdint>
namespace rex {
struct Memory {
 uint32_t cursor=512000,lastFreed=0;
 uint32_t SystemHeapAlloc(uint32_t size,uint32_t align){
   cursor=(cursor+align-1)&~(align-1);const uint32_t out=cursor;cursor+=size;return out;
 }
 void SystemHeapFree(uint32_t address){lastFreed=address;}
};
struct Runtime {
 static Runtime* instance(){static Runtime runtime;return &runtime;}
 Memory* memory(){return &mem;} Memory mem;
};
}
''')
            (directory / 'rex/memory.h').write_text('#pragma once\n#include "runtime.h"\n')
            code = '''
#include <cassert>
#include <vector>
#include "src/practice/tekken6_expanded_rows.inc"
std::vector<uint32_t> constructors,destructors;
extern "C" void sub_8236AF50(PPCContext& c,uint8_t*){constructors.push_back(c.r3.u64);}
extern "C" void sub_8236AFE8(PPCContext& c,uint8_t*){constructors.push_back(c.r3.u64);}
extern "C" void sub_82369EC0(PPCContext& c,uint8_t*){destructors.push_back(c.r3.u64);}
int main(){
 using namespace t6_expanded_rows;
 std::vector<uint8_t> ram(1024*1024);auto* b=ram.data();PPCContext c{};
 const uint32_t owner=4096,other=65536;
 std::memset(b+owner+2972,255,14);
 store32(b,owner+2988,0xDEADBEEF);
 c.r3.u64=42;c.fpscr.csr=99;
 assert(T6RowsCapacity(owner)==14);
 T6RowsInitialize(c,b,owner);
 assert(T6RowsCapacity(owner)==18 && constructors.size()==12);
 assert(c.r3.u64==42 && c.fpscr.csr==99);
 const auto storage=windows.at(owner);
 assert(storage.allocation+allocationSize<=ram.size());
 for(uint32_t bank=0;bank<3;++bank)for(uint32_t index=0;index<capacity;++index){
   const auto address=T6RowsMap(owner+offsets[bank]+index*strides[bank],bank,owner);
   assert(address==row(owner,storage,bank,index));
   assert(T6RowsMap(owner+offsets[bank]+index*strides[bank]+7,bank,owner)==address+7);
   if(index+1<capacity)assert(T6RowsAdvance(address,strides[bank],owner)==row(owner,storage,bank,index+1));
   for(uint32_t dest=0;dest<3;++dest)assert(T6RowsRelated(address,dest,owner)==row(owner,storage,dest,index));
 }
 for(uint32_t index=0;index<capacity;++index){
   const auto address=T6RowsSlotMap(owner+2972+index,owner);
   assert(address==storage.allocation+slotMapOffset+index && b[address]==255);
   b[address]=index;
 }
 assert(b[owner+2988]==0xDE && b[owner+2989]==0xAD && b[owner+2990]==0xBE && b[owner+2991]==0xEF);
 assert(T6RowsSlotMap(owner+2972,other)==owner+2972);
 assert(T6RowsSlotMap(owner+2972+capacity,owner)==owner+2972+capacity);
 T6RowsInitialize(c,b,owner);assert(constructors.size()==12);
 T6RowsInitialize(c,b,other);assert(constructors.size()==24);
 assert(T6RowsSlotMap(other+2972,other)!=T6RowsSlotMap(owner+2972,owner));
 T6RowsDestroy(c,b,owner);
 assert(destructors.size()==12 && T6RowsCapacity(owner)==14 && T6RowsCapacity(other)==18);
 assert(T6RowsSlotMap(owner+2972,owner)==owner+2972);
 assert(rex::Runtime::instance()->memory()->lastFreed==storage.allocation);
 T6RowsDestroy(c,b,other);assert(destructors.size()==24 && windows.empty());
}
'''
            source = directory / 'rows.cpp'
            source.write_text(code)
            binary = directory / 'rows-test'
            subprocess.run([compiler, '-std=c++17', '-pthread', '-I'+str(directory),
                            '-I'+str(ROOT), str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], cwd=directory, check=True)


if __name__ == '__main__':
    unittest.main()
