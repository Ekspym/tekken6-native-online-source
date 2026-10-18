#pragma once
// Tekken 6 Native Online launcher - Stages, characters, costumes and their game codes.

#include "common.h"

// The 18 stages a match can start on: the game's stage id and the name (user, against screenshots of every id,
// 2026-10-13), alphabetical. Lobby stage box and the PRACTICE page.
inline const std::pair<int, const char*> kStages[] = {
	// (12 Central Tower removed, user 2026-10-15: "moc se buguje"; the server does not draw or accept it either)
	{19, "Anger of the Earth"}, {13, "Azazel's Chamber"}, {3, "Cemetery"}, {4, "City After Dark"},
	{18, "Electric Fountain"}, {9, "Fallen Colony"}, {5, "Fiesta Del Tomate"}, {11, "Gargoyle's Perch"},
	{20, "Hidden Retreat"}, {1, "High Roller's Club"}, {16, "Lightning Storm"}, {7, "Manji Valley"},
	{0, "Mystical Forest"}, {8, "Noh Theater"}, {6, "Rustic Asia"}, {2, "Temple Grounds"},
	{17, "Tunnel Disaster"}, {10, "Urban War Zone"}};
int testChar(int c);

// Characters in the order of the game's name list 0x827A5370 with KUMA and PANDA as two entries; the game takes the
// character CODE: kCharCodes below (the battle player setup sub_82245090 takes 2 * game id + 1, not 2 * index + 1)
inline const char* const kChars[] = {"PAUL", "LAW", "LEI", "KING", "YOSHIMITSU", "NINA", "HWOARANG", "XIAOYU",
	"CHRISTIE", "JIN", "JULIA", "KUMA", "PANDA", "BRYAN", "HEIHACHI", "KAZUYA", "LEE", "STEVE", "MARDUK", "MOKUJIN",
	"JACK-6", "ROGER Jr.", "ANNA", "WANG", "GANRYU", "ASUKA", "BRUCE", "BAEK", "DEVIL JIN", "RAVEN", "FENG",
	"ARMOR KING", "LILI", "DRAGUNOV", "EDDY", "BOB", "ZAFINA", "MIGUEL", "LEO", "AZAZEL", "NANCY-M1847J", "LARS",
	"ALISA"};
inline constexpr int kCharCount = 43;
bool charHidden(int index);
bool hasCostumes(int index);

// Custom looks through the game's Customize menu (2026-10-15): kept out of 2.1.x (user: "to tam ale nedavej"), in 2.2.0
// (user 2026-10-15: "zacneme pripravovat 2.2.0 a tam bude tohle") - false = no Customize look / Normal look buttons,
// no record sent or injected
inline constexpr bool kCustomLooks = true;
int p3Code(int index);
int charCode(int index, int costume);
std::pair<int, int> matchCodes(int char1, int costume1, int char2, int costume2);
QList<int> charsByName();
QString charName(int c);
