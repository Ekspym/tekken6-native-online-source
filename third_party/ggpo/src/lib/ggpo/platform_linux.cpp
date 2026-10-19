#include "platform_linux.h"

int Platform::GetConfigInt(const char* name) {
   const char* value = std::getenv(name);
   return value ? std::atoi(value) : 0;
}

bool Platform::GetConfigBool(const char* name) {
   const char* value = std::getenv(name);
   return value && (std::atoi(value) != 0 || _stricmp(value, "true") == 0);
}
