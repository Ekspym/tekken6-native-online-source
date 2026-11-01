"""Compile the real voice-guard functions against controlled runtime fixtures."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class SnapshotVoiceGuardTest(unittest.TestCase):
    def test_advanced_and_legacy_guard_paths(self):
        compiler = shutil.which('g++')
        if not compiler:
            self.skipTest('g++ is required for the standalone guard test')
        source = (ROOT / 'src/t6_voices.cpp').read_text()
        code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#define REXLOG_WARN(...) ((void)0)
struct Config {bool enabled=false;} config;
Config& net_config(){return config;}
bool legacyPractice=false,advanced=false;
const char* T6Env(const char*){return legacyPractice?"1":nullptr;}
bool T6AdvancedRequested(){return advanced;}
union Register {uint64_t u64;uint32_t u32;};
struct PPCContext {Register r3{};uint64_t lr=0;};
std::unordered_map<uint32_t,uint32_t> ram;
bool readable=true;
bool PageReadable(uint32_t){return readable;}
uint32_t T6Rd32(uint32_t address){return ram[address];}
bool T6LogSample(uint64_t& count,unsigned,unsigned){++count;return false;}
uint32_t g_net_frame=0;bool g_net_resim=false;
uint64_t g_stale_voice_calls=0,g_dead_calls=0;
std::mutex g_dead_mutex;
std::unordered_map<uint32_t,uint32_t> g_dead;
'''
        for signature in ['bool VoiceGuardOn()', 'bool StaleVoice(', 'bool Dead(', 'void SetDead(', 'bool DeadCall(']:
            code += function(source, signature) + '\n'
        code += r'''
int main(int argc,char** argv){
 assert(argc==2);
 config.enabled=!std::strcmp(argv[1],"net");
 legacyPractice=!std::strcmp(argv[1],"legacy");
 const bool legacy=config.enabled||legacyPractice;
 assert(VoiceGuardOn()==legacy);
 advanced=true;assert(VoiceGuardOn());
 PPCContext ctx;constexpr uint32_t voice=0x30001000u,vt=0x82001000u;
 ram[voice]=0x3F800000u;ctx.r3.u64=voice;
 assert(DeadCall(ctx,nullptr,0x824EECF0u,0x88960001u) && ctx.r3.u32==0x88960001u);
 ram[voice]=vt;ram[vt]=0x824EE000u;ram[vt+92]=0x824EE500u;
 ctx.r3.u64=voice;assert(!DeadCall(ctx,nullptr,0x824EECF0u,0x88960001u));
 SetDead(voice,true);ctx.r3.u64=voice;assert(DeadCall(ctx,nullptr,0x824EECF0u,0x88960001u));
 SetDead(voice,false);ctx.r3.u64=voice;assert(!DeadCall(ctx,nullptr,0x824EECF0u,0x88960001u));
 readable=false;ctx.r3.u64=voice;assert(StaleVoice(ctx,nullptr,0x824EECF0u));readable=true;
 ctx.r3.u64=0x70001000u;assert(!StaleVoice(ctx,nullptr,0x824EEDE0u));
 advanced=false;assert(VoiceGuardOn()==legacy);
 puts("Advanced practice stale/released voice protection and legacy behavior passed.");
}
'''
        with tempfile.TemporaryDirectory(prefix='snapshot-voice-guard-') as directory:
            path = pathlib.Path(directory)
            (path / 'test.cpp').write_text(code)
            binary = path / 'test'
            subprocess.run([compiler, '-std=c++17', '-pthread', str(path / 'test.cpp'), '-o', str(binary)], check=True)
            for mode in ['advanced', 'net', 'legacy']:
                subprocess.run([str(binary), mode], check=True)


if __name__ == '__main__':
    unittest.main()
