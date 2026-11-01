// Native combat text positions. No changes to fonts, colors or game state.
#pragma once
void AdjustNativePracticeHud(PPCRegister& group,PPCRegister& format,
 PPCRegister& a,PPCRegister& b,PPCRegister& x,PPCRegister& y,
 PPCRegister& color,PPCRegister& label,PPCRegister& caller) {
 if(!practiceRageOwner.load(std::memory_order_acquire))return;
 // Return addresses identify ONLY the native practice HUD draw calls.
 if(caller.u32==0x824636D4u && format.u32==0x8208BC3Cu) {
   if(a.u32==300u)a.u32=210u; // white damage
   return;
 }
 const bool animated=(caller.u32==0x824623ECu && format.u32==0x8208BBDCu)
  ||(caller.u32==0x8246247Cu && format.u32==0x8208BBECu)
  ||(caller.u32==0x82462634u && format.u32==0x8208BBFCu);
 if(!animated)return;
 if(label.u32==0x827C5494u && y.u32==330u)y.u32=240u;
 else if(label.u32==0x827C54B4u && y.u32==360u)y.u32=270u;
 else if(label.u32==0x827C54F4u && y.u32==430u) {
   y.u32=320u; x.s64=int64_t(x.s32)-30; // WALL HIT, below COUNTER
 }
 else if(label.u32==0x827C54D4u && y.u32==410u) {
   y.u32=300u; x.s64=int64_t(x.s32)-30; // preserve entrance/exit animation
 }
}
