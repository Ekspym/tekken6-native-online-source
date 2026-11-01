// Native prompt frame position, verified against practice-widget-draw.txt.
#pragma once
namespace practice_prompt_widget {
uint32_t Word(const uint8_t* p,uint32_t a){return uint32_t(p[a])<<24|uint32_t(p[a+1])<<16|uint32_t(p[a+2])<<8|p[a+3];}
unsigned Half(const uint8_t* p,uint32_t a){return unsigned(p[a])<<8|p[a+1];}
}
void MoveNativePracticePromptFrame(PPCRegister& object,PPCRegister& x,PPCRegister& y) {
 if(!practiceRageOwner.load(std::memory_order_acquire))return;
 const uint32_t a=object.u32;
 // These are the two static native prompt frames, not shared menu widgets.
 if(a!=0x827C5560u && a!=0x827C9B80u)return;
 auto* runtime=rex::Runtime::instance();auto* p=runtime?runtime->virtual_membase():nullptr;
 if(!p || practice_prompt_widget::Word(p,a)!=0x8207B3CCu ||
    practice_prompt_widget::Half(p,a+42)!=300 || practice_prompt_widget::Half(p,a+44)!=30 ||
    x.s32!=(a==0x827C5560u?90:890) || y.s32!=520)return;
 // r23 is the final draw origin. The native renderer uses it for both the
 // frame and its child drawing context, then restores the parent's context.
 // No stored position is changed, so the offset cannot accumulate each frame.
 y.s64=640;
 static std::atomic<unsigned> logged{0};const unsigned bit=a==0x827C5560u?1u:2u;
 if(!(logged.fetch_or(bit)&bit)) {
   if(auto* f=std::fopen("practice-prompt-widget-position.txt", "a")) {
     std::fprintf(f,"Native frame %08X: x=%d y=520 -> 640 (background and child context)\n",a,x.s32);
     std::fclose(f);
   }
 }
}
