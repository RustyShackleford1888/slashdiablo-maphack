#include "ItemLink.h"
#include "../../BH.h"
#include "../../Common.h"
#include "../../Constants.h"
#include "../../D2Helpers.h"
#include "../../D2Ptrs.h"
#include "../../D2Stubs.h"
#include "../../D2Version.h"
#include "../../MPQInit.h"
#include "../../Patch.h"
#include "../../MPQReader.h"
#include "../../TableReader.h"
#include "../../Drawing/Hook.h"
#include "../../D2Strings.h"
#include "../Item/ItemDisplay.h"

// Defined in Item.cpp. Builds the same wide string the inventory tooltip uses.
void __fastcall D2CLIENT_GetItemDesc(UnitAny* pItem, wchar_t* buffer);

#include <algorithm>
#include <cstring>
#include <deque>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
int gItemLinkSuppress = 0;
void* gItemLinkTrampoline = NULL;
void __cdecl ItemLink_NoteDraw(const wchar_t* str, int x, int y);
}

using std::string;
using std::wstring;
using std::vector;

// Wire format inside a chat line:
//   [Item Name] + (#base64)
// Color codes are 0xFF 'c' <char>. The game does not draw those three
// characters, so the line shows as [Item Name]. The blob is stripped before
// the line is drawn and kept locally for the hover tooltip.
// Other clients need this same maphack. Without it, the {#...} text stays visible.

static const char kAlpha[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_";
static const size_t kChatCap = 220;
static const size_t kMaxLines = 40;
static const int kIdCodes = 4;

static CRITICAL_SECTION gLock;
static bool gLockReady = false;

// Text and color are separate lists. A struct that holds both makes this
// compiler disagree about the element size (28 vs 32) and smash the heap.
struct LinkedItem {
	vector<wstring> text;
	vector<int> color;
};

struct HitRect {
	int left, top, right, bottom;
	uint32_t id;
	int color;
};

static std::unordered_map<uint32_t, LinkedItem> gItems;
static std::unordered_map<string, uint32_t> gByLabel;
static std::deque<uint32_t> gItemOrder;
struct PendingLink {
	wstring token;
	string blob;
};
static vector<PendingLink> gPending;
// Pieces of one tooltip that did not fit in the first message.
static vector<string> gRest;
struct FollowUp {
	int kind;
	BYTE opcode;
	BYTE b1;
	BYTE b2;
	wstring line;
	wstring token;
	string blob;
};
static std::deque<FollowUp> gFollow;
static bool gSendingFollow = false;
struct TipParts {
	int count;
	int have;
	DWORD when;
	wstring label;
	vector<string> piece;
	TipParts() : count(0), have(0), when(0) {}
};
static std::unordered_map<string, TipParts> gAssembly;
static BYTE* gWhisperSite = NULL;
static DWORD gWhisperReturn = 0;
static BYTE* gCommandSite = NULL;
static DWORD gCommandReturn = 0;
static vector<HitRect> gRects;
static wstring gQueuedInsert;
static int gQueueWait = 0;
static bool gOpenChat = false;
static bool gChatWasOpen = false;
static uint32_t gNextId = 1;
static int gNoteDepth = 0;

static void LockItems() {
	if (gLockReady)
		EnterCriticalSection(&gLock);
}

static void UnlockItems() {
	if (gLockReady)
		LeaveCriticalSection(&gLock);
}

static int AlphaIndex(char c) {
	const char* p = strchr(kAlpha, c);
	return p ? (int)(p - kAlpha) : -1;
}

static uint32_t TakeId() {
	uint32_t id = gNextId++ & 0x00FFFFFFu;
	if (id == 0)
		id = gNextId++ & 0x00FFFFFFu;
	return id;
}

static void Remember(uint32_t id, LinkedItem item) {
	LockItems();
	if (gItems.find(id) == gItems.end())
		gItemOrder.push_back(id);
	gItems[id] = std::move(item);
	while (gItemOrder.size() > 200) {
		uint32_t old = gItemOrder.front();
		gItemOrder.pop_front();
		gItems.erase(old);
		for (auto it = gByLabel.begin(); it != gByLabel.end(); ) {
			if (it->second == old)
				it = gByLabel.erase(it);
			else
				++it;
		}
	}
	UnlockItems();
}

static void BindLabel(const string& label, uint32_t id) {
	if (label.empty())
		return;
	LockItems();
	gByLabel[label] = id;
	UnlockItems();
}

static bool LookupLabel(const string& label, uint32_t* id) {
	LockItems();
	auto it = gByLabel.find(label);
	bool ok = it != gByLabel.end();
	if (ok)
		*id = it->second;
	UnlockItems();
	return ok;
}

static bool Lookup(uint32_t id, LinkedItem& out) {
	LockItems();
	auto it = gItems.find(id);
	bool ok = it != gItems.end();
	if (ok)
		out = it->second;
	UnlockItems();
	return ok;
}

static wstring ToWide(const string& text) {
	if (text.empty())
		return L"";
	int len = MultiByteToWideChar(CODE_PAGE, 0, text.c_str(), (int)text.size(), NULL, 0);
	if (len <= 0)
		return L"";
	wstring out(len, 0);
	MultiByteToWideChar(CODE_PAGE, 0, text.c_str(), (int)text.size(), &out[0], len);
	return out;
}

static string ToAnsi(const wstring& text) {
	if (text.empty())
		return "";
	int len = WideCharToMultiByte(CODE_PAGE, 0, text.c_str(), (int)text.size(), NULL, 0, "?", NULL);
	if (len <= 0)
		return "";
	string out(len, 0);
	WideCharToMultiByte(CODE_PAGE, 0, text.c_str(), (int)text.size(), &out[0], len, "?", NULL);
	return out;
}

static string Base64Encode(const uint8_t* data, size_t len) {
	string out;
	out.reserve((len + 2) / 3 * 4);
	for (size_t i = 0; i < len; i += 3) {
		unsigned n = data[i] << 16;
		if (i + 1 < len)
			n |= data[i + 1] << 8;
		if (i + 2 < len)
			n |= data[i + 2];
		out.push_back(kAlpha[(n >> 18) & 63]);
		out.push_back(kAlpha[(n >> 12) & 63]);
		if (i + 1 < len)
			out.push_back(kAlpha[(n >> 6) & 63]);
		if (i + 2 < len)
			out.push_back(kAlpha[n & 63]);
	}
	return out;
}

static bool Base64Decode(const string& in, vector<uint8_t>& out) {
	out.clear();
	unsigned acc = 0;
	int bits = 0;
	for (size_t i = 0; i < in.size(); i++) {
		int v = AlphaIndex(in[i]);
		if (v < 0)
			return false;
		acc = (acc << 6) | (unsigned)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back((uint8_t)((acc >> bits) & 0xFF));
		}
	}
	return !out.empty();
}

struct Reader {
	const uint8_t* p;
	size_t n;
	size_t i;
	bool ok;
	Reader(const vector<uint8_t>& b) : p(b.data()), n(b.size()), i(0), ok(true) {}
	uint8_t u8() {
		if (i >= n) { ok = false; return 0; }
		return p[i++];
	}
	uint16_t u16() {
		uint16_t v = u8();
		v |= (uint16_t)u8() << 8;
		return v;
	}
	int32_t i32() {
		uint32_t v = u8();
		v |= (uint32_t)u8() << 8;
		v |= (uint32_t)u8() << 16;
		v |= (uint32_t)u8() << 24;
		return (int32_t)v;
	}
	string str() {
		uint8_t len = u8();
		if (i + len > n) { ok = false; return ""; }
		string s(reinterpret_cast<const char*>(p + i), reinterpret_cast<const char*>(p + i + len));
		i += len;
		return s;
	}
};

struct Writer {
	vector<uint8_t> b;
	void u8(uint8_t v) { b.push_back(v); }
	void u16(uint16_t v) { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); }
	void i32(int32_t v) {
		uint32_t u = (uint32_t)v;
		b.push_back((uint8_t)u);
		b.push_back((uint8_t)(u >> 8));
		b.push_back((uint8_t)(u >> 16));
		b.push_back((uint8_t)(u >> 24));
	}
	void str(const string& s) {
		size_t n = s.size() > 60 ? 60 : s.size();
		b.push_back((uint8_t)n);
		b.insert(b.end(), s.begin(), s.begin() + n);
	}
};

static const ItemStatCostTxt* StatRow(int id) {
	if (id < 0 || !p_D2COMMON_sgptDataTable || !*p_D2COMMON_sgptDataTable)
		return NULL;
	sgptDataTable* table = *p_D2COMMON_sgptDataTable;
	if (!table->pItemStatCostTxt || (DWORD)id >= table->dwItemStatCostRecs)
		return NULL;
	return &table->pItemStatCostTxt[id];
}

static wstring Locale(WORD id) {
	if (id == 0 || id == 0xFFFF)
		return L"";
	wchar_t* text = D2LANG_GetLocaleText(id);
	if (!text || !text[0])
		return L"";
	return text;
}

static wstring SkillName(int id) {
	if (!Tables::isInitialized() || id < 0)
		return L"Unknown Skill";
	int n = Tables::Skills.size();
	for (int i = 0; i < n; i++) {
		JSONObject* row = Tables::Skills.entryAt(i);
		if (row && (int)row->getNumber("Id") == id) {
			string name = row->getString("skill");
			if (!name.empty())
				return ToWide(name);
		}
	}
	return L"Unknown Skill";
}

static const char* SkillTabName(int id) {
	switch (id) {
	case 0: return "Bow and Crossbow Skills";
	case 1: return "Passive and Magic Skills";
	case 2: return "Javelin and Spear Skills";
	case 8: return "Fire Skills";
	case 9: return "Lightning Skills";
	case 10: return "Cold Skills";
	case 16: return "Curses";
	case 17: return "Poison and Bone Skills";
	case 18: return "Summoning Skills";
	case 24: return "Combat Skills";
	case 25: return "Offensive Auras";
	case 26: return "Defensive Auras";
	case 32: return "Combat Skills";
	case 33: return "Masteries";
	case 34: return "Warcries";
	case 40: return "Summoning Skills";
	case 41: return "Shape Shifting Skills";
	case 42: return "Elemental Skills";
	case 48: return "Traps";
	case 49: return "Shadow Disciplines";
	case 50: return "Martial Arts";
	default: return NULL;
	}
}

static const char* ClassName(int id) {
	switch (id) {
	case 0: return "Amazon";
	case 1: return "Sorceress";
	case 2: return "Necromancer";
	case 3: return "Paladin";
	case 4: return "Barbarian";
	case 5: return "Druid";
	case 6: return "Assassin";
	default: return NULL;
	}
}

static wstring NumText(int value, bool plus, bool percent, bool eighths) {
	int shown = value;
	int frac = -1;
	if (eighths) {
		int av = value < 0 ? -value : value;
		shown = value / 8;
		int rem = av % 8;
		frac = rem * 100 / 8;
	}
	wchar_t buf[64];
	if (eighths && frac > 0) {
		if (frac % 10 == 0)
			frac /= 10;
		if (plus && value >= 0)
			swprintf_s(buf, frac < 10 ? L"+%d.%d" : L"+%d.%02d", shown, frac);
		else
			swprintf_s(buf, frac < 10 ? L"%d.%d" : L"%d.%02d", shown, frac);
	} else if (plus && value >= 0) {
		swprintf_s(buf, percent ? L"+%d%%" : L"+%d", shown);
	} else {
		swprintf_s(buf, percent ? L"%d%%" : L"%d", shown);
	}
	if (eighths && frac > 0 && percent)
		wcscat_s(buf, L"%");
	return buf;
}

static wstring JoinVal(int descval, const wstring& num, const wstring& text) {
	if (text.empty())
		return num;
	if (descval == 2) {
		if (text.back() != L' ')
			return text + L" " + num;
		return text + num;
	}
	return num + L" " + text;
}

struct RawStat {
	uint16_t id;
	uint16_t layer;
	int32_t value;
};

static wstring FormatStat(const RawStat& stat) {
	const ItemStatCostTxt* row = StatRow(stat.id);
	if (!row || row->bDescFunc == 0 || row->bDescFunc == 19)
		return L"";

	int value = stat.value;
	// Per-level stats are stored in 256ths. Flat life and mana on an item are not.
	if (row->bOp == 13)
		value /= 256;
	if (value == 0)
		return L"";

	bool eighths = row->bDescFunc >= 6 && row->bDescFunc <= 9;
	wstring text = Locale(value < 0 ? row->wDescStrNeg : row->wDescStrPos);
	if (text.empty())
		text = Locale(row->wDescStr2);

	if (stat.id == STAT_CLASSSKILLS) {
		const char* cls = ClassName(stat.layer);
		wstring name = cls ? ToWide(string(cls) + " Skill Levels") : L"Skill Levels";
		return NumText(value, true, false, false) + L" to " + name;
	}
	if (row->bDescFunc == 13) {
		return NumText(value, true, false, false) + L" to " + SkillName(stat.layer);
	}
	if (row->bDescFunc == 14) {
		const char* tab = SkillTabName(stat.layer);
		wstring name = tab ? ToWide(tab) : L"Skill Tab";
		return NumText(value, true, false, false) + L" to " + name;
	}
	if (row->bDescFunc == 15) {
		int level = stat.layer & 0x3F;
		int skill = stat.layer >> 6;
		int cur = stat.value & 0xFF;
		int mx = (stat.value >> 8) & 0xFF;
		wchar_t buf[64];
		swprintf_s(buf, L"Level %d ", level);
		wstring line = buf + SkillName(skill);
		swprintf_s(buf, L" (%d/%d Charges)", cur, mx);
		line += buf;
		return line;
	}
	if (row->bDescFunc == 16 || stat.id == STAT_SKILLONHIT || stat.id == STAT_SKILLONKILL ||
		stat.id == STAT_SKILLONDEATH || stat.id == STAT_SKILLONSTRIKING || stat.id == STAT_SKILLWHENSTRUCK ||
		stat.id == STAT_SKILLONLEVELUP) {
		int level = stat.layer & 0x3F;
		int skill = stat.layer >> 6;
		int chance = stat.value & 0xFF;
		wchar_t buf[32];
		swprintf_s(buf, L"%d%% chance to cast level %d ", chance, level);
		wstring line = buf + SkillName(skill);
		if (!text.empty())
			line += L" " + text;
		return line;
	}
	if (row->bDescFunc == 11) {
		int seconds = value ? 100 / value : 0;
		wchar_t buf[96];
		swprintf_s(buf, L"Repairs 1 durability in %d seconds", seconds);
		return buf;
	}

	bool plus = row->bDescFunc == 1 || row->bDescFunc == 4 || row->bDescFunc == 6 || row->bDescFunc == 8 || row->bDescFunc == 12;
	bool percent = row->bDescFunc == 2 || row->bDescFunc == 4 || row->bDescFunc == 7 || row->bDescFunc == 8;
	if (text.empty()) {
		if ((int)stat.id < (int)AllStatList.size() && AllStatList[stat.id])
			text = ToWide(AllStatList[stat.id]->name);
		else
			text = L"bonus";
	}
	return JoinVal(row->bDescVal, NumText(value, plus, percent, eighths), text);
}

static int QualityColor(uint8_t quality) {
	int color = ItemColorFromQuality(quality);
	if (color < White)
		color = White;
	if (color > Yellow)
		color = White;
	return color;
}

static string CleanName(const string& raw) {
	string out;
	for (size_t i = 0; i < raw.size();) {
		unsigned char c = (unsigned char)raw[i];
		if (c == 0xFF && i + 2 < raw.size() && raw[i + 1] == 'c') {
			i += 3;
			continue;
		}
		if (c == '\n' || c == '\r')
			break;
		if (c == '[' || c == ']' || c == '{' || c == '}' || c == '#') {
			i++;
			continue;
		}
		if (c < 32) {
			i++;
			continue;
		}
		out.push_back((char)c);
		if (out.size() >= 48)
			break;
		i++;
	}
	while (!out.empty() && out.back() == ' ')
		out.pop_back();
	size_t start = 0;
	while (start < out.size() && out[start] == ' ')
		start++;
	if (start)
		out.erase(0, start);
	return out;
}

static int CallGetItemName(UnitAny* item, wchar_t* buffer, int cap) {
	__try {
		buffer[0] = 0;
		D2CLIENT_GetItemName(item, buffer, cap);
		buffer[cap - 1] = 0;
		return 1;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		buffer[0] = 0;
		return 0;
	}
}

static string BaseLabel(UnitAny* item);
static bool ReadableRange(const void* ptr, size_t bytes);

static string ModName(AutoMagicTxt* row) {
	if (!row || !ReadableRange(row, 32) || !row->szName[0])
		return "";
	return CleanName(row->szName);
}

static wchar_t* ProbeLocale(WORD id);

static string LocaleName(DWORD id) {
	if (id == 0 || id > 0xFFFF)
		return "";
	wchar_t* text = ProbeLocale((WORD)id);
	if (!text || text == (wchar_t*)-1 || !text[0])
		return "";
	char* ansi = UnicodeToAnsi(text);
	string name = ansi ? CleanName(ansi) : "";
	delete[] ansi;
	// String 737 is "/unignore <accountname>". A unique row can point at a
	// help line. That is not an item name.
	if (!name.empty() && name[0] == '/')
		return "";
	return name;
}

// UniqueItems "index" is the txt key. The tooltip uses the string table.
static string TableLocaleName(UnitAny* item) {
	sgptDataTable* table = p_D2COMMON_sgptDataTable ? *p_D2COMMON_sgptDataTable : NULL;
	if (!table || !item || !item->pItemData)
		return "";
	DWORD quality = item->pItemData->dwQuality;
	DWORD index = item->pItemData->dwFileIndex;
	if (quality == ITEM_QUALITY_UNIQUE) {
		if (!table->pUniqueItemsTxt || index >= table->dwUniqItemsRecs)
			return "";
		return LocaleName(table->pUniqueItemsTxt[index]._1);
	}
	if (quality == ITEM_QUALITY_SET) {
		if (!table->pSetItemsTxt || index >= table->dwSetItemsRecs)
			return "";
		return LocaleName(table->pSetItemsTxt[index].dwTblIndex);
	}
	return "";
}

static string SpecialName(UnitAny* item) {
	if (!item || !item->pItemData)
		return "";
	if (!(item->pItemData->dwFlags & ITEM_IDENTIFIED))
		return "";
	if (item->pItemData->dwFlags & ITEM_RUNEWORD) {
		string runeword = LocaleName(item->pItemData->wPrefix[0]);
		if (!runeword.empty())
			return runeword;
	}
	DWORD quality = item->pItemData->dwQuality;
	if (quality == ITEM_QUALITY_UNIQUE || quality == ITEM_QUALITY_SET)
		return TableLocaleName(item);
	if (!Tables::isInitialized())
		return "";
	if (quality == ITEM_QUALITY_RARE || quality == ITEM_QUALITY_CRAFT) {
		JSONObject* pre = Tables::RarePrefix.entryAt((int)item->pItemData->wRarePrefix - 156);
		JSONObject* suf = Tables::RareSuffix.entryAt((int)item->pItemData->wRareSuffix - 1);
		string name = pre ? pre->getString("name") : "";
		string suffix = suf ? suf->getString("name") : "";
		if (!suffix.empty()) {
			if (!name.empty())
				name += " ";
			name += suffix;
		}
		return CleanName(name);
	}
	if (quality == ITEM_QUALITY_MAGIC) {
		string prefix;
		string suffix;
		for (int i = 0; i < 3; i++) {
			if (item->pItemData->wPrefix[i]) {
				string part = ModName(D2COMMON_GetItemMagicalMods(item->pItemData->wPrefix[i]));
				if (!part.empty()) {
					if (!prefix.empty())
						prefix += " ";
					prefix += part;
				}
			}
			if (item->pItemData->wSuffix[i]) {
				string part = ModName(D2COMMON_GetItemMagicalMods(item->pItemData->wSuffix[i]));
				if (!part.empty()) {
					if (!suffix.empty())
						suffix += " ";
					suffix += part;
				}
			}
		}
		string name = prefix;
		string base = BaseLabel(item);
		if (!base.empty()) {
			if (!name.empty())
				name += " ";
			name += base;
		}
		if (!suffix.empty()) {
			if (!name.empty())
				name += " ";
			name += suffix;
		}
		return CleanName(name);
	}
	return "";
}

// "o T6 Balanced Knife" is the type line. The bracket wants "Enchanted Icicle".
static bool IsTypeLine(const string& line, const string& base) {
	if (base.empty() || line.empty())
		return false;
	if (line == base)
		return true;
	size_t pos = line.find(base);
	if (pos == string::npos)
		return false;
	string extra = line.substr(0, pos) + line.substr(pos + base.size());
	size_t a = 0;
	while (a < extra.size() && extra[a] == ' ')
		a++;
	size_t b = extra.size();
	while (b > a && extra[b - 1] == ' ')
		b--;
	return b - a <= 6;
}

static bool IsCountLabel(const string& name) {
	size_t i = 0;
	while (i < name.size() && name[i] == ' ')
		i++;
	string head;
	for (; i < name.size() && head.size() < 9; i++) {
		char c = name[i];
		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
		head.push_back(c);
	}
	return head.compare(0, 6, "count:") == 0 || head.compare(0, 9, "quantity:") == 0;
}

static string MiscCodeName(UnitAny* item);

static string ItemLabel(UnitAny* item) {
	wchar_t buffer[256];
	buffer[0] = 0;
	if (item && D2COMMON_GetItemText(item->dwTxtFileNo))
		CallGetItemName(item, buffer, 256);
	char* ansi = buffer[0] ? UnicodeToAnsi(buffer) : NULL;
	string raw = ansi ? ansi : "";
	delete[] ansi;
	string base = BaseLabel(item);
	string first;
	string named;
	string cur;
	bool sawCount = false;
	for (size_t i = 0; i <= raw.size(); i++) {
		if (i == raw.size() || raw[i] == '\n' || raw[i] == '\r') {
			string line = CleanName(cur);
			cur.clear();
			if (IsCountLabel(line)) {
				sawCount = true;
				if (i < raw.size() && raw[i] == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n')
					i++;
				continue;
			}
			if (!line.empty() && first.empty())
				first = line;
			if (!line.empty() && !IsTypeLine(line, base)) {
				named = line;
				break;
			}
			if (i < raw.size() && raw[i] == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n')
				i++;
			continue;
		}
		cur.push_back(raw[i]);
	}
	string label;
	if (!named.empty())
		label = named;
	// Code uts is named Ward, and the unique is also Ward. The line above
	// treats that as the item type and drops it. It is still the right name.
	// The unique-row backup below would replace it with an unrelated string.
	else if (!first.empty() && first == base)
		label = first;
	else {
		string special = SpecialName(item);
		if (!special.empty() && !IsTypeLine(special, base))
			label = special;
		else if (!first.empty())
			label = first;
		else if (!base.empty())
			label = base;
		else
			label = "Item";
	}
	// A stack's name line is "Count: 6". The real name is the string keyed by
	// the misc.txt code, such as %c4 -> "Diffuser's Cube".
	if (label.empty() || label == "Item" || IsCountLabel(label) || (sawCount && named.empty())) {
		string misc = MiscCodeName(item);
		if (!misc.empty())
			return misc;
	}
	return label;
}

static string BaseLabel(UnitAny* item) {
	ItemText* txt = D2COMMON_GetItemText(item->dwTxtFileNo);
	if (!txt)
		return "";
	wchar_t* base = D2LANG_GetLocaleText(txt->nLocaleTxtNo);
	if (!base)
		return "";
	char* ansi = UnicodeToAnsi(base);
	string label = ansi ? CleanName(ansi) : "";
	delete[] ansi;
	return label;
}

static bool ReadableRange(const void* ptr, size_t bytes) {
	if (!ptr || bytes == 0)
		return false;
	MEMORY_BASIC_INFORMATION mbi;
	if (!VirtualQuery(ptr, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
		return false;
	if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
		return false;
	uintptr_t begin = (uintptr_t)ptr;
	uintptr_t end = begin + bytes;
	uintptr_t regionEnd = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
	return end >= begin && end <= regionEnd;
}

static void ReadStatNode(StatList* list, vector<RawStat>& stats) {
	if (!ReadableRange(list, sizeof(StatList)))
		return;
	WORD count = list->wStatCount1;
	if (!list->pStat || count == 0 || count > 64)
		return;
	if (!ReadableRange(list->pStat, sizeof(Stat) * count))
		return;
	for (WORD i = 0; i < count && stats.size() < 40; i++) {
		int id = list->pStat[i].wStatIndex;
		if (IsHiddenDisplayStat(id))
			continue;
		if (id == STAT_SOCKETS || id == STAT_DURABILITY || id == STAT_MAXDURABILITY || id == STAT_VALUE)
			continue;
		// Shown on its own as the real defense, not as "+602 Defense".
		if (id == STAT_DEFENSE)
			continue;
		const ItemStatCostTxt* row = StatRow(id);
		if (!row || row->bDescFunc == 0)
			continue;
		bool seen = false;
		for (size_t n = 0; n < stats.size(); n++) {
			if (stats[n].id == list->pStat[i].wStatIndex && stats[n].layer == list->pStat[i].wSubIndex)
				seen = true;
		}
		if (seen)
			continue;
		RawStat s;
		s.id = list->pStat[i].wStatIndex;
		s.layer = list->pStat[i].wSubIndex;
		s.value = (int32_t)list->pStat[i].dwStatValue;
		stats.push_back(s);
	}
}

static StatList* CallGetStatList(UnitAny* item) {
	__try {
		return D2COMMON_GetStatList(item, 0, 0x40);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return NULL;
	}
}

// The base node only has things like the armor's rolled defense. Affixes live on
// the magic list (flag 0x40) and the nodes linked from it.
static void CollectStats(UnitAny* item, vector<RawStat>& stats) {
	if (!item || !ReadableRange(item, 0x60))
		return;
	ReadStatNode(item->pStats, stats);
	StatList* node = CallGetStatList(item);
	for (int guard = 0; node && guard < 8; guard++) {
		if (!ReadableRange(node, 0x50))
			break;
		if (node != item->pStats)
			ReadStatNode(node, stats);
		StatList* next = node->pPrevLink;
		if (!next || next == node || next == item->pStats)
			break;
		node = next;
	}
}

static void WriteStats(Writer& w, const vector<RawStat>& stats) {
	w.u8((uint8_t)stats.size());
	for (size_t i = 0; i < stats.size(); i++) {
		w.u16(stats[i].id);
		w.u16(stats[i].layer);
		w.i32(stats[i].value);
	}
}

static void ReadStats(Reader& r, vector<RawStat>& stats) {
	uint8_t n = r.u8();
	if (n > 40) {
		r.ok = false;
		return;
	}
	for (uint8_t i = 0; i < n; i++) {
		RawStat s;
		s.id = r.u16();
		s.layer = r.u16();
		s.value = r.i32();
		stats.push_back(s);
	}
}

struct PackedItem {
	uint8_t quality;
	uint8_t flags;
	uint8_t sockets;
	uint16_t ilvl;
	uint16_t dur;
	uint16_t maxDur;
	uint16_t defense;
	uint16_t dmgMin;
	uint16_t dmgMax;
	uint16_t dmgMin2;
	uint16_t dmgMax2;
	uint16_t reqStr;
	uint16_t reqDex;
	uint16_t reqLvl;
	string name;
	string base;
	string runeword;
	vector<RawStat> stats;
	vector<PackedItem> socketed;
};

enum {
	kFlagEth = 1,
	kFlagUnid = 2,
	kFlagRune = 4,
	kFlagCut = 8
};

static PackedItem PackUnit(UnitAny* item, bool withSockets) {
	PackedItem p;
	memset(&p.quality, 0, sizeof(uint8_t) * 3);
	p.ilvl = 0;
	p.dur = 0;
	p.maxDur = 0;
	p.defense = 0;
	p.dmgMin = 0;
	p.dmgMax = 0;
	p.dmgMin2 = 0;
	p.dmgMax2 = 0;
	p.reqStr = 0;
	p.reqDex = 0;
	p.reqLvl = 0;
	if (!item || !item->pItemData)
		return p;
	p.quality = (uint8_t)item->pItemData->dwQuality;
	p.ilvl = (uint16_t)item->pItemData->dwItemLevel;
	if (item->pItemData->dwFlags & ITEM_ETHEREAL)
		p.flags |= kFlagEth;
	if (!(item->pItemData->dwFlags & ITEM_IDENTIFIED))
		p.flags |= kFlagUnid;
	if (item->pItemData->dwFlags & ITEM_RUNEWORD)
		p.flags |= kFlagRune;
	p.sockets = (uint8_t)D2COMMON_GetUnitStat(item, STAT_SOCKETS, 0);
	p.dur = (uint16_t)D2COMMON_GetUnitStat(item, STAT_DURABILITY, 0);
	p.maxDur = (uint16_t)D2COMMON_GetUnitStat(item, STAT_MAXDURABILITY, 0);
	p.name = ItemLabel(item);
	p.base = BaseLabel(item);
	bool eth = (p.flags & kFlagEth) != 0;
	if (D2COMMON_IsMatchingType(item, ITEM_TYPE_ALLARMOR)) {
		int defense = (int)D2COMMON_GetUnitStat(item, STAT_DEFENSE, 0);
		if (defense > 0 && defense < 65535)
			p.defense = (uint16_t)defense;
		ItemsTxt* armor = NULL;
		ItemText* txt = D2COMMON_GetItemText(item->dwTxtFileNo);
		if (txt && p_D2COMMON_ArmorTxt && *p_D2COMMON_ArmorTxt && p_D2COMMON_ArmorTxtRecords) {
			int records = (int)*p_D2COMMON_ArmorTxtRecords;
			for (int i = 0; i < records; i++) {
				ItemsTxt* row = &(*p_D2COMMON_ArmorTxt)[i];
				if (strcmp(row->szcode, txt->szCode) == 0) {
					armor = row;
					break;
				}
			}
		}
		if (armor) {
			int str = armor->wreqstr;
			int dex = armor->wreqdex;
			if (eth) {
				str += 10;
				dex += 10;
			}
			int cut = (int)D2COMMON_GetUnitStat(item, STAT_REDUCEDREQUIREMENTS, 0);
			if (cut > 0) {
				if (cut > 100)
					cut = 100;
				str = str * (100 - cut) / 100;
				dex = dex * (100 - cut) / 100;
			}
			if (str > 0 && str < 65535)
				p.reqStr = (uint16_t)str;
			if (dex > 0 && dex < 65535)
				p.reqDex = (uint16_t)dex;
		}
	}
	int dmgMin = (int)D2COMMON_GetUnitStat(item, STAT_MINIMUMDAMAGE, 0);
	int dmgMax = (int)D2COMMON_GetUnitStat(item, STAT_MAXIMUMDAMAGE, 0);
	int dmgMin2 = (int)D2COMMON_GetUnitStat(item, STAT_SECONDARYMINIMUMDAMAGE, 0);
	int dmgMax2 = (int)D2COMMON_GetUnitStat(item, STAT_SECONDARYMAXIMUMDAMAGE, 0);
	if (dmgMax > 0 && dmgMax < 65535) {
		p.dmgMin = (uint16_t)(dmgMin > 0 ? dmgMin : 0);
		p.dmgMax = (uint16_t)dmgMax;
	}
	if (dmgMax2 > 0 && dmgMax2 < 65535 && (dmgMin2 != dmgMin || dmgMax2 != dmgMax)) {
		p.dmgMin2 = (uint16_t)(dmgMin2 > 0 ? dmgMin2 : 0);
		p.dmgMax2 = (uint16_t)dmgMax2;
	}
	UnitAny* player = D2CLIENT_GetPlayerUnit();
	if (player) {
		int req = (int)D2COMMON_GetItemLevelRequirement(item, player);
		if (req > 0 && req < 256)
			p.reqLvl = (uint16_t)req;
	}
	if (p.flags & kFlagRune) {
		wchar_t* rw = D2LANG_GetLocaleText(item->pItemData->wPrefix[0]);
		if (rw) {
			char* ansi = UnicodeToAnsi(rw);
			p.runeword = ansi ? CleanName(ansi) : "";
			delete[] ansi;
		}
	}
	if (!(p.flags & kFlagUnid))
		CollectStats(item, p.stats);
	if (withSockets && p.sockets && item->pInventory && ReadableRange(item->pInventory, 0x14)) {
		for (UnitAny* s = item->pInventory->pFirstItem; s && p.socketed.size() < 6; ) {
			if (!ReadableRange(s, 0x20) || s->dwType != UNIT_ITEM || !s->pItemData)
				break;
			UnitAny* next = s->pItemData->pNextInvItem;
			p.socketed.push_back(PackUnit(s, false));
			if (next == s)
				break;
			s = next;
		}
		// Runeword mods live on the item. The socketed runes are just the names.
		if (p.flags & kFlagRune) {
			for (size_t i = 0; i < p.socketed.size(); i++)
				p.socketed[i].stats.clear();
		}
	}
	return p;
}

static void WritePacked(Writer& w, const PackedItem& p, bool nested) {
	w.u8(p.quality);
	w.u8(p.flags);
	w.u8(p.sockets);
	w.u16(p.ilvl);
	w.u16(p.dur);
	w.u16(p.maxDur);
	w.u16(p.defense);
	w.u16(p.dmgMin);
	w.u16(p.dmgMax);
	w.u16(p.dmgMin2);
	w.u16(p.dmgMax2);
	w.u16(p.reqStr);
	w.u16(p.reqDex);
	w.u16(p.reqLvl);
	w.str(p.name);
	w.str(p.base);
	w.str(p.runeword);
	WriteStats(w, p.stats);
	if (nested)
		return;
	w.u8((uint8_t)p.socketed.size());
	for (size_t i = 0; i < p.socketed.size(); i++)
		WritePacked(w, p.socketed[i], true);
}

static PackedItem ReadPacked(Reader& r, bool nested) {
	PackedItem p;
	p.quality = r.u8();
	p.flags = r.u8();
	p.sockets = r.u8();
	p.ilvl = r.u16();
	p.dur = r.u16();
	p.maxDur = r.u16();
	p.defense = r.u16();
	p.dmgMin = r.u16();
	p.dmgMax = r.u16();
	p.dmgMin2 = r.u16();
	p.dmgMax2 = r.u16();
	p.reqStr = r.u16();
	p.reqDex = r.u16();
	p.reqLvl = r.u16();
	p.name = r.str();
	p.base = r.str();
	p.runeword = r.str();
	ReadStats(r, p.stats);
	if (!nested) {
		uint8_t n = r.u8();
		if (n > 6) {
			r.ok = false;
			return p;
		}
		for (uint8_t i = 0; i < n; i++)
			p.socketed.push_back(ReadPacked(r, true));
	}
	return p;
}

static void AddLine(LinkedItem& item, const wstring& text, int color) {
	item.text.push_back(text);
	item.color.push_back(color);
}

static void AddStats(LinkedItem& item, const vector<RawStat>& stats, const wchar_t* prefix) {
	vector<int> priority;
	vector<wstring> text;
	for (size_t i = 0; i < stats.size(); i++) {
		wstring line = FormatStat(stats[i]);
		if (line.empty())
			continue;
		if (prefix)
			line = prefix + line;
		const ItemStatCostTxt* row = StatRow(stats[i].id);
		priority.push_back(row ? (int)row->wDescPriority : 0);
		text.push_back(line);
	}
	vector<size_t> order(text.size());
	for (size_t i = 0; i < order.size(); i++)
		order[i] = i;
	std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
		return priority[a] > priority[b];
	});
	for (size_t i = 0; i < order.size(); i++)
		AddLine(item, text[order[i]], Blue);
}

static wstring MpQLabel(WORD id, const wchar_t* fallback) {
	wchar_t* text = D2LANG_GetLocaleText(id);
	wstring label = (text && text[0]) ? wstring(text) : wstring(fallback);
	while (!label.empty() && label.back() == L' ')
		label.pop_back();
	return label;
}

static LinkedItem BuildTip(const PackedItem& p) {
	LinkedItem item;
	int color = QualityColor(p.quality);
	if (!p.runeword.empty())
		AddLine(item, ToWide(p.runeword), Gold);
	AddLine(item, ToWide(p.name.empty() ? "Item" : p.name), color);
	if (!p.base.empty() && p.base != p.name)
		AddLine(item, ToWide(p.base), color);
	if (p.flags & kFlagEth)
		AddLine(item, L"Ethereal", Blue);
	if (p.flags & kFlagUnid)
		AddLine(item, MpQLabel(D2STR_ITEMSTATS1B, L"Unidentified"), Red);
	wchar_t buf[128];
	if (p.defense > 0) {
		swprintf_s(buf, L"%s %u", MpQLabel(D2STR_ITEMSTATS1H, L"Defense:").c_str(), (unsigned)p.defense);
		AddLine(item, buf, White);
	}
	if (p.dmgMax > 0) {
		swprintf_s(buf, L"%s %u %s %u",
			MpQLabel(D2STR_ITEMSTATS1G, L"Damage:").c_str(),
			(unsigned)p.dmgMin,
			MpQLabel(D2STR_ITEMSTAST1K, L"to").c_str(),
			(unsigned)p.dmgMax);
		AddLine(item, buf, White);
	}
	if (p.dmgMax2 > 0) {
		swprintf_s(buf, L"%s %u %s %u",
			MpQLabel(D2STR_ITEMSTATS1M, L"Two-Hand Damage:").c_str(),
			(unsigned)p.dmgMin2,
			MpQLabel(D2STR_ITEMSTAST1K, L"to").c_str(),
			(unsigned)p.dmgMax2);
		AddLine(item, buf, White);
	}
	if (p.maxDur > 0) {
		swprintf_s(buf, L"%s %u %s %u",
			MpQLabel(D2STR_ITEMSTATS1D, L"Durability:").c_str(),
			(unsigned)p.dur,
			MpQLabel(D2STR_ITEMSTATS1J, L"of").c_str(),
			(unsigned)p.maxDur);
		AddLine(item, buf, White);
	}
	if (p.reqStr > 0) {
		swprintf_s(buf, L"%s %u", MpQLabel(D2STR_ITEMSTATS1E, L"Required Strength:").c_str(), (unsigned)p.reqStr);
		AddLine(item, buf, White);
	}
	if (p.reqDex > 0) {
		swprintf_s(buf, L"%s %u", MpQLabel(D2STR_ITEMSTATS1F, L"Required Dexterity:").c_str(), (unsigned)p.reqDex);
		AddLine(item, buf, White);
	}
	if (p.reqLvl > 0) {
		swprintf_s(buf, L"%s %u", MpQLabel(D2STR_ITEMSTATS1P, L"Required Level:").c_str(), (unsigned)p.reqLvl);
		AddLine(item, buf, White);
	}
	if (p.ilvl > 0) {
		swprintf_s(buf, L"Item Level: %u", (unsigned)p.ilvl);
		AddLine(item, buf, Grey);
	}

	AddStats(item, p.stats, NULL);

	if (p.sockets > 0) {
		swprintf_s(buf, L"%s (%u)", MpQLabel(D2STR_SOCKETABLE, L"Socketed").c_str(), (unsigned)p.sockets);
		AddLine(item, buf, Grey);
	}
	for (size_t i = 0; i < p.socketed.size(); i++) {
		const PackedItem& s = p.socketed[i];
		int sc = s.name.find("Rune") != string::npos ? Gold : QualityColor(s.quality);
		AddLine(item, L"  " + ToWide(s.name.empty() ? "Gem" : s.name), sc);
		AddStats(item, s.stats, L"    ");
	}
	if (p.flags & kFlagCut)
		AddLine(item, L"Some socketed items were omitted.", Grey);
	return item;
}

static string EncodeUnit(UnitAny* item) {
	PackedItem full = PackUnit(item, true);
	Writer w;
	w.u8(2);
	WritePacked(w, full, false);
	string blob = Base64Encode(w.b.data(), w.b.size());
	if (blob.size() <= 180)
		return blob;

	full.flags |= kFlagCut;
	full.socketed.clear();
	w.b.clear();
	w.u8(2);
	WritePacked(w, full, false);
	blob = Base64Encode(w.b.data(), w.b.size());
	if (blob.size() <= 180)
		return blob;

	while (full.stats.size() > 8 && blob.size() > 180) {
		full.stats.pop_back();
		full.flags |= kFlagCut;
		w.b.clear();
		w.u8(2);
		WritePacked(w, full, false);
		blob = Base64Encode(w.b.data(), w.b.size());
	}
	return blob;
}

static int CallGetItemDesc(UnitAny* item, wchar_t* buffer, int cap) {
	__try {
		buffer[0] = 0;
		D2CLIENT_GetItemDesc(item, buffer);
		buffer[cap - 1] = 0;
		return buffer[0] ? 1 : 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		buffer[0] = 0;
		return 0;
	}
}

static bool LineVisible(const wstring& line) {
	for (size_t i = 0; i < line.size(); i++) {
		if (line[i] == (wchar_t)0x00FF && i + 2 < line.size() && line[i + 1] == L'c') {
			i += 2;
			continue;
		}
		if (line[i] != L' ' && line[i] != L'\t')
			return true;
	}
	return false;
}

// The game builds this from the string tables in the MPQ, including color codes.
static bool CaptureGameDesc(UnitAny* item, LinkedItem& out) {
	wchar_t desc[1024];
	if (!item || !CallGetItemDesc(item, desc, 1024))
		return false;
	const wchar_t* p = desc;
	while (*p && out.text.size() < kMaxLines) {
		const wchar_t* end = p;
		while (*end && *end != L'\n' && *end != L'\r')
			end++;
		if (end > p) {
			wstring line(p, end);
			while (!line.empty() && (line.back() == L' ' || line.back() == L'\t'))
				line.pop_back();
			if (LineVisible(line))
				AddLine(out, line, White);
		}
		if (*end == L'\r')
			end++;
		if (*end == L'\n')
			end++;
		if (end == p)
			break;
		p = end;
	}
	// The game buffer is bottom-first. Draw the name at the top.
	std::reverse(out.text.begin(), out.text.end());
	std::reverse(out.color.begin(), out.color.end());
	return !out.text.empty();
}

static bool HasColorCode(const wstring& line) {
	for (size_t i = 0; i + 2 < line.size(); i++) {
		if (line[i] == (wchar_t)0x00FF && line[i + 1] == L'c')
			return true;
	}
	return false;
}

static wstring VisibleKey(const wstring& line) {
	wstring out;
	for (size_t i = 0; i < line.size(); i++) {
		if (line[i] == (wchar_t)0x00FF && i + 2 < line.size() && line[i + 1] == L'c') {
			i += 2;
			continue;
		}
		out.push_back(line[i]);
	}
	while (!out.empty() && (out.back() == L' ' || out.back() == L'\t'))
		out.pop_back();
	size_t start = 0;
	while (start < out.size() && (out[start] == L' ' || out[start] == L'\t'))
		start++;
	if (start)
		out.erase(0, start);
	return out;
}

static bool HasExact(const LinkedItem& item, const wstring& key) {
	if (key.empty())
		return false;
	for (size_t i = 0; i < item.text.size(); i++) {
		if (VisibleKey(item.text[i]) == key)
			return true;
	}
	return false;
}

static void AddColored(LinkedItem& item, const wstring& text, int color) {
	if (!LineVisible(text) || item.text.size() >= kMaxLines)
		return;
	wstring line = text;
	if (!HasColorCode(line))
		line = GetColorCode(color) + line;
	AddLine(item, line, color);
}

static void AddStatHeader(LinkedItem& item, const PackedItem& p) {
	if (p.flags & kFlagEth)
		AddColored(item, L"Ethereal", Blue);
	if (p.flags & kFlagUnid)
		AddColored(item, MpQLabel(D2STR_ITEMSTATS1B, L"Unidentified"), Red);
	wchar_t buf[128];
	if (p.defense > 0) {
		swprintf_s(buf, L"%s %u", MpQLabel(D2STR_ITEMSTATS1H, L"Defense:").c_str(), (unsigned)p.defense);
		AddColored(item, buf, White);
	}
	if (p.dmgMax > 0) {
		swprintf_s(buf, L"%s %u %s %u",
			MpQLabel(D2STR_ITEMSTATS1G, L"Damage:").c_str(),
			(unsigned)p.dmgMin,
			MpQLabel(D2STR_ITEMSTAST1K, L"to").c_str(),
			(unsigned)p.dmgMax);
		AddColored(item, buf, White);
	}
	if (p.dmgMax2 > 0) {
		swprintf_s(buf, L"%s %u %s %u",
			MpQLabel(D2STR_ITEMSTATS1M, L"Two-Hand Damage:").c_str(),
			(unsigned)p.dmgMin2,
			MpQLabel(D2STR_ITEMSTAST1K, L"to").c_str(),
			(unsigned)p.dmgMax2);
		AddColored(item, buf, White);
	}
	if (p.maxDur > 0) {
		swprintf_s(buf, L"%s %u %s %u",
			MpQLabel(D2STR_ITEMSTATS1D, L"Durability:").c_str(),
			(unsigned)p.dur,
			MpQLabel(D2STR_ITEMSTATS1J, L"of").c_str(),
			(unsigned)p.maxDur);
		AddColored(item, buf, White);
	}
	if (p.reqStr > 0) {
		swprintf_s(buf, L"%s %u", MpQLabel(D2STR_ITEMSTATS1E, L"Required Strength:").c_str(), (unsigned)p.reqStr);
		AddColored(item, buf, White);
	}
	if (p.reqDex > 0) {
		swprintf_s(buf, L"%s %u", MpQLabel(D2STR_ITEMSTATS1F, L"Required Dexterity:").c_str(), (unsigned)p.reqDex);
		AddColored(item, buf, White);
	}
	if (p.reqLvl > 0) {
		swprintf_s(buf, L"%s %u", MpQLabel(D2STR_ITEMSTATS1P, L"Required Level:").c_str(), (unsigned)p.reqLvl);
		AddColored(item, buf, White);
	}
}

static bool ParseStatRange(const wstring& line, size_t bracket, size_t& end) {
	if (bracket >= line.size() || line[bracket] != L'[')
		return false;
	size_t p = bracket + 1;
	while (p < line.size() && line[p] == L' ')
		p++;
	if (p >= line.size() || line[p] < L'0' || line[p] > L'9')
		return false;
	while (p < line.size() && line[p] >= L'0' && line[p] <= L'9')
		p++;
	while (p < line.size() && line[p] == L' ')
		p++;
	if (p >= line.size() || line[p] != L'-')
		return false;
	p++;
	while (p < line.size() && line[p] == L' ')
		p++;
	if (p >= line.size() || line[p] < L'0' || line[p] > L'9')
		return false;
	while (p < line.size() && line[p] >= L'0' && line[p] <= L'9')
		p++;
	while (p < line.size() && line[p] == L' ')
		p++;
	if (p >= line.size() || line[p] != L']')
		return false;
	end = p + 1;
	if (end + 2 < line.size() && line[end] == (wchar_t)0x00FF && line[end + 1] == L'c')
		end += 3;
	return true;
}

static wstring StripStatRanges(const wstring& line) {
	wstring out;
	for (size_t i = 0; i < line.size(); ) {
		size_t end = 0;
		if (ParseStatRange(line, i, end)) {
			if (out.size() >= 3 && out[out.size() - 3] == (wchar_t)0x00FF && out[out.size() - 2] == L'c')
				out.resize(out.size() - 3);
			if (!out.empty() && out.back() == L' ')
				out.pop_back();
			i = end;
			continue;
		}
		out.push_back(line[i]);
		i++;
	}
	while (!out.empty() && (out.back() == L' ' || out.back() == L'\t'))
		out.pop_back();
	return out;
}

static void AddGameNameLines(UnitAny* item, LinkedItem& out) {
	wchar_t buffer[256];
	buffer[0] = 0;
	if (!item || !item->pItemData || !CallGetItemName(item, buffer, 256))
		return;
	int color = QualityColor((uint8_t)item->pItemData->dwQuality);
	LinkedItem names;
	const wchar_t* p = buffer;
	while (*p && names.text.size() < kMaxLines) {
		const wchar_t* end = p;
		while (*end && *end != L'\n' && *end != L'\r')
			end++;
		if (end > p) {
			wstring line(p, end);
			while (!line.empty() && (line.back() == L' ' || line.back() == L'\t'))
				line.pop_back();
			AddColored(names, line, color);
		}
		if (*end == L'\r')
			end++;
		if (*end == L'\n')
			end++;
		if (end == p)
			break;
		p = end;
	}
	// Stack names come out bottom-first: "Count: 15" then "* T5 Sacred Prism".
	if (!names.text.empty() && IsCountLabel(ToAnsi(VisibleKey(names.text[0])))) {
		std::reverse(names.text.begin(), names.text.end());
		std::reverse(names.color.begin(), names.color.end());
	}
	for (size_t i = 0; i < names.text.size() && out.text.size() < kMaxLines; i++)
		AddLine(out, names.text[i], i < names.color.size() ? names.color[i] : color);
}

static bool Mentions(const LinkedItem& item, const wstring& key) {
	if (key.empty())
		return false;
	for (size_t i = 0; i < item.text.size(); i++) {
		if (VisibleKey(item.text[i]).find(key) != wstring::npos)
			return true;
	}
	return false;
}

static string TrimField(const string& in) {
	size_t a = 0;
	while (a < in.size() && (in[a] == ' ' || in[a] == '\t' || in[a] == '\r'))
		a++;
	size_t b = in.size();
	while (b > a && (in[b - 1] == ' ' || in[b - 1] == '\t' || in[b - 1] == '\r'))
		b--;
	return in.substr(a, b - a);
}

static string CodeKey(const string& code) {
	string key = TrimField(code);
	if (key.size() > 3)
		key.resize(3);
	for (size_t i = 0; i < key.size(); i++) {
		if (key[i] >= 'A' && key[i] <= 'Z')
			key[i] = (char)(key[i] - 'A' + 'a');
	}
	return key;
}

// misc.txt: the description key lives in spelldescstr on the row whose code
// matches the item. The loose excel file is the one Resurgence edits; the
// patch mpq fills any code that file does not list.
static const std::unordered_map<string, string>& SpellKeys() {
	static std::unordered_map<string, string> keys;
	static bool ready = false;
	if (ready)
		return keys;
	auto add = [&](const string& code, const string& spell) {
		string c = CodeKey(code);
		string s = TrimField(spell);
		if (c.empty() || s.empty() || s == "0")
			return;
		keys[c] = s;
	};
	std::ifstream in(BH::path + "data\\global\\excel\\Misc.txt", std::ios::binary);
	if (in) {
		string header;
		if (std::getline(in, header)) {
			if (header.size() >= 3 && (unsigned char)header[0] == 0xEF)
				header.erase(0, 3);
			vector<string> cols;
			string field;
			std::stringstream hss(header);
			while (std::getline(hss, field, '\t'))
				cols.push_back(TrimField(field));
			int codeCol = -1;
			int spellCol = -1;
			for (size_t i = 0; i < cols.size(); i++) {
				if (cols[i] == "code")
					codeCol = (int)i;
				else if (cols[i] == "spelldescstr")
					spellCol = (int)i;
			}
			string line;
			while (codeCol >= 0 && spellCol >= 0 && std::getline(in, line)) {
				vector<string> cells;
				std::stringstream fss(line);
				while (std::getline(fss, field, '\t'))
					cells.push_back(field);
				if ((int)cells.size() <= codeCol || (int)cells.size() <= spellCol)
					continue;
				add(cells[codeCol], cells[spellCol]);
			}
		}
	}
	auto misc = MpqDataMap.find("misc");
	if (misc != MpqDataMap.end() && misc->second) {
		for (size_t i = 0; i < misc->second->data.size(); i++) {
			const std::map<string, string>& row = misc->second->data[i];
			auto code = row.find("code");
			auto spell = row.find("spelldescstr");
			if (code == row.end() || spell == row.end())
				continue;
			string c = CodeKey(code->second);
			if (c.empty() || keys.find(c) != keys.end())
				continue;
			add(code->second, spell->second);
		}
	}
	if (!keys.empty() || misc != MpqDataMap.end())
		ready = true;
	return keys;
}

static wchar_t* LookupSpellText(const char* key) {
	if (!key || !key[0] || D2Version::GetGameVersionID() != VERSION_113c)
		return NULL;
	wchar_t* text = NULL;
	__try {
		text = D2LANG_GetStringByReferenceString(key);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return NULL;
	}
	if (!text || !text[0])
		return NULL;
	// A missing key comes back as placeholder text, never as a null.
	bool same = true;
	for (size_t i = 0; key[i]; i++) {
		wchar_t want = (wchar_t)(unsigned char)key[i];
		if (text[i] != want) {
			same = false;
			break;
		}
		if (i + 1 == strlen(key) && text[i + 1] != 0)
			same = false;
	}
	if (same)
		return NULL;
	return text;
}

static string MiscCodeName(UnitAny* item) {
	ItemText* txt = item ? D2COMMON_GetItemText(item->dwTxtFileNo) : NULL;
	if (!txt || !txt->szCode[0])
		return "";
	char code[4] = { txt->szCode[0], txt->szCode[1], txt->szCode[2], 0 };
	wchar_t* text = LookupSpellText(code);
	if (!text)
		return "";
	wstring line;
	for (size_t i = 0; text[i] && text[i] != L'\n' && text[i] != L'\r'; ) {
		if (text[i] == (wchar_t)0x00FF && text[i + 1] == L'c' && text[i + 2]) {
			i += 3;
			continue;
		}
		line.push_back(text[i]);
		i++;
	}
	string name = CleanName(ToAnsi(line));
	if (name.empty() || IsCountLabel(name))
		return "";
	return name;
}

static void AddSpellDesc(UnitAny* item, LinkedItem& out) {
	ItemText* txt = item ? D2COMMON_GetItemText(item->dwTxtFileNo) : NULL;
	if (!txt || !txt->szCode[0])
		return;
	char code[4] = { txt->szCode[0], txt->szCode[1], txt->szCode[2], 0 };
	const std::unordered_map<string, string>& keys = SpellKeys();
	auto found = keys.find(CodeKey(code));
	if (found == keys.end())
		return;
	wchar_t* text = LookupSpellText(found->second.c_str());
	if (!text)
		return;
	// The string table lists the bottom line first, same as the stat buffer.
	size_t begin = out.text.size();
	const wchar_t* p = text;
	while (*p && out.text.size() < kMaxLines) {
		const wchar_t* end = p;
		while (*end && *end != L'\n' && *end != L'\r')
			end++;
		if (end > p) {
			wstring line(p, end);
			while (!line.empty() && (line.back() == L' ' || line.back() == L'\t'))
				line.pop_back();
			wstring shown = VisibleKey(line);
			if (LineVisible(line) && !HasExact(out, shown)) {
				int color = White;
				if (line.size() >= 3 && line[0] == (wchar_t)0x00FF && line[1] == L'c' && line[2] >= L'0' && line[2] <= L'9')
					color = (int)(line[2] - L'0');
				AddLine(out, line, color);
			}
		}
		if (*end == L'\r')
			end++;
		if (*end == L'\n')
			end++;
		if (end == p)
			break;
		p = end;
	}
	if (out.text.size() > begin) {
		std::reverse(out.text.begin() + begin, out.text.end());
		std::reverse(out.color.begin() + begin, out.color.end());
	}
}

// Name and type come from the game. Defense and requirements use the same
// string-table labels as the inventory tooltip. The lines under those are the
// game's own property text, without the rolled stat ranges.
static size_t BuildSharedTip(UnitAny* item, LinkedItem& out) {
	out.text.clear();
	out.color.clear();
	if (!item || !item->pItemData)
		return 0;
	AddGameNameLines(item, out);
	wstring title = ToWide(ItemLabel(item));
	if (!title.empty() && out.text.size() == out.color.size() && !Mentions(out, VisibleKey(title))) {
		out.text.insert(out.text.begin(), title);
		out.color.insert(out.color.begin(), QualityColor((uint8_t)item->pItemData->dwQuality));
	}
	PackedItem p = PackUnit(item, false);
	if (out.text.empty()) {
		int color = QualityColor(p.quality);
		if (!p.runeword.empty())
			AddColored(out, ToWide(p.runeword), Gold);
		AddColored(out, ToWide(p.name.empty() ? "Item" : p.name), color);
	}
	wstring base = ToWide(p.base);
	if (!base.empty() && !Mentions(out, base))
		AddColored(out, base, QualityColor(p.quality));
	AddStatHeader(out, p);
	size_t header = out.text.size();
	LinkedItem desc;
	if (CaptureGameDesc(item, desc)) {
		for (size_t i = 0; i < desc.text.size() && out.text.size() < kMaxLines; i++) {
			wstring stat = StripStatRanges(desc.text[i]);
			if (!LineVisible(stat) || HasExact(out, VisibleKey(stat)))
				continue;
			AddLine(out, stat, i < desc.color.size() ? desc.color[i] : White);
		}
	}
	AddSpellDesc(item, out);
	int qty = (int)D2COMMON_GetUnitStat(item, STAT_AMMOQUANTITY, 0);
	if (qty > 1) {
		wchar_t buf[64];
		swprintf_s(buf, L"Quantity: %d", qty);
		if (!HasExact(out, buf))
			AddColored(out, buf, White);
	}
	return header;
}

static wchar_t* ProbeLocale(WORD id) {
	__try {
		return D2LANG_GetLocaleText(id);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return (wchar_t*)-1;
	}
}

// Pull the words out of a formatted string such as "%d%% Increased Attack Speed"
// so they can stand in for the text the game already filled in.
static void LiteralSegments(const wchar_t* text, size_t n, vector<wstring>& segs) {
	wstring cur;
	auto flush = [&]() {
		size_t a = 0;
		size_t b = cur.size();
		while (a < b && cur[a] == L' ')
			a++;
		while (b > a && cur[b - 1] == L' ')
			b--;
		if (b > a && b - a >= 5 && b - a <= 48)
			segs.push_back(cur.substr(a, b - a));
		cur.clear();
	};
	for (size_t i = 0; i < n; i++) {
		wchar_t ch = text[i];
		if (ch == L'%') {
			if (i + 1 < n && text[i + 1] == L'%') {
				cur.push_back(L'%');
				i++;
				continue;
			}
			flush();
			i++;
			while (i < n && (text[i] == L'+' || text[i] == L'-' || text[i] == L' ' || text[i] == L'#' || text[i] == L'0'))
				i++;
			while (i < n && text[i] >= L'0' && text[i] <= L'9')
				i++;
			if (i < n && text[i] == L'.') {
				i++;
				while (i < n && text[i] >= L'0' && text[i] <= L'9')
					i++;
			}
			if (i < n && (text[i] == L'h' || text[i] == L'l' || text[i] == L'L' || text[i] == L'z'))
				i++;
			continue;
		}
		if (ch == L'\n' || ch == L'\r' || ch == (wchar_t)0x00FF) {
			flush();
			continue;
		}
		cur.push_back(ch);
	}
	flush();
}

// Long tooltip phrases are replaced with their string-table id so the line
// still fits in a chat packet. Both clients have the same table.
struct PhraseBook {
	vector<wstring> text;
	vector<uint16_t> id;
	vector<uint8_t> seg;
	vector<int> bucket[128];
	vector<int> other;
	bool ready;
	PhraseBook() : ready(false) {}
};

static void AddPhrase(PhraseBook& book, std::unordered_map<wstring, uint8_t>& seen, const wstring& key, uint16_t id, uint8_t seg) {
	if (key.size() < 5 || key.size() > 48)
		return;
	if (seen.find(key) != seen.end())
		return;
	seen.emplace(key, 1);
	int idx = (int)book.text.size();
	wchar_t first = key[0];
	book.text.push_back(key);
	book.id.push_back(id);
	book.seg.push_back(seg);
	if (first >= 0 && first < 128)
		book.bucket[first].push_back(idx);
	else
		book.other.push_back(idx);
}

static PhraseBook& LocalePhrases() {
	static PhraseBook book;
	if (book.ready)
		return book;
	std::unordered_map<wstring, uint8_t> seen;
	int faults = 0;
	for (int id = 1; id < 25000; id++) {
		wchar_t* text = ProbeLocale((WORD)id);
		if (text == (wchar_t*)-1) {
			if (++faults >= 3)
				break;
			continue;
		}
		faults = 0;
		if (!text || !text[0])
			continue;
		size_t len = 0;
		while (text[len] && len < 180)
			len++;
		if (text[len])
			continue;
		wstring copy(text, len);
		bool percent = false;
		bool skip = false;
		for (size_t i = 0; i < copy.size(); i++) {
			if (copy[i] == L'\n' || copy[i] == L'\r' || copy[i] == (wchar_t)0x00FF)
				skip = true;
			if (copy[i] == L'%')
				percent = true;
		}
		if (skip)
			continue;
		if (!percent)
			AddPhrase(book, seen, copy, (uint16_t)id, 0xFF);
		else {
			vector<wstring> segs;
			LiteralSegments(copy.c_str(), copy.size(), segs);
			for (size_t s = 0; s < segs.size() && s < 8; s++)
				AddPhrase(book, seen, segs[s], (uint16_t)id, (uint8_t)s);
		}
	}
	book.ready = true;
	return book;
}

struct ColorChunk {
	bool setColor;
	wchar_t code;
	wstring text;
};

static void SplitColors(const wstring& line, vector<ColorChunk>& chunks) {
	ColorChunk cur;
	cur.setColor = false;
	cur.code = 0;
	for (size_t i = 0; i < line.size(); i++) {
		if (line[i] == (wchar_t)0x00FF && i + 2 < line.size() && line[i + 1] == L'c') {
			if (!cur.text.empty() || cur.setColor)
				chunks.push_back(cur);
			cur.text.clear();
			cur.setColor = true;
			cur.code = line[i + 2];
			i += 2;
			continue;
		}
		cur.text.push_back(line[i]);
	}
	if (!cur.text.empty() || cur.setColor)
		chunks.push_back(cur);
}

static void EmitEscaped(vector<uint8_t>& payload, wchar_t ch) {
	if (payload.size() >= 255)
		return;
	if (ch >= 0x20 && ch < 0x7F) {
		payload.push_back((uint8_t)ch);
		return;
	}
	char buf[4];
	int n = WideCharToMultiByte(CODE_PAGE, 0, &ch, 1, buf, 4, NULL, NULL);
	for (int k = 0; k < n && payload.size() + 2 <= 255; k++) {
		payload.push_back(0x82);
		payload.push_back((uint8_t)buf[k]);
	}
}

static bool ReadRange(const wstring& text, size_t i, size_t& len, unsigned& lo, unsigned& hi) {
	if (i >= text.size() || text[i] != L'[')
		return false;
	size_t p = i + 1;
	while (p < text.size() && text[p] == L' ')
		p++;
	unsigned a = 0;
	int digits = 0;
	while (p < text.size() && text[p] >= L'0' && text[p] <= L'9') {
		a = a * 10u + (unsigned)(text[p] - L'0');
		if (a > 65535u)
			return false;
		p++;
		digits++;
	}
	if (digits == 0)
		return false;
	while (p < text.size() && text[p] == L' ')
		p++;
	if (p >= text.size() || text[p] != L'-')
		return false;
	p++;
	while (p < text.size() && text[p] == L' ')
		p++;
	unsigned b = 0;
	digits = 0;
	while (p < text.size() && text[p] >= L'0' && text[p] <= L'9') {
		b = b * 10u + (unsigned)(text[p] - L'0');
		if (b > 65535u)
			return false;
		p++;
		digits++;
	}
	if (digits == 0)
		return false;
	while (p < text.size() && text[p] == L' ')
		p++;
	if (p >= text.size() || text[p] != L']')
		return false;
	len = p + 1 - i;
	if (len <= 5)
		return false;
	lo = a;
	hi = b;
	return true;
}

static void TokenizeText(const wstring& text, vector<uint8_t>& payload) {
	if (text.empty() || payload.size() >= 255)
		return;
	PhraseBook& book = LocalePhrases();
	size_t i = 0;
	while (i < text.size() && payload.size() < 255) {
		size_t rangeLen = 0;
		unsigned lo = 0;
		unsigned hi = 0;
		if (ReadRange(text, i, rangeLen, lo, hi) && payload.size() + 5 <= 255) {
			payload.push_back(0x83);
			payload.push_back((uint8_t)lo);
			payload.push_back((uint8_t)(lo >> 8));
			payload.push_back((uint8_t)hi);
			payload.push_back((uint8_t)(hi >> 8));
			i += rangeLen;
			continue;
		}
		const vector<int>* bucket = &book.other;
		if (text[i] >= 0 && text[i] < 128)
			bucket = &book.bucket[text[i]];
		size_t bestLen = 0;
		int bestCost = 99;
		uint16_t bestId = 0;
		uint8_t bestSeg = 0xFF;
		for (size_t n = 0; n < bucket->size(); n++) {
			int idx = (*bucket)[n];
			const wstring& phrase = book.text[idx];
			int cost = book.seg[idx] == 0xFF ? 3 : 4;
			if ((int)phrase.size() <= cost || phrase.size() < bestLen)
				continue;
			if (phrase.size() == bestLen && cost >= bestCost)
				continue;
			if (i + phrase.size() > text.size())
				continue;
			if (text.compare(i, phrase.size(), phrase) != 0)
				continue;
			bestLen = phrase.size();
			bestCost = cost;
			bestId = book.id[idx];
			bestSeg = book.seg[idx];
		}
		if (bestLen > 0 && payload.size() + (size_t)bestCost <= 255) {
			payload.push_back(bestSeg == 0xFF ? 0x80 : 0x84);
			payload.push_back((uint8_t)bestId);
			payload.push_back((uint8_t)(bestId >> 8));
			if (bestSeg != 0xFF)
				payload.push_back(bestSeg);
			i += bestLen;
			continue;
		}
		if (text[i] >= L'0' && text[i] <= L'9') {
			size_t p = i;
			unsigned v = 0;
			int digits = 0;
			bool fit = true;
			while (p < text.size() && text[p] >= L'0' && text[p] <= L'9') {
				v = v * 10u + (unsigned)(text[p] - L'0');
				if (v > 65535u) {
					fit = false;
					break;
				}
				p++;
				digits++;
			}
			if (fit && digits >= 4 && payload.size() + 3 <= 255) {
				payload.push_back(0x85);
				payload.push_back((uint8_t)v);
				payload.push_back((uint8_t)(v >> 8));
				i = p;
				continue;
			}
		}
		EmitEscaped(payload, text[i]);
		i++;
	}
}

static void WriteTokenLine(Writer& w, const wstring& text, int color) {
	vector<ColorChunk> chunks;
	SplitColors(text, chunks);
	vector<uint8_t> payload;
	for (size_t i = 0; i < chunks.size() && payload.size() < 255; i++) {
		if (chunks[i].setColor && payload.size() + 2 <= 255) {
			payload.push_back(0x81);
			payload.push_back((uint8_t)chunks[i].code);
		}
		TokenizeText(chunks[i].text, payload);
	}
	if (color < 0)
		color = White;
	if (color > 255)
		color = White;
	w.u8((uint8_t)color);
	w.u8((uint8_t)payload.size());
	w.b.insert(w.b.end(), payload.begin(), payload.end());
}

static wstring PhraseSegment(uint16_t id, uint8_t seg) {
	wchar_t* text = ProbeLocale(id);
	if (!text || text == (wchar_t*)-1 || !text[0])
		return L"";
	wstring copy(text);
	vector<wstring> segs;
	LiteralSegments(copy.c_str(), copy.size(), segs);
	if (seg >= segs.size())
		return L"";
	return segs[seg];
}

static wstring Detokenize(const uint8_t* data, size_t n) {
	wstring out;
	size_t i = 0;
	while (i < n) {
		uint8_t b = data[i++];
		if (b == 0x80 && i + 1 < n) {
			uint16_t id = (uint16_t)data[i] | ((uint16_t)data[i + 1] << 8);
			i += 2;
			out += Locale(id);
		} else if (b == 0x84 && i + 2 < n) {
			uint16_t id = (uint16_t)data[i] | ((uint16_t)data[i + 1] << 8);
			uint8_t seg = data[i + 2];
			i += 3;
			out += PhraseSegment(id, seg);
		} else if (b == 0x83 && i + 3 < n) {
			unsigned lo = (unsigned)data[i] | ((unsigned)data[i + 1] << 8);
			unsigned hi = (unsigned)data[i + 2] | ((unsigned)data[i + 3] << 8);
			i += 4;
			wchar_t buf[32];
			swprintf_s(buf, L"[%u - %u]", lo, hi);
			out += buf;
		} else if (b == 0x85 && i + 1 < n) {
			unsigned v = (unsigned)data[i] | ((unsigned)data[i + 1] << 8);
			i += 2;
			wchar_t buf[16];
			swprintf_s(buf, L"%u", v);
			out += buf;
		} else if (b == 0x81 && i < n) {
			out.push_back((wchar_t)0x00FF);
			out.push_back(L'c');
			out.push_back((wchar_t)data[i++]);
		} else if (b == 0x82 && i < n) {
			char c = (char)data[i++];
			wchar_t wch = 0;
			if (MultiByteToWideChar(CODE_PAGE, 0, &c, 1, &wch, 1) > 0 && wch)
				out.push_back(wch);
		} else if (b >= 0x20 && b < 0x7F) {
			out.push_back((wchar_t)b);
		}
	}
	return out;
}

static vector<uint8_t> TokenBytes(const LinkedItem& tip) {
	Writer w;
	w.u8(5);
	int n = (int)tip.text.size();
	if (n > (int)kMaxLines)
		n = (int)kMaxLines;
	w.u8((uint8_t)n);
	for (int i = 0; i < n; i++) {
		int color = i < (int)tip.color.size() ? tip.color[i] : White;
		WriteTokenLine(w, tip.text[i], color);
	}
	return w.b;
}

static bool DecodeTokenized(const vector<uint8_t>& raw, LinkedItem& item) {
	Reader r(raw);
	if (r.u8() != 5)
		return false;
	uint8_t n = r.u8();
	if (!r.ok || n == 0 || n > kMaxLines)
		return false;
	for (uint8_t i = 0; i < n; i++) {
		int color = r.u8();
		uint8_t len = r.u8();
		if (!r.ok || r.i + len > r.n)
			return false;
		wstring line = Detokenize(r.p + r.i, len);
		r.i += len;
		if (LineVisible(line))
			AddLine(item, line, color);
	}
	return r.ok && !item.text.empty() && item.text.size() == item.color.size();
}

static string EncodeDesc(const LinkedItem& tip) {
	Writer w;
	w.u8(3);
	int n = (int)tip.text.size();
	if (n > (int)kMaxLines)
		n = (int)kMaxLines;
	w.u8((uint8_t)n);
	for (int i = 0; i < n; i++) {
		string bytes = ToAnsi(tip.text[i]);
		if (bytes.size() > 200)
			bytes.resize(200);
		w.u8((uint8_t)bytes.size());
		w.b.insert(w.b.end(), bytes.begin(), bytes.end());
		int color = i < (int)tip.color.size() ? tip.color[i] : White;
		w.u8((uint8_t)color);
	}
	return Base64Encode(w.b.data(), w.b.size());
}

static bool DecodeDesc(const vector<uint8_t>& raw, LinkedItem& item) {
	Reader r(raw);
	if (r.u8() != 3)
		return false;
	uint8_t n = r.u8();
	if (!r.ok || n == 0 || n > kMaxLines)
		return false;
	for (uint8_t i = 0; i < n; i++) {
		uint8_t len = r.u8();
		if (!r.ok || r.i + len > r.n)
			return false;
		string bytes(reinterpret_cast<const char*>(r.p + r.i), reinterpret_cast<const char*>(r.p + r.i + len));
		r.i += len;
		int color = r.u8();
		if (!r.ok)
			return false;
		wstring line = ToWide(bytes);
		if (LineVisible(line))
			AddLine(item, line, color);
	}
	return r.ok && !item.text.empty() && item.text.size() == item.color.size();
}

static bool LzCompress(const uint8_t* in, size_t n, vector<uint8_t>& out) {
	if (!in || n < 8 || n > 4096)
		return false;
	HMODULE nt = GetModuleHandleW(L"ntdll.dll");
	if (!nt)
		return false;
	typedef LONG (WINAPI* GetWsFn)(USHORT, ULONG*, ULONG*);
	typedef LONG (WINAPI* CompFn)(USHORT, unsigned char*, ULONG, unsigned char*, ULONG, ULONG, ULONG*, void*);
	GetWsFn getWs = (GetWsFn)GetProcAddress(nt, "RtlGetCompressionWorkSpaceSize");
	CompFn comp = (CompFn)GetProcAddress(nt, "RtlCompressBuffer");
	if (!getWs || !comp)
		return false;
	ULONG ws = 0;
	ULONG frag = 0;
	if (getWs(2, &ws, &frag) < 0 || ws == 0 || ws > 1024 * 1024)
		return false;
	vector<uint8_t> work(ws);
	vector<uint8_t> dst(n);
	ULONG finalSize = 0;
	LONG st = comp(2, (unsigned char*)in, (ULONG)n, dst.data(), (ULONG)dst.size(), 4096, &finalSize, work.data());
	if (st < 0 || finalSize == 0 || finalSize + 5 >= n)
		return false;
	out.assign(dst.begin(), dst.begin() + finalSize);
	return true;
}

static bool LzDecompress(const uint8_t* in, size_t n, size_t expect, vector<uint8_t>& out) {
	if (!in || n == 0 || expect == 0 || expect > 4096)
		return false;
	HMODULE nt = GetModuleHandleW(L"ntdll.dll");
	if (!nt)
		return false;
	typedef LONG (WINAPI* DecFn)(USHORT, unsigned char*, ULONG, unsigned char*, ULONG, ULONG*);
	DecFn dec = (DecFn)GetProcAddress(nt, "RtlDecompressBuffer");
	if (!dec)
		return false;
	out.resize(expect);
	ULONG finalSize = 0;
	LONG st = dec(2, out.data(), (ULONG)expect, (unsigned char*)in, (ULONG)n, &finalSize);
	if (st < 0 || finalSize != expect)
		return false;
	return true;
}

static string EncodePacked(const LinkedItem& tip) {
	vector<uint8_t> raw = TokenBytes(tip);
	if (raw.empty())
		return "";
	string best = Base64Encode(raw.data(), raw.size());
	vector<uint8_t> comp;
	if (LzCompress(raw.data(), raw.size(), comp)) {
		Writer w;
		w.u8(6);
		w.u16((uint16_t)raw.size());
		w.u16((uint16_t)comp.size());
		w.b.insert(w.b.end(), comp.begin(), comp.end());
		string packed = Base64Encode(w.b.data(), w.b.size());
		if (packed.size() < best.size())
			best = packed;
	}
	string plain = EncodeDesc(tip);
	if (!plain.empty() && plain.size() < best.size())
		best = plain;
	return best;
}

static LinkedItem StripColorCodes(const LinkedItem& tip) {
	LinkedItem out;
	for (size_t i = 0; i < tip.text.size(); i++) {
		const wstring& line = tip.text[i];
		wstring plain;
		int color = i < tip.color.size() ? tip.color[i] : White;
		bool saw = false;
		for (size_t c = 0; c < line.size(); c++) {
			if (line[c] == (wchar_t)0x00FF && c + 2 < line.size() && line[c + 1] == L'c') {
				if (!saw) {
					color = (int)(line[c + 2] - L'0');
					if (color < 0 || color > 15)
						color = White;
					saw = true;
				}
				c += 2;
				continue;
			}
			plain.push_back(line[c]);
		}
		if (LineVisible(plain))
			AddLine(out, plain, color);
	}
	return out;
}

static size_t BlobBudget(const string& label) {
	// A whisper puts "/whisper *account " in front of the same bracket.
	const size_t whisperPrefix = 32;
	size_t chrome = label.size() + 2 + 3 + 1;
	if (whisperPrefix + chrome + 24 >= 250)
		return 48;
	return 250 - whisperPrefix - chrome;
}

static string FitTip(LinkedItem tip, size_t keep, size_t budget) {
	string full = EncodePacked(tip);
	if (!full.empty() && full.size() <= budget)
		return full;
	LinkedItem plain = StripColorCodes(tip);
	string slim = EncodePacked(plain);
	if (!slim.empty() && slim.size() <= budget)
		return slim;
	string best = full.size() <= slim.size() || slim.empty() ? full : slim;
	LinkedItem cur = best == slim && !slim.empty() ? plain : tip;
	while (cur.text.size() > keep && !cur.text.empty()) {
		cur.text.pop_back();
		if (!cur.color.empty())
			cur.color.pop_back();
		string packed = EncodePacked(cur);
		if (!packed.empty() && packed.size() <= budget)
			return packed;
	}
	if (!best.empty() && best.size() <= budget)
		return best;
	return "";
}

static bool DecodeBlob(const string& blob, LinkedItem& item) {
	vector<uint8_t> raw;
	if (!Base64Decode(blob, raw) || raw.empty())
		return false;
	if (raw[0] == 6) {
		if (raw.size() < 5)
			return false;
		uint16_t rawSize = (uint16_t)raw[1] | ((uint16_t)raw[2] << 8);
		uint16_t compSize = (uint16_t)raw[3] | ((uint16_t)raw[4] << 8);
		if (rawSize < 2 || rawSize > 4096 || compSize == 0 || (size_t)compSize + 5 > raw.size())
			return false;
		vector<uint8_t> plain;
		if (!LzDecompress(raw.data() + 5, compSize, rawSize, plain))
			return false;
		return DecodeTokenized(plain, item);
	}
	if (raw[0] == 5)
		return DecodeTokenized(raw, item);
	if (raw[0] == 3)
		return DecodeDesc(raw, item);
	if (raw[0] != 2)
		return false;
	Reader r(raw);
	r.u8();
	PackedItem p = ReadPacked(r, false);
	if (!r.ok || p.name.empty())
		return false;
	item = BuildTip(p);
	return !item.text.empty() && item.text.size() == item.color.size();
}

static string IdCodes(uint32_t id) {
	string out;
	for (int i = 0; i < kIdCodes; i++) {
		out.push_back((char)0xFF);
		out.push_back('c');
		out.push_back(kAlpha[(id >> (6 * i)) & 63]);
	}
	return out;
}

static wstring IdCodesW(uint32_t id) {
	wstring out;
	for (int i = 0; i < kIdCodes; i++) {
		out.push_back((wchar_t)0x00FF);
		out.push_back(L'c');
		out.push_back((wchar_t)kAlpha[(id >> (6 * i)) & 63]);
	}
	return out;
}

static bool ReadIdChars(const char* chars, uint32_t* id) {
	uint32_t v = 0;
	for (int i = 0; i < kIdCodes; i++) {
		int idx = AlphaIndex(chars[i]);
		if (idx < 0)
			return false;
		v |= (uint32_t)idx << (6 * i);
	}
	*id = v;
	return true;
}

static bool ReadIdAt(const string& s, size_t pos, uint32_t* id, size_t* end) {
	char chars[kIdCodes];
	size_t p = pos;
	for (int i = 0; i < kIdCodes; i++) {
		if (p + 2 >= s.size())
			return false;
		if ((unsigned char)s[p] != 0xFF || s[p + 1] != 'c')
			return false;
		chars[i] = s[p + 2];
		p += 3;
	}
	if (!ReadIdChars(chars, id))
		return false;
	*end = p;
	return true;
}

// A long tooltip is (#group:index:count:chunk). ':' is not in the blob alphabet.
static bool TakePart(const string& blob, string& gid, int& index, int& count, string& data) {
	size_t a = blob.find(':');
	if (a == string::npos || a == 0 || a > 6)
		return false;
	size_t b = blob.find(':', a + 1);
	if (b == string::npos)
		return false;
	size_t c = blob.find(':', b + 1);
	if (c == string::npos)
		return false;
	for (size_t i = 0; i < a; i++) {
		if (AlphaIndex(blob[i]) < 0)
			return false;
	}
	index = 0;
	count = 0;
	for (size_t i = a + 1; i < b; i++) {
		if (blob[i] < '0' || blob[i] > '9')
			return false;
		index = index * 10 + (blob[i] - '0');
	}
	for (size_t i = b + 1; i < c; i++) {
		if (blob[i] < '0' || blob[i] > '9')
			return false;
		count = count * 10 + (blob[i] - '0');
	}
	if (index < 1 || count < 2 || count > 12 || index > count)
		return false;
	data = blob.substr(c + 1);
	gid = blob.substr(0, a);
	return !data.empty();
}

static void StorePart(const string& blob, const string& label) {
	string gid, data;
	int index = 0, count = 0;
	if (!TakePart(blob, gid, index, count, data))
		return;
	TipParts& box = gAssembly[gid];
	if (box.count == 0) {
		box.count = count;
		box.piece.assign((size_t)count, "");
		box.when = GetTickCount();
	}
	if (box.count == count && box.piece[(size_t)index - 1].empty()) {
		box.piece[(size_t)index - 1] = data;
		box.have++;
	}
	if (!label.empty() && box.label.empty())
		box.label = ToWide(label);
	if (box.have < box.count)
		return;
	string joined;
	for (size_t i = 0; i < box.piece.size(); i++)
		joined += box.piece[i];
	string key = ToAnsi(box.label);
	gAssembly.erase(gid);
	LinkedItem item;
	if (!DecodeBlob(joined, item) || item.text.empty())
		return;
	uint32_t existing = 0;
	if (key.empty() || !LookupLabel(key, &existing)) {
		uint32_t id = TakeId();
		Remember(id, item);
		BindLabel(key, id);
	}
}

// '(' is the live wrapper. '{' is still accepted so an older link can be read.
static bool NextBlob(const string& s, size_t search, size_t& start, size_t& end) {
	size_t paren = s.find("(#", search);
	size_t brace = s.find("{#", search);
	if (paren == string::npos && brace == string::npos)
		return false;
	if (paren == string::npos || (brace != string::npos && brace < paren))
		start = brace;
	else
		start = paren;
	char close = s[start] == '(' ? ')' : '}';
	end = s.find(close, start + 2);
	return end != string::npos;
}

// avail is the number of readable bytes at msg. When slideTail is set, bytes
// after the original message are moved up over the removed blob. D2Net sizes
// packet 0x26 with strlen, so a shorter C string left sitting on top of the
// blob makes those leftover bytes the next packet.
// A chunk line ends in "continued". "//c continued", "[Name] continued",
// and the //c echo "#continued" all count. "discontinued" does not.
static bool IsContinuedText(const char* msg) {
	if (!msg)
		return false;
	size_t n = strlen(msg);
	while (n && (msg[n - 1] == ' ' || msg[n - 1] == '\t'))
		n--;
	const char word[] = "continued";
	const size_t w = sizeof(word) - 1;
	if (n < w || memcmp(msg + (n - w), word, w) != 0)
		return false;
	if (n == w)
		return true;
	char prev = msg[n - w - 1];
	if (prev == ' ' || prev == '\t')
		return true;
	return (prev < 'A' || prev > 'Z') && (prev < 'a' || prev > 'z');
}

static bool RewriteMessage(char* msg, size_t avail = 0, bool slideTail = false);

// A relay sometimes ends the text at '(' and leaves "#data)" after that null.
// Pull it back onto the line so the chunk can still be joined.
static bool SalvageBlob(char* text) {
	if (!text || !IsContinuedText(text))
		return false;
	size_t n = strlen(text);
	char* tail = text + n + 1;
	MEMORY_BASIC_INFORMATION mbi;
	if (!VirtualQuery(tail, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
		return false;
	if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
		return false;
	size_t room = (BYTE*)mbi.BaseAddress + mbi.RegionSize - (BYTE*)tail;
	if (room < 4)
		return false;
	if (room > 480)
		room = 480;
	size_t i = 0;
	if (tail[0] == '#' )
		i = 1;
	else if (tail[0] == '(' && room > 2 && tail[1] == '#')
		i = 2;
	else
		return false;
	bool closed = false;
	for (; i < room; i++) {
		unsigned char c = (unsigned char)tail[i];
		if (c == ')') {
			closed = true;
			break;
		}
		bool ok = c == ':' || c == '-' || c == '_'
			|| (c >= '0' && c <= '9')
			|| (c >= 'A' && c <= 'Z')
			|| (c >= 'a' && c <= 'z');
		if (!ok)
			return false;
	}
	if (!closed)
		return false;
	if (tail[0] == '#')
		text[n] = '(';
	else
		memmove(text + n, tail, i + 2);
	return RewriteMessage(text);
}

static bool RewriteMessage(char* msg, size_t avail, bool slideTail) {
	if (!msg)
		return false;
	size_t cap = 0;
	if (avail) {
		while (cap < avail && msg[cap])
			cap++;
		if (cap == 0 || cap >= avail)
			return false;
	} else {
		cap = strlen(msg);
		if (cap == 0)
			return false;
	}
	if (!strstr(msg, "(#") && !strstr(msg, "{#"))
		return false;
	string s(msg, cap);
	bool changed = false;
	size_t search = 0;
	while (search < s.size()) {
		size_t start = 0;
		size_t end = 0;
		if (!NextBlob(s, search, start, end))
			break;
		string blob = s.substr(start + 2, end - (start + 2));
		string gid, partData;
		int partIndex = 0, partCount = 0;
		bool part = TakePart(blob, gid, partIndex, partCount, partData);
		LinkedItem item;
		bool decoded = !part && DecodeBlob(blob, item);
		s.erase(start, end - start + 1);
		if (part) {
			// A //c follow-up is "[Account] continued". That bracket is the
			// speaker, not the item. Keep the name from the first line.
			string inside;
			if (!IsContinuedText(s.c_str())) {
				size_t bracket = s.rfind(']', start);
				if (bracket != string::npos && bracket <= start) {
					size_t open = s.rfind('[', bracket);
					if (open != string::npos)
						inside = s.substr(open + 1, bracket - open - 1);
				}
			}
			StorePart(blob, inside);
		} else if (decoded) {
			size_t bracket = s.rfind(']', start);
			string inside;
			if (bracket != string::npos && bracket <= start) {
				size_t open = s.rfind('[', bracket);
				if (open != string::npos)
					inside = s.substr(open + 1, bracket - open - 1);
			}
			uint32_t existing = 0;
			if (inside.empty() || !LookupLabel(inside, &existing)) {
				uint32_t id = TakeId();
				Remember(id, item);
				BindLabel(inside, id);
			}
		}
		changed = true;
		search = start;
	}
	if (!changed || s.size() > cap)
		return false;
	memcpy(msg, s.c_str(), s.size() + 1);
	if (slideTail && avail) {
		size_t oldEnd = cap + 1;
		size_t newEnd = s.size() + 1;
		if (oldEnd > newEnd && oldEnd <= avail) {
			size_t tail = avail - oldEnd;
			if (tail)
				memmove(msg + newEnd, msg + oldEnd, tail);
			memset(msg + newEnd + tail, 0, oldEnd - newEnd);
		}
	}
	return true;
}

bool ItemLink_PrepareGameChat(BYTE* packet, DWORD size) {
	if (!packet || packet[0] != 0x26 || size < 16 || size > 65536)
		return false;
	size_t name = 0;
	while (10 + name < size && packet[10 + name])
		name++;
	if (10 + name >= size)
		return false;
	size_t msgOff = 11 + name;
	if (msgOff >= size)
		return false;
	char* msg = (char*)packet + msgOff;
	if (!RewriteMessage(msg, size - msgOff, true))
		return false;
	return IsContinuedText(msg);
}

bool ItemLink_FilterWhisperText(char* text) {
	if (!text)
		return false;
	if (!RewriteMessage(text))
		SalvageBlob(text);
	// *chat1 sends the chunk back as "[Name] continued". The blob is not
	// always still on that text, but the line is still only a chunk.
	return IsContinuedText(text);
}

static wstring BracketLabel(const string& label) {
	return L"[" + ToWide(label) + L"]";
}

static wchar_t* ChatBuffer() {
	return (wchar_t*)p_D2CLIENT_ChatMsg;
}

static bool ChatContains(const wstring& token) {
	wchar_t* buf = ChatBuffer();
	if (!buf || !buf[0] || token.empty())
		return false;
	MEMORY_BASIC_INFORMATION mbi;
	if (!VirtualQuery(buf, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
		return false;
	size_t n = 0;
	while (n < kChatCap && buf[n])
		n++;
	return wstring(buf, n).find(token) != wstring::npos;
}

// A message can carry one link. A deleted link can be replaced.
static bool MessageAlreadyLinked() {
	if (!gQueuedInsert.empty())
		return true;
	bool chatOpen = D2CLIENT_GetUIState(UI_CHAT_CONSOLE) != 0;
	LockItems();
	if (!gFollow.empty() || !gRest.empty()) {
		UnlockItems();
		return true;
	}
	bool found = false;
	for (size_t i = 0; i < gPending.size(); ) {
		if (chatOpen && ChatContains(gPending[i].token)) {
			found = true;
			++i;
		} else {
			gPending.erase(gPending.begin() + i);
		}
	}
	UnlockItems();
	return found;
}

static void SyncChatCaret(size_t len);

static bool AppendToChat(const wstring& text) {
	wchar_t* buf = ChatBuffer();
	if (!buf || text.empty())
		return false;
	MEMORY_BASIC_INFORMATION mbi;
	if (!VirtualQuery(buf, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
		return false;
	size_t n = 0;
	while (n < kChatCap && buf[n])
		n++;
	if (n >= kChatCap || buf[n] != 0)
		return false;
	bool needSpace = n > 0 && buf[n - 1] != L' ';
	size_t extra = text.size() + (needSpace ? 1 : 0);
	if (n + extra >= kChatCap)
		return false;
	if (needSpace)
		buf[n++] = L' ';
	memcpy(buf + n, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
	SyncChatCaret(n + text.size());
	return true;
}

// 1.13c chat caret. Backspace deletes buf[caret - 1] and stops there, so an
// appended [Item] is only "next to" the caret when this matches the length.
static DWORD* ChatCaret() {
	if (D2Version::GetGameVersionID() != VERSION_113c)
		return NULL;
	HMODULE mod = GetModuleHandleW(L"D2CLIENT.dll");
	if (!mod)
		return NULL;
	DWORD* caret = (DWORD*)((BYTE*)mod + 0x11C028);
	MEMORY_BASIC_INFORMATION mbi;
	if (!VirtualQuery(caret, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
		return NULL;
	if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
		return NULL;
	return caret;
}

static void SyncChatCaret(size_t len) {
	DWORD* caret = ChatCaret();
	if (!caret)
		return;
	if (len > 0xFE)
		len = 0xFE;
	*caret = (DWORD)len;
}

bool ItemLink_Backspace() {
	wchar_t* buf = ChatBuffer();
	if (!buf || !buf[0])
		return false;
	MEMORY_BASIC_INFORMATION mbi;
	if (!VirtualQuery(buf, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
		return false;
	size_t n = 0;
	while (n < kChatCap && buf[n])
		n++;
	if (n == 0)
		return false;
	DWORD* caretPtr = ChatCaret();
	size_t caret = caretPtr ? (size_t)*caretPtr : n;
	if (caret > n)
		caret = n;
	if (caret == 0 || buf[caret - 1] != L']')
		return false;
	size_t open = caret - 1;
	while (open > 0 && buf[open] != L'[')
		open--;
	if (buf[open] != L'[')
		return false;
	wstring token(buf + open, buf + caret);
	bool matched = false;
	LockItems();
	for (size_t i = 0; i < gPending.size(); ) {
		if (gPending[i].token == token) {
			gPending.erase(gPending.begin() + i);
			matched = true;
		} else {
			++i;
		}
	}
	if (matched && gPending.empty())
		gRest.clear();
	UnlockItems();
	if (!matched)
		return false;
	size_t start = open;
	if (start > 0 && buf[start - 1] == L' ')
		start--;
	size_t tail = n - caret;
	memmove(buf + start, buf + caret, (tail + 1) * sizeof(wchar_t));
	SyncChatCaret(start + tail);
	return true;
}

// Leave room for the part header and for the chatbot's "/f m [name] " prefix.
static vector<string> ChunkBlob(const string& full, size_t budget) {
	vector<string> out;
	if (full.empty())
		return out;
	if (full.size() <= budget) {
		out.push_back(full);
		return out;
	}
	size_t room = budget > 48 ? budget - 48 : (budget > 16 ? budget - 16 : budget);
	if (room < 8)
		return out;
	size_t count = (full.size() + room - 1) / room;
	if (count < 2 || count > 12)
		return out;
	static uint32_t seq = 1;
	uint32_t v = seq++;
	char gid[5];
	for (int i = 0; i < 4; i++) {
		gid[i] = kAlpha[v & 63];
		v >>= 6;
	}
	gid[4] = 0;
	for (size_t i = 0; i < count; i++) {
		string chunk = full.substr(i * room, room);
		out.push_back(string(gid) + ":" + std::to_string(i + 1) + ":" + std::to_string(count) + ":" + chunk);
	}
	return out;
}

// Keep "/m name", "//c", or "/f m". Drop any normal chat words in front of the item.
static wstring CommandPrefix(const wstring& before) {
	size_t i = 0;
	while (i < before.size() && (before[i] == L' ' || before[i] == L'\t'))
		i++;
	if (i >= before.size() || before[i] != L'/')
		return L"";
	size_t start = i;
	while (i < before.size() && before[i] != L' ' && before[i] != L'\t')
		i++;
	wstring cmd = before.substr(start, i - start);
	wstring lower = cmd;
	for (size_t c = 0; c < lower.size(); c++) {
		if (lower[c] >= L'A' && lower[c] <= L'Z')
			lower[c] = lower[c] - L'A' + L'a';
	}
	bool needsWord = lower == L"/m" || lower == L"/w" || lower == L"/msg" || lower == L"/whisper" || lower == L"/f";
	if (!needsWord)
		return cmd + L" ";
	while (i < before.size() && (before[i] == L' ' || before[i] == L'\t'))
		i++;
	size_t word = i;
	while (i < before.size() && before[i] != L' ' && before[i] != L'\t')
		i++;
	if (word == i)
		return cmd + L" ";
	return cmd + L" " + before.substr(word, i - word) + L" ";
}

static void QueueFollowUps(const wstring& before, const wstring& token, int kind, BYTE opcode, BYTE b1, BYTE b2) {
	vector<string> rest;
	LockItems();
	rest.swap(gRest);
	UnlockItems();
	if (rest.empty() || token.empty())
		return;
	wstring line = CommandPrefix(before) + L"continued";
	if (line.size() > 180)
		return;
	LockItems();
	for (size_t i = 0; i < rest.size(); i++)
		gFollow.push_back(FollowUp{ kind, opcode, b1, b2, line, L"continued", rest[i] });
	UnlockItems();
}

static void QueueLink(UnitAny* item) {
	if (!item || item->dwType != UNIT_ITEM || !item->pItemData)
		return;
	if (MessageAlreadyLinked()) {
		PrintText(Red, "Only one item can be linked per message.");
		return;
	}
	try {
		string label = ItemLabel(item);
		LinkedItem tip;
		BuildSharedTip(item, tip);
		size_t budget = BlobBudget(label);
		size_t bracket = label.size() + 2;
		if (bracket < 9 && budget > 9 - bracket)
			budget -= 9 - bracket;
		// Overhead speech text is 232 bytes and includes the character name.
		// A whisper-sized line overflows it and crashes in-game chat. //c never
		// builds that bubble, which is why the same item survives there.
		size_t lineCap = 190;
		size_t lineChrome = label.size() + 5;
		if (lineCap > lineChrome + 16 && budget > lineCap - lineChrome)
			budget = lineCap - lineChrome;
		string blob;
		if (!tip.text.empty())
			blob = EncodePacked(tip);
		if (blob.empty())
			blob = EncodeUnit(item);
		vector<string> parts = ChunkBlob(blob, budget);
		if (label.empty() || parts.empty()) {
			PrintText(Red, "Item link is too long for chat.");
			return;
		}
		LinkedItem sent;
		if (!DecodeBlob(blob, sent) || sent.text.empty())
			return;
		tip = sent;
		uint32_t id = TakeId();
		Remember(id, tip);
		BindLabel(label, id);
		wstring shown = BracketLabel(label);
		LockItems();
		gPending.clear();
		gRest.clear();
		gPending.push_back(PendingLink{ shown, parts[0] });
		for (size_t i = 1; i < parts.size(); i++)
			gRest.push_back(parts[i]);
		UnlockItems();
		if (D2CLIENT_GetUIState(UI_CHAT_CONSOLE)) {
			if (!AppendToChat(shown))
				PrintText(Red, "Chat box is full.");
			return;
		}
		gQueuedInsert = shown;
		gQueueWait = 0;
		gOpenChat = true;
	} catch (...) {
		PrintText(Red, "Couldn't link that item.");
	}
}

static bool ReadIdW(const wchar_t* s, size_t n, size_t pos, uint32_t* id, size_t* end) {
	char chars[kIdCodes];
	size_t p = pos;
	for (int i = 0; i < kIdCodes; i++) {
		if (p + 2 >= n)
			return false;
		if (s[p] != (wchar_t)0x00FF || s[p + 1] != L'c')
			return false;
		if (s[p + 2] > 127)
			return false;
		chars[i] = (char)s[p + 2];
		p += 3;
	}
	if (!ReadIdChars(chars, id))
		return false;
	*end = p;
	return true;
}

void ItemLink_ExpandOutgoing(wchar_t* msg, char* packet) {
	// The game already turned the chat box into the packet before this hook.
	// Color codes never make it into that packet, so the box only holds [Name]
	// and the blob is written onto the packet here.
	if (!msg || !msg[0] || !packet)
		return;
	if (packet[0] != 0x14 && packet[0] != 0x15)
		return;
	char* text = packet + 3;
	const size_t cap = 190;

	vector<PendingLink> pending;
	LockItems();
	pending.swap(gPending);
	UnlockItems();
	if (pending.empty())
		return;

	size_t n = 0;
	while (n < 255 && msg[n])
		n++;
	wstring s(msg, n);
	size_t search = 0;
	bool changed = false;
	wstring followBefore, followToken;
	for (size_t i = 0; i < pending.size(); i++) {
		size_t pos = s.find(pending[i].token, search);
		if (pos == wstring::npos)
			continue;
		wstring payload = L"(#" + ToWide(pending[i].blob) + L")";
		if (s.size() + payload.size() >= cap) {
			PrintText(Red, "Item link is too long for chat.");
			continue;
		}
		followBefore = s.substr(0, pos);
		followToken = pending[i].token;
		size_t at = pos + pending[i].token.size();
		s.insert(at, payload);
		changed = true;
		break;
	}
	if (!changed)
		return;
	QueueFollowUps(followBefore, followToken, 1, packet[0], packet[1], packet[2]);

	char ansi[256];
	int wrote = WideCharToMultiByte(CP_ACP, 0, s.c_str(), -1, ansi, (int)cap, NULL, NULL);
	if (wrote <= 1 || (size_t)wrote > cap)
		return;
	memcpy(text, ansi, wrote);
}

static int VisibleWidth(const wchar_t* s, int count) {
	if (count <= 0)
		return 0;
	int width = 0;
	wchar_t tmp[256];
	int n = 0;
	for (int i = 0; i < count; i++) {
		if (s[i] == (wchar_t)0x00FF && i + 2 < count && s[i + 1] == L'c') {
			if (n) {
				tmp[n] = 0;
				width += D2WIN_GetTextWidth(tmp);
				n = 0;
			}
			i += 2;
			continue;
		}
		if (n >= 255) {
			tmp[n] = 0;
			width += D2WIN_GetTextWidth(tmp);
			n = 0;
		}
		tmp[n++] = s[i];
	}
	if (n) {
		tmp[n] = 0;
		width += D2WIN_GetTextWidth(tmp);
	}
	return width;
}

static int WcsLenSafe(const wchar_t* s, int cap) {
	MEMORY_BASIC_INFORMATION mbi;
	if (!s || !VirtualQuery(s, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
		return 0;
	if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
		return 0;
	size_t bytes = (BYTE*)mbi.BaseAddress + mbi.RegionSize - (BYTE*)s;
	int room = (int)(bytes / sizeof(wchar_t));
	if (room > cap)
		room = cap;
	int n = 0;
	while (n < room && s[n])
		n++;
	return n;
}

// wrapWidth is the pixel width of one visual row. 0 means the game drew this
// string as a single line at (x, y). Returns how many rows the line occupies.
static int RecordLine(const wchar_t* str, int x, int y, int wrapWidth) {
	if (gItemLinkSuppress || gNoteDepth || !str || !str[0])
		return 0;
	gNoteDepth++;
	int len = WcsLenSafe(str, 512);
	int full = VisibleWidth(str, len);
	int rows = 1;
	if (full <= 0)
		rows = 0;
	else if (wrapWidth > 0 && full > wrapWidth)
		rows = (full + wrapWidth - 1) / wrapWidth;

	for (int i = 0; i < len; i++) {
		if (str[i] != L'[')
			continue;
		int close = -1;
		for (int j = i + 1; j < len; j++) {
			if (str[j] == L']') {
				close = j;
				break;
			}
			if (str[j] == L'[')
				break;
		}
		if (close < 0)
			continue;
		uint32_t id = 0;
		size_t idEnd = 0;
		if (!ReadIdW(str, (size_t)len, (size_t)close + 1, &id, &idEnd)) {
			wstring inside(str + i + 1, str + close);
			if (!LookupLabel(ToAnsi(inside), &id))
				continue;
		}
		int prefix = VisibleWidth(str, i);
		int span = VisibleWidth(str + i, close - i + 1);
		if (span <= 0)
			continue;
		int row = 0;
		int local = prefix;
		if (wrapWidth > 0) {
			row = prefix / wrapWidth;
			local = prefix % wrapWidth;
			int room = wrapWidth - local;
			if (span > room)
				span = room;
		}
		int drawX = x + local;
		int drawY = y + row * 15;
		HitRect hit;
		hit.left = drawX - 1;
		hit.right = drawX + span + 1;
		hit.top = drawY - 14;
		hit.bottom = drawY + 2;
		hit.id = id;
		hit.color = Gold;
		if (i >= 3 && str[i - 3] == (wchar_t)0x00FF && str[i - 2] == L'c' && str[i - 1] >= L'0' && str[i - 1] <= L'9')
			hit.color = (int)(str[i - 1] - L'0');
		gRects.push_back(hit);
		i = close;
	}
	if (gRects.size() > 96)
		gRects.clear();
	gNoteDepth--;
	return rows;
}

void __cdecl ItemLink_NoteDraw(const wchar_t* str, int x, int y) {
	RecordLine(str, x, y, 0);
}

// 1.13c chat lists. The fading lines at the bottom and the message log are
// stored here and drawn without going through a hook we can rely on.
static const int kBottomHead = 0x113AAA;
static const int kBottomCount = 0x113AAE;
static const int kTopHead = 0x113AA4;
static const int kTopCount = 0x113AA8;
static const int kChatShiftA = 0xFAD98;
static const int kChatShiftB = 0xFADC4;
static const int kLogBaseFlag = 0xFADDCC;
static const int kLogMode = 0x11C414;
static const int kChatWrap = 300;

static DWORD ClientDword(BYTE* base, int rva) {
	if (!base || !ReadableRange(base + rva, 4))
		return 0;
	return *(DWORD*)(base + rva);
}

static void ScanGameChat() {
	if (D2Version::GetGameVersionID() != VERSION_113c)
		return;
	if (!D2CLIENT_GetPlayerUnit())
		return;
	BYTE* base = (BYTE*)GetModuleHandleW(L"D2CLIENT.dll");
	if (!base)
		return;

	DWORD oldFont = D2WIN_SetTextSize(0xD);

	WORD bottomCount = 0;
	if (ReadableRange(base + kBottomCount, 2))
		bottomCount = *(WORD*)(base + kBottomCount);
	DWORD bottom = ClientDword(base, kBottomHead);
	if (bottomCount && bottom) {
		int screenH = (int)*p_D2CLIENT_ScreenSizeY;
		int x = 0x0F;
		if (ClientDword(base, kChatShiftA) || ClientDword(base, kChatShiftB))
			x = 0x64;
		int msgDrop = 0;
		int guard = 0;
		int limit = bottomCount > 24 ? 24 : (int)bottomCount;
		for (DWORD p = bottom; p && guard < limit; guard++) {
			if (!ReadableRange((void*)p, 0x26))
				break;
			int lineOff = 0;
			for (int i = 0; i < 6; i++) {
				wchar_t* s = *(wchar_t**)(p + i * 4);
				if (s && ReadableRange(s, sizeof(wchar_t)) && s[0]) {
					int y = msgDrop - (lineOff >> 1) + screenH - 0x73;
					RecordLine(s, x, y, 0);
				}
				lineOff += 0x19;
			}
			DWORD next = *(DWORD*)(p + 0x22);
			if (next == p)
				break;
			p = next;
			msgDrop -= 0x19;
		}
	}

	WORD topCount = 0;
	if (ReadableRange(base + kTopCount, 2))
		topCount = *(WORD*)(base + kTopCount);
	DWORD top = ClientDword(base, kTopHead);
	if (topCount && top) {
		int baseY = ClientDword(base, kLogBaseFlag) ? 0x5F : 0x14;
		DWORD mode = ClientDword(base, kLogMode);
		int x = 0x0F;
		int wrap = 0;
		if (mode == 1 || mode == 2) {
			wrap = kChatWrap;
			if (mode == 2) {
				int sw = (int)*p_D2CLIENT_ScreenSizeX;
				x = sw / 2 + 0x0F;
			}
		}
		int line = 0;
		int guard = 0;
		int limit = topCount > 24 ? 24 : (int)topCount;
		for (DWORD p = top; p && guard < limit; guard++) {
			if (!ReadableRange((void*)p, 0x26))
				break;
			for (int i = 0; i < 6; i++) {
				wchar_t* s = *(wchar_t**)(p + i * 4);
				if (!s || !ReadableRange(s, sizeof(wchar_t)) || !s[0])
					continue;
				int y = line * 15 + baseY;
				line += RecordLine(s, x, y, wrap);
			}
			DWORD next = *(DWORD*)(p + 0x22);
			if (next == p)
				break;
			p = next;
		}
	}

	D2WIN_SetTextSize(oldFont);
}

static void* gTrampoline = NULL;
static BYTE* gDrawText = NULL;
static BYTE gSaved[24];
static int gStolen = 0;

static int ModRmExtra(const BYTE* p) {
	BYTE mod = p[0] >> 6;
	BYTE rm = p[0] & 7;
	int n = 1;
	if (mod != 3 && rm == 4) {
		n += 1;
		BYTE base = p[1] & 7;
		if (mod == 0 && base == 5)
			n += 4;
	}
	if (mod == 0 && rm == 5)
		n += 4;
	else if (mod == 1)
		n += 1;
	else if (mod == 2)
		n += 4;
	return n;
}

static int InsnLen(const BYTE* p) {
	switch (p[0]) {
	case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55:
	case 0x56: case 0x57: case 0x58: case 0x59: case 0x5A: case 0x5B:
	case 0x5C: case 0x5D: case 0x5E: case 0x5F: case 0x90: case 0xC3:
		return 1;
	case 0x6A:
		return 2;
	case 0x68: case 0xA1: case 0xB8: case 0xB9: case 0xBA: case 0xBB:
	case 0xBC: case 0xBD: case 0xBE: case 0xBF:
		return 5;
	case 0x83:
		return 1 + ModRmExtra(p + 1) + 1;
	case 0x81:
		return 1 + ModRmExtra(p + 1) + 4;
	case 0x8B: case 0x89: case 0x8A: case 0x88: case 0x3B: case 0x39:
	case 0x85: case 0x33: case 0x31: case 0x2B: case 0x03: case 0x01:
		return 1 + ModRmExtra(p + 1);
	case 0xFF:
		if (p[1] == 0x25)
			return 6;
		return -1;
	default:
		return -1;
	}
}

static void __declspec(naked) DrawTextHook() {
	__asm {
		cmp dword ptr [gItemLinkSuppress], 0
		jne skip_note
		pushad
		mov eax, dword ptr [esp + 36]
		push eax
		push edx
		push ecx
		call ItemLink_NoteDraw
		add esp, 12
		popad
	skip_note:
		jmp dword ptr [gItemLinkTrampoline]
	}
}

static BYTE* FindClientDrawThunk(BYTE* draw) {
	HMODULE client = GetModuleHandleW(L"D2CLIENT.dll");
	if (!client || !draw)
		return NULL;
	BYTE* base = (BYTE*)client;
	BYTE* p = base;
	BYTE* end = base + 0x200000;
	MEMORY_BASIC_INFORMATION mbi;
	while (p < end && VirtualQuery(p, &mbi, sizeof(mbi))) {
		BYTE* regionEnd = (BYTE*)mbi.BaseAddress + mbi.RegionSize;
		if (regionEnd > end)
			regionEnd = end;
		bool exec = (mbi.State == MEM_COMMIT) && (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
		if (exec) {
			for (BYTE* q = (BYTE*)mbi.BaseAddress > p ? (BYTE*)mbi.BaseAddress : p; q + 6 <= regionEnd; q++) {
				if (q[0] != 0xFF || q[1] != 0x25)
					continue;
				DWORD slot = *(DWORD*)(q + 2);
				if (!ReadableRange((void*)slot, 4))
					continue;
				if (*(BYTE**)slot == draw)
					return q;
			}
		}
		if (regionEnd <= p)
			break;
		p = regionEnd;
	}
	if (D2Version::GetGameVersionID() == VERSION_113c) {
		BYTE* t = (BYTE*)Patch::GetDllOffset(D2CLIENT, 0xD37E);
		if (t && ((t[0] == 0xFF && t[1] == 0x25) || t[0] == 0xE9))
			return t;
	}
	return NULL;
}

static void InstallDrawHook() {
	if (gDrawText)
		return;
	int ordinal = D2Version::GetGameVersionID() == VERSION_113d ? -10076 : -10150;
	BYTE* draw = (BYTE*)Patch::GetDllOffset(D2WIN, ordinal);
	// Hook the call thunk inside D2Client. Another renderer replaces D2Win's
	// DrawText after we load, which used to drop every chat hover.
	BYTE* fn = FindClientDrawThunk(draw);
	if (!fn)
		fn = draw;
	if (!fn)
		return;
	void* tramp = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!tramp)
		return;
	int total = 0;
	if (fn[0] == 0xE9) {
		// Another renderer already detoured DrawText. Chain to its hook.
		BYTE* dest = fn + 5 + *(int*)(fn + 1);
		BYTE* t = (BYTE*)tramp;
		t[0] = 0xE9;
		*(DWORD*)(t + 1) = (DWORD)dest - (DWORD)(t + 5);
		total = 5;
	} else {
		while (total < 5) {
			int n = InsnLen(fn + total);
			if (n <= 0 || total + n > (int)sizeof(gSaved)) {
				VirtualFree(tramp, 0, MEM_RELEASE);
				return;
			}
			total += n;
		}
		memcpy(tramp, fn, total);
		BYTE* t = (BYTE*)tramp + total;
		t[0] = 0xE9;
		*(DWORD*)(t + 1) = (DWORD)(fn + total) - (DWORD)(t + 5);
	}
	memcpy(gSaved, fn, total);
	gItemLinkTrampoline = tramp;
	DWORD old = 0;
	VirtualProtect(fn, total, PAGE_EXECUTE_READWRITE, &old);
	fn[0] = 0xE9;
	*(DWORD*)(fn + 1) = (DWORD)DrawTextHook - (DWORD)(fn + 5);
	for (int i = 5; i < total; i++)
		fn[i] = 0x90;
	VirtualProtect(fn, total, old, &old);
	gTrampoline = tramp;
	gDrawText = fn;
	gStolen = total;
}

static void RemoveDrawHook() {
	if (!gDrawText || !gStolen)
		return;
	DWORD old = 0;
	VirtualProtect(gDrawText, gStolen, PAGE_EXECUTE_READWRITE, &old);
	memcpy(gDrawText, gSaved, gStolen);
	VirtualProtect(gDrawText, gStolen, old, &old);
	if (gTrampoline)
		VirtualFree(gTrampoline, 0, MEM_RELEASE);
	gItemLinkTrampoline = NULL;
	gTrampoline = NULL;
	gDrawText = NULL;
	gStolen = 0;
}

static void DrawHover() {
	ScanGameChat();
	int mx = (int)*p_D2CLIENT_MouseX;
	int my = (int)*p_D2CLIENT_MouseY;
	uint32_t id = 0;
	int lx = 0, ly = 0, lw = 0, color = Gold;
	for (size_t i = 0; i < gRects.size(); i++) {
		const HitRect& r = gRects[i];
		if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
			id = r.id;
			lx = r.left;
			ly = r.bottom;
			lw = r.right - r.left;
			color = r.color;
		}
	}
	gRects.clear();
	if (!id)
		return;

	LinkedItem item;
	if (!Lookup(id, item) || item.text.empty())
		return;

	vector<wstring> lines;
	vector<int> colors;
	for (size_t i = 0; i < item.text.size(); i++) {
		wstring stat = StripStatRanges(item.text[i]);
		if (!LineVisible(stat))
			continue;
		lines.push_back(stat);
		colors.push_back(i < item.color.size() ? item.color[i] : White);
	}
	if (lines.empty())
		return;

	gItemLinkSuppress++;
	DWORD oldFont = D2WIN_SetTextSize(0);
	int maxW = 0;
	for (size_t i = 0; i < lines.size(); i++) {
		int w = VisibleWidth(lines[i].c_str(), (int)lines[i].size());
		if (w > maxW)
			maxW = w;
	}
	const int lineH = 13;
	const int pad = 6;
	const int gapX = 40;
	const int gapY = 32;
	int boxW = maxW + pad * 2;
	int boxH = (int)lines.size() * lineH + pad * 2;
	int x = mx + gapX;
	int y = my + gapY;
	int sw = (int)*p_D2CLIENT_ScreenSizeX;
	int sh = (int)*p_D2CLIENT_ScreenSizeY;
	if (sw > 0 && x + boxW > sw - 4)
		x = mx - boxW - gapX;
	if (sh > 0 && y + boxH > sh - 4)
		y = my - boxH - gapY;
	if (x < 2)
		x = 2;
	if (y < 2)
		y = 2;

	D2GFX_DrawRectangle(x, y, x + boxW, y + boxH, 0, Drawing::BTOneHalf);
	D2GFX_DrawLine(x, y, x + boxW, y, color, -1);
	D2GFX_DrawLine(x, y + boxH, x + boxW, y + boxH, color, -1);
	D2GFX_DrawLine(x, y, x, y + boxH, color, -1);
	D2GFX_DrawLine(x + boxW, y, x + boxW, y + boxH, color, -1);
	D2GFX_DrawLine(lx, ly, lx + lw, ly, color, -1);

	int ty = y + pad + 10;
	for (size_t i = 0; i < lines.size(); i++) {
		int color = colors[i];
		wstring shown = lines[i];
		if (!HasColorCode(shown) && shown.find(L'[') != wstring::npos) {
			wstring green = GetColorCode(DarkGreen);
			wstring back = GetColorCode(color);
			wstring painted;
			for (size_t c = 0; c < shown.size(); c++) {
				if (shown[c] == L'[')
					painted += green;
				painted.push_back(shown[c]);
				if (shown[c] == L']')
					painted += back;
			}
			shown.swap(painted);
		}
		int lineW = VisibleWidth(shown.c_str(), (int)shown.size());
		int tx = x + (boxW - lineW) / 2;
		D2WIN_DrawText(shown.c_str(), tx, ty, color, 0);
		ty += lineH;
	}
	D2WIN_SetTextSize(oldFont);
	gItemLinkSuppress--;
}

static bool WhisperCommand(const char* text) {
	while (*text == ' ')
		text++;
	const char* cmds[] = { "/whisper", "/msg", "/m", "/w" };
	for (int i = 0; i < 4; i++) {
		const char* cmd = cmds[i];
		size_t n = strlen(cmd);
		size_t c = 0;
		for (; c < n; c++) {
			char a = text[c];
			if (a >= 'A' && a <= 'Z')
				a = (char)(a - 'A' + 'a');
			if (a != cmd[c])
				break;
		}
		if (c != n)
			continue;
		char next = text[n];
		if (next == ' ' || next == 0)
			return true;
	}
	return false;
}

// The chat box only holds [Name]. The blob is written into the ANSI line the
// command parser is about to send. cap is the size of that line.
static void InsertPendingBlob(char* text, size_t cap) {
	LockItems();
	if (gPending.empty()) {
		UnlockItems();
		return;
	}
	vector<PendingLink> pending;
	pending.swap(gPending);
	UnlockItems();

	size_t len = 0;
	while (len < cap && text[len])
		len++;
	if (text[len]) {
		LockItems();
		if (gPending.empty())
			gPending.swap(pending);
		UnlockItems();
		return;
	}

	bool inserted = false;
	wstring followBefore, followToken;
	for (size_t i = 0; i < pending.size(); i++) {
		string token = ToAnsi(pending[i].token);
		if (token.empty())
			continue;
		char* at = strstr(text, token.c_str());
		if (!at)
			continue;
		string payload = "(#" + pending[i].blob + ")";
		size_t atPos = (size_t)(at - text) + token.size();
		if (len + payload.size() >= cap) {
			PrintText(Red, "Item link is too long for a whisper.");
			break;
		}
		followBefore = ToWide(string(text, at));
		followToken = pending[i].token;
		memmove(text + atPos + payload.size(), text + atPos, len - atPos + 1);
		memcpy(text + atPos, payload.c_str(), payload.size());
		inserted = true;
		break;
	}
	if (inserted)
		QueueFollowUps(followBefore, followToken, 2, 0, 0, 0);
	if (!inserted) {
		LockItems();
		if (gPending.empty())
			gPending.swap(pending);
		UnlockItems();
	}
}

// /m skips the normal chat hook. The command parser reads this ANSI line,
// so the blob has to be in it before that parser runs.
static void __fastcall ExpandWhisper(char* text) {
	if (!text || text[0] != '/' || !WhisperCommand(text))
		return;
	InsertPendingBlob(text, 250);
}

// /f m and //c are not whispers. They fall through to the realm command handler.
static void __fastcall ExpandRealmCommand(char* text) {
	if (!text || text[0] != '/')
		return;
	InsertPendingBlob(text, 250);
}

static void __declspec(naked) WhisperSend_Hook() {
	__asm {
		pushad
		mov ecx, eax
		call ExpandWhisper
		popad
		sub esp, 0x430
		jmp dword ptr [gWhisperReturn]
	}
}

static void InstallWhisperHook() {
	if (gWhisperSite)
		return;
	if (D2Version::GetGameVersionID() != VERSION_113c)
		return;
	BYTE* fn = (BYTE*)Patch::GetDllOffset(D2CLIENT, 0x70500);
	if (!fn)
		return;
	if (fn[0] != 0x81 || fn[1] != 0xEC || fn[2] != 0x30 || fn[3] != 0x04 || fn[4] != 0x00 || fn[5] != 0x00)
		return;
	gWhisperReturn = (DWORD)(fn + 6);
	DWORD old = 0;
	VirtualProtect(fn, 6, PAGE_EXECUTE_READWRITE, &old);
	fn[0] = 0xE9;
	*(DWORD*)(fn + 1) = (DWORD)WhisperSend_Hook - (DWORD)(fn + 5);
	fn[5] = 0x90;
	VirtualProtect(fn, 6, old, &old);
	gWhisperSite = fn;
}

static void RemoveWhisperHook() {
	if (!gWhisperSite)
		return;
	BYTE bytes[6] = { 0x81, 0xEC, 0x30, 0x04, 0x00, 0x00 };
	DWORD old = 0;
	VirtualProtect(gWhisperSite, 6, PAGE_EXECUTE_READWRITE, &old);
	memcpy(gWhisperSite, bytes, 6);
	VirtualProtect(gWhisperSite, 6, old, &old);
	gWhisperSite = NULL;
	gWhisperReturn = 0;
}

static void __declspec(naked) RealmCommand_Hook() {
	__asm {
		pushad
		mov ecx, esi
		call ExpandRealmCommand
		popad
		mov ecx, esi
		call dword ptr [eax + 0x14]
		jmp dword ptr [gCommandReturn]
	}
}

static void InstallCommandHook() {
	if (gCommandSite)
		return;
	if (D2Version::GetGameVersionID() != VERSION_113c)
		return;
	BYTE* site = (BYTE*)Patch::GetDllOffset(D2CLIENT, 0x70C32);
	if (!site)
		return;
	if (site[0] != 0x8B || site[1] != 0xCE || site[2] != 0xFF || site[3] != 0x50 || site[4] != 0x14)
		return;
	gCommandReturn = (DWORD)(site + 5);
	DWORD old = 0;
	VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
	site[0] = 0xE9;
	*(DWORD*)(site + 1) = (DWORD)RealmCommand_Hook - (DWORD)(site + 5);
	VirtualProtect(site, 5, old, &old);
	gCommandSite = site;
}

static void RemoveCommandHook() {
	if (!gCommandSite)
		return;
	BYTE bytes[5] = { 0x8B, 0xCE, 0xFF, 0x50, 0x14 };
	DWORD old = 0;
	VirtualProtect(gCommandSite, 5, PAGE_EXECUTE_READWRITE, &old);
	memcpy(gCommandSite, bytes, 5);
	VirtualProtect(gCommandSite, 5, old, &old);
	gCommandSite = NULL;
	gCommandReturn = 0;
}

void ItemLink::OnLoad() {
	if (!gLockReady) {
		InitializeCriticalSection(&gLock);
		gLockReady = true;
	}
	InstallDrawHook();
	InstallWhisperHook();
	InstallCommandHook();
}

void ItemLink::OnUnload() {
	RemoveCommandHook();
	RemoveWhisperHook();
	RemoveDrawHook();
	if (gLockReady) {
		DeleteCriticalSection(&gLock);
		gLockReady = false;
	}
}

void ItemLink::OnLoop() {
	if (!D2CLIENT_GetPlayerUnit()) {
		gQueuedInsert.clear();
		gOpenChat = false;
		gChatWasOpen = false;
		return;
	}
	bool chatOpen = D2CLIENT_GetUIState(UI_CHAT_CONSOLE) != 0;
	if (gChatWasOpen && !chatOpen && !gSendingFollow) {
		// Enter moves the leftover parts into gFollow before the box closes.
		// A dismissed box still holds the draft, which would block the next link.
		LockItems();
		bool abandon = gFollow.empty();
		if (abandon) {
			gPending.clear();
			gRest.clear();
		}
		UnlockItems();
		if (abandon) {
			gQueuedInsert.clear();
			gOpenChat = false;
			gQueueWait = 0;
		}
	}
	if (!gAssembly.empty()) {
		DWORD now = GetTickCount();
		for (auto it = gAssembly.begin(); it != gAssembly.end(); ) {
			if (now - it->second.when > 15000)
				it = gAssembly.erase(it);
			else
				++it;
		}
	}
	if (!gSendingFollow && gQueuedInsert.empty() && !gOpenChat && !D2CLIENT_GetUIState(UI_CHAT_CONSOLE)) {
		FollowUp next;
		bool send = false;
		LockItems();
		if (!gFollow.empty() && gPending.empty()) {
			next = gFollow.front();
			gFollow.pop_front();
			gPending.push_back(PendingLink{ next.token, next.blob });
			send = true;
		}
		UnlockItems();
		// The game's chat send appends a name field after the text. A hand-built
		// packet leaves that field off, and the server reads the next bytes as a
		// player name ("... is not in the game."). Submit the line normally so
		// the same hook that sent part 1 attaches this piece.
		if (send) {
			gSendingFollow = true;
			D2CLIENT_SubmitChat(next.line.c_str());
			gSendingFollow = false;
		}
	}
	// Opening chat from the mouse handler re-enters the window proc and aborts the game.
	if (gOpenChat) {
		if (!D2CLIENT_GetUIState(UI_CHAT_CONSOLE))
			D2CLIENT_SetUIVar(UI_CHAT_CONSOLE, 0, 0);
		gOpenChat = false;
		gQueueWait = 0;
		gChatWasOpen = D2CLIENT_GetUIState(UI_CHAT_CONSOLE) != 0;
		return;
	}
	if (gQueuedInsert.empty()) {
		gChatWasOpen = D2CLIENT_GetUIState(UI_CHAT_CONSOLE) != 0;
		return;
	}
	if (!D2CLIENT_GetUIState(UI_CHAT_CONSOLE)) {
		if (++gQueueWait < 40) {
			gChatWasOpen = false;
			return;
		}
	}
	if (!AppendToChat(gQueuedInsert))
		PrintText(Red, "Couldn't insert the item link.");
	gQueuedInsert.clear();
	gQueueWait = 0;
	gChatWasOpen = D2CLIENT_GetUIState(UI_CHAT_CONSOLE) != 0;
}

void ItemLink::OnGameExit() {
	gQueuedInsert.clear();
	gOpenChat = false;
	gChatWasOpen = false;
	gRects.clear();
	gAssembly.clear();
	LockItems();
	gPending.clear();
	gRest.clear();
	gFollow.clear();
	UnlockItems();
}

void ItemLink::OnDraw() {
	DrawHover();
}

void ItemLink::OnOOGDraw() {
	DrawHover();
}

void ItemLink::OnLeftClick(bool up, unsigned int x, unsigned int y, bool* block) {
	if (up || !block)
		return;
	if (!(GetKeyState(VK_CONTROL) & 0x8000))
		return;
	if (D2CLIENT_GetCursorItem())
		return;

	UnitAny* item = *p_D2CLIENT_SelectedInvItem;
	bool ground = false;
	if (!item || item->dwType != UNIT_ITEM) {
		item = D2CLIENT_GetSelectedUnit();
		ground = true;
	}
	if (!item || item->dwType != UNIT_ITEM || !item->pItemData)
		return;
	if (ground && !D2CLIENT_GetUIState(UI_CHAT_CONSOLE))
		return;

	// Read the item here. The pointer is not safe on a later frame.
	QueueLink(item);
	*block = true;
}

void ItemLink::OnChatMsg(const char* user, const char* msg, bool fromGame, bool* block) {
	(void)user;
	(void)fromGame;
	(void)block;
	if (!msg)
		return;
	RewriteMessage(const_cast<char*>(msg));
}

void ItemLink::OnChatPacketRecv(BYTE* packet, bool* block) {
	if (!packet || packet[0] != 0xFF || packet[1] != 0x0F)
		return;
	unsigned short event = *(unsigned short*)(packet + 4);
	// Whisper, talk, broadcast, the copy of a whisper you sent, info, emote.
	// //c and /f m come back on one of these, not as in-game chat.
	if (event != 4 && event != 5 && event != 6 && event != 0x0A && event != 0x12 && event != 0x17)
		return;
	unsigned short len = *(unsigned short*)(packet + 2);
	if (len < 32 || len > 1024)
		return;
	char* from = (char*)packet + 28;
	size_t name = 0;
	while (name + 28 < len && from[name])
		name++;
	if (name + 29 >= len)
		return;
	char* text = from + name + 1;
	if (!RewriteMessage(text))
		SalvageBlob(text);
	if (IsContinuedText(text) && block)
		*block = true;
}
