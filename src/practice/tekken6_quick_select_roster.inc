// Standard playable roster; choice IDs verified against 82744AA8.
#pragma once
#include <cstdint>
namespace quick_select_roster {
struct Fighter {const char* name; uint32_t id, choice; bool fixedCostume;};
constexpr Fighter fighters[]={
 {"Alisa Bosconovitch", 41, 80, false},
 {"Anna Williams", 21, 44, false},
 {"Armor King", 30, 62, false},
 {"Asuka Kazama", 24, 50, false},
 {"Baek Doo San", 26, 54, false},
 {"Bob", 34, 70, false},
 {"Bruce Irvin", 25, 52, false},
 {"Bryan Fury", 12, 26, false},
 {"Christie Monteiro", 8, 16, false},
 {"Craig Marduk", 17, 36, false},
 {"Devil Jin", 27, 56, false},
 {"Eddy Gordo", 33, 18, false},
 {"Feng Wei", 29, 60, false},
 {"Ganryu", 23, 48, false},
 {"Heihachi Mishima", 13, 28, false},
 {"Hwoarang", 6, 12, false},
 {"Jack-6", 19, 40, false},
 {"Jin Kazama", 9, 20, false},
 {"Julia Chang", 10, 22, false},
 {"Kazuya Mishima", 14, 30, false},
 {"King", 3, 6, false},
 {"Kuma", 11, 24, true},
 {"Lars Alexanderson", 40, 78, false},
 {"Lee Chaolan", 15, 32, false},
 {"Lei Wulong", 2, 4, false},
 {"Leo", 37, 76, false},
 {"Lili", 31, 64, false},
 {"Ling Xiaoyu", 7, 14, false},
 {"Marshall Law", 1, 2, false},
 {"Miguel", 36, 74, false},
 {"Mokujin", 18, 38, false},
 {"Nina Williams", 5, 10, false},
 {"Panda", 11, 25, true},
 {"Paul Phoenix", 0, 0, false},
 {"Raven", 28, 58, false},
 {"Roger Jr.", 20, 42, false},
 {"Sergei Dragunov", 32, 66, false},
 {"Steve Fox", 16, 34, false},
 {"Wang Jinrei", 22, 46, false},
 {"Yoshimitsu", 4, 8, false},
 {"Zafina", 35, 72, false},
};
struct Stage {const char* name; uint32_t id;};
constexpr Stage stages[]={
 {"Anger of the Earth", 19},
 {"Azazel's Chamber", 13},
 {"Cemetery", 3},
 {"City After Dark", 4},
 {"Electric Fountain", 18},
 {"Fallen Colony", 9},
 {"Fiesta del Tomate", 5},
 {"Gargoyle's Perch", 11},
 {"Hidden Retreat", 20},
 {"High Roller's Club", 1},
 {"Lightning Storm", 16},
 {"Manji Valley", 7},
 {"Mystical Forest", 0},
 {"Noh Theater", 8},
 {"Rustic Asia", 6},
 {"Temple Grounds", 2},
 {"Tunnel Disaster", 17},
 {"Urban War Zone", 10},
};
constexpr uint32_t fighterCount=sizeof(fighters)/sizeof(fighters[0]);
constexpr uint32_t stageCount=sizeof(stages)/sizeof(stages[0]);
constexpr uint32_t playerRow=503, opponentRow=504, stageRow=505, applyRow=506;
constexpr uint32_t playerFirst=507, opponentFirst=playerFirst+fighterCount;
constexpr uint32_t stageFirst=opponentFirst+fighterCount;
constexpr uint32_t originalTotalCount=stageFirst+stageCount;
constexpr uint32_t subtreeSize=originalTotalCount-502;
// RECORD SLOTS MENU TEST V1: 3 menus, each with 4 action records.
constexpr uint32_t recordSlotBase=originalTotalCount+2*subtreeSize;
// EIGHT RECORD SLOTS V1: root, eight slots, MODE SELECT, PLAYBACK WEIGHTS.
constexpr uint32_t recordSlotCount=8;
constexpr uint32_t recordModeOffset=recordSlotCount+1, recordWeightsOffset=recordSlotCount+2;
constexpr uint32_t recordSlotStride=recordSlotCount+3;
// WEIGHTED PLAYBACK TEST V1: three menus, three weights, eleven values each.
constexpr uint32_t recordWeightBase=recordSlotBase+3*recordSlotStride;
constexpr uint32_t weightOptionCount=11, weightGroupSize=12;
constexpr uint32_t weightModeSize=recordSlotCount*weightGroupSize;
// RECORD MODE SWITCH TEST V1: three mode-choice records per practice mode.
constexpr uint32_t recordModeChoiceBase=recordWeightBase+3*weightModeSize;
// RECORD MODE FIX V1: separate QUICK SELECT copies for the RECORD/PLAYBACK panel.
constexpr uint32_t recordQuickBase=recordModeChoiceBase+9;
// RECORD MENU CONTROLS V1: append rows without changing existing slot IDs.
constexpr uint32_t recordExtrasBase=recordQuickBase+3*subtreeSize;
constexpr uint32_t recordExtrasStride=9;
// RECORD RESET TOGGLE V1: append two options per menu without moving existing IDs.
constexpr uint32_t recordResetOptionsBase=recordExtrasBase+3*recordExtrasStride;
// FRAME DATA MENU V3: Player/Opponent selectors in Free Style and each recording root.
constexpr uint32_t frameDataBase=recordResetOptionsBase+6;
constexpr uint32_t frameDataCount=12, frameDataStride=3;
// Six independently linked Extra Settings sections: three native mode lists,
// then the Mode Select lists inside each Record/Playback copy.
constexpr uint32_t extraSettingsBase=frameDataBase+frameDataCount*frameDataStride;
constexpr uint32_t extraSettingsCount=6, extraSettingsStride=9;
constexpr uint32_t afterContactRow=extraSettingsBase+extraSettingsCount*extraSettingsStride;
constexpr uint32_t afterContactCount=10, afterContactFirst=afterContactRow+1;
constexpr uint32_t totalCount=afterContactFirst+afterContactCount;
constexpr uint32_t stringBudget=6144;
}
