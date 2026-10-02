/**
 *
 * Item.cpp
 * BH: Copyright 2011 (C) McGod
 * SlashDiablo Maphack: Copyright (C) SlashDiablo Community
 *
 *  This file is part of SlashDiablo Maphack.
 *
 *  SlashDiablo Maphack is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as published
 *  by the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   ==========================================================
 *   D2Ex2
 *   https://github.com/lolet/D2Ex2
 *   ==========================================================
 *   Copyright (c) 2011-2014 Bartosz Jankowski
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *   ==========================================================
 *
 */

#include "Item.h"
#include "../../D2Ptrs.h"
#include "../../D2Strings.h"
#include "../../BH.h"
#include "../../D2Stubs.h"
#include "../../D2Helpers.h"
#include "ItemDisplay.h"
#include "../../MPQInit.h"
#include "lrucache.hpp"

ItemsTxtStat* GetAllStatModifier(ItemsTxtStat* pStats, int nStats, int nStat, ItemsTxtStat* pOrigin);
ItemsTxtStat* GetItemsTxtStatByMod(ItemsTxtStat* pStats, int nStats, int nStat, int nStatParam);
RunesTxt* GetRunewordTxtById(int rwId);

map<std::string, Toggle> Item::Toggles;
unordered_set<string> Item::no_ilvl_codes;
unsigned int Item::filterLevelSetting = 0;
unsigned int Item::pingLevelSetting = 0;
unsigned int Item::trackerPingLevelSetting = -1;
int Item::statRangeColor = TextColor::DarkGreen;
UnitAny* Item::viewingUnit;

Patch* itemNamePatch = new Patch(Call, D2CLIENT, { 0x92366, 0x96736 }, (int)ItemName_Interception, 6);
Patch* itemPropertiesPatch = new Patch(Jump, D2CLIENT, { 0x5612C, 0x2E3FC }, (int)GetProperties_Interception, 6);
Patch* itemPropertyStringDamagePatch = new Patch(Call, D2CLIENT, { 0x55D7B, 0x2E04B }, (int)GetItemPropertyStringDamage_Interception, 5);
Patch* itemPropertyStringPatch = new Patch(Call, D2CLIENT, { 0x55D9D, 0x2E06D }, (int) GetItemPropertyString_Interception, 5);
// Replaces "mov eax, [ebp+60h]; test eax, eax" at the start of the socketed-stat merge.
// 1.13c: D2Client+0x55C15  1.13d: D2Client+0x2DEE5. Returning +0x4B skips the merge loop.
Patch* skipSocketedStatMergePatch = new Patch(Call, D2CLIENT, { 0x55C15, 0x2DEE5 }, (int)SkipSocketedStatMerge_Intercept, 5);
// Inventory hover draws the finished tooltip here. ecx is the wide string.
// The property builder is only the stat block; set lines are concatenated later,
// so the socket list has to be added on this call, just before the text is drawn.
// 1.13c buffer is 0x800 wchars. 1.13d: D2Client+0x9775B.
Patch* tooltipSocketStatsPatch1 = new Patch(Call, D2CLIENT, { 0x9338B, 0x9775B }, (int)TooltipDraw_Intercept_800, 5);
// Second hover layout. Both sites draw the same 0x400-wchar buffer.
// 1.13d: D2Client+0x97F89 and D2Client+0x988FA.
Patch* tooltipSocketStatsPatch2 = new Patch(Call, D2CLIENT, { 0x93BB9, 0x97F89 }, (int)TooltipDraw_Intercept_400, 5);
Patch* tooltipSocketStatsPatch3 = new Patch(Call, D2CLIENT, { 0x9452A, 0x988FA }, (int)TooltipDraw_Intercept_400, 5);
// D2Win tooltip draw thunk replaced by the patches above. Set in OnLoad.
static DWORD tooltipDrawAddr = 0;
static int tooltipTextCap = 0x400;
// Hover text is capped by D2Win. Extra lines stay on later pages instead of being dropped.
static DWORD g_tooltipPageUnitId = 0;
static DWORD g_tooltipPageType = 0xFFFFFFFF;
static int g_tooltipPage = 0;
static int g_tooltipPageCount = 1;
// Parent hover tooltip box, measured while that tooltip is built. 0 tick means it is not showing.
static int g_itemTipW = 0;
static int g_itemTipH = 0;
static int g_itemTipFont = 0;
static DWORD g_itemTipTick = 0;
// Checked: the separate tooltip. Unchecked: the same stats on the item tooltip.
static bool g_socketStatsOnTooltip = false;

static bool PageHoveredTooltip(bool up, BYTE key, LPARAM lParam, bool* block) {
	if (key != VK_LEFT && key != VK_RIGHT)
		return false;
	if (g_tooltipPageCount <= 1)
		return false;
	UnitAny* hovered = p_D2CLIENT_SelectedInvItem ? *p_D2CLIENT_SelectedInvItem : nullptr;
	if (!hovered || hovered->dwUnitId != g_tooltipPageUnitId || hovered->dwType != g_tooltipPageType)
		return false;
	*block = true;
	// Bit 30 is set when the key was already down, so holding it does not skip pages.
	if (up || (lParam & (1 << 30)))
		return true;
	if (key == VK_RIGHT && g_tooltipPage + 1 < g_tooltipPageCount)
		g_tooltipPage++;
	else if (key == VK_LEFT && g_tooltipPage > 0)
		g_tooltipPage--;
	return true;
}
Patch* viewInvPatch1 = new Patch(Call, D2CLIENT, { 0x953E2, 0x997B2 }, (int)ViewInventoryPatch1_ASM, 6);
Patch* viewInvPatch2 = new Patch(Call, D2CLIENT, { 0x94AB4, 0x98E84 }, (int)ViewInventoryPatch2_ASM, 6);
Patch* viewInvPatch3 = new Patch(Call, D2CLIENT, { 0x93A6F, 0x97E3F }, (int)ViewInventoryPatch3_ASM, 5);

//ported to 1.13c/d from https://github.com/jieaido/d2hackmap/blob/master/PermShowItem.cpp
Patch* permShowItems1 = new Patch(Call, D2CLIENT, { 0xC3D4E, 0x1D74E }, (int)PermShowItemsPatch1_ASM, 6);
Patch* permShowItems2 = new Patch(Call, D2CLIENT, { 0xC0E9A, 0x1A89A }, (int)PermShowItemsPatch1_ASM, 6);
Patch* permShowItems3 = new Patch(Call, D2CLIENT, { 0x59483, 0x4EA13 }, (int)PermShowItemsPatch2_ASM, 6);
Patch* permShowItems4 = new Patch(Call, D2CLIENT, { 0x5908A, 0x4E61A }, (int)PermShowItemsPatch3_ASM, 6);
Patch* permShowItems5 = new Patch(Call, D2CLIENT, { 0xA6BA3, 0x63443 }, (int)PermShowItemsPatch4_ASM, 6);

using namespace Drawing;

void Item::OnLoad() {
	LoadConfig();

	viewInvPatch1->Install();
	viewInvPatch2->Install();
	viewInvPatch3->Install();

	permShowItems1->Install();
	permShowItems2->Install();
	permShowItems3->Install();
	permShowItems4->Install();
	permShowItems5->Install();

	tooltipDrawAddr = (DWORD)Patch::GetDllOffset(D2CLIENT,
		D2Version::GetGameVersionID() == VERSION_113d ? 0xD2A4 : 0xD3B4);

	itemPropertiesPatch->Install();
	itemPropertyStringDamagePatch->Install();
	itemPropertyStringPatch->Install();
	skipSocketedStatMergePatch->Install();
	tooltipSocketStatsPatch1->Install();
	tooltipSocketStatsPatch2->Install();
	tooltipSocketStatsPatch3->Install();

	if (Toggles["Show Ethereal"].state || Toggles["Show Sockets"].state || Toggles["Show iLvl"].state || Toggles["Color Mod"].state ||
		Toggles["Show Rune Numbers"].state || Toggles["Alt Item Style"].state || Toggles["Shorten Item Names"].state || Toggles["Advanced Item Display"].state)
		itemNamePatch->Install();

	DrawSettings();
}

void ResetCaches() {
	item_desc_cache.ResetCache();
	item_name_cache.ResetCache();
	map_action_cache.ResetCache();
	do_not_block_cache.ResetCache();
	ignore_cache.ResetCache();
}


struct ItemStatAnnouncement {
	int statIds[4];
	int numStats;
	int layer;
	const char* message;
};

static const ItemStatAnnouncement g_announcements[] = {
	// CTC Procs: 376=on melee attack, 407=on kill, 375=when struck, 406=on striking
	{{376, 407, 375, 406}, 4, 23364, "Empower: +5 all skills"},
	{{376, 407, 375, 406}, 4, 23236, "Diamond Skin: +35% magic resistance, +6000 defense"},
	{{376, 407, 375, 406}, 4, 23300, "Clarity: 300% increased mana recovery, 20% faster cast rate"},
	{{376, 407, 375, 406}, 4, 23428, "Conduction: +20% lightning damage, 20% lightning pierce, 20% faster cast rate"},
	{{376, 407, 375, 406}, 4, 23492, "Avalanche: 20% increased attack speed, 30% cold damage"},
	{{376, 407, 375, 406}, 4, 23556, "Hellfire: 40% fire damage, 15% fire absorb"},
	{{376, 407, 375, 406}, 4, 23620, "Brutality: 40% crushing blow, 100% Enhanced Damage, +40 stun duration"},
	{{376, 407, 375, 406}, 4, 23684, "Purity: 50% fire/cold/lit res, 10% fire/cold/lit max res"},
	{{376, 407, 375, 406}, 4, 23748, "Corruption: 40% poison damage, 20% magic pierce"},
	{{376, 407, 375, 406}, 4, 23812, "Carnage: 400% enhanced damage"},
	{{376, 407, 375, 406}, 4, 23876, "Resurgence: +3 all skills, 15% IAS, 100% enhanced damage"},
	{{376, 407, 375, 406}, 4, 23940, "Grace: 80% FHR, +1000 AR, 33% deadly strike, 80% FBR"},
	{{376, 407, 375, 406}, 4, 24004, "Aegis: 60% FBR, 30% block, 50% edef, 25% phys res"},
	{{376, 407, 375, 406}, 4, 24068, "Blood Rage: 200% AR, 15% life leech, PMH, 30% IAS"},
	{{376, 407, 375, 406}, 4, 24196, "Good Fortune: 200% MF, 500% GF"},
	{{376, 407, 375, 406}, 4, 24260, "Haste: 40% movement velocity, +10 weapon speed"},
	{{376, 407, 375, 406}, 4, 37380, "Elemental Adaption: 1% ele res per 1% ele mastery"},
	{{376, 407, 375, 406}, 4, 37444, "Elemental Volatility: 1% ele pierce per 4% ele mastery"},
	{{376, 407, 375, 406}, 4, 38020, "Elemental Destruction: 1% ele mastery per 4% ele res"},
	{{376, 407, 375, 406}, 4, 24322, "Fire Immunity (2 seconds)"},
	{{376, 407, 375, 406}, 4, 24386, "Cold Immunity (2 seconds)"},
	{{376, 407, 375, 406}, 4, 24450, "Lightning Immunity (2 seconds)"},

	// CTC Skills: 195=on melee attack, 196=on kill, 201=when struck, 198=on striking
	//{{195, 196, 201, 198}, 4, 25310, "CTC Chain Lightning"},
	//{{195, 196, 201, 198}, 4, 25950, "CTC Poison Nova"},
	//{{195, 196, 201, 198}, 4, 25758, "CTC Frozen Orb"},
	//{{195, 196, 201, 198}, 4, 25438, "CTC Meteor"},

	// Auras (stat 151)
	{{151, 0, 0, 0}, 1, 530, "Enchant Fire: 100-200 fire dmg/lvl. Aura mastery. 24 radius"},
	{{151, 0, 0, 0}, 1, 531, "Enchant Cold: 100-200 cold dmg/lvl. Aura mastery. 24 radius"},
	{{151, 0, 0, 0}, 1, 532, "Enchant Lightning: 100-200 lit dmg/lvl. Aura mastery. 24 radius"},
	{{151, 0, 0, 0}, 1, 537, "Enchant Bones: 1% CB/lvl, 20% ED/lvl, 4 PDR/lvl. 24 radius"},
	{{151, 0, 0, 0}, 1, 539, "Enchant Vigor: 1 wep speed/lvl, 2 velocity/lvl. 24 radius"},
	{{151, 0, 0, 0}, 1, 419, "Pestilence: -8% enemy psn res, -1%/lvl; 150/lvl psn dmg/s. 18 radius"},

	// Mercenary-only (stat 470, layer 0)
	{{470, 0, 0, 0}, 1, 0, "Mercenary Only: Lowers Life, Mana, Stamina on non-mercs"},
};

static const int g_numAnnouncements = sizeof(g_announcements) / sizeof(g_announcements[0]);

void Item::AnnounceHoveredItemStats() {
	if (!IsGameReady()) return;

	UnitAny* item = *p_D2CLIENT_SelectedInvItem;

	if (!item || item->dwType != UNIT_ITEM) {
		UnitAny* selected = D2CLIENT_GetSelectedUnit();
		if (selected && selected->dwType == UNIT_ITEM)
			item = selected;
	}

	if (!item || item->dwType != UNIT_ITEM) return;

	for (int a = 0; a < g_numAnnouncements; a++) {
		const ItemStatAnnouncement& ann = g_announcements[a];
		for (int s = 0; s < ann.numStats; s++) {
			if (D2COMMON_GetUnitStat(item, ann.statIds[s], ann.layer) > 0) {
				PrintText(Gold, "%s", ann.message);
				break;
			}
		}
	}
}

static void ClearIronGolemSlot(UnitAny* live);

void Item::OnGameJoin() {
	ClearIronGolemSlot(nullptr);
	viewingUnit = NULL;
	// reset the item name cache upon joining games
	// (GUIDs not unique across games)
	ResetCaches();
	OnLoop();
	//if (ItemDisplay::UntestedSettingsUsed()) {
	//	PrintText(10, "Warning - using experimental config settings");
	//}
}

void Item::LoadConfig() {
	BH::config->ReadToggle("Show Ethereal", "None", true, Toggles["Show Ethereal"]);
	BH::config->ReadToggle("Show Sockets", "None", true, Toggles["Show Sockets"]);
	BH::config->ReadToggle("Show ILvl", "None", true, Toggles["Show iLvl"]);
	BH::config->ReadToggle("Show Rune Numbers", "None", true, Toggles["Show Rune Numbers"]);
	BH::config->ReadToggle("Alt Item Style", "None", true, Toggles["Alt Item Style"]);
	BH::config->ReadToggle("Color Mod", "None", false, Toggles["Color Mod"]);
	BH::config->ReadToggle("Shorten Item Names", "None", false, Toggles["Shorten Item Names"]);
	BH::config->ReadToggle("Always Show Items", "None", false, Toggles["Always Show Items"]);
	BH::config->ReadToggle("Advanced Item Display", "None", false, Toggles["Advanced Item Display"]);
	BH::config->ReadToggle("Item Drop Notifications", "None", false, Toggles["Item Drop Notifications"]);
	BH::config->ReadToggle("Item Close Notifications", "None", false, Toggles["Item Close Notifications"]);
	BH::config->ReadToggle("Item Detailed Notifications", "None", false, Toggles["Item Detailed Notifications"]);
	BH::config->ReadToggle("Verbose Notifications", "None", false, Toggles["Verbose Notifications"]);
	BH::config->ReadToggle("Allow Unknown Items", "None", false, Toggles["Allow Unknown Items"]);
	BH::config->ReadToggle("Suppress Invalid Stats", "None", false, Toggles["Suppress Invalid Stats"]);
	BH::config->ReadToggle("Always Show Item Stat Ranges", "None", true, Toggles["Always Show Item Stat Ranges"]);
	BH::config->ReadToggle("Separate Socketed Stats", "VK_SHIFT", false, Toggles["Separate Socketed Stats"]);
	BH::config->ReadBoolean("Socketed Stats On Tooltip", g_socketStatsOnTooltip);
	BH::config->ReadInt("Filter Level", filterLevelSetting);
	BH::config->ReadInt("Ping Level", pingLevelSetting);
	BH::config->ReadInt("Run Details Ping Level", trackerPingLevelSetting);
	BH::config->ReadInt("Stat Range Color", statRangeColor);

	LoadNoIlvlCodes();

	ItemDisplay::UninitializeItemRules();

	//InitializeMPQData();

	BH::config->ReadKey("Show Players Gear", "VK_0", showPlayer);
	BH::config->ReadKey("Announce Item Stats", "VK_BACKTICK", announceStatKey);
}

void Item::LoadNoIlvlCodes() {
	// this method does not support saving back to the file
	vector<pair<string, string>> no_ilvls;

	BH::itemConfig->ReadMapList("No Item Level", no_ilvls);

	no_ilvl_codes.clear();

	string buf;
	for (auto & entry: no_ilvls) {
		stringstream ss(entry.second);
		while (ss >> buf) {
			no_ilvl_codes.insert(buf);
		}
	}
}

void Item::ResetPatches() {
	//todo figure out a way to not have to install/remove the patches onloop
	//we only remove it because one of them will break being able to not
	//target monsters with your normal show items key.
	if (Toggles["Always Show Items"].state) {
		permShowItems1->Install();
		permShowItems2->Install();
		permShowItems3->Install();
		permShowItems4->Install();
		permShowItems5->Install();
	} else {
		permShowItems1->Remove();
		permShowItems2->Remove();
		permShowItems3->Remove();
		permShowItems4->Remove();
		permShowItems5->Remove();
	}
}

void Item::DrawSettings() {
	settingsTab = new UITab("Item", BH::settingsUI);
	int y = 10;
	int keyhook_x = 230;

	// looking to deprecate all/most settings that don't use advanced configs now
	/* new Checkhook(settingsTab, 4, y, &Toggles["Show Ethereal"].state, "Show Ethereal"); */
	/* new Keyhook(settingsTab, keyhook_x, y+2, &Toggles["Show Ethereal"].toggle, ""); */
	/* y += 15; */

	/* new Checkhook(settingsTab, 4, y, &Toggles["Show Sockets"].state, "Show Sockets"); */
	/* new Keyhook(settingsTab, keyhook_x, y+2, &Toggles["Show Sockets"].toggle, ""); */
	/* y += 15; */

	new Checkhook(settingsTab, 4, y, &Toggles["Show iLvl"].state, "Show iLvl");
	new Keyhook(settingsTab, keyhook_x, y+2, &Toggles["Show iLvl"].toggle, "");
	y += 15;

	// looking to deprecate all/most settings that don't use advanced configs now
	/* new Checkhook(settingsTab, 4, y, &Toggles["Show Rune Numbers"].state, "Show Rune #"); */
	/* new Keyhook(settingsTab, keyhook_x, y+2, &Toggles["Show Rune Numbers"].toggle, ""); */
	/* y += 15; */

	/* new Checkhook(settingsTab, 4, y, &Toggles["Alt Item Style"].state, "Alt Style"); */
	/* new Keyhook(settingsTab, keyhook_x, y+2, &Toggles["Alt Item Style"].toggle, ""); */
	/* y += 15; */

	/* new Checkhook(settingsTab, 4, y, &Toggles["Color Mod"].state, "Color Mod"); */
	/* new Keyhook(settingsTab, keyhook_x, y+2, &Toggles["Color Mod"].toggle, ""); */
	/* y += 15; */

	/* new Checkhook(settingsTab, 4, y, &Toggles["Shorten Item Names"].state, "Shorten Names"); */
	/* new Keyhook(settingsTab, keyhook_x, y+2, &Toggles["Shorten Item Names"].toggle, ""); */
	/* y += 15; */

	new Checkhook(settingsTab, 4, y, &Toggles["Always Show Items"].state, "Always Show Items");
	new Keyhook(settingsTab, keyhook_x, y + 2, &Toggles["Always Show Items"].toggle, "");
	y += 15;

	new Checkhook(settingsTab, 4, y, &Toggles["Always Show Item Stat Ranges"].state, "Always Show Item Stat Ranges");
	new Keyhook(settingsTab, keyhook_x, y+2, &Toggles["Always Show Item Stat Ranges"].toggle, "");
	y += 15;

	new Checkhook(settingsTab, 4, y, &Toggles["Separate Socketed Stats"].state, "Split Socketed Stats");
	new Keyhook(settingsTab, keyhook_x, y+2, &Toggles["Separate Socketed Stats"].toggle, "");
	y += 13;

	new Checkhook(settingsTab, 22, y, &g_socketStatsOnTooltip, "Show Beside the Item");
	y += 15;
	
	new Checkhook(settingsTab, 4, y, &Toggles["Advanced Item Display"].state, "Advanced Item Display");
	new Keyhook(settingsTab, keyhook_x, y+2, &Toggles["Advanced Item Display"].toggle, "");
	y += 15;

	new Checkhook(settingsTab, 4, y, &Toggles["Item Drop Notifications"].state, "Item Drop Notifications");
	new Keyhook(settingsTab, keyhook_x, y+2, &Toggles["Item Drop Notifications"].toggle, "");
	y += 15;

	new Checkhook(settingsTab, 4, y, &Toggles["Item Close Notifications"].state, "Item Close Notifications");
	new Keyhook(settingsTab, keyhook_x, y+2, &Toggles["Item Close Notifications"].toggle, "");
	y += 15;

	new Checkhook(settingsTab, 4, y, &Toggles["Item Detailed Notifications"].state, "Item Detailed Notifications");
	new Keyhook(settingsTab, keyhook_x, y + 2, &Toggles["Item Detailed Notifications"].toggle, "");
	y += 15;

	new Checkhook(settingsTab, 4, y, &Toggles["Verbose Notifications"].state, "Verbose Notifications");
	new Keyhook(settingsTab, keyhook_x, y+2, &Toggles["Verbose Notifications"].toggle, "");
	y += 15;

	new Checkhook(settingsTab, 4, y, &Toggles["Suppress Invalid Stats"].state, "Suppress Invalid Stats");
	new Keyhook(settingsTab, keyhook_x, y+2, &Toggles["Suppress Invalid Stats"].toggle, "");
	y += 15;
	
	new Keyhook(settingsTab, 4, y+2, &showPlayer, "Show Player's Gear:   ");
	y += 15;

	new Keyhook(settingsTab, 4, y+2, &announceStatKey, "Announce Item Stats:  ");
	y += 15;

	new Texthook(settingsTab, 4, y, "\377c4Filter Level:");

	vector<string> options;
	options.push_back("\377c00 - None");
	options.push_back("\377c01 - Minimal");
	options.push_back("\377c02 - Moderate");
	options.push_back("\377c03 - Aggressive");
	new Combohook(settingsTab, 85, y, 120, &filterLevelSetting, options);

	new Texthook(settingsTab, 234, y, "\377c4Ping Tiers <=:");

	vector<string> ping_options;
	ping_options.push_back("\377c00");
	ping_options.push_back("\377c01");
	ping_options.push_back("\377c02");
	ping_options.push_back("\377c03");
	ping_options.push_back("\377c04");
	ping_options.push_back("\377c05");
	ping_options.push_back("\377c06");
	new Combohook(settingsTab, 330, y, 40, &pingLevelSetting, ping_options);
}

void Item::OnUnload() {
	itemNamePatch->Remove();
	itemPropertiesPatch->Remove();
	itemPropertyStringDamagePatch->Remove();
	itemPropertyStringPatch->Remove();
	skipSocketedStatMergePatch->Remove();
	tooltipSocketStatsPatch1->Remove();
	tooltipSocketStatsPatch2->Remove();
	tooltipSocketStatsPatch3->Remove();
	viewInvPatch1->Remove();
	viewInvPatch2->Remove();
	viewInvPatch3->Remove();
	permShowItems1->Remove();
	permShowItems2->Remove();
	permShowItems3->Remove();
	permShowItems4->Remove();
	permShowItems5->Remove();
	ItemDisplay::UninitializeItemRules();
}

static const DWORD kInventorySignature = 0x01020304;
static int g_golemSlotIndex = -1;
static UnitAny* g_golemItem = nullptr;
void __fastcall D2CLIENT_GetItemDesc(UnitAny* pItem, wchar_t* buffer);

static bool IsGearInspectTarget(UnitAny* unit) {
	if (!unit || unit->dwMode == 0 || unit->dwMode == 17)
		return false;
	if (unit->dwType == UNIT_PLAYER)
		return true;
	if (unit->dwType != UNIT_MONSTER)
		return false;
	DWORD id = unit->dwTxtFileNo;
	return id == 291 || id == 357 || id == 418; // Iron Golem, Valkyrie, Shadow Master
}

// Unsummon highlights your iron golem without always making GetSelectedUnit return it.
static UnitAny* IronGolemUnderCursor() {
	UnitAny* player = D2CLIENT_GetPlayerUnit();
	if (!player || !player->pAct || !player->pAct->pRoom1 || !p_D2CLIENT_MouseX || !p_D2CLIENT_MouseY)
		return nullptr;

	int mouseX = (int)*p_D2CLIENT_MouseX;
	int mouseY = (int)*p_D2CLIENT_MouseY;
	UnitAny* best = nullptr;
	int bestScore = 0x7fffffff;

	for (Room1* room = player->pAct->pRoom1; room; room = room->pRoomNext) {
		for (UnitAny* unit = room->pUnitFirst; unit; unit = unit->pListNext) {
			if (unit->dwType != UNIT_MONSTER || unit->dwTxtFileNo != 291 || !unit->pPath)
				continue;
			if (!IsGearInspectTarget(unit))
				continue;

			long x = D2CLIENT_GetUnitX(unit);
			long y = D2CLIENT_GetUnitY(unit);
			D2COMMON_MapToAbsScreen(&x, &y);
			x -= (long)D2CLIENT_GetMouseXOffset();
			y -= (long)D2CLIENT_GetMouseYOffset();

			int dx = mouseX - (int)x;
			int dy = mouseY - (int)y;
			// Screen point is the feet. The sprite sits above that.
			if (dx < -70 || dx > 70 || dy < -130 || dy > 30)
				continue;

			int score = dx * dx + dy * dy;
			if (unit->dwOwnerType == UNIT_PLAYER && unit->dwOwnerId == player->dwUnitId)
				score -= 20000;
			if (score < bestScore) {
				bestScore = score;
				best = unit;
			}
		}
	}
	return best;
}

static UnitAny** IronGolemBodySlots(UnitAny* golem, int* count) {
	*count = 0;
	if (!golem || !golem->pInventory)
		return nullptr;
	Inventory* inv = golem->pInventory;
	if (inv->dwSignature != kInventorySignature || !inv->pStores || inv->dwStoresCount < 1)
		return nullptr;

	InventoryStore* grid = &inv->pStores[0];
	// Body locations are a 13 by 1 grid. Anything else is not the paper doll.
	if (!grid->pArray || grid->Width != 13 || grid->Height != 1)
		return nullptr;
	*count = 13;
	return (UnitAny**)grid->pArray;
}

static UnitAny* IronGolemSourceItem(UnitAny* golem) {
	if (!golem || !golem->pInventory)
		return nullptr;
	Inventory* inv = golem->pInventory;
	for (UnitAny* item = inv->pFirstItem; item; ) {
		if (item->dwType == UNIT_ITEM)
			return item;
		if (!item->pItemData)
			break;
		item = item->pItemData->pNextInvItem;
	}
	if (inv->pCursorItem && inv->pCursorItem->dwType == UNIT_ITEM)
		return inv->pCursorItem;
	return nullptr;
}

static bool IronGolemItemInBodySlot(UnitAny* golem, UnitAny* item, int* slotOut) {
	int count = 0;
	UnitAny** slots = IronGolemBodySlots(golem, &count);
	if (!slots || !item)
		return false;
	for (int i = 1; i < count; ++i) {
		if (slots[i] == item) {
			if (slotOut)
				*slotOut = i;
			return true;
		}
	}
	return false;
}

// Only clears a slot this inspect session filled. A null unit means the golem is already gone.
static void ClearIronGolemSlot(UnitAny* live) {
	if (g_golemSlotIndex >= 0 && g_golemItem && live) {
		int count = 0;
		UnitAny** slots = IronGolemBodySlots(live, &count);
		if (slots && g_golemSlotIndex < count && slots[g_golemSlotIndex] == g_golemItem)
			slots[g_golemSlotIndex] = nullptr;
	}
	g_golemSlotIndex = -1;
	g_golemItem = nullptr;
}

// The paper doll draws body-grid slots. The golem item is often only in the inventory list.
static void ShowIronGolemItem(UnitAny* golem) {
	UnitAny* item = IronGolemSourceItem(golem);
	int count = 0;
	UnitAny** slots = IronGolemBodySlots(golem, &count);
	if (!item || !slots) {
		ClearIronGolemSlot(golem);
		return;
	}

	int found = -1;
	for (int i = 1; i < count; ++i) {
		if (slots[i] == item) {
			found = i;
			break;
		}
	}
	if (found >= 0) {
		if (g_golemSlotIndex >= 0 && g_golemSlotIndex != found)
			ClearIronGolemSlot(golem);
		return;
	}

	int slot = 0;
	if (item->pItemData) {
		int body = item->pItemData->BodyLocation;
		if (body >= 1 && body < count && !slots[body])
			slot = body;
	}
	if (!slot) {
		const int prefer[] = {
			EQUIP_BODY, EQUIP_RIGHT_PRIMARY, EQUIP_LEFT_PRIMARY,
			EQUIP_HEAD, EQUIP_FEET, EQUIP_GLOVES, EQUIP_BELT
		};
		for (int i = 0; i < (int)(sizeof(prefer) / sizeof(prefer[0])); ++i) {
			int body = prefer[i];
			if (body < count && !slots[body]) {
				slot = body;
				break;
			}
		}
	}
	if (!slot || slots[slot])
		return;

	if (g_golemSlotIndex >= 0 && g_golemSlotIndex != slot)
		ClearIronGolemSlot(golem);

	slots[slot] = item;
	g_golemSlotIndex = slot;
	g_golemItem = item;
}

static UnitAny* LiveViewUnit(UnitAny* unit) {
	if (!unit)
		return nullptr;
	UnitAny* live = D2CLIENT_FindServerSideUnit(unit->dwUnitId, unit->dwType);
	if (!live)
		live = D2CLIENT_FindClientSideUnit(unit->dwUnitId, unit->dwType);
	return live;
}

static void DrawIronGolemFallback(UnitAny* unit) {
	if (!unit || unit->dwType != UNIT_MONSTER || unit->dwTxtFileNo != 291)
		return;
	if (!D2CLIENT_GetUIState(0x01))
		return;

	UnitAny* item = IronGolemSourceItem(unit);
	if (item && IronGolemItemInBodySlot(unit, item, nullptr))
		return;

	int x = *p_D2CLIENT_PanelOffsetX + 160 + 320;
	int y = 190;
	DWORD oldFont = D2WIN_SetTextSize(1);
	if (!item) {
		const wchar_t* missing = L"No item";
		DWORD width = 0;
		DWORD height = 0;
		D2WIN_GetTextSize((wchar_t*)missing, &width, &height);
		D2WIN_DrawText((wchar_t*)missing, x - (int)width / 2, y, White, 0);
		D2WIN_SetTextSize(oldFont);
		return;
	}

	wchar_t name[256];
	name[0] = 0;
	D2CLIENT_GetItemName(item, name, 256);
	if (name[0]) {
		DWORD width = 0;
		DWORD height = 0;
		D2WIN_GetTextSize(name, &width, &height);
		D2WIN_DrawText(name, x - (int)width / 2, y, Gold, 0);
		y += 16;
	}

	wchar_t desc[0x400];
	desc[0] = 0;
	D2CLIENT_GetItemDesc(item, desc);
	wchar_t* line = desc;
	int lines = 0;
	while (*line && lines < 14) {
		wchar_t* nl = wcschr(line, L'\n');
		if (nl)
			*nl = 0;
		if (line[0]) {
			DWORD width = 0;
			DWORD height = 0;
			D2WIN_GetTextSize(line, &width, &height);
			D2WIN_DrawText(line, x - (int)width / 2, y, White, 0);
			y += 13;
			lines++;
		}
		if (!nl)
			break;
		line = nl + 1;
	}
	D2WIN_SetTextSize(oldFont);
}

void Item::OnLoop() {
	ResetPatches();
	static unsigned int localFilterLevel = 0;
	static unsigned int localPingLevel = 0;
	// This is a bit of a hack to reset the cache when the user changes the item filter level
	if (localFilterLevel != filterLevelSetting) {
		ResetCaches();
		localFilterLevel = filterLevelSetting;
	}
	if (localPingLevel != pingLevelSetting) {
		ResetCaches();
		localPingLevel = pingLevelSetting;
	}
	if (!D2CLIENT_GetUIState(0x01)) {
		ClearIronGolemSlot(LiveViewUnit(viewingUnit));
		viewingUnit = NULL;
	}
	
	if (Toggles["Advanced Item Display"].state) {
		ItemDisplay::InitializeItemRules();
	}

	if (viewingUnit && viewingUnit->dwUnitId) {
		UnitAny* live = LiveViewUnit(viewingUnit);
		if (!live || !live->pInventory) {
			ClearIronGolemSlot(nullptr);
			viewingUnit = NULL;
			D2CLIENT_SetUIVar(0x01, 1, 0);
		} else {
			viewingUnit = live;
			if (live->dwType == UNIT_MONSTER && live->dwTxtFileNo == 291)
				ShowIronGolemItem(live);
			else
				ClearIronGolemSlot(live);
		}
	}
}

void Item::OnKey(bool up, BYTE key, LPARAM lParam, bool* block) {
	if (PageHoveredTooltip(up, key, lParam, block))
		return;
	if (key == announceStatKey) {
		*block = true;
		if (up) return;
		AnnounceHoveredItemStats();
		return;
	}
	if (key == showPlayer) {
		*block = true;
		if (up)
			return;
		UnitAny* selectedUnit = D2CLIENT_GetSelectedUnit();
		if (!IsGearInspectTarget(selectedUnit))
			selectedUnit = IronGolemUnderCursor();
		if (IsGearInspectTarget(selectedUnit)) {
			viewingUnit = selectedUnit;
			if (!D2CLIENT_GetUIState(0x01))
				D2CLIENT_SetUIVar(0x01, 0, 0);
			if (selectedUnit->dwType == UNIT_MONSTER && selectedUnit->dwTxtFileNo == 291)
				ShowIronGolemItem(selectedUnit);
			return;
		}
	}
	for (map<string,Toggle>::iterator it = Toggles.begin(); it != Toggles.end(); it++) {
		if (key == (*it).second.toggle) {
			// Hold these keys to preview socketed stats. Releasing restores the normal tooltip.
			if ((*it).first == "Separate Socketed Stats")
				continue;
			*block = true;
			if (up) {
				(*it).second.state = !(*it).second.state;
			}
			return;
		}
	}
}

void Item::OnLeftClick(bool up, unsigned int x, unsigned int y, bool* block) {
	if (up)
		return;
	if (D2CLIENT_GetUIState(0x01) && viewingUnit != NULL && x >= 400)
		*block = true;
}

int CreateUnitItemInfo(UnitItemInfo *uInfo, UnitAny *item) {
	char* code = D2COMMON_GetItemText(item->dwTxtFileNo)->szCode;
	uInfo->itemCode[0] = code[0]; uInfo->itemCode[1] = code[1]; uInfo->itemCode[2] = code[2]; uInfo->itemCode[3] = 0;
	uInfo->item = item;
	if (ItemAttributeMap.find(uInfo->itemCode) != ItemAttributeMap.end()) {
		uInfo->attrs = ItemAttributeMap[uInfo->itemCode];
		return 0;
	} else {
		return -1;
	}
}

void __fastcall Item::ItemNamePatch(wchar_t *name, UnitAny *item)
{
	char* szName = UnicodeToAnsi(name);
	string itemName = szName;
	char* code = D2COMMON_GetItemText(item->dwTxtFileNo)->szCode;

	if (Toggles["Advanced Item Display"].state) {
		UnitItemInfo uInfo;
		if (!CreateUnitItemInfo(&uInfo, item)) {
			GetItemName(&uInfo, itemName);
		} else {
			HandleUnknownItemCode(uInfo.itemCode, "name");
		}
	} else {
		OrigGetItemName(item, itemName, code);
	}

	// Some common color codes for text strings (see TextColor enum):
	// \xFF" "c; (purple)
	// \xFF" "c0 (white)
	// \xFF" "c1 (red)
	// \xFF" "c2 (green)
	// \xFF" "c3 (blue)
	// \xFF" "c4 (gold)
	// \xFF" "c5 (gray)
	// \xFF" "c6 (black)
	// \xFF" "c7 (tan)
	// \xFF" "c8 (orange)
	// \xFF" "c9 (yellow)

	/* Test code to display item codes */
	//string test3 = test_code;
	//itemName += " {" + test3 + "}";

	MultiByteToWideChar(CODE_PAGE, MB_PRECOMPOSED, itemName.c_str(), itemName.length(), name, itemName.length());
	name[itemName.length()] = 0;  // null-terminate the string since MultiByteToWideChar doesn't
	delete[] szName;
}

void Item::OrigGetItemName(UnitAny *item, string &itemName, char *code)
{
	bool displayItemLevel = Toggles["Show iLvl"].state;
	if (Toggles["Shorten Item Names"].state)
	{
		// We will also strip ilvls from these items
		if (code[0] == 't' && code[1] == 's' && code[2] == 'c')  // town portal scroll
		{
			itemName = "\xFF" "c2**\xFF" "c0TP";
			displayItemLevel = false;
		}
		else if (code[0] == 'i' && code[1] == 's' && code[2] == 'c')  // identify scroll
		{
			itemName = "\xFF" "c2**\xFF" "c0ID";
			displayItemLevel = false;
		}
		else if (code[0] == 'v' && code[1] == 'p' && code[2] == 's')  // stamina potion
		{
			itemName = "Stam";
			displayItemLevel = false;
		}
		else if (code[0] == 'y' && code[1] == 'p' && code[2] == 's')  // antidote potion
		{
			itemName = "Anti";
			displayItemLevel = false;
		}
		else if (code[0] == 'w' && code[1] == 'm' && code[2] == 's')  // thawing potion
		{
			itemName = "Thaw";
			displayItemLevel = false;
		}
		else if (code[0] == 'g' && code[1] == 'p' && code[2] == 's')  // rancid gas potion
		{
			itemName = "Ranc";
			displayItemLevel = false;
		}
		else if (code[0] == 'o' && code[1] == 'p' && code[2] == 's')  // oil potion
		{
			itemName = "Oil";
			displayItemLevel = false;
		}
		else if (code[0] == 'g' && code[1] == 'p' && code[2] == 'm')  // choking gas potion
		{
			itemName = "Chok";
			displayItemLevel = false;
		}
		else if (code[0] == 'o' && code[1] == 'p' && code[2] == 'm')  // exploding potion
		{
			itemName = "Expl";
			displayItemLevel = false;
		}
		else if (code[0] == 'g' && code[1] == 'p' && code[2] == 'l')  // strangling gas potion
		{
			itemName = "Stra";
			displayItemLevel = false;
		}
		else if (code[0] == 'o' && code[1] == 'p' && code[2] == 'l')  // fulminating potion
		{
			itemName = "Fulm";
			displayItemLevel = false;
		}
		else if (code[0] == 'h' && code[1] == 'p')  // healing potions
		{
			if (code[2] == '1')
			{
				itemName = "\xFF" "c1**\xFF" "c0Min Heal";
				displayItemLevel = false;
			}
			else if (code[2] == '2')
			{
				itemName = "\xFF" "c1**\xFF" "c0Lt Heal";
				displayItemLevel = false;
			}
			else if (code[2] == '3')
			{
				itemName = "\xFF" "c1**\xFF" "c0Heal";
				displayItemLevel = false;
			}
			else if (code[2] == '4')
			{
				itemName = "\xFF" "c1**\xFF" "c0Gt Heal";
				displayItemLevel = false;
			}
			else if (code[2] == '5')
			{
				itemName = "\xFF" "c1**\xFF" "c0Sup Heal";
				displayItemLevel = false;
			}
		}
		else if (code[0] == 'm' && code[1] == 'p')  // mana potions
		{
			if (code[2] == '1')
			{
				itemName = "\xFF" "c3**\xFF" "c0Min Mana";
				displayItemLevel = false;
			}
			else if (code[2] == '2')
			{
				itemName = "\xFF" "c3**\xFF" "c0Lt Mana";
				displayItemLevel = false;
			}
			else if (code[2] == '3')
			{
				itemName = "\xFF" "c3**\xFF" "c0Mana";
				displayItemLevel = false;
			}
			else if (code[2] == '4')
			{
				itemName = "\xFF" "c3**\xFF" "c0Gt Mana";
				displayItemLevel = false;
			}
			else if (code[2] == '5')
			{
				itemName = "\xFF" "c3**\xFF" "c0Sup Mana";
				displayItemLevel = false;
			}
		}
		else if (code[0] == 'r' && code[1] == 'v')  // rejuv potions
		{
			if (code[2] == 's')
			{
				itemName = "\xFF" "c;**\xFF" "c0Rejuv";
				displayItemLevel = false;
			}
			else if (code[2] == 'l')
			{
				itemName = "\xFF" "c;**\xFF" "c0Full";
				displayItemLevel = false;
			}
		}
		else if (code[1] == 'q' && code[2] == 'v')
		{
			if (code[0] == 'a')  // arrows
			{
				displayItemLevel = false;
			}
			else if (code[0] == 'c')  // bolts
			{
				displayItemLevel = false;
			}
		}
		else if (code[0] == 'k' && code[1] == 'e' && code[2] == 'y')  // key
		{
			displayItemLevel = false;
		}
	}

	/*Suffix Color Mod*/
	if( Toggles["Color Mod"].state )
	{
		/*Essences*/
		if( code[0] == 't' && code[1] == 'e' && code[2] == 's' )
		{
			itemName = itemName + " (Andariel/Duriel)";
		}
		if( code[0] == 'c' && code[1] == 'e' && code[2] == 'h' )
		{
			itemName = itemName + " (Mephtisto)";
		}
		if( code[0] == 'b' && code[1] == 'e' && code[2] == 't' )
		{
			itemName = itemName + " (Diablo)";
		}
		if( code[0] == 'f' && code[1] == 'e' && code[2] == 'd' )
		{
			itemName = itemName + " (Baal)";
		}
	}

	if( Toggles["Alt Item Style"].state )
	{
		if (Toggles["Show Rune Numbers"].state && D2COMMON_GetItemText(item->dwTxtFileNo)->nType == 74)
		{
			itemName = to_string(item->dwTxtFileNo - 609) + " - " + itemName;
		}
		else
		{
			if (Toggles["Show Sockets"].state)
			{
				int sockets = D2COMMON_GetUnitStat(item, STAT_SOCKETS, 0);
				if (sockets > 0)
				{
					itemName += "(" + to_string(sockets) + ")";
				}
			}

			if (Toggles["Show Ethereal"].state && item->pItemData->dwFlags & ITEM_ETHEREAL)
			{
				itemName = "Eth " + itemName;
			}
	
			/*show iLvl unless it is equal to 1*/
			if (displayItemLevel && item->pItemData->dwItemLevel != 1)
			{
				itemName += " L" + to_string(item->pItemData->dwItemLevel);
			}
		}
	}
	else
	{
		if (Toggles["Show Sockets"].state) {
			int sockets = D2COMMON_GetUnitStat(item, STAT_SOCKETS, 0);
			if (sockets > 0)
				itemName += "(" + to_string(sockets) + ")";
		}
		if (Toggles["Show Ethereal"].state && item->pItemData->dwFlags & ITEM_ETHEREAL)
			itemName += "(Eth)";

		if (displayItemLevel)
			itemName += "(L" + to_string(item->pItemData->dwItemLevel) + ")";

		if (Toggles["Show Rune Numbers"].state && D2COMMON_GetItemText(item->dwTxtFileNo)->nType == 74)
			itemName = "[" + to_string(item->dwTxtFileNo - 609) + "]" + itemName;
	}

	/*Affix (Colors) Color Mod*/
	if( Toggles["Color Mod"].state )
	{
		///*Flawless Gems*/
		//if( (code[0] == 'g' && code[1] == 'l'					) ||
		//	(code[0] == 's' && code[1] == 'k' && code[2] == 'l' ) )
		//{
		//	itemName = "\xFF" "c:" + itemName;
		//}
		///*Perfect Gems*/
		//if( (code[0] == 'g' && code[1] == 'p'                   ) ||
		//	(code[0] == 's' && code[1] == 'k' && code[2] == 'p' ) )
		//{
		//	itemName = "\xFF" "c<" + itemName;
		//}
		/*Ethereal*/
		if( item->pItemData->dwFlags & 0x400000 )
		{
			/*Turn ethereal elite armors (and paladin shields) purple*/
			if( (code[0] == 'u'                                    ) ||
				(code[0] == 'p' && code[1] == 'a' && code[2] >= 'b') )
			{
				itemName = "\xFF" "c;" + itemName;
			}
		}
		/*Runes*/
		if( code[0] == 'r' )
		{
			if( code[1] == '0' )
			{
				itemName = "\xFF" "c0" + itemName;
			}
			else if( code[1] == '1' )
			{
				if( code[2] <= '6')
				{
					itemName = "\xFF" "c4" + itemName;
				}
				else
				{
					itemName = "\xFF" "c8" + itemName;
				}
			}
			else if( code[1] == '2' )
			{
				if( code[2] <= '2' )
				{
					itemName = "\xFF" "c8" + itemName;
				}
				else
				{
					itemName = "\xFF" "c1" + itemName;
				}
			}
			else if( code[1] == '3' )
			{
				itemName = "\xFF" "c1" + itemName;
			}
		}
	}
}

static ItemsTxt* GetArmorText(UnitAny* pItem) {
	ItemText* itemTxt = D2COMMON_GetItemText(pItem->dwTxtFileNo);
	int armorTxtRecords = *p_D2COMMON_ArmorTxtRecords;
	for (int i = 0; i < armorTxtRecords; i++) {
		ItemsTxt* armorTxt = &(*p_D2COMMON_ArmorTxt)[i];
		if (strcmp(armorTxt->szcode, itemTxt->szCode) == 0) {
			return armorTxt;
		}
	}
	return NULL;
}

bool Item::SocketStatsSplitActive() {
	unsigned int key = Toggles["Separate Socketed Stats"].toggle;
	if (key != 0 && (GetKeyState(key) & 0x8000) != 0)
		return true;
	return Toggles["Separate Socketed Stats"].state;
}

bool Item::SocketTooltipsActive() {
	return g_socketStatsOnTooltip && SocketStatsSplitActive();
}

// Set while GetItemDesc is building a socketed gem/rune/jewel, so that call
// does not try to split sockets again.
static int g_buildingSocketedDesc = 0;

// ebp in the tooltip builder is the item whose stats are being merged.
// Return 1 to skip folding socketed units into that description.
static int __declspec(noinline) __cdecl ShouldSkipSocketedStatMerge(UnitAny* pItem) {
	if (g_buildingSocketedDesc || !Item::SocketStatsSplitActive())
		return 0;
	if (!pItem || pItem->dwType != UNIT_ITEM || !pItem->pItemData)
		return 0;
	// Runeword mods live on the item itself. The socketed runes are not a separate stat block.
	if (pItem->pItemData->dwFlags & ITEM_RUNEWORD)
		return 0;
	if (!pItem->pInventory || !pItem->pInventory->pFirstItem)
		return 0;
	return 1;
}

// D2CLIENT GetItemDesc: ecx = item, edi = destination, stack = (item, newlines, 0).
void __declspec(naked) __fastcall D2CLIENT_GetItemDesc(UnitAny* /*pItem*/, wchar_t* /*buffer*/) {
	__asm {
		push edi
		mov edi, edx
		push 0
		push 1
		push ecx
		call D2CLIENT_GetItemDesc_I
		pop edi
		ret
	}
}

static void TrimTrailingNewlines(wchar_t* text) {
	size_t len = wcslen(text);
	while (len > 0 && (text[len - 1] == L'\n' || text[len - 1] == L'\r' || text[len - 1] == L' '))
		text[--len] = 0;
}

// Stat lines are drawn blue. A color code sitting on '[' is the stat-range color and stays.
static void StripSocketStatColors(const wchar_t* src, std::wstring& plain, std::wstring& rangeColor) {
	plain.clear();
	rangeColor.clear();
	for (int i = 0; src[i]; ) {
		if (src[i] == 0x00FF && src[i + 1] == L'c' && src[i + 2] != 0) {
			if (src[i + 3] == L'[')
				rangeColor.assign(src + i, 3);
			i += 3;
			continue;
		}
		plain.push_back(src[i]);
		i++;
	}
}

static std::wstring ColorSocketStatLine(const std::wstring& plain, const std::wstring& rangeColor, const std::wstring& blue) {
	size_t bracket = plain.find(L'[');
	if (bracket == std::wstring::npos || rangeColor.empty())
		return blue + plain;
	return blue + plain.substr(0, bracket) + rangeColor + plain.substr(bracket) + blue;
}

struct TipLine {
	std::wstring text;
	bool socketHeader;
	bool socketStat;
	int socketGroup;
};

static void RememberTooltipItem(UnitAny* item) {
	DWORD id = item ? item->dwUnitId : 0;
	DWORD type = item ? item->dwType : 0xFFFFFFFF;
	if (id != g_tooltipPageUnitId || type != g_tooltipPageType) {
		g_tooltipPageUnitId = id;
		g_tooltipPageType = type;
		g_tooltipPage = 0;
		g_tooltipPageCount = 1;
	}
}

static int TooltipCharLimit(int textCap) {
	int maxLen = textCap - 1;
	if (maxLen > 0x3FF)
		maxLen = 0x3FF;
	// The old fit check kept one wchar of slack under the 0x400 draw reject.
	return maxLen - 1;
}

static int JoinedTooltipLen(const std::vector<std::wstring>& lines) {
	int len = 0;
	for (const std::wstring& line : lines)
		len += (int)line.size();
	if (!lines.empty())
		len += (int)lines.size() - 1;
	return len;
}

static void SplitTooltipLines(const wchar_t* text, std::vector<std::wstring>& bottomFirst) {
	bottomFirst.clear();
	if (!text || !text[0])
		return;
	const wchar_t* p = text;
	while (*p) {
		const wchar_t* nl = wcschr(p, L'\n');
		if (!nl) {
			bottomFirst.emplace_back(p);
			break;
		}
		bottomFirst.emplace_back(p, nl);
		p = nl + 1;
	}
}

// Visual bottom is the start of the string. topToBottom[0] is the item name.
static void WriteTooltipTopToBottom(wchar_t* wTxt, int textCap, const std::vector<std::wstring>& topToBottom) {
	std::wstring out;
	for (int i = (int)topToBottom.size() - 1; i >= 0; --i) {
		if (!out.empty())
			out.push_back(L'\n');
		out += topToBottom[i];
	}
	if (textCap < 1)
		return;
	if ((int)out.size() >= textCap)
		out.resize(textCap - 1);
	wcscpy_s(wTxt, textCap, out.c_str());
}

static std::wstring TooltipPagerText(int page, int count) {
	std::wstring gold = GetColorCode(TextColor::Gold);
	std::wstring white = GetColorCode(TextColor::White);
	std::wstring text;
	if (page > 0)
		text += gold + L"< ";
	text += white + std::to_wstring(page + 1) + L"/" + std::to_wstring(count);
	if (page + 1 < count)
		text += L" " + gold + L">";
	text += white;
	return text;
}

static void CollectSocketedTooltipLines(UnitAny* pItem, std::vector<TipLine>& extra) {
	const int kMaxSockets = 6;
	if (!Item::SocketStatsSplitActive())
		return;
	if (!pItem || pItem->dwType != UNIT_ITEM || !pItem->pItemData || !pItem->pInventory)
		return;
	if (pItem->pItemData->dwFlags & ITEM_RUNEWORD)
		return;

	UnitAny* socks[kMaxSockets];
	wchar_t descs[kMaxSockets][0x401];
	int count = 0;

	g_buildingSocketedDesc++;
	for (UnitAny* sock = pItem->pInventory->pFirstItem; sock && count < kMaxSockets; ) {
		UnitAny* next = (sock->pItemData) ? sock->pItemData->pNextInvItem : nullptr;
		socks[count] = sock;
		descs[count][0] = 0;
		D2CLIENT_GetItemDesc(sock, descs[count]);
		TrimTrailingNewlines(descs[count]);
		count++;
		sock = next;
	}
	g_buildingSocketedDesc--;

	if (count == 0)
		return;

	// Each socket stays together: its name, then its own stats. Nothing is added together.
	std::wstring header = GetColorCode(TextColor::Gold) + L"Socketed Stats:" + GetColorCode(TextColor::White);
	std::wstring white = GetColorCode(TextColor::White);
	std::wstring blue = GetColorCode(TextColor::Blue);
	std::vector<TipLine> items;
	for (int d = 0; d < count; ++d) {
		int before = (int)items.size();
		// Later sockets keep a gap above the name. The first one sits on the line under the header.
		if (d > 0)
			items.push_back({ L"", false, true, d });

		wchar_t nameBuf[256];
		nameBuf[0] = 0;
		D2CLIENT_GetItemName(socks[d], nameBuf, 256);
		TrimTrailingNewlines(nameBuf);
		std::vector<std::wstring> nameLines;
		SplitTooltipLines(nameBuf, nameLines);
		for (const std::wstring& nameLine : nameLines) {
			std::wstring plain;
			std::wstring ignored;
			StripSocketStatColors(nameLine.c_str(), plain, ignored);
			if (!plain.empty())
				items.push_back({ white + plain, false, true, d });
		}

		std::vector<std::wstring> statLines;
		SplitTooltipLines(descs[d], statLines);
		for (const std::wstring& statLine : statLines) {
			std::wstring plain;
			std::wstring rangeColor;
			StripSocketStatColors(statLine.c_str(), plain, rangeColor);
			if (plain.empty())
				continue;
			items.push_back({ ColorSocketStatLine(plain, rangeColor, blue), false, true, d });
		}
		if (d > 0 && (int)items.size() == before + 1)
			items.pop_back();
	}
	if (items.empty())
		return;

	extra.push_back({ L"", false, false, -1 });
	extra.push_back({ header, true, false, -1 });
	extra.insert(extra.end(), items.begin(), items.end());
}

static void FitHoveredTooltip(wchar_t* wTxt, int textCap, const std::vector<TipLine>& extra) {
	if (!wTxt || textCap < 32)
		return;
	int maxChars = TooltipCharLimit(textCap);
	if (maxChars < 16)
		return;

	std::vector<std::wstring> bottomFirst;
	SplitTooltipLines(wTxt, bottomFirst);

	std::vector<TipLine> visual;
	visual.reserve(bottomFirst.size() + extra.size());
	for (int i = (int)bottomFirst.size() - 1; i >= 0; --i)
		visual.push_back({ bottomFirst[i], false, false, -1 });
	int originalCount = (int)visual.size();
	visual.insert(visual.end(), extra.begin(), extra.end());
	if (visual.empty()) {
		g_tooltipPage = 0;
		g_tooltipPageCount = 1;
		return;
	}

	std::vector<std::wstring> all;
	all.reserve(visual.size());
	for (const TipLine& line : visual)
		all.push_back(line.text);
	if (JoinedTooltipLen(all) <= maxChars) {
		g_tooltipPage = 0;
		g_tooltipPageCount = 1;
		if (!extra.empty())
			WriteTooltipTopToBottom(wTxt, textCap, all);
		return;
	}

	// Reserve the longest pager ("< 99/99 >") so a page never crosses the draw limit.
	int pagerReserve = (int)TooltipPagerText(1, 99).size();
	std::wstring continuedHeader = GetColorCode(TextColor::Gold) + L"Socketed Stats Continued:" + GetColorCode(TextColor::White);
	std::wstring firstHeader = GetColorCode(TextColor::Gold) + L"Socketed Stats:" + GetColorCode(TextColor::White);

	std::vector<int> statIdx;
	for (int i = originalCount; i < (int)visual.size(); ++i) {
		if (visual[i].socketStat)
			statIdx.push_back(i);
	}

	// Socketed overflow keeps the item tooltip on every page and only pages the socketed mods.
	if (!statIdx.empty() && originalCount > 0) {
		auto fixedCost = [&](int baseLines) {
			int len = 0;
			int n = 0;
			for (int i = 0; i < baseLines; ++i) {
				len += (int)visual[i].text.size();
				n++;
			}
			if (n > 1)
				len += n - 1;
			// blank line, header, and pager
			return len + 3 + (int)continuedHeader.size() + pagerReserve;
		};
		int baseLines = originalCount;
		while (baseLines > 1 && fixedCost(baseLines) >= maxChars)
			baseLines--;

		int statBudget = maxChars - fixedCost(baseLines);
		if (statBudget < 1)
			statBudget = 1;

		struct SocketBlock {
			std::vector<int> lines;
			int cost;
		};
		std::vector<SocketBlock> blocks;
		for (int idx : statIdx) {
			if (blocks.empty() || visual[blocks.back().lines.back()].socketGroup != visual[idx].socketGroup) {
				SocketBlock block;
				block.cost = 0;
				blocks.push_back(block);
			}
			blocks.back().lines.push_back(idx);
			blocks.back().cost += 1 + (int)visual[idx].text.size();
		}

		std::vector<std::vector<int>> pages;
		std::vector<int> cur;
		int used = 0;
		for (SocketBlock& block : blocks) {
			// A single socket that cannot fit on a page keeps its name and drops only its own trailing stats.
			while (block.lines.size() > 1 && block.cost > statBudget) {
				int last = block.lines.back();
				block.cost -= 1 + (int)visual[last].text.size();
				block.lines.pop_back();
			}
			if (block.lines.empty())
				continue;
			if (!cur.empty() && used + block.cost > statBudget) {
				pages.push_back(cur);
				cur.clear();
				used = 0;
			}
			cur.insert(cur.end(), block.lines.begin(), block.lines.end());
			used += block.cost;
		}
		if (!cur.empty())
			pages.push_back(cur);
		if (pages.empty())
			pages.push_back(std::vector<int>());

		if (g_tooltipPage >= (int)pages.size())
			g_tooltipPage = (int)pages.size() - 1;
		if (g_tooltipPage < 0)
			g_tooltipPage = 0;
		g_tooltipPageCount = (int)pages.size();

		std::vector<std::wstring> show;
		for (int i = 0; i < baseLines; ++i)
			show.push_back(visual[i].text);
		show.push_back(L"");
		show.push_back(g_tooltipPage == 0 ? firstHeader : continuedHeader);
		bool skipLeadingBlank = true;
		for (int idx : pages[g_tooltipPage]) {
			if (skipLeadingBlank && visual[idx].text.empty())
				continue;
			skipLeadingBlank = false;
			show.push_back(visual[idx].text);
		}
		show.push_back(TooltipPagerText(g_tooltipPage, (int)pages.size()));
		WriteTooltipTopToBottom(wTxt, textCap, show);
		return;
	}

	int bodyStart = originalCount > 0 ? 1 : 0;
	int pinnedLen = bodyStart == 1 ? (int)visual[0].text.size() : 0;
	int bodyBudget = maxChars - pinnedLen - (bodyStart == 1 ? 1 : 0) - pagerReserve;
	if (bodyBudget < 1)
		bodyBudget = 1;

	std::vector<std::vector<int>> pages;
	std::vector<int> cur;
	int used = 0;
	for (int i = bodyStart; i < (int)visual.size(); ++i) {
		int cost = 1 + (int)visual[i].text.size();
		if (cost > bodyBudget) {
			int keep = bodyBudget - 1;
			if (keep < 0)
				keep = 0;
			visual[i].text.resize(keep);
			cost = 1 + (int)visual[i].text.size();
		}
		if (cost > bodyBudget)
			continue;
		if (!cur.empty() && used + cost > bodyBudget) {
			pages.push_back(cur);
			cur.clear();
			used = 0;
		}
		cur.push_back(i);
		used += cost;
	}
	if (!cur.empty())
		pages.push_back(cur);
	if (pages.empty())
		pages.push_back(std::vector<int>());

	if (g_tooltipPage >= (int)pages.size())
		g_tooltipPage = (int)pages.size() - 1;
	if (g_tooltipPage < 0)
		g_tooltipPage = 0;
	g_tooltipPageCount = (int)pages.size();

	std::vector<std::wstring> show;
	if (bodyStart == 1)
		show.push_back(visual[0].text);
	for (int idx : pages[g_tooltipPage])
		show.push_back(visual[idx].text);
	show.push_back(TooltipPagerText(g_tooltipPage, (int)pages.size()));

	while (JoinedTooltipLen(show) > maxChars && show.size() > 2)
		show.erase(show.end() - 2);
	WriteTooltipTopToBottom(wTxt, textCap, show);
}

static void __stdcall AppendSocketedToHoveredTooltip(wchar_t* text) {
	if (g_buildingSocketedDesc)
		return;
	UnitAny* hovered = p_D2CLIENT_SelectedInvItem ? *p_D2CLIENT_SelectedInvItem : nullptr;
	RememberTooltipItem(hovered);
	std::vector<TipLine> extra;
	// The tooltip hotkey draws each socket beside the item instead of on this box.
	bool socketCards = Item::SocketTooltipsActive();
	if (hovered && !socketCards)
		CollectSocketedTooltipLines(hovered, extra);
	FitHoveredTooltip(text, tooltipTextCap, extra);
	if (!socketCards || !text || !text[0]) {
		g_itemTipTick = 0;
		return;
	}
	DWORD previousFont = D2WIN_SetTextSize(0);
	D2WIN_SetTextSize(previousFont);
	g_itemTipFont = (int)previousFont;
	DWORD width = 0;
	DWORD height = 0;
	D2WIN_GetTextSize(text, &width, &height);
	g_itemTipW = (int)width;
	g_itemTipH = (int)height;
	g_itemTipTick = GetTickCount();
}

static void BuildSocketCards(UnitAny* pItem, std::vector<std::vector<std::wstring>>& cards) {
	cards.clear();
	const int kMaxSockets = 6;
	if (!pItem || pItem->dwType != UNIT_ITEM || !pItem->pItemData || !pItem->pInventory)
		return;
	if (pItem->pItemData->dwFlags & ITEM_RUNEWORD)
		return;

	UnitAny* socks[kMaxSockets];
	wchar_t descs[kMaxSockets][0x401];
	int count = 0;

	g_buildingSocketedDesc++;
	for (UnitAny* sock = pItem->pInventory->pFirstItem; sock && count < kMaxSockets; ) {
		UnitAny* next = (sock->pItemData) ? sock->pItemData->pNextInvItem : nullptr;
		socks[count] = sock;
		descs[count][0] = 0;
		D2CLIENT_GetItemDesc(sock, descs[count]);
		TrimTrailingNewlines(descs[count]);
		count++;
		sock = next;
	}
	g_buildingSocketedDesc--;

	std::wstring white = GetColorCode(TextColor::White);
	std::wstring blue = GetColorCode(TextColor::Blue);
	for (int d = 0; d < count; ++d) {
		std::vector<std::wstring> lines;
		wchar_t nameBuf[256];
		nameBuf[0] = 0;
		D2CLIENT_GetItemName(socks[d], nameBuf, 256);
		TrimTrailingNewlines(nameBuf);
		std::vector<std::wstring> nameLines;
		SplitTooltipLines(nameBuf, nameLines);
		for (const std::wstring& nameLine : nameLines) {
			std::wstring plain;
			std::wstring ignored;
			StripSocketStatColors(nameLine.c_str(), plain, ignored);
			if (!plain.empty())
				lines.push_back(white + plain);
		}

		std::vector<std::wstring> statLines;
		SplitTooltipLines(descs[d], statLines);
		// The description string starts at the bottom stat. Walk it backward so the first mod is under the name.
		for (int i = (int)statLines.size() - 1; i >= 0; --i) {
			std::wstring plain;
			std::wstring rangeColor;
			StripSocketStatColors(statLines[i].c_str(), plain, rangeColor);
			if (plain.empty())
				continue;
			lines.push_back(ColorSocketStatLine(plain, rangeColor, blue));
		}
		if (!lines.empty())
			cards.push_back(lines);
	}
}

// d2gl copies a hovered player or merc name into a 50-character buffer and skips the box.
static bool FramedTooltipWouldSwallow() {
	UnitAny* unit = D2CLIENT_GetSelectedUnit();
	if (!unit)
		return false;
	if (unit->dwType == 0)
		return true;
	if (unit->dwType == 1) {
		DWORD id = unit->dwTxtFileNo;
		if (id == 0x10F || id == 0x152 || id == 0x167 || id == 0x420 || id == 0x231)
			return true;
	}
	return false;
}

static void DrawSocketCards(const std::vector<std::vector<std::wstring>>& cards) {
	const int kGap = 8;

	// Same font the item tooltip was just measured with.
	DWORD oldFont = D2WIN_SetTextSize(g_itemTipFont);

	int screenW = p_D2CLIENT_ScreenSizeX ? *p_D2CLIENT_ScreenSizeX : 800;
	int screenH = p_D2CLIENT_ScreenSizeY ? *p_D2CLIENT_ScreenSizeY : 600;
	if (screenW < 100)
		screenW = 800;
	if (screenH < 100)
		screenH = 600;

	// The hover tooltip is centered on these anchors. CursorHoverX sits 16 bytes after them.
	DWORD* hoverX = p_D2CLIENT_CursorHoverX;
	int xAbove = (int)hoverX[-4];
	int yBottom = (int)hoverX[-3];
	int xBelow = (int)hoverX[-2];
	int yTop = (int)hoverX[-1];
	int boxW = g_itemTipW + 8;
	int centerX;
	int bottomY;
	if (yBottom - g_itemTipH > 0) {
		centerX = xAbove;
		bottomY = yBottom + 2;
	} else {
		centerX = xBelow;
		bottomY = yTop + g_itemTipH + 2;
	}
	int tipLeft = centerX - boxW / 2;
	int tipRight = tipLeft + boxW;
	int tipTop = bottomY - g_itemTipH - 4;
	if (tipTop < 2)
		tipTop = 2;

	// Reading order. d2gl's framed tooltip draws the first line at the bottom, so the
	// string passed to DrawFramedText is this list reversed.
	std::vector<std::wstring> lines;
	lines.push_back(GetColorCode(TextColor::Gold) + L"Socketed Stats:");
	for (const std::vector<std::wstring>& socketLines : cards) {
		for (const std::wstring& line : socketLines)
			lines.push_back(line);
	}

	std::wstring text;
	for (int i = (int)lines.size() - 1; i >= 0; --i) {
		if (!text.empty())
			text.push_back(L'\n');
		text += lines[i];
	}

	// Measure last so d2gl's last framed-text height is this block, not a single line.
	DWORD textW = 0;
	DWORD textH = 0;
	D2WIN_GetTextSize((wchar_t*)text.c_str(), &textW, &textH);
	if (textW == 0 || textH == 0) {
		D2WIN_SetTextSize(oldFont);
		return;
	}

	// d2gl's inventory tooltip is this string in a box padded 10px wide and 5px tall.
	int panelW = (int)textW + 20;
	int panelH = (int)textH + 10;
	int leftRoom = tipLeft - kGap - 2;
	int rightRoom = screenW - 2 - (tipRight + kGap);
	bool placeLeft = leftRoom >= rightRoom;
	int inner = placeLeft ? tipLeft - kGap : tipRight + kGap;
	int x = placeLeft ? inner - panelW : inner;
	int y = tipTop;
	if (y + panelH > screenH - 5)
		y = screenH - 5 - panelH;
	if (y < 5)
		y = 5;

	if (!FramedTooltipWouldSwallow()) {
		// DrawFramedText is centered on x, and the box bottom sits on y.
		// d2gl then paints the same quality gradient and border as the item tooltip.
		int drawX = x + panelW / 2;
		int drawY = y + panelH;
		if (drawY == 32)
			drawY = 33;
		D2WIN_DrawFramedText(text.c_str(), drawX, drawY, 0, 1);
		D2WIN_SetTextSize(oldFont);
		return;
	}

	Drawing::Boxhook::Draw(x, y, panelW, panelH, 0, Drawing::BTOneFourth);
	int textY = y;
	int lineCount = (int)lines.size();
	int lineStep = (int)textH / (lineCount > 0 ? lineCount : 1);
	if (lineStep < 1)
		lineStep = 1;
	for (const std::wstring& line : lines) {
		if (!line.empty()) {
			DWORD lw = 0;
			DWORD lh = 0;
			D2WIN_GetTextSize((wchar_t*)line.c_str(), &lw, &lh);
			D2WIN_DrawText(line.c_str(), x + (panelW - (int)lw) / 2, textY + lineStep, White, 0);
		}
		textY += lineStep;
	}
	D2WIN_SetTextSize(oldFont);
}

void Item::OnDraw() {
	DrawIronGolemFallback(viewingUnit);
	// Hold shows them beside the item tooltip. Release removes them until the key is held again.
	if (!SocketTooltipsActive())
		return;
	if (g_itemTipTick == 0 || GetTickCount() - g_itemTipTick > 150)
		return;
	UnitAny* hovered = p_D2CLIENT_SelectedInvItem ? *p_D2CLIENT_SelectedInvItem : nullptr;
	if (!hovered)
		return;
	std::vector<std::vector<std::wstring>> cards;
	BuildSocketCards(hovered, cards);
	DrawSocketCards(cards);
}

void __stdcall Item::OnProperties(wchar_t * wTxt, UnitAny* pDescItem)
{
	if (g_buildingSocketedDesc)
		return;

	const int MAXLEN = 1024;
	static wchar_t wDesc[128];// a buffer for converting the description
	UnitAny* pItem = pDescItem ? pDescItem : *p_D2CLIENT_SelectedInvItem;
	UnitItemInfo uInfo;
	if (!pItem || pItem->dwType != UNIT_ITEM || CreateUnitItemInfo(&uInfo, pItem)) {
		return; // unknown item code
	}

	// Add description
	if (Toggles["Advanced Item Display"].state) {
		int aLen = wcslen(wTxt);
		string desc = item_desc_cache.Get(&uInfo);
		if (desc != "") {
			auto chars_written = MultiByteToWideChar(CODE_PAGE, MB_PRECOMPOSED, desc.c_str(), -1, wDesc, 128);
			swprintf_s(wTxt + aLen, MAXLEN - aLen,
				L"%s%s\n",
				(chars_written > 0) ? wDesc : L"\377c1 Descirption string too long!",
				GetColorCode(TextColor::White).c_str());
		}
	}

	if (!(Toggles["Always Show Item Stat Ranges"].state ||
				GetKeyState(VK_CONTROL) & 0x8000) ||
			pItem == nullptr ||
			pItem->dwType != UNIT_ITEM) { /* skip armor range */ }
	else if (D2COMMON_IsMatchingType(pItem, ITEM_TYPE_ALLARMOR)) {
		//Any Armor ItemTypes.txt
		int aLen = 0;
		bool ebugged = false;
		bool spawned_with_ed = false;
		aLen = wcslen(wTxt);
		ItemsTxt* armorTxt = GetArmorText(pItem);
		DWORD base = D2COMMON_GetBaseStatSigned(pItem, STAT_DEFENSE, 0); // includes eth bonus if applicable
		DWORD min = armorTxt->dwminac; // min of non-eth base
		DWORD max_no_ed = armorTxt->dwmaxac; // max of non-eth base
		bool is_eth = pItem->pItemData->dwFlags & ITEM_ETHEREAL;
		if (((base == max_no_ed + 1) && !is_eth) || ((base == 3*(max_no_ed+1)/2) && is_eth)) { // means item spawned with ED
			spawned_with_ed = true;
		}
		if (is_eth) {
			min = (DWORD)(min * 1.50);
			max_no_ed = (DWORD)(max_no_ed * 1.50);
			if (base > max_no_ed && !spawned_with_ed) { // must be ebugged
				min = (DWORD)(min * 1.50);
				max_no_ed = (DWORD)(max_no_ed * 1.50);
				ebugged = true;
			}
		}

		// Items with enhanced def mod will spawn with base def as max +1.
		// Don't show range if item spawned with edef and hasn't been upgraded.
		if (!spawned_with_ed) {
			swprintf_s(wTxt + aLen, MAXLEN - aLen,
					L"%sBase Defense: %d %s[%d - %d]%s%s\n",
					GetColorCode(TextColor::White).c_str(),
					base,
					GetColorCode(statRangeColor).c_str(),
					min, max_no_ed,
					ebugged ? L"\377c5 Ebug" : L"",
					GetColorCode(TextColor::White).c_str()
					);
		}
	}

	int ilvl = pItem->pItemData->dwItemLevel;
	int alvl = GetAffixLevel(ilvl, (BYTE)uInfo.attrs->qualityLevel, uInfo.attrs->magicLevel);
	int quality = pItem->pItemData->dwQuality;
	// Add alvl
	if (Toggles["Advanced Item Display"].state && Toggles["Show iLvl"].state
			&& ilvl != alvl 
			&& (quality == ITEM_QUALITY_MAGIC || quality == ITEM_QUALITY_RARE || quality == ITEM_QUALITY_CRAFT)) {
		int aLen = wcslen(wTxt);
		swprintf_s(wTxt + aLen, MAXLEN - aLen,
				L"%sAffix Level: %d\n",
				GetColorCode(TextColor::White).c_str(),
				alvl);
	}

	// Add ilvl
	if (Toggles["Advanced Item Display"].state &&
			Toggles["Show iLvl"].state &&
			ilvl > 1 &&
			no_ilvl_codes.count(uInfo.itemCode) == 0)
	{
		int aLen = wcslen(wTxt);
		swprintf_s(wTxt + aLen, MAXLEN - aLen,
				L"%sItem Level: %d\n",
				GetColorCode(TextColor::White).c_str(),
				ilvl);
	}
}

BOOL __stdcall Item::OnDamagePropertyBuild(UnitAny* pItem, DamageStats* pDmgStats, int nStat, wchar_t* wOut) {
	wchar_t newDesc[128];

	// Ignore a max stat, use just a min dmg prop to gen the property string
	if (nStat == STAT_MAXIMUMFIREDAMAGE || nStat == STAT_MAXIMUMCOLDDAMAGE || nStat == STAT_MAXIMUMLIGHTNINGDAMAGE|| nStat == STAT_MAXIMUMMAGICALDAMAGE ||
		nStat == STAT_MAXIMUMPOISONDAMAGE || nStat == STAT_POISONDAMAGELENGTH || nStat == STAT_ENHANCEDMAXIMUMDAMAGE)
		return TRUE;

	int stat_min, stat_max;
	wchar_t* szProp = nullptr;
	bool ranged = true;
	if (nStat == STAT_MINIMUMFIREDAMAGE) {
		if (pDmgStats->nFireDmgRange == 0)
			return FALSE;
		stat_min = pDmgStats->nMinFireDmg;
		stat_max = pDmgStats->nMaxFireDmg;
		if (stat_min >= stat_max) {
			szProp = D2LANG_GetLocaleText(D2STR_STRMODFIREDAMAGE);
			ranged = false;
		}
		else {
			szProp = D2LANG_GetLocaleText(D2STR_STRMODFIREDAMAGERANGE);
		}
	}
	else if (nStat == STAT_MINIMUMCOLDDAMAGE) {
		if (pDmgStats->nColdDmgRange == 0)
			return FALSE;
		stat_min = pDmgStats->nMinColdDmg;
		stat_max = pDmgStats->nMaxColdDmg;
		if (stat_min >= stat_max) {
			szProp = D2LANG_GetLocaleText(D2STR_STRMODCOLDDAMAGE);
			ranged = false;
		}
		else {
			szProp = D2LANG_GetLocaleText(D2STR_STRMODCOLDDAMAGERANGE);
		}
	}
	else if (nStat == STAT_MINIMUMLIGHTNINGDAMAGE) {
		if (pDmgStats->nLightDmgRange == 0)
			return FALSE;
		stat_min = pDmgStats->nMinLightDmg;
		stat_max = pDmgStats->nMaxLightDmg;
		if (stat_min >= stat_max) {
			szProp = D2LANG_GetLocaleText(D2STR_STRMODLIGHTNINGDAMAGE);
			ranged = false;
		}
		else {
			szProp = D2LANG_GetLocaleText(D2STR_STRMODLIGHTNINGDAMAGERANGE);
		}
	}
	else if (nStat == STAT_MINIMUMMAGICALDAMAGE) {
		if (pDmgStats->nMagicDmgRange == 0)
			return FALSE;
		stat_min = pDmgStats->nMinMagicDmg;
		stat_max = pDmgStats->nMaxMagicDmg;
		if (stat_min >= stat_max) {
			szProp = D2LANG_GetLocaleText(D2STR_STRMODMAGICDAMAGE);
			ranged = false;
		}
		else {
			szProp = D2LANG_GetLocaleText(D2STR_STRMODMAGICDAMAGERANGE);
		}
	}
	else if (nStat == STAT_MINIMUMPOISONDAMAGE) {
		if (pDmgStats->nPsnDmgRange == 0)
			return FALSE;
		if (pDmgStats->nPsnCount <= 0)
			pDmgStats->nPsnCount = 1;

		pDmgStats->nPsnLen = pDmgStats->nPsnLen / pDmgStats->nPsnCount;

		pDmgStats->nMinPsnDmg = stat_min = ((pDmgStats->nMinPsnDmg * pDmgStats->nPsnLen) + 128) / 256;
		pDmgStats->nMaxPsnDmg = stat_max = ((pDmgStats->nMaxPsnDmg * pDmgStats->nPsnLen) + 128) / 256;

		if (stat_min >= stat_max) {
			szProp = D2LANG_GetLocaleText(D2STR_STRMODPOISONDAMAGE);
			swprintf_s(newDesc, 128, szProp, stat_max, pDmgStats->nPsnLen / 25); // Per frame
		}
		else {
			szProp = D2LANG_GetLocaleText(D2STR_STRMODPOISONDAMAGERANGE);
			swprintf_s(newDesc, 128, szProp, stat_min, stat_max, pDmgStats->nPsnLen / 25);
		}
		wcscat_s(wOut, 1024, newDesc);
		return TRUE;
	}
	else if (nStat == STAT_SECONDARYMAXIMUMDAMAGE) {
		if (pDmgStats->dword14)
			return TRUE;
		return pDmgStats->nDmgRange != 0;
	}
	else if (nStat == STAT_MINIMUMDAMAGE || nStat == STAT_MAXIMUMDAMAGE || nStat == STAT_SECONDARYMINIMUMDAMAGE) {
		if (pDmgStats->dword14)
			return TRUE;
		if (!pDmgStats->nDmgRange)
			return FALSE;

		stat_min = pDmgStats->nMinDmg;
		stat_max = pDmgStats->nMaxDmg;

		if (stat_min >= stat_max) {
			return FALSE;
		}
		else {
			pDmgStats->dword14 = TRUE;
			szProp = D2LANG_GetLocaleText(D2STR_STRMODMINDAMAGERANGE);

		}
	}
	else if (nStat == STAT_ENHANCEDMINIMUMDAMAGE) {
		if (!pDmgStats->nDmgPercentRange)
			return FALSE;
		stat_min = pDmgStats->nMinDmgPercent;
		stat_max = (int) (D2LANG_GetLocaleText(10023)); // "Enhanced damage"
		szProp = L"+%d%% %s\n";
	}

	if (szProp == nullptr) {
		return FALSE;
	}

	if (ranged) {
		swprintf_s(newDesc, 128, szProp, stat_min, stat_max);
	}
	else {
		swprintf_s(newDesc, 128, szProp, stat_max);
	}

	// <!--
	if (newDesc[wcslen(newDesc) - 1] == L'\n')
		newDesc[wcslen(newDesc) - 1] = L'\0';
	if (newDesc[wcslen(newDesc) - 1] == L'\n')
		newDesc[wcslen(newDesc) - 1] = L'\0';

	OnPropertyBuild(newDesc, nStat, pItem, 0);
	// Beside this add-on the function is almost 1:1 copy of Blizzard's one -->
	wcscat_s(wOut, 1024, newDesc);
	wcscat_s(wOut, 1024, L"\n");

	return TRUE;
}

void __stdcall Item::OnPropertyBuild(wchar_t* wOut, int nStat, UnitAny* pItem, int nStatParam) {
	if (!(Toggles["Always Show Item Stat Ranges"].state || GetKeyState(VK_CONTROL) & 0x8000) || pItem == nullptr || pItem->dwType != UNIT_ITEM) {
		return;
	}

	ItemsTxtStat* stat = nullptr;
	ItemsTxtStat* all_stat = nullptr; // Stat for common modifer like all-res, all-stats

	switch (pItem->pItemData->dwQuality) {
	case ITEM_QUALITY_SET:
	{
		SetItemsTxt * pTxt = &(*p_D2COMMON_sgptDataTable)->pSetItemsTxt[pItem->pItemData->dwFileIndex];
		if (!pTxt)
			break;
		stat = GetItemsTxtStatByMod(pTxt->hStats, 9 + 10, nStat, nStatParam);
		if (stat)
			all_stat = GetAllStatModifier(pTxt->hStats, 9 + 10, nStat, stat);
	}
	case ITEM_QUALITY_UNIQUE:
	{
		if (pItem->pItemData->dwQuality == ITEM_QUALITY_UNIQUE) {
			UniqueItemsTxt * pTxt = &(*p_D2COMMON_sgptDataTable)->pUniqueItemsTxt[pItem->pItemData->dwFileIndex];
			if (pTxt == nullptr) {
				break;
			}

			stat = GetItemsTxtStatByMod(pTxt->hStats, 12, nStat, nStatParam);

			if (stat != nullptr) {
				all_stat = GetAllStatModifier(pTxt->hStats, 12, nStat, stat);
			}
		}
		
		if (stat != nullptr) {
			int statMin = stat->dwMin;
			int statMax = stat->dwMax;

			if (all_stat != nullptr) {
				statMin += all_stat->dwMin;
				statMax += all_stat->dwMax;
			}

			if (statMin < statMax) {
				int	aLen = wcslen(wOut);
				int leftSpace = 128 - aLen > 0 ? 128 - aLen : 0;

				if (nStat == STAT_LIFEPERLEVEL || nStat == STAT_MANAPERLEVEL || nStat == STAT_MAXENHANCEDDMGPERLEVEL || nStat == STAT_MAXDAMAGEPERLEVEL)
				{
					statMin = D2COMMON_GetBaseStatSigned(D2CLIENT_GetPlayerUnit(), STAT_LEVEL, 0) * statMin >> 3;
					statMax = D2COMMON_GetBaseStatSigned(D2CLIENT_GetPlayerUnit(), STAT_LEVEL, 0) * statMax >> 3;
				}
				if (leftSpace) {
					swprintf_s(wOut + aLen, leftSpace,
							L" %s[%d - %d]%s",
							GetColorCode(statRangeColor).c_str(),
							statMin,
							statMax,
							GetColorCode(TextColor::Blue).c_str());
				}
			}
		}
	} break;
	default:
	{
		if (pItem->pItemData->dwFlags & ITEM_RUNEWORD) {
			RunesTxt* pTxt = GetRunewordTxtById(pItem->pItemData->wPrefix[0]);
			if (!pTxt)
				break;
			stat = GetItemsTxtStatByMod(pTxt->hStats, 7, nStat, nStatParam);
			if (stat) {
				int statMin = stat->dwMin;
				int statMax = stat->dwMax;

				all_stat = GetAllStatModifier(pTxt->hStats, 7, nStat, stat);

				if (all_stat) {
					statMin += all_stat->dwMin;
					statMax += all_stat->dwMax;
				}

				if (stat->dwMin != stat->dwMax) {
					int	aLen = wcslen(wOut);
					int leftSpace = 128 - aLen > 0 ? 128 - aLen : 0;

					if (nStat == STAT_LIFEPERLEVEL || nStat == STAT_MANAPERLEVEL || nStat == STAT_MAXENHANCEDDMGPERLEVEL || nStat == STAT_MAXDAMAGEPERLEVEL)
					{
						statMin = D2COMMON_GetBaseStatSigned(D2CLIENT_GetPlayerUnit(), STAT_LEVEL, 0) * statMin >> 3;
						statMax = D2COMMON_GetBaseStatSigned(D2CLIENT_GetPlayerUnit(), STAT_LEVEL, 0) * statMax >> 3;
					}
					if (leftSpace)
						swprintf_s(wOut + aLen, leftSpace,
								L" %s[%d - %d]%s",
								GetColorCode(statRangeColor).c_str(),
								statMin,
								statMax,
								GetColorCode(TextColor::Blue).c_str());
				}
			}
		}
		else if (pItem->pItemData->dwQuality == ITEM_QUALITY_MAGIC || pItem->pItemData->dwQuality == ITEM_QUALITY_RARE || pItem->pItemData->dwQuality == ITEM_QUALITY_CRAFT)
		{
			int nAffixes = *p_D2COMMON_AutoMagicTxt - D2COMMON_GetItemMagicalMods(1); // Number of affixes without Automagic
			int min = 0, max = 0;
			int type = D2COMMON_GetItemType(pItem);
			BnetData* pData = (*p_D2LAUNCH_BnData);
			int is_expansion = pData->nCharFlags & PLAYER_TYPE_EXPANSION;
			for (int i = 1;; ++i) {
				if (!pItem->pItemData->wAutoPrefix && i > nAffixes) // Don't include Automagic.txt affixes if item doesn't use them
					break;
				AutoMagicTxt* pTxt = D2COMMON_GetItemMagicalMods(i);
				if (!pTxt)
					break;
				bool is_classic_affix = pTxt->wVersion==1;
				bool is_expansion_affix = pTxt->wVersion!=0;
				// skip affixes that are not valid for expansion when using expansion stat ranges
				if (is_expansion && !is_expansion_affix) continue;
				// skip non-classic affixes when using classic stat ranges
				if (!is_expansion && !is_classic_affix) continue;
				//Skip if stat level is > 99
				if (pTxt->dwLevel > 99)
					continue;
				//Skip if stat is not spawnable
				if (pItem->pItemData->dwQuality < ITEM_QUALITY_CRAFT && !pTxt->wSpawnable)
					continue;
				//Skip for rares+
				if (pItem->pItemData->dwQuality >= ITEM_QUALITY_RARE  && !pTxt->nRare)
					continue;
				//Firstly check Itemtype
				bool found_itype = false;
				bool found_etype = false;

				for (int j = 0; j < 5; ++j)
				{
					if (!pTxt->wEType[j] || pTxt->wEType[j] == 0xFFFF)
						break;
					if (D2COMMON_IsMatchingType(pItem, pTxt->wEType[j])) {
						found_etype = true;
						break;
					}
				}
				if (found_etype) // next if excluded type
					continue;

				for (int j = 0; j < 7; ++j)
				{
					if (!pTxt->wIType[j] || pTxt->wIType[j] == 0xFFFF)
						break;
					if (D2COMMON_IsMatchingType(pItem, pTxt->wIType[j])) {
						found_itype = true;
						break;
					}
				}
				if (!found_itype)
					continue;

				stat = GetItemsTxtStatByMod(pTxt->hMods, 3, nStat, nStatParam);
				if (!stat)
					continue;
				min = min == 0 ? stat->dwMin : ((stat->dwMin < min) ? stat->dwMin : min);
				max = (stat->dwMax > max) ? stat->dwMax : max;
				//DEBUGMSG(L"%s: update min to %d, and max to %d (record #%d)", wOut, min, max, i)
			}
			if (min < max) {
				int	aLen = wcslen(wOut);
				int leftSpace = 128 - aLen > 0 ? 128 - aLen : 0;
				if (nStat == STAT_MAXENHANCEDDMGPERLEVEL || nStat == STAT_MAXDAMAGEPERLEVEL || nStat == STAT_LIFEPERLEVEL || nStat == STAT_MANAPERLEVEL)
				{
					min = D2COMMON_GetBaseStatSigned(D2CLIENT_GetPlayerUnit(), STAT_LEVEL, 0) * min >> 3;
					max = D2COMMON_GetBaseStatSigned(D2CLIENT_GetPlayerUnit(), STAT_LEVEL, 0) * max >> 3;
				}
				if (leftSpace)
					swprintf_s(wOut + aLen, leftSpace,
							L" %s[%d - %d]%s",
							GetColorCode(statRangeColor).c_str(),
							min,
							max,
							GetColorCode(TextColor::Blue).c_str());
			}
		}

	} break;

	}
}

/*
	Search mod used in MagicPrefix.txt, UniqueItemsTxt, RunesTxt, etc. (index from Properties.txt) by ItemStatCost.txt stat index
	@param nStatParam - param column for property (skill id etc)
	@param nStat - ItemStatCost.txt record id
	@param nStats - number of pStats
	@param pStats - pointer to ItemsTxtStat* array [PropertiesTxt Id, min, max val)
*/
ItemsTxtStat* GetItemsTxtStatByMod(ItemsTxtStat* pStats, int nStats, int nStat, int nStatParam)
{
	if (nStat == STAT_SKILLONKILL || nStat == STAT_SKILLONHIT || nStat == STAT_SKILLONSTRIKING || nStat == STAT_SKILLONDEATH ||
		nStat == STAT_SKILLONLEVELUP || nStat == STAT_SKILLWHENSTRUCK || nStat == STAT_CHARGED ||
		nStat == STAT_MINIMUMCOLDDAMAGE || nStat == STAT_MINIMUMLIGHTNINGDAMAGE || nStat == STAT_MINIMUMFIREDAMAGE || nStat == STAT_MINIMUMPOISONDAMAGE || nStat == STAT_MINIMUMMAGICALDAMAGE) // Skip skills without ranges
	{
		return nullptr;
	}
	for (int i = 0; i<nStats; ++i) {
		if (pStats[i].dwProp == 0xffffffff) {
			break;
		}
		PropertiesTxt * pProp = &(*p_D2COMMON_sgptDataTable)->pPropertiesTxt[pStats[i].dwProp];
		if (pProp == nullptr) {
			break;
		}
		if (pProp->wStat[0] == 0xFFFF && pProp->nFunc[0] == 7 && (nStat == STAT_ENHANCEDDAMAGE || nStat == STAT_ENHANCEDMINIMUMDAMAGE || nStat == STAT_ENHANCEDMAXIMUMDAMAGE ||
			nStat == STAT_MAXENHANCEDDMGPERTIME || nStat == STAT_MAXENHANCEDDMGPERLEVEL)) {
			return &pStats[i];
		}
		else if (pProp->wStat[0] == 0xFFFF && pProp->nFunc[0] == 6 && (nStat == STAT_MAXIMUMDAMAGE || nStat == STAT_SECONDARYMAXIMUMDAMAGE ||
			nStat == STAT_MAXDAMAGEPERTIME || nStat == STAT_MAXDAMAGEPERLEVEL)) {
			return &pStats[i];
		}
		else if (pProp->wStat[0] == 0xFFFF && pProp->nFunc[0] == 5 && (nStat == STAT_MINIMUMDAMAGE || nStat == STAT_SECONDARYMINIMUMDAMAGE)) {
			return &pStats[i];
		}
		for (int j = 0; j < 7; ++j)
		{
			if (pProp->wStat[j] == 0xFFFF) {
				break;
			}
			if (pProp->wStat[j] == nStat && pStats[i].dwPar == nStatParam) {
				return &pStats[i];
			}
		}
	}
	return nullptr;
}

/*
	Find other mod that inflates the original
	@param pOrigin  - original stat
	@param nStat - ItemStatCost.txt record id
	@param nStats - number of pStats
	@param pStats - pointer to ItemsTxtStat* array [PropertiesTxt Id, min, max val)
*/
ItemsTxtStat* GetAllStatModifier(ItemsTxtStat* pStats, int nStats, int nStat, ItemsTxtStat* pOrigin)
{
	for (int i = 0; i<nStats; ++i) {
		if (pStats[i].dwProp == 0xffffffff)
			break;
		if (pStats[i].dwProp == pOrigin->dwProp)
			continue;

		PropertiesTxt * pProp = &(*p_D2COMMON_sgptDataTable)->pPropertiesTxt[pStats[i].dwProp];
		if (pProp == nullptr) {
			break;
		}

		for (int j = 0; j < 7; ++j) {
			if (pProp->wStat[j] == 0xFFFF) {
				break;
			}
			if (pProp->wStat[j] == nStat) {
				return &pStats[i];
			}
		}
	}
	return nullptr;
}

RunesTxt* GetRunewordTxtById(int rwId)
{
	int n = *(D2COMMON_GetRunesTxtRecords());
	for (int i = 1; i < n; ++i)
	{
		RunesTxt* pTxt = D2COMMON_GetRunesTxt(i);
		if (!pTxt)
			break;
		if (pTxt->dwRwId == rwId)
			return pTxt;
	}
	return 0;
}

UnitAny* Item::GetViewUnit ()
{
	UnitAny* player = D2CLIENT_GetPlayerUnit();
	UnitAny* view = viewingUnit ? viewingUnit : player;
	if (!view)
		return player;
	// Monster and player ids are separate and overlap. Match type as well.
	if (player && view->dwType == player->dwType && view->dwUnitId == player->dwUnitId)
		return player;

	if (viewingUnit && viewingUnit->dwType == UNIT_PLAYER && viewingUnit->pPlayerData) {
		Drawing::Texthook::Draw(*p_D2CLIENT_PanelOffsetX + 160 + 320, 300, Drawing::Center, 0, White, "%s", viewingUnit->pPlayerData->szName);
	} else if (viewingUnit) {
		wchar_t* wname = D2CLIENT_GetUnitName(viewingUnit);
		char name[128];
		name[0] = 0;
		if (wname)
			WideCharToMultiByte(CODE_PAGE, 0, wname, -1, name, sizeof(name), NULL, NULL);
		if (name[0])
			Drawing::Texthook::Draw(*p_D2CLIENT_PanelOffsetX + 160 + 320, 300, Drawing::Center, 0, White, "%s", name);
	}
	return viewingUnit ? viewingUnit : view;
}

void __declspec(naked) ItemName_Interception()
{
	__asm {
		mov ecx, edi
		mov edx, ebx
		call Item::ItemNamePatch
		mov al, [ebp+0x12a]
		ret
	}
}


__declspec(naked) void __fastcall GetProperties_Interception()
{
	__asm
	{
		mov edx, dword ptr [esp + 0x80C] // item passed to GetItemDesc
		push edx
		push eax
		call Item::OnProperties
		add esp, 0x808
		ret 12
	}
}

// Called in place of "mov eax, [ebp+60h]; test eax, eax".
// When splitting, return to the instruction the original je would have taken (return + 0x4B).
void __declspec(naked) SkipSocketedStatMerge_Intercept()
{
	__asm {
		push ecx
		push edx
		push ebp
		call ShouldSkipSocketedStatMerge
		add esp, 4
		pop edx
		pop ecx
		test eax, eax
		jz original
		add dword ptr [esp], 0x4B
		ret
	original:
		mov eax, dword ptr [ebp + 0x60]
		test eax, eax
		ret
	}
}

// Replaces the call that draws a finished hover tooltip. ecx is the wide string.
void __declspec(naked) TooltipDraw_Intercept()
{
	__asm {
		push eax
		push ecx
		push edx
		push ecx
		call AppendSocketedToHoveredTooltip
		pop edx
		pop ecx
		pop eax
		jmp dword ptr [tooltipDrawAddr]
	}
}

void __declspec(naked) TooltipDraw_Intercept_800()
{
	__asm {
		mov dword ptr [tooltipTextCap], 0x800
		jmp TooltipDraw_Intercept
	}
}

void __declspec(naked) TooltipDraw_Intercept_400()
{
	__asm {
		mov dword ptr [tooltipTextCap], 0x400
		jmp TooltipDraw_Intercept
	}
}

/*	Wrapper over D2CLIENT.0x2E04B (1.13d)
	BOOL __userpurge ITEMS_BuildDamagePropertyDesc@<eax>(DamageStats *pStats@<eax>, int nStat, wchar_t *wOut)
	Function is pretty simple so I decided to rewrite it.
	@esp-0x20:	pItem
*/
void __declspec(naked) GetItemPropertyStringDamage_Interception()
{
	__asm {
		push[esp + 8]			// wOut
		push[esp + 8]			// nStat
		push eax				// pStats
		push[esp - 0x20 + 12]	// pItem

		call Item::OnDamagePropertyBuild

		ret 8
	}
}

/* Wrapper over D2CLIENT.0x2E06D (1.13d)
	As far I know this: int __userpurge ITEMS_ParseStats_6FADCE40<eax>(signed __int32 nStat<eax>, wchar_t *wOut<esi>, UnitAny *pItem, StatListEx *pStatList, DWORD nStatParam, DWORD nStatValue, int a7)
	Warning: wOut is 128 words length only!
	@ebx the nStat value
	@edi pStatListEx
	@esp-0x10 seems to always keep pItem *careful*
*/
void __declspec(naked) GetItemPropertyString_Interception()
{
	static DWORD rtn = 0; // if something is stupid but works then it's not stupid!
	__asm
	{
		pop rtn
		// Firstly generate string using old function
		call D2CLIENT_ParseStats_J
		push rtn

		push [esp - 4] // preserve nStatParam

		push eax // Store result
		mov eax, [esp - 0x10 + 8 + 4] // pItem
		push ecx
		push edx

		// Then pass the output to our func
		push [esp + 12] // nStatParam
		push eax // pItem
		push ebx // nStat
		push esi // wOut

		call Item::OnPropertyBuild

		pop edx
		pop ecx
		pop eax

		add esp, 4 // clean nStatParam

		ret
	}
}

void __declspec(naked) ViewInventoryPatch1_ASM()
{
	__asm {
		push eax;
		call Item::GetViewUnit;
		mov esi, eax;
		pop eax;
		ret;
	}
}
void __declspec(naked) ViewInventoryPatch2_ASM()
{
	__asm {
		push eax;
		call Item::GetViewUnit;
		mov ebx, eax;
		pop eax;
		ret;
	}
}
void __declspec(naked) ViewInventoryPatch3_ASM()
{
	__asm
	{
		push eax;
		push ebx;
		call Item::GetViewUnit;

		mov ebx, [edi];
		cmp ebx, 1;
		je OldCode;

		mov edi, eax;

		OldCode:
		pop ebx;
		pop eax;
		test eax, eax;
		mov ecx, dword ptr [edi + 0x60];

		ret;
	}
}

//seems to force alt to be down
BOOL Item::PermShowItemsPatch1()
{
	return Toggles["Always Show Items"].state || D2CLIENT_GetUIState(UI_GROUND_ITEMS);
}

//these two seem to deal w/ fixing the inv/waypoints when alt is down
//one of them breaks being able to not hover monsters when holding alt
//e.g. if ur wwing as a barb and dont want to lock a monster u usually hold
//alt (or space or whatever u have show items bound to). this is broken with
//these patches.
BOOL Item::PermShowItemsPatch2() {
	return Toggles["Always Show Items"].state || D2CLIENT_GetUIState(UI_GROUND_ITEMS);
}

BOOL Item::PermShowItemsPatch3() {
	return Toggles["Always Show Items"].state || D2CLIENT_GetUIState(UI_GROUND_ITEMS);
}


void __declspec(naked) PermShowItemsPatch1_ASM()
{
	__asm {
		call Item::PermShowItemsPatch1
		test eax, eax
		ret
	}
}


void __declspec(naked) PermShowItemsPatch2_ASM()
{
	__asm {
		call Item::PermShowItemsPatch2
		test eax, eax
		je orgcode
		ret
		orgcode :
		mov eax, dword ptr[esp + 0x20]
			test eax, eax
			ret
	}
}


void __declspec(naked) PermShowItemsPatch3_ASM()
{
	__asm {
		push ebp
		push esi
		call Item::PermShowItemsPatch3
		test eax, eax
		pop esi
		pop ebp
		jz 	outcode
		cmp ebp, 0x20
		jge outcode
		ret
		outcode :
		add dword ptr[esp], 0x38A  //to 6FB0DD89
			ret
	}
}


void __declspec(naked) PermShowItemsPatch4_ASM()
{
	__asm {
		push eax
		call Item::PermShowItemsPatch1
		mov ecx, eax
		pop eax
		ret
	}
}
