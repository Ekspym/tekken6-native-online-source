"""Check real preference I/O and startup frame-toggle integration across processes."""
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


class PracticePreferencesTest(unittest.TestCase):
    def test_restart_and_validation(self):
        compiler = shutil.which('g++')
        if not compiler:
            self.skipTest('g++ is required for the standalone preference check')
        with tempfile.TemporaryDirectory(prefix='practice-preferences-') as temporary:
            directory = pathlib.Path(temporary)
            (directory / 'rex').mkdir()
            (directory / 'rex/runtime.h').write_text('''#pragma once
#include <filesystem>
namespace rex {
struct Runtime {
 std::filesystem::path path;
 static Runtime* instance(){static Runtime runtime;return &runtime;}
 const std::filesystem::path& user_data_root(){return path;}
};
}
''')
            code = r'''
#include <atomic>
#include <cassert>
#include <cstring>
#include <memory>
#include <mutex>
#include <cstdio>
#define REXLOG_WARN(...) ((void)0)
const char* T6Env(const char*){return nullptr;}
const char* T6PracticeEnv(const char*){return "1";}
std::atomic<int> frameDataPreference{-1};
#include "practice/tekken6_practice_preferences.inc"
'''
            source = (ROOT / 'src/t6_advanced_practice.cpp').read_text()
            code += function(source, 'bool T6FrameDataEnabled(')
            code += '\n' + function(source, 'void T6SetFrameDataEnabled(')
            code += r'''
int main(int argc,char** argv){
 assert(argc==3);rex::Runtime::instance()->path=argv[1];
 using namespace t6_practice_preferences;
 if(!std::strcmp(argv[2],"write")){
  assert(T6FrameDataEnabled(0) && T6FrameDataEnabled(1));
  T6InitializePracticeSettings({0,0,0,0,0,1,1});
  T6SavePracticeSetting(Dummy1,7);T6SavePracticeSetting(Dummy2,10);
  T6SavePracticeSetting(PlayerRage,1);T6SavePracticeSetting(OpponentRage,0);
  T6SavePracticeSetting(PositionReset,1);
  T6SetFrameDataEnabled(0,false);T6SetFrameDataEnabled(1,true);
 }else{
  // Saved choices override initial environment defaults in a brand-new process.
  assert(!T6FrameDataEnabled(0) && T6FrameDataEnabled(1));
  T6InitializePracticeSettings({0,0,0,0,0,1,0});
  assert(T6SavedPracticeSetting(Dummy1)==7 && T6SavedPracticeSetting(Dummy2)==10);
  assert(T6SavedPracticeSetting(PlayerRage)==1 && T6SavedPracticeSetting(OpponentRage)==0);
  assert(T6SavedPracticeSetting(PositionReset)==1);
 }
 auto file=std::filesystem::path(argv[1])/"validation.ini";
 {
  std::ofstream f(file);f<<"TEKKEN6_PRACTICE_SETTINGS 1\n"
   "dummy1 8\ndummy2 -1\nplayer_rage 9\nposition_reset 1\n"
   "player_frame_data 1junk\nopponent_frame_data 0 extra\nunknown 7\n";
 }
 Store invalid(file);
 assert(invalid.Get(Dummy1)==-1 && invalid.Get(Dummy2)==-1);
 assert(invalid.Get(PlayerRage)==-1 && invalid.Get(PositionReset)==1);
 assert(invalid.Get(PlayerFrameData)==-1 && invalid.Get(OpponentFrameData)==-1);
 assert(!invalid.Set(Dummy1,8) && invalid.Get(Dummy1)==-1);
 assert(invalid.Set(Dummy2,10) && invalid.Flush());
 Store reread(file);assert(reread.Get(Dummy2)==10);
 const auto stamp=std::filesystem::last_write_time(file);
 assert(reread.Flush() && std::filesystem::last_write_time(file)==stamp);
 {std::ofstream f(file);f<<"TEKKEN6_PRACTICE_SETTINGS 99\ndummy1 7\n";}
 Store future(file);assert(future.Get(Dummy1)==-1);
 puts("Practice settings restart, frame-toggle startup, validation and atomic replacement passed.");
}
'''
            (directory / 'test.cpp').write_text(code)
            binary = directory / 'test'
            subprocess.run([compiler, '-std=c++17', '-pthread', '-I', str(directory),
                            '-I', str(ROOT / 'src'), str(directory / 'test.cpp'), '-o', str(binary)], check=True)
            for mode in ['write', 'read']:
                subprocess.run([str(binary), str(directory / 'profile'), mode], check=True)


if __name__ == '__main__':
    unittest.main()
