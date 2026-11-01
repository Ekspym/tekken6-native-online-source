#pragma once
#include "t6_practice_preferences.h"
#include <rex/runtime.h>
namespace practice_preferences_detail {
std::mutex mutex;
std::unique_ptr<t6_practice_preferences::Store> store;
t6_practice_preferences::Store* Get(){
  if(!store){
    auto* runtime=rex::Runtime::instance();if(!runtime)return nullptr;
    auto path=runtime->user_data_root()/"practice-settings.ini";
    if(const char* explicitPath=T6Env("T6_PRACTICE_SETTINGS_FILE");explicitPath && *explicitPath)
      path=std::filesystem::u8path(explicitPath);
    store=std::make_unique<t6_practice_preferences::Store>(path);
  }
  return store.get();
}
void Flush(t6_practice_preferences::Store& preferences){
  if(!preferences.Flush())REXLOG_WARN("Could not save practice settings; current session choices remain active");
}
}
int T6SavedPracticeSetting(t6_practice_preferences::Setting setting){
  std::lock_guard<std::mutex> lock(practice_preferences_detail::mutex);
  auto* preferences=practice_preferences_detail::Get();return preferences?preferences->Get(setting):-1;
}
void T6SavePracticeSetting(t6_practice_preferences::Setting setting,unsigned value){
  std::lock_guard<std::mutex> lock(practice_preferences_detail::mutex);
  if(auto* preferences=practice_preferences_detail::Get()){
    preferences->Set(setting,static_cast<int>(value));practice_preferences_detail::Flush(*preferences);
  }
}
void T6InitializePracticeSettings(const std::array<unsigned,t6_practice_preferences::Count>& defaults){
  std::lock_guard<std::mutex> lock(practice_preferences_detail::mutex);
  if(auto* preferences=practice_preferences_detail::Get()){
    for(unsigned i=0;i<t6_practice_preferences::Count;++i){
      auto setting=static_cast<t6_practice_preferences::Setting>(i);
      if(preferences->Get(setting)<0)preferences->Set(setting,static_cast<int>(defaults[i]));
    }
    practice_preferences_detail::Flush(*preferences);
  }
}
