// Tekken 6 Native Online launcher - Stages, characters, costumes and their game codes.

#include "characters.h"

// T6_TEST_CHAR=<kChars index>: the character of this launcher in automatic tests (instead of the stored one)
int testChar(int c)
{
	bool ok = false;
	const int t = qEnvironmentVariable("T6_TEST_CHAR").toInt(&ok);
	return ok && t >= 0 && t < 43 ? t : c;
}

// not playable online (boss AZAZEL, NANCY-M1847J): left out of the selection; the indexes stay (server, ranks)
bool charHidden(int index)
{
	return index == 39 || index == 40;
}

// Character code of the battle player setup per kChars index. Measured in practice (tools\char_codes.ps1, 2026-10-12):
// 2 * k + 1 with k the game's character id, which is NOT the kChars position - EDDY is 9 (between CHRISTIE and JIN),
// KUMA / PANDA share 12 (24 = KUMA, 25 = PANDA), AZAZEL 34, LARS 39, ALISA 40, NANCY 41 (2 * index + 1 gave JIN -> EDDY,
// EDDY -> AZAZEL, LARS -> NANCY). Even codes are the same character in the other costume.
static const int kCharCodes[] = {1, 3, 5, 7, 9, 11, 13, 15, 17, 21, 23, 24, 25, 27, 29, 31, 33, 35, 37, 39, 41, 43, 45, 47,
	49, 51, 53, 55, 57, 59, 61, 63, 65, 67, 19, 71, 73, 75, 77, 69, 83, 79, 81};
static_assert(sizeof(kCharCodes) / sizeof(kCharCodes[0]) == 43);

// costume: 0 = P1, 1 = P2. The odd codes above are the P2 costume (all matches used it before 2026-10-14), code - 1 is
// P1. KUMA / PANDA (24 / 25) share one id: one costume each.
bool hasCostumes(int index)
{
	return index != 11 && index != 12;
}

// P3 costume (user 2026-10-17): the game's third model of 15 characters = the third column of its code table 0x827AB928
// (game id * 3 + 2, 102 = none; practice: 97 = KING's third outfit). 0 = the character has none.
int p3Code(int index)
{
	switch (index >= 0 && index < 43 ? kCharCodes[index] : 0)
	{
	case 7: return 97;   // KING
	case 9: return 94;   // YOSHIMITSU
	case 15: return 100; // XIAOYU
	case 21: return 86;  // JIN
	case 27: return 98;  // BRYAN
	case 31: return 87;  // KAZUYA
	case 37: return 99;  // MARDUK
	case 43: return 88;  // ROGER Jr.
	case 45: return 89;  // ANNA
	case 51: return 90;  // ASUKA
	case 55: return 95;  // BAEK
	case 63: return 96;  // ARMOR KING
	case 65: return 91;  // LILI
	case 73: return 92;  // ZAFINA
	case 79: return 93;  // LARS
	default: return 0;
	}
}

// costume 2 = P3 (a character without one plays P2, like a launcher before 2.2.5 does with costume 2)
int charCode(int index, int costume)
{
	const int code = index >= 0 && index < 43 ? kCharCodes[index] : 33;
	if (costume == 2 && p3Code(index))
		return p3Code(index);
	return hasCostumes(index) && costume == 0 ? code - 1 : code;
}

// the two players' codes: each plays the costume he chose, also the same one in a mirror match (user 2026-10-14)
std::pair<int, int> matchCodes(int char1, int costume1, int char2, int costume2)
{
	return {charCode(char1, costume1), charCode(char2, costume2)};
}

// the characters a player can pick, alphabetical (the index stays the character's id for the server and the code)
QList<int> charsByName()
{
	QList<int> order;
	for (int i = 0; i < kCharCount; i++)
		if (!charHidden(i))
			order.push_back(i);
	std::sort(order.begin(), order.end(), [](int a, int b) { return QString(kChars[a]).compare(kChars[b], Qt::CaseInsensitive) < 0; });
	return order;
}

QString charName(int c)
{
	return c >= 0 && c < kCharCount ? kChars[c] : "?";
}
