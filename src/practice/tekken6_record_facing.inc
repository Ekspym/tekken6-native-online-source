// Facing-relative recording samples. Included after recording mode globals.
#pragma once
namespace record_facing_detail {
constexpr uint32_t MirrorHorizontal(uint32_t input) {
 // D-pad horizontal bits, repeated horizontal bits, and native stick-X flags.
 // Vertical directions and every attack/system button are retained.
 constexpr uint32_t lowLeft=0x00000004u, lowRight=0x00000008u;
 constexpr uint32_t repeatLeft=0x00400000u, repeatRight=0x00800000u;
 constexpr uint32_t stickNegative=0x40000000u, stickPositive=0x80000000u;
 constexpr uint32_t mask=lowLeft|lowRight|repeatLeft|repeatRight|stickNegative|stickPositive;
 return (input&~mask)|((input&lowLeft)<<1)|((input&lowRight)>>1)|
        ((input&repeatLeft)<<1)|((input&repeatRight)>>1)|
        ((input&stickNegative)<<1)|((input&stickPositive)>>1);
}
bool ReadSide(uint32_t offset,uint32_t& side) {
 if(!practiceRageOwner.load(std::memory_order_acquire) ||
    !recordPlaybackModeActive.load(std::memory_order_acquire))return false;
 auto* runtime=rex::Runtime::instance();auto* base=runtime?runtime->virtual_membase():nullptr;
 if(!base)return false;
 constexpr uint32_t opponent=0x82901840u;
 const uint32_t a=opponent+offset;
 side=uint32_t(base[a])<<24|uint32_t(base[a+1])<<16|uint32_t(base[a+2])<<8|base[a+3];
 return side<=1;
}
void LogSide(const char* event,uint32_t side,uint32_t input,uint32_t output) {
 static std::mutex mutex;
 std::lock_guard<std::mutex> lock(mutex);
 static std::FILE* file=std::fopen("record-facing-playback.txt","w");
 if(file){std::fprintf(file,"%s side=%u input=%08X output=%08X\n",event,side,input,output);std::fflush(file);}
}
}
uint32_t EncodeRecordPlaybackDirection(uint32_t input) {
 uint32_t side=0;
 // +0x288 is the live orientation copied into +0x28C by native input setup.
 if(!record_facing_detail::ReadSide(0x288u,side))return input;
 const uint32_t output=side?record_facing_detail::MirrorHorizontal(input):input;
 static std::atomic<int> previousSide{-1};
 if(previousSide.exchange(static_cast<int>(side),std::memory_order_relaxed)!=static_cast<int>(side))
   record_facing_detail::LogSide("record",side,input,output);
 return output;
}
uint32_t DecodeRecordPlaybackDirection(uint32_t input) {
 uint32_t side=0;
 // Native 0x822A4510 has already selected +0x28C before the input getter.
 // Decode here, at consumption time, so a side swap during playback is handled.
 if(!record_facing_detail::ReadSide(0x28Cu,side))return input;
 const uint32_t output=side?record_facing_detail::MirrorHorizontal(input):input;
 static std::atomic<int> previousSide{-1};
 if(previousSide.exchange(static_cast<int>(side),std::memory_order_relaxed)!=static_cast<int>(side))
   record_facing_detail::LogSide("playback",side,input,output);
 return output;
}
