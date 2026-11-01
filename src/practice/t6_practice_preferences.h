#pragma once
#include <array>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#endif

namespace t6_practice_preferences {
enum Setting { Dummy1, Dummy2, PlayerRage, OpponentRage, PositionReset,
               PlayerFrameData, OpponentFrameData, AfterContact, Count };
inline constexpr const char* names[Count]={"dummy1","dummy2","player_rage","opponent_rage",
  "position_reset","player_frame_data","opponent_frame_data","after_contact"};
inline constexpr int maximum[Count]={7,10,1,1,1,1,1,9};
// Preferences contain choices only, never addresses or gameplay snapshots.
class Store {
 public:
  explicit Store(std::filesystem::path path):path_(std::move(path)){values_.fill(-1);Load();}
  int Get(Setting setting) const{return values_[setting];}
  bool Set(Setting setting,int value){
    if(value<0 || value>maximum[setting])return false;
    if(values_[setting]!=value){values_[setting]=value;dirty_=true;}
    return true;
  }
  bool Flush(){
    if(!dirty_)return true;
    std::error_code ec;
    if(!path_.parent_path().empty())std::filesystem::create_directories(path_.parent_path(),ec);
    if(ec)return false;
    auto temporary=path_;temporary+=".tmp";
    {
      std::ofstream file(temporary,std::ios::trunc);
      file<<"TEKKEN6_PRACTICE_SETTINGS 1\n";
      for(unsigned i=0;i<Count;++i)if(values_[i]>=0)file<<names[i]<<' '<<values_[i]<<'\n';
      file.flush();if(!file)return false;
      file.close();if(!file)return false;
    }
#ifdef _WIN32
    if(!MoveFileExW(temporary.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return false;
#else
    std::filesystem::rename(temporary,path_,ec);if(ec)return false;
#endif
    dirty_=false;return true;
  }
 private:
  void Load(){
    std::ifstream file(path_);std::string line;
    if(!std::getline(file,line))return;
    if(!line.empty() && line.back()=='\r')line.pop_back();
    if(line!="TEKKEN6_PRACTICE_SETTINGS 1")return;
    while(std::getline(file,line)){
      std::istringstream input(line);std::string key,word,extra;
      if(!(input>>key>>word) || (input>>extra))continue;
      int value=-1;auto parsed=std::from_chars(word.data(),word.data()+word.size(),value);
      if(parsed.ec!=std::errc{} || parsed.ptr!=word.data()+word.size())continue;
      for(unsigned i=0;i<Count;++i)if(key==names[i] && value>=0 && value<=maximum[i])values_[i]=value;
    }
  }
  std::filesystem::path path_;
  std::array<int,Count> values_;
  bool dirty_=false;
};
}
