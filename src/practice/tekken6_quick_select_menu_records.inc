// Native QUICK SELECT records for the experimental Apply build.
#pragma once
#include <cstdint>
#include "tekken6_quick_select_roster.inc"
#include <cstring>
#include <initializer_list>
#include <utility>

namespace quick_select_menu_detail {
constexpr uint32_t quickRow = 502;
constexpr uint32_t recordSize = 96;
constexpr uint32_t textSize = 40;
uint32_t read(const uint8_t* base, uint32_t a) {
  const uint8_t* p = base + a;
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 |
         uint32_t(p[2]) << 8 | uint32_t(p[3]);
}
void write(uint8_t* base, uint32_t a, uint32_t value) {
  for (int i = 3; i >= 0; --i) { base[a + i] = uint8_t(value); value >>= 8; }
}
}

void InstallPracticeQuickSelectRecords(uint8_t* base, uint32_t records,
                                       uint32_t texts, uint32_t& strings) {
  using namespace quick_select_menu_detail;
  auto text = [&](const char* value) {
    const uint32_t address = strings;
    const size_t size = std::strlen(value) + 1;
    std::memcpy(base + strings, value, size);
    strings += static_cast<uint32_t>(size);
    return address;
  };
  const uint32_t minusOne = text("-1");
  auto node = [&](uint32_t id, uint32_t source, const char* label,
                  uint32_t parent, uint32_t children, uint32_t first,
                  uint32_t previous, uint32_t next) {
    const uint32_t record = records + id * recordSize;
    const uint32_t entry = texts + id * textSize;
    std::memcpy(base + record, base + records + source * recordSize, recordSize);
    std::memcpy(base + entry, base + texts + source * textSize, textSize);
    std::memset(base + record + 48, 0, recordSize - 48);
    write(base, record, id);
    write(base, record + 4, parent);
    write(base, record + 8, children);
    write(base, record + 16, first);
    write(base, record + 20, previous);
    write(base, record + 24, next);
    write(base, record + 28, id * 10);
    write(base, entry, text(label));
    for (uint32_t field = 1; field <= 4; ++field)
      write(base, entry + field * 4, minusOne);
  };
  using namespace quick_select_roster;
  // Change labels/localization fallback only; native choices and handlers stay intact.
  for(auto item:{std::pair<uint32_t,const char*>{302,"OPPONENT ACTION"},{311,"GROUND TECHNIQUE"}}) {
    const uint32_t entry=texts+item.first*textSize;
    write(base,entry,text(item.second));
    for(uint32_t field=1;field<=4;++field)write(base,entry+field*4,minusOne);
  }
  node(quickRow, 297, "QUICK SELECT", 297, 4, playerRow, 299, 0);
  node(playerRow, 323, "Player", quickRow, fighterCount, playerFirst, 0, opponentRow);
  node(opponentRow, 323, "Opponent", quickRow, fighterCount, opponentFirst, playerRow, stageRow);
  node(stageRow, 323, "Stage", quickRow, stageCount, stageFirst, opponentRow, applyRow);
  node(applyRow, 346, "Apply", quickRow, 0, 0, stageRow, 0);
  for(uint32_t i=0;i<fighterCount;++i) {
    node(playerFirst+i,324,fighters[i].name,playerRow,0,0,
         i?playerFirst+i-1:0,i+1<fighterCount?playerFirst+i+1:0);
    node(opponentFirst+i,324,fighters[i].name,opponentRow,0,0,
         i?opponentFirst+i-1:0,i+1<fighterCount?opponentFirst+i+1:0);
  }
  for(uint32_t i=0;i<stageCount;++i)
    node(stageFirst+i,324,stages[i].name,stageRow,0,0,
         i?stageFirst+i-1:0,i+1<stageCount?stageFirst+i+1:0);
  write(base, records + 297 * recordSize + 8, 3);
  write(base, records + 299 * recordSize + 24, quickRow);
  // Copy the entire submenu so each native mode has its own parent links,
  // sibling links and selected values. Text strings are shared read-only.
  const uint32_t parents[] = {350, 382};
  const uint32_t tails[] = {352, 384};
  for (uint32_t mode = 0; mode < 2; ++mode) {
    const uint32_t delta = (mode + 1) * subtreeSize;
    for (uint32_t oldId = quickRow; oldId < originalTotalCount; ++oldId) {
      const uint32_t id = oldId + delta;
      const uint32_t src = records + oldId * recordSize;
      const uint32_t dst = records + id * recordSize;
      std::memcpy(base + dst, base + src, recordSize);
      std::memcpy(base + texts + id * textSize,
                  base + texts + oldId * textSize, textSize);
      write(base, dst, id);
      write(base, dst + 28, id * 10);
      for (uint32_t offset : {4u, 16u, 20u, 24u}) {
        const uint32_t value = read(base, dst + offset);
        if (value >= quickRow && value < originalTotalCount)
          write(base, dst + offset, value + delta);
      }
    }
    const uint32_t root = records + (quickRow + delta) * recordSize;
    write(base, root + 4, parents[mode]);
    write(base, root + 20, tails[mode]);
    write(base, root + 24, 0);
    write(base, records + parents[mode] * recordSize + 8, 3);
    write(base, records + tails[mode] * recordSize + 24, quickRow + delta);
  }
  // RECORD SLOTS MENU TEST V1
  const char* slotsTest=T6PracticeEnv("TEKKEN6_TEST_RECORD_SLOTS");
  if (slotsTest && std::strcmp(slotsTest,"1")==0) {
    // COMMAND MENU PROBE V1: preserve the normal slot layout unless opted in.
    // LOOP PLAYBACK TEST V1: do not add the command-list entry.
    // EIGHT RECORD SLOTS V1: obsolete command-list row is not allocated.
    // WEIGHTED PLAYBACK TEST V1
    const char* weightedTest=T6PracticeEnv("TEKKEN6_TEST_WEIGHTED_PLAYBACK");
    const bool weightedEnabled=weightedTest && std::strcmp(weightedTest,"1")==0;
    const uint32_t parent[] = {297,350,382};
    // RECORD MODE SWITCH TEST V1: retain the existing menu without the opt-in flag.
    const char* modeFlag=T6PracticeEnv("TEKKEN6_TEST_RECORD_MODE");
    const bool modeEnabled=modeFlag && std::strcmp(modeFlag,"1")==0;
    const char* label[]={"Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5", "Slot 6", "Slot 7", "Slot 8", "Exit RECORD/PLAYBACK"};
    for (uint32_t mode=0; mode<3; ++mode) {
      const uint32_t root=recordSlotBase+mode*recordSlotStride;
      const uint32_t quick=quickRow+mode*subtreeSize;
      node(root,297,"RECORD/PLAYBACK",parent[mode],recordSlotCount+1+(weightedEnabled?1:0),modeEnabled?root+recordModeOffset:root+1,quick,0);
      write(base,records+quick*recordSize+24,root);
      write(base,records+parent[mode]*recordSize+8,4);
      // RECORD MODE SWITCH TEST V1: mode choices precede the slot actions.
      if(modeEnabled) {
        const uint32_t choices=recordModeChoiceBase+mode*3;
        const char* modeLabels[]={"FREE STYLE", "VS CPU Training", "DEFENSIVE TRAINING"};
        // RECORD MODE FIX V1: fourth choice opens an independently linked QUICK SELECT.
        node(root+recordModeOffset,297,"MODE SELECT",root,4,choices,0,root+1);
        for(uint32_t option=0;option<3;++option)
          node(choices+option,346,modeLabels[option],root+recordModeOffset,0,0,
               option?choices+option-1:0,option<2?choices+option+1:recordQuickBase+mode*subtreeSize);
        const uint32_t srcRoot=quickRow+mode*subtreeSize;
        const uint32_t dstRoot=recordQuickBase+mode*subtreeSize;
        for(uint32_t i=0;i<subtreeSize;++i) {
          const uint32_t src=records+(srcRoot+i)*recordSize, dst=records+(dstRoot+i)*recordSize;
          std::memcpy(base+dst,base+src,recordSize);
          std::memcpy(base+texts+(dstRoot+i)*textSize,base+texts+(srcRoot+i)*textSize,textSize);
          write(base,dst,dstRoot+i);write(base,dst+28,(dstRoot+i)*10);
          for(uint32_t offset:{4u,16u,20u,24u}) {
            const uint32_t value=read(base,dst+offset);
            if(value>=srcRoot && value<srcRoot+subtreeSize) write(base,dst+offset,dstRoot+(value-srcRoot));
          }
        }
        write(base,records+dstRoot*recordSize+4,root+recordModeOffset);
        write(base,records+dstRoot*recordSize+20,choices+2);
        write(base,records+dstRoot*recordSize+24,0);
        for (uint32_t slot=0;slot<recordSlotCount;++slot)
          node(root+1+slot,346,label[slot],root,0,0,
               slot?root+slot:root+recordModeOffset,slot+1<recordSlotCount?root+2+slot:0);
      } else {
        for(uint32_t slot=0;slot<recordSlotCount+1;++slot)
          node(root+1+slot,346,label[slot],root,0,0,
               slot?root+slot:0,slot<recordSlotCount?root+2+slot:0);
      }
      if(weightedEnabled) {
        const uint32_t first=recordWeightBase+mode*weightModeSize;
        const uint32_t tail=root+(modeEnabled?recordSlotCount:recordModeOffset);
        const uint32_t weightsRoot=root+recordWeightsOffset;
        const char* weightLabels[]={"Slot 1 weight", "Slot 2 weight", "Slot 3 weight", "Slot 4 weight", "Slot 5 weight", "Slot 6 weight", "Slot 7 weight", "Slot 8 weight"};
        const char* values[]={"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"};
        write(base,records+tail*recordSize+24,weightsRoot);
        node(weightsRoot,297,"PLAYBACK WEIGHTS",root,recordSlotCount,first,tail,0);
        for(uint32_t slot=0;slot<recordSlotCount;++slot) {
          const uint32_t row=first+slot*weightGroupSize;
          node(row,323,weightLabels[slot],weightsRoot,weightOptionCount,row+1,
               slot?row-weightGroupSize:0,slot+1<recordSlotCount?row+weightGroupSize:0);
          write(base,records+row*recordSize+80,10); // EIGHT RECORD SLOTS V1: equal default weight 10.
          for(uint32_t v=0;v<weightOptionCount;++v)
            node(row+1+v,324,values[v],row,0,0,v?row+v:0,v+1<weightOptionCount?row+v+2:0);
        }
      }
      // RECORD MENU CONTROLS V1: five more root rows, fifteen with weights.
      if(modeEnabled) {
        const uint32_t first=recordExtrasBase+mode*recordExtrasStride;
        const uint32_t tail=weightedEnabled?root+recordWeightsOffset:root+recordSlotCount;
        write(base,records+root*recordSize+8,recordSlotCount+1+(weightedEnabled?1:0)+3);
        write(base,records+tail*recordSize+24,first);
        // RECORD RESET TOGGLE V1: native selector 332; value 0 Off, 1 ON.
        const uint32_t resetOptions=recordResetOptionsBase+mode*2;
        const uint32_t nativeResetRows[]={332,364,397};
        node(first,332,"POSITION RESET",root,2,resetOptions,tail,first+7);
        node(resetOptions,333,"OFF",first,0,0,0,resetOptions+1);
        node(resetOptions+1,334,"ON",first,0,0,resetOptions,0);
        write(base,records+first*recordSize+80,read(base,records+nativeResetRows[mode]*recordSize+80));
        node(first+1,323,"PLAYER RAGE",root,2,first+2,first,first+4);
        node(first+2,324,"OFF",first+1,0,0,0,first+3);
        node(first+3,325,"ON",first+1,0,0,first+2,0);
        node(first+4,323,"OPPONENT RAGE",root,2,first+5,first+1,first+7);
        node(first+5,324,"OFF",first+4,0,0,0,first+6);
        node(first+6,325,"ON",first+4,0,0,first+5,0);
        node(first+7,346,"CHARACTER SELECT",root,0,0,first,first+8);
        node(first+8,347,"RETURN TO MAIN MENU",root,0,0,first+7,0);
        write(base,records+(first+1)*recordSize+80,read(base,records+496*recordSize+80));
        write(base,records+(first+4)*recordSize+80,read(base,records+499*recordSize+80));
      }
    }
  }
  // Rage and Frame Data live only in Extra Settings. Keep the original Rage
  // records as canonical values for the existing gameplay threshold hook.
  write(base,records+332*recordSize+24,335);
  write(base,records+335*recordSize+20,332);
  write(base,records+296*recordSize+8,15);
  node(afterContactRow,323,"ACTION AFTER A HIT OR BLOCK",296,afterContactCount,afterContactFirst,302,311);
  write(base,records+302*recordSize+24,afterContactRow);
  write(base,records+311*recordSize+20,afterContactRow);
  const char* afterLabels[]={"STANDING","STANDING BLOCK","CROUCHING","CROUCHING BLOCK","BLOCK ALL",
    "SIDESTEP (LEFT)","SIDESTEP (RIGHT)","SIDEWALK (LEFT)","SIDEWALK (RIGHT)","BACKDASH"};
  for(uint32_t i=0;i<afterContactCount;++i)
    node(afterContactFirst+i,324,afterLabels[i],afterContactRow,0,0,i?afterContactFirst+i-1:0,i+1<afterContactCount?afterContactFirst+i+1:0);
  for(uint32_t copy=0;copy<extraSettingsCount;++copy) {
    const bool recording=copy>=3;
    const uint32_t mode=copy%3;
    const char* flag=T6PracticeEnv("TEKKEN6_TEST_RECORD_MODE");
    if(recording && (!slotsTest || std::strcmp(slotsTest,"1") ||
                     !flag || std::strcmp(flag,"1"))) continue;
    const uint32_t nativeParents[]={297,350,382};
    const uint32_t parent=recording?recordSlotBase+mode*recordSlotStride+recordModeOffset:nativeParents[mode];
    uint32_t previous=read(base,records+parent*recordSize+16);
    while(read(base,records+previous*recordSize+24))previous=read(base,records+previous*recordSize+24);
    const uint32_t root=extraSettingsBase+copy*extraSettingsStride;
    const uint32_t playerFrame=frameDataBase+copy*2*frameDataStride;
    const uint32_t opponentFrame=playerFrame+frameDataStride;
    node(root,297,"EXTRA SETTINGS",parent,6,root+1,previous,0);
    write(base,records+previous*recordSize+24,root);
    write(base,records+parent*recordSize+8,read(base,records+parent*recordSize+8)+1);
    node(root+1,323,"PLAYER RAGE",root,2,root+2,0,playerFrame);
    node(root+2,324,"OFF",root+1,0,0,0,root+3);
    node(root+3,325,"ON",root+1,0,0,root+2,0);
    node(root+4,323,"OPPONENT RAGE",root,2,root+5,playerFrame,opponentFrame);
    node(root+5,324,"OFF",root+4,0,0,0,root+6);
    node(root+6,325,"ON",root+4,0,0,root+5,0);
    node(root+7,346,"CHARACTER SELECT",root,0,0,opponentFrame,root+8);
    node(root+8,347,"RETURN TO MAIN MENU",root,0,0,root+7,0);
    for(uint32_t side=0;side<2;++side) {
      const uint32_t row=playerFrame+side*frameDataStride;
      node(row,323,side?"OPPONENT FRAME DATA":"PLAYER FRAME DATA",root,2,row+1,root+(side?4:1),root+(side?7:4));
      node(row+1,324,"OFF",row,0,0,0,row+2);
      node(row+2,325,"ON",row,0,0,row+1,0);
      write(base,records+row*recordSize+80,T6FrameDataEnabled(side)?1:0);
      write(base,records+(root+(side?4:1))*recordSize+80,read(base,records+(side?499:496)*recordSize+80));
    }
  }
}
