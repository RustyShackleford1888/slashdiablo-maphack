/* Maphack Module
 *
 */
#include "../../D2Ptrs.h"
#include "../../D2Helpers.h"
#include "../../D2Stubs.h"
#include "../../D2Intercepts.h"
#include "Maphack.h"
#include "../../BH.h"
#include "../../Drawing.h"
#include "../Item/ItemDisplay.h"
#include "../Item/Item.h"
#include "../../AsyncDrawBuffer.h"
#include "../ScreenInfo/ScreenInfo.h"

#pragma optimize( "", off)

using namespace Drawing;
Patch* weatherPatch = new Patch(Jump, D2COMMON, { 0x6CC56, 0x30C36 }, (int)Weather_Interception, 5);
Patch* lightingPatch = new Patch(Call, D2CLIENT, { 0xA9A37, 0x233A7 }, (int)Lighting_Interception, 6);
Patch* infraPatch = new Patch(Call, D2CLIENT, { 0x66623, 0xB4A23 }, (int)Infravision_Interception, 7);
Patch* shakePatch = new Patch(Call, D2CLIENT, { 0x442A2, 0x452F2 }, (int)Shake_Interception, 5);
Patch* diabloDeadMessage = new Patch((PatchType)0x68, D2CLIENT, { 0x52E84, 0x693B4 }, (int)0x14, 5);

Patch* monsterNamePatch = new Patch(Call, D2WIN, { 0x13550, 0x140E0 }, (int)HoverObject_Interception, 5);
Patch* cpuPatch = new Patch(NOP, D2CLIENT, { 0x3CB7C, 0x2770C }, 0, 9);
Patch* fpsPatch = new Patch(NOP, D2CLIENT, { 0x44E51, 0x45EA1 }, 0, 8);

Patch* skipNpcMessages1 = new Patch(Call, D2CLIENT, { 0x4BB07, 0x7EB87 }, (int)NPCQuestMessageStartPatch_ASM, 6);
Patch* skipNpcMessages2 = new Patch(Call, D2CLIENT, { 0x48BD6, 0x7B4C6 }, (int)NPCQuestMessageEndPatch1_ASM, 8);
Patch* skipNpcMessages3 = new Patch(Call, D2CLIENT, { 0x4819F, 0x7A9CF }, (int)NPCQuestMessageEndPatch2_ASM, 5);
Patch* skipNpcMessages4 = new Patch(Call, D2CLIENT, { 0x7E9B7, 0x77737 }, (int)NPCMessageLoopPatch_ASM, 6);


static BOOL fSkipMessageReq = 0;
static DWORD mSkipMessageTimer = 0;
static DWORD mSkipQuestMessage = 1;

DrawDirective automapDraw(true, 5);

Maphack::Maphack() : Module("Maphack") {
	revealType = MaphackRevealAct;
	ResetRevealed();
	missileColors["Player"] = 0x97;
	missileColors["Neutral"] = 0x0A;
	missileColors["Party"] = 0x84;
	missileColors["Hostile"] = 0x5B;
	monsterColors["Normal"] = 0x5B;
	monsterColors["Minion"] = 0x60;
	monsterColors["Champion"] = 0x91;
	monsterColors["Boss"] = 0x84;

	monsterResistanceThreshold = 99;
	lkLinesColor = 105;

	ReadConfig();
}

void Maphack::LoadConfig() {
	enhancementColors.clear();
	auraColors.clear();
	automapMonsterColors.clear();
	automapMonsterLines.clear();
	automapHiddenMonsters.clear();
	// Clear all color maps before reloading
	monsterColors.clear();
	MonsterColors.clear();
	missileColors.clear();
	SuperUniqueColors.clear();
	MonsterLines.clear();
	MonsterHides.clear();
	ReadConfig();
}

void Maphack::ReadConfig() {
	BH::config->ReadInt("Reveal Mode", revealType);
	BH::config->ReadInt("Show Monster Resistance", monsterResistanceThreshold);
	BH::config->ReadInt("LK Chest Lines", lkLinesColor);

	BH::config->ReadKey("Reload Config", "VK_NUMPAD0", reloadConfig);
	BH::config->ReadToggle("Show Settings", "VK_NUMPAD8", true, Toggles["Show Settings"]);

	BH::config->ReadAssoc("Missile Color", missileColors);
	BH::config->ReadAssoc("Monster Color", monsterColors);

	TextColorMap["\377c0"] = 0x20;  // white
	TextColorMap["\377c1"] = 0x0A;  // red
	TextColorMap["\377c2"] = 0x84;  // green
	TextColorMap["\377c3"] = 0x97;  // blue
	TextColorMap["\377c4"] = 0x0D;  // gold
	TextColorMap["\377c5"] = 0xD0;  // gray
	TextColorMap["\377c6"] = 0x00;  // black
	TextColorMap["\377c7"] = 0x5A;  // tan
	TextColorMap["\377c8"] = 0x60;  // orange
	TextColorMap["\377c9"] = 0x0C;  // yellow
	TextColorMap["\377c;"] = 0x9B;  // purple
	TextColorMap["\377c:"] = 0x76;  // dark green
	TextColorMap["\377c\x06"] = 0x66; // coral
	TextColorMap["\377c\x07"] = 0x82; // sage
	TextColorMap["\377c\x09"] = 0xCB; // teal
	TextColorMap["\377c\x0C"] = 0xD6; // light gray

std::vector<std::pair<std::string, std::string>> enhancementColorsString;
BH::config->ReadMapList("Enhancement Color", enhancementColorsString);

std::vector<std::pair<int, int>> enhancementColors; // Assuming these are integer pairs

// Assuming StringToNumber converts strings to integers
for (const auto& entry : enhancementColorsString) {
    int firstValue = StringToNumber(entry.first);
    int secondValue = StringToNumber(entry.second);
    enhancementColors.emplace_back(firstValue, secondValue);
}
std::vector<std::pair<std::string, std::string>> auraColorsString;
BH::config->ReadMapList("Aura Color", auraColorsString);

std::vector<std::pair<int, int>> auraColors; // Assuming these are integer pairs

// Assuming StringToNumber converts strings to integers
for (const auto& entry : auraColorsString) {
    int firstValue = StringToNumber(entry.first);
    int secondValue = StringToNumber(entry.second);
    auraColors.emplace_back(firstValue, secondValue);
}


	BH::config->ReadAssoc("Monster Color", MonsterColors);
	for (auto it = MonsterColors.cbegin(); it != MonsterColors.cend(); it++) {
		// If the key is a number, it means a monster we've assigned a specific color
		int monsterId = -1;
		stringstream ss((*it).first);
		if ((ss >> monsterId).fail()) {
			continue;
		} else {
			int monsterColor = StringToNumber((*it).second);
			automapMonsterColors[monsterId] = monsterColor;
		}
	}

	BH::config->ReadAssoc("Super Unique Color", SuperUniqueColors);
	for (auto it = SuperUniqueColors.cbegin(); it != SuperUniqueColors.cend(); it++) {
		// If the key is a number, it means a monster we've assigned a specific color
		int monsterId = -1;
		stringstream ss((*it).first);
		if ((ss >> monsterId).fail()) {
			continue;
		}
		else {
			int monsterColor = StringToNumber((*it).second);
			automapSuperUniqueColors[monsterId] = monsterColor;
		}
	}

	
	BH::config->ReadAssoc("Monster Line", MonsterLines);
	for (auto it = MonsterLines.cbegin(); it != MonsterLines.cend(); it++) {
		// If the key is a number, it means a monster we've assigned a specific color
		int monsterId = -1;
		stringstream ss((*it).first);
		if ((ss >> monsterId).fail()) {
			continue;
		} else {
			int lineColor = StringToNumber((*it).second);
			automapMonsterLines[monsterId] = lineColor;
		}
	}

	BH::config->ReadAssoc("Monster Hide", MonsterHides);
	for (auto it = MonsterHides.cbegin(); it != MonsterHides.cend(); it++) {
		// If the key is a number, it means do not draw this monster on map
		int monsterId = -1;
		stringstream ss((*it).first);
		if ((ss >> monsterId).fail()) {
			continue;
		} else {
			automapHiddenMonsters.push_back(monsterId);
		}
	}

	BH::config->ReadToggle("Reveal Map", "None", true, Toggles["Auto Reveal"]);
	BH::config->ReadToggle("Show Monsters", "None", true, Toggles["Show Monsters"]);
	BH::config->ReadToggle("Show Missiles", "None", true, Toggles["Show Missiles"]);
	BH::config->ReadToggle("Show Chests", "None", true, Toggles["Show Chests"]);
	BH::config->ReadToggle("Force Light Radius", "None", true, Toggles["Force Light Radius"]);
	BH::config->ReadToggle("Remove Weather", "None", true, Toggles["Remove Weather"]);
	BH::config->ReadToggle("Infravision", "None", true, Toggles["Infravision"]);
	BH::config->ReadToggle("Remove Shake", "None", false, Toggles["Remove Shake"]);
	BH::config->ReadToggle("Display Level Names", "None", true, Toggles["Display Level Names"]);
	BH::config->ReadToggle("Monster Resistances", "None", true, Toggles["Monster Resistances"]);
	BH::config->ReadToggle("Monster Enchantments", "None", true, Toggles["Monster Enchantments"]);
	BH::config->ReadToggle("Apply CPU Patch", "None", true, Toggles["Apply CPU Patch"]);
	BH::config->ReadToggle("Apply FPS Patch", "None", true, Toggles["Apply FPS Patch"]);
	BH::config->ReadToggle("Show Automap On Join", "None", false, Toggles["Show Automap On Join"]);
	BH::config->ReadToggle("Skip NPC Quest Messages", "None", true, Toggles["Skip NPC Quest Messages"]);

	BH::config->ReadInt("Minimap Max Ghost", automapDraw.maxGhost);
}

void Maphack::ResetRevealed() {
	revealedGame = false;
	for (int act = 0; act < 6; act++)
		revealedAct[act] = false;
	for (int level = 0; level < 255; level++)
		revealedLevel[level] = false;
}

void Maphack::ResetPatches() {
	//Lighting Patch
	if (Toggles["Force Light Radius"].state)
		lightingPatch->Install();
	else
		lightingPatch->Remove();

	//Weather Patch
	if (Toggles["Remove Weather"].state)
		weatherPatch->Install();
	else
		weatherPatch->Remove();

	//Infravision Patch
	if (Toggles["Infravision"].state)
		infraPatch->Install();
	else
		infraPatch->Remove();
		//GameShake Patch
	if (Toggles["Remove Shake"].state)
		shakePatch->Install();
	else
		shakePatch->Remove();
	//Monster Health Bar Patch
	if (Toggles["Monster Resistances"].state)
		monsterNamePatch->Install();
	else
		monsterNamePatch->Remove();

	// patch for cpu-overutilization fix
	if (Toggles["Apply CPU Patch"].state)
		cpuPatch->Install();
	else
		cpuPatch->Remove();

	if (Toggles["Apply FPS Patch"].state)
		fpsPatch->Install();
	else
		fpsPatch->Remove();

	if (Toggles["Skip NPC Quest Messages"].state) {
		skipNpcMessages1->Install();
		skipNpcMessages2->Install();
		skipNpcMessages3->Install();
		skipNpcMessages4->Install();
	} else {
		skipNpcMessages1->Remove();
		skipNpcMessages2->Remove();
		skipNpcMessages3->Remove();
		skipNpcMessages4->Remove();
	}
}

void Maphack::OnLoad() {
	/*ResetRevealed();
	ReadConfig();
	ResetPatches();*/
	diabloDeadMessage->Install();

	settingsTab = new UITab("Maphack", BH::settingsUI);

	new Texthook(settingsTab, 80, 3, "Toggles");
	unsigned int Y = 0;
	int keyhook_x = 150;
	int col2_x = 250;
	new Checkhook(settingsTab, 4, (Y += 15), &Toggles["Auto Reveal"].state, "Auto Reveal");
	new Keyhook(settingsTab, keyhook_x, (Y + 2), &Toggles["Auto Reveal"].toggle, "");

	new Checkhook(settingsTab, 4, (Y += 15), &Toggles["Show Monsters"].state, "Show Monsters");
	new Keyhook(settingsTab, keyhook_x, (Y + 2), &Toggles["Show Monsters"].toggle, "");

	new Checkhook(settingsTab, 4, (Y += 15), &Toggles["Monster Enchantments"].state, "  Enchantments");
	new Keyhook(settingsTab, keyhook_x, (Y + 2), &Toggles["Monster Enchantments"].toggle, "");

	new Checkhook(settingsTab, 4, (Y += 15), &Toggles["Monster Resistances"].state, "  Resistances");
	new Keyhook(settingsTab, keyhook_x, (Y + 2), &Toggles["Monster Resistances"].toggle, "");
	
	new Checkhook(settingsTab, 4, (Y += 15), &Toggles["Show Missiles"].state, "Show Missiles");
	new Keyhook(settingsTab, keyhook_x, (Y + 2), &Toggles["Show Missiles"].toggle, "");

	new Checkhook(settingsTab, 4, (Y += 15), &Toggles["Show Chests"].state, "Show Chests");
	new Keyhook(settingsTab, keyhook_x, (Y + 2), &Toggles["Show Chests"].toggle, "");

	new Checkhook(settingsTab, 4, (Y += 15), &Toggles["Force Light Radius"].state, "Light Radius");
	new Keyhook(settingsTab, keyhook_x, (Y + 2), &Toggles["Force Light Radius"].toggle, "");

	new Checkhook(settingsTab, 4, (Y += 15), &Toggles["Remove Weather"].state, "Remove Weather");
	new Keyhook(settingsTab, keyhook_x, (Y + 2), &Toggles["Remove Weather"].toggle, "");

	new Checkhook(settingsTab, 4, (Y += 15), &Toggles["Infravision"].state, "Infravision");
	new Keyhook(settingsTab, keyhook_x, (Y + 2), &Toggles["Infravision"].toggle, "");

	new Checkhook(settingsTab, 4, (Y += 15), &Toggles["Remove Shake"].state, "Remove Shake");
	new Keyhook(settingsTab, keyhook_x, (Y + 2), &Toggles["Remove Shake"].toggle, "");

	new Checkhook(settingsTab, 4, (Y += 15), &Toggles["Display Level Names"].state, "Level Names");
	new Keyhook(settingsTab, keyhook_x, (Y + 2), &Toggles["Display Level Names"].toggle, "");

	new Checkhook(settingsTab, 4, (Y += 15), &Toggles["Apply CPU Patch"].state, "CPU Patch");
	new Keyhook(settingsTab, keyhook_x, (Y + 2), &Toggles["Apply CPU Patch"].toggle, "");

	new Checkhook(settingsTab, 4, (Y += 15), &Toggles["Apply FPS Patch"].state, "FPS Patch (SP Only)");
	new Keyhook(settingsTab, keyhook_x, (Y + 2), &Toggles["Apply FPS Patch"].toggle, "");

	new Checkhook(settingsTab, 4, (Y += 15), &Toggles["Show Automap On Join"].state, "Show Automap On Join");
	new Keyhook(settingsTab, keyhook_x, (Y + 2), &Toggles["Show Automap On Join"].toggle, "");

	new Checkhook(settingsTab, 4, (Y += 15), &Toggles["Skip NPC Quest Messages"].state, "Skip NPC Quest Messages");
	new Keyhook(settingsTab, keyhook_x, (Y + 2), &Toggles["Skip NPC Quest Messages"].toggle, "");

	new Texthook(settingsTab, col2_x + 5, 3, "Missile Colors");

	new Colorhook(settingsTab, col2_x, 17, &missileColors["Player"], "Player");
	new Colorhook(settingsTab, col2_x, 32, &missileColors["Neutral"], "Neutral");
	new Colorhook(settingsTab, col2_x, 47, &missileColors["Party"], "Party");
	new Colorhook(settingsTab, col2_x, 62, &missileColors["Hostile"], "Hostile");

	new Texthook(settingsTab, col2_x + 5, 77, "Monster Colors");

	new Colorhook(settingsTab, col2_x, 92, &monsterColors["Normal"], "Normal");
	new Colorhook(settingsTab, col2_x, 107, &monsterColors["Minion"], "Minion");
	new Colorhook(settingsTab, col2_x, 122, &monsterColors["Champion"], "Champion");
	new Colorhook(settingsTab, col2_x, 137, &monsterColors["Boss"], "Boss");

	new Texthook(settingsTab, 6, (Y += 15), "Reveal Type:");

	vector<string> options;
	options.push_back("Game");
	options.push_back("Act");
	options.push_back("Level");
	new Combohook(settingsTab, 100, Y, 70, &revealType, options);

}

void Maphack::OnKey(bool up, BYTE key, LPARAM lParam, bool* block) {
	bool ctrlState = ((GetKeyState(VK_LCONTROL) & 0x80) || (GetKeyState(VK_RCONTROL) & 0x80));
	if (key == 0x52 && ctrlState || key == reloadConfig) {
		*block = true;
		if (up)
			BH::ReloadConfig();
		return;
	}
	for (map<string,Toggle>::iterator it = Toggles.begin(); it != Toggles.end(); it++) {
		if (key == (*it).second.toggle) {
			*block = true;
			if (up) {
				(*it).second.state = !(*it).second.state;
				ResetPatches();
			}
			return;
		}
	}
	return;
}

void Maphack::OnUnload() {
	lightingPatch->Remove();
	weatherPatch->Remove();
	infraPatch->Remove();
	shakePatch->Remove();
	skipNpcMessages1->Remove();
	skipNpcMessages2->Remove();
	skipNpcMessages3->Remove();
	skipNpcMessages4->Remove();
	diabloDeadMessage->Remove();
}

void Maphack::OnLoop() {
	//// Remove or install patchs based on state.
	ResetPatches();
	BH::settingsUI->SetVisible(Toggles["Show Settings"].state);

	// Get the player unit for area information.
	UnitAny* unit = D2CLIENT_GetPlayerUnit();
	if (!unit || !Toggles["Auto Reveal"].state)
		return;
	
	// Reveal the automap based on configuration.
	switch((MaphackReveal)revealType) {
		case MaphackRevealGame:
			RevealGame();
		break;
		case MaphackRevealAct:
			RevealAct(unit->pAct->dwAct + 1);
		break;
		case MaphackRevealLevel:
			RevealLevel(unit->pPath->pRoom1->pRoom2->pLevel);
		break;
	}
}

bool IsObjectChest(ObjectTxt *obj)
{
	//ObjectTxt *obj = D2COMMON_GetObjectTxt(objno);
	return (obj->nSelectable0 && (
		(obj->nOperateFn == 1) || //bed, undef grave, casket, sarc
		(obj->nOperateFn == 3) || //basket, urn, rockpile, trapped soul
		(obj->nOperateFn == 4) || //chest, corpse, wooden chest, buriel chest, skull and rocks, dead barb
		(obj->nOperateFn == 5) || //barrel
		(obj->nOperateFn == 7) || //exploding barrel
		(obj->nOperateFn == 14) || //loose bolder etc....*
		(obj->nOperateFn == 19) || //armor stand
		(obj->nOperateFn == 20) || //weapon rack
		(obj->nOperateFn == 33) || //writ
		(obj->nOperateFn == 48) || //trapped soul
		(obj->nOperateFn == 51) || //stash
		(obj->nOperateFn == 68)    //evil urn
		));
}

BYTE nChestClosedColour = 0x09;
BYTE nChestLockedColour = 0x09;

Act* lastAct = NULL;

void Maphack::OnDraw() {
	UnitAny* player = D2CLIENT_GetPlayerUnit();

	if (!player || !player->pAct || player->pPath->pRoom1->pRoom2->pLevel->dwLevelNo == 0)
		return;
	// We're looping over all items and setting 2 flags:
	// UNITFLAG_NO_EXPERIENCE - Whether the item has been checked for a drop notification (to prevent checking it again)
	// UNITFLAG_REVEALED      - Whether the item should be notified and drawn on the automap
	// To my knowledge these flags arent typically used on items. So we can abuse them for our own use.
	for (Room1* room1 = player->pAct->pRoom1; room1; room1 = room1->pRoomNext) {
		for (UnitAny* unit = room1->pUnitFirst; unit; unit = unit->pListNext) {
			if (unit->dwType == UNIT_ITEM && (unit->dwFlags & UNITFLAG_NO_EXPERIENCE) == 0x0) {
				DWORD dwFlags = unit->pItemData->dwFlags;
				UnitItemInfo uInfo;
				uInfo.item = unit;
				uInfo.itemCode[0] = D2COMMON_GetItemText(unit->dwTxtFileNo)->szCode[0];
				uInfo.itemCode[1] = D2COMMON_GetItemText(unit->dwTxtFileNo)->szCode[1];
				uInfo.itemCode[2] = D2COMMON_GetItemText(unit->dwTxtFileNo)->szCode[2];
				uInfo.itemCode[3] = 0;
				if (ItemAttributeMap.find(uInfo.itemCode) != ItemAttributeMap.end()) {
					uInfo.attrs = ItemAttributeMap[uInfo.itemCode];
					vector<Action> actions = map_action_cache.Get(&uInfo);
					for (auto &action : actions) {
						if (action.colorOnMap != UNDEFINED_COLOR ||
								action.borderColor != UNDEFINED_COLOR ||
								action.dotColor != UNDEFINED_COLOR ||
								action.pxColor != UNDEFINED_COLOR ||
								action.lineColor != UNDEFINED_COLOR) { // has map action
							// Skip notification if ping level requirement not met
							if (action.pingLevel > Item::GetPingLevel()) continue;
							unit->dwFlags |= UNITFLAG_REVEALED;
							if ((*BH::MiscToggles2)["Item Detailed Notifications"].state
							  && ((*BH::MiscToggles2)["Item Close Notifications"].state || (dwFlags & ITEMFLAG_NEW))
							  && action.notifyColor != DEAD_COLOR) {
								std::string itemName = GetItemName(unit);
								size_t start_pos = 0;
								while ((start_pos = itemName.find('\n', start_pos)) != std::string::npos) {
									itemName.replace(start_pos, 1, " - ");
									start_pos += 3;
								}
								PrintText(ItemColorFromQuality(unit->pItemData->dwQuality), "%s", itemName.c_str());
								if (!action.noTracking && !IsTown(GetPlayerArea()) && action.pingLevel <= Item::GetTrackerPingLevel()) {
									ScreenInfo::AddDrop(unit);
								}
								//PrintText(ItemColorFromQuality(unit->pItemData->dwQuality), "%s %x", itemName.c_str(), dwFlags);
								break;
							}

						}
					}
				}
			}
			unit->dwFlags |= UNITFLAG_NO_EXPERIENCE;
		}
	}
}

void Maphack::OnAutomapDraw() {
	UnitAny* player = D2CLIENT_GetPlayerUnit();
	
	if (!player || !player->pAct || player->pPath->pRoom1->pRoom2->pLevel->dwLevelNo == 0)
		return;

	if (lastAct != player->pAct){
		lastAct = player->pAct;
		automapDraw.forceUpdate();
	}

	if (!IsInitialized()){
		Drawing::Texthook::Draw(10, 70, Drawing::None, 12, Gold, "Loading MPQ Data...");
	}
	
	automapDraw.draw([=](AsyncDrawBuffer &automapBuffer) -> void {
		POINT MyPos;
		Drawing::Hook::ScreenToAutomap(&MyPos,
			D2CLIENT_GetUnitX(D2CLIENT_GetPlayerUnit()),
			D2CLIENT_GetUnitY(D2CLIENT_GetPlayerUnit()));
		for (Room1* room1 = player->pAct->pRoom1; room1; room1 = room1->pRoomNext) {
			for (UnitAny* unit = room1->pUnitFirst; unit; unit = unit->pListNext) {
				//POINT automapLoc;
				DWORD xPos, yPos;

				// Draw monster on automap
				if (unit->dwType == UNIT_MONSTER && IsValidMonster(unit) && Toggles["Show Monsters"].state) {
					int lineColor = -1;
					int color = monsterColors["Normal"];
					if (unit->pMonsterData->fBoss)
						color = monsterColors["Boss"];
					if (unit->pMonsterData->fChamp)
						color = monsterColors["Champion"];
					if (unit->pMonsterData->fMinion)
						color = monsterColors["Minion"];
					//Cow king pack
					if (unit->dwTxtFileNo == 391 &&
							unit->pMonsterData->anEnchants[0] == ENCH_MAGIC_RESISTANT &&
							unit->pMonsterData->anEnchants[1] == ENCH_LIGHTNING_ENCHANTED &&
							unit->pMonsterData->anEnchants[3] != 0)
						color = 0xE1;

					// User can override colors of non-boss monsters
					if (automapMonsterColors.find(unit->dwTxtFileNo) != automapMonsterColors.end() && !unit->pMonsterData->fBoss) {
						color = automapMonsterColors[unit->dwTxtFileNo];
					}

					// User can hide monsters from map
					if (std::find(automapHiddenMonsters.begin(), automapHiddenMonsters.end(), unit->dwTxtFileNo) != automapHiddenMonsters.end() ) {
						continue;
					}

					// User can make it draw lines to monsters
					if (automapMonsterLines.find(unit->dwTxtFileNo) != automapMonsterLines.end() ) {
						lineColor = automapMonsterLines[unit->dwTxtFileNo];
					}

					//Determine immunities
					string szImmunities[] = { "\377c0p", "\377c8i", "\377c1i", "\377c9i", "\377c3i", "\377c2i" };
					string szResistances[] = { "\377c7r", "\377c8r", "\377c1r", "\377c9r", "\377c3r", "\377c2r" };
					DWORD dwImmunities[] = {
						STAT_DMGREDUCTIONPCT,
						STAT_MAGICDMGREDUCTIONPCT,
						STAT_FIRERESIST,
						STAT_LIGHTNINGRESIST,
						STAT_COLDRESIST,
						STAT_POISONRESIST
					};
					string immunityText;
					for (int n = 0; n < 6; n++) {
						int nImm = D2COMMON_GetUnitStat(unit, dwImmunities[n], 0);
						if (nImm >= 100) {
							immunityText += szImmunities[n];
						}
						else if (nImm >= monsterResistanceThreshold) {
							immunityText += szResistances[n];
						}
					}
					
					std::unordered_map<unsigned int, bool> enhancements;

					//Determine Enchantments
					string enchantText;
					string szEnchantments[] = {"\377c3m", "\377c1e", "\377c9e", "\377c3e"};
						
					for (int n = 0; n < 9; n++) {
						if (Toggles["Monster Enchantments"].state && unit->pMonsterData->fBoss) {
							if (unit->pMonsterData->anEnchants[n] == ENCH_MANA_BURN)
								enchantText += szEnchantments[0];
							if (unit->pMonsterData->anEnchants[n] == ENCH_FIRE_ENCHANTED)
								enchantText += szEnchantments[1];
							if (unit->pMonsterData->anEnchants[n] == ENCH_LIGHTNING_ENCHANTED)
								enchantText += szEnchantments[2];
							if (unit->pMonsterData->anEnchants[n] == ENCH_COLD_ENCHANTED)
								enchantText += szEnchantments[3];
						}
						enhancements[unit->pMonsterData->anEnchants[n]] = true;
					}

					for (auto& enhancement : enhancementColors) {
						if (enhancements.find(enhancement.first) != enhancements.end() && enhancement.second > 0 && !unit->pMonsterData->fBoss) {
							color = enhancement.second;
							break;
						}
					}

					// User can override colors of super unique monsters
					if (unit->pMonsterData->fSuperUniq &&
						automapSuperUniqueColors.find(unit->pMonsterData->wUniqueNo) != automapSuperUniqueColors.end()) {
						color = automapSuperUniqueColors[unit->pMonsterData->wUniqueNo];
					}

					// auras has highest predence
					if (enhancements[ENCH_AURACHANT]) {
						for (auto& enhancement : auraColors) {
							if (D2COMMON_GetUnitState(unit, enhancement.first) != 0 && enhancement.second > 0) {
								color = enhancement.second;
								break;
							}
						}
					}

					xPos = unit->pPath->xPos;
					yPos = unit->pPath->yPos;
					automapBuffer.push([immunityText, enchantText, color, xPos, yPos, lineColor, MyPos]()->void{
						POINT automapLoc;
						Drawing::Hook::ScreenToAutomap(&automapLoc, xPos, yPos);
						if (immunityText.length() > 0)
							Drawing::Texthook::Draw(automapLoc.x, automapLoc.y - 8, Drawing::Center, 6, White, immunityText);
						if (enchantText.length() > 0)
							Drawing::Texthook::Draw(automapLoc.x, automapLoc.y - 14, Drawing::Center, 6, White, enchantText);
						Drawing::Crosshook::Draw(automapLoc.x, automapLoc.y, color);
						if (lineColor != -1) {
							Drawing::Linehook::Draw(MyPos.x, MyPos.y, automapLoc.x, automapLoc.y, lineColor);
						}
					});
				}
				else if (unit->dwType == UNIT_MISSILE && Toggles["Show Missiles"].state) {
					int color = 255;
					switch (GetRelation(unit)) {
					case 0:
						continue;
						break;
					case 1://Me
						color = missileColors["Player"];
						break;
					case 2://Neutral
						color = missileColors["Neutral"];
						break;
					case 3://Partied
						color = missileColors["Party"];
						break;
					case 4://Hostile
						color = missileColors["Hostile"];
						break;
					}

					xPos = unit->pPath->xPos;
					yPos = unit->pPath->yPos;					
					automapBuffer.push([color, unit, xPos, yPos]()->void{
						POINT automapLoc;
						Drawing::Hook::ScreenToAutomap(&automapLoc, xPos, yPos);
						Drawing::Boxhook::Draw(automapLoc.x - 1, automapLoc.y - 1, 2, 2, color, Drawing::BTHighlight);
					});
				}
				else if (unit->dwType == UNIT_ITEM && (unit->dwFlags & UNITFLAG_REVEALED) == UNITFLAG_REVEALED) {
					UnitItemInfo uInfo;
					uInfo.item = unit;
					uInfo.itemCode[0] = D2COMMON_GetItemText(unit->dwTxtFileNo)->szCode[0];
					uInfo.itemCode[1] = D2COMMON_GetItemText(unit->dwTxtFileNo)->szCode[1];
					uInfo.itemCode[2] = D2COMMON_GetItemText(unit->dwTxtFileNo)->szCode[2];
					uInfo.itemCode[3] = 0;
					if (ItemAttributeMap.find(uInfo.itemCode) != ItemAttributeMap.end()) {
						uInfo.attrs = ItemAttributeMap[uInfo.itemCode];
						const vector<Action> actions = map_action_cache.Get(&uInfo);
						for (auto &action : actions) {
							// skip action if the ping level requirement isn't met
							if (action.pingLevel > Item::GetPingLevel()) continue;
							auto color = action.colorOnMap;
							auto borderColor = action.borderColor;
							auto dotColor = action.dotColor;
							auto pxColor = action.pxColor;
							auto lineColor = action.lineColor;
							xPos = unit->pItemPath->dwPosX;
							yPos = unit->pItemPath->dwPosY;
							automapBuffer.push_top_layer(
									[color, unit, xPos, yPos, MyPos, borderColor, dotColor, pxColor, lineColor]()->void{
								POINT automapLoc;
								Drawing::Hook::ScreenToAutomap(&automapLoc, xPos, yPos);
								if (borderColor != UNDEFINED_COLOR)
									Drawing::Boxhook::Draw(automapLoc.x - 4, automapLoc.y - 4, 8, 8, borderColor, Drawing::BTHighlight);
								if (color != UNDEFINED_COLOR)
									Drawing::Boxhook::Draw(automapLoc.x - 3, automapLoc.y - 3, 6, 6, color, Drawing::BTHighlight);
								if (dotColor != UNDEFINED_COLOR)
									Drawing::Boxhook::Draw(automapLoc.x - 2, automapLoc.y - 2, 4, 4, dotColor, Drawing::BTHighlight);
								if (pxColor != UNDEFINED_COLOR)
									Drawing::Boxhook::Draw(automapLoc.x - 1, automapLoc.y - 1, 2, 2, pxColor, Drawing::BTHighlight);
								if (lineColor != UNDEFINED_COLOR) {
									Drawing::Linehook::Draw(MyPos.x, MyPos.y, automapLoc.x, automapLoc.y, lineColor);
								}
							});
							if (action.stopProcessing) break;
						}
					}
					else {
						HandleUnknownItemCode(uInfo.itemCode, "on map");
					}
				}
				else if (unit->dwType == UNIT_OBJECT && !unit->dwMode /* Not opened */ && Toggles["Show Chests"].state && IsObjectChest(unit->pObjectData->pTxt)) {
					xPos = unit->pObjectPath->dwPosX;
					yPos = unit->pObjectPath->dwPosY;
					automapBuffer.push([xPos, yPos]()->void{
						POINT automapLoc;
						Drawing::Hook::ScreenToAutomap(&automapLoc, xPos, yPos);
						Drawing::Boxhook::Draw(automapLoc.x - 1, automapLoc.y - 1, 2, 2, 255, Drawing::BTHighlight);
					});
				}				
			}
		}
		if (lkLinesColor > 0 && player->pPath->pRoom1->pRoom2->pLevel->dwLevelNo == MAP_A3_LOWER_KURAST) {
			for(Room2 *pRoom =  player->pPath->pRoom1->pRoom2->pLevel->pRoom2First; pRoom; pRoom = pRoom->pRoom2Next) {
				for (PresetUnit* preset = pRoom->pPreset; preset; preset = preset->pPresetNext) {
					DWORD xPos, yPos;
					int lkLineColor = lkLinesColor;
					if (preset->dwTxtFileNo == 160) {
						xPos = (preset->dwPosX) + (pRoom->dwPosX * 5);
						yPos = (preset->dwPosY) + (pRoom->dwPosY * 5);
						automapBuffer.push([xPos, yPos, MyPos, lkLineColor]()->void{
							POINT automapLoc;
							Drawing::Hook::ScreenToAutomap(&automapLoc, xPos, yPos);
							Drawing::Linehook::Draw(MyPos.x, MyPos.y, automapLoc.x, automapLoc.y, lkLineColor);
						});
					}
				}
			}
		}
		if (!Toggles["Display Level Names"].state)
			return;
		for (list<LevelList*>::iterator it = automapLevels.begin(); it != automapLevels.end(); it++) {
			if (player->pAct->dwAct == (*it)->act) {
				string tombStar = ((*it)->levelId == player->pAct->pMisc->dwStaffTombLevel) ? "\377c2*" : "\377c4";
				POINT unitLoc;
				Hook::ScreenToAutomap(&unitLoc, (*it)->x, (*it)->y);
				char* name = UnicodeToAnsi(D2CLIENT_GetLevelName((*it)->levelId));
				std::string nameStr = name;
				delete[] name;

				automapBuffer.push([nameStr, tombStar, unitLoc]()->void{
					Texthook::Draw(unitLoc.x, unitLoc.y - 15, Center, 6, Gold, "%s%s", nameStr.c_str(), tombStar.c_str());
				});
			}
		}
	});
}

void Maphack::OnGameJoin() {
	ResetRevealed();
	automapLevels.clear();
	*p_D2CLIENT_AutomapOn = Toggles["Show Automap On Join"].state;
}

void Squelch(DWORD Id, BYTE button) {
	LPBYTE aPacket = new BYTE[7];	//create packet
	*(BYTE*)&aPacket[0] = 0x5d;	
	*(BYTE*)&aPacket[1] = button;	
	*(BYTE*)&aPacket[2] = 1;	
	*(DWORD*)&aPacket[3] = Id;
	D2NET_SendPacket(7, 0, aPacket);

	delete [] aPacket;	//clearing up data

	return;
}

void Maphack::OnGamePacketRecv(BYTE *packet, bool *block) {
	switch (packet[0]) {

	case 0x9c: {
		INT64 icode   = 0;
        char code[5]  = "";
        BYTE mode     = packet[1];
        DWORD gid     = *(DWORD*)&packet[4];
        BYTE dest     = ((packet[13] & 0x1C) >> 2);

        switch(dest)
        {
                case 0: 
                case 2:
                        icode = *(INT64 *)(packet+15)>>0x04;
                        break;
                case 3:
                case 4:
                case 6:
                        if(!((mode == 0 || mode == 2) && dest == 3))
                        {
                                if(mode != 0xF && mode != 1 && mode != 12)
                                        icode = *(INT64 *)(packet+17) >> 0x1C;
                                else
                                        icode = *(INT64 *)(packet+15) >> 0x04;
                        } 
                        else  
                                icode = *(INT64 *)(packet+17) >> 0x05;
                        break;
                default:
                        break;
        }

        memcpy(code, &icode, 4);
        if(code[3] == ' ') code[3] = '\0';

        //PrintText(1, "%s", code);

		//if(mode == 0x0 || mode == 0x2 || mode == 0x3) {
		//	BYTE ear = packet[10] & 0x01;
		//	if(ear) *block = true;
		//}
		break;
		}

	case 0xa8:
	case 0xa7: {
			//if(packet[1] == 0x0) {
			//	if(packet[6+(packet[0]-0xa7)] == 100) {
			//		UnitAny* pUnit = D2CLIENT_FindServerSideUnit(*(DWORD*)&packet[2], 0);
			//		if(pUnit)
			//			PrintText(1, "Alert: \377c4Player \377c2%s \377c4drank a \377c1Health \377c4potion!", pUnit->pPlayerData->szName);
			//	} else if (packet[6+(packet[0]-0xa7)] == 105) {
			//		UnitAny* pUnit = D2CLIENT_FindServerSideUnit(*(DWORD*)&packet[2], 0);
			//		if(pUnit)
			//			if(pUnit->dwTxtFileNo == 1)
			//				if(D2COMMON_GetUnitState(pUnit, 30))
			//					PrintText(1, "Alert: \377c4ES Sorc \377c2%s \377c4drank a \377c3Mana \377c4Potion!", pUnit->pPlayerData->szName);
			//	} else if (packet[6+(packet[0]-0xa7)] == 102) {//remove portal delay
			//		*block = true;
			//	}
			//}
			break;			   
		}
	case 0x94: {
			BYTE Count = packet[1];
			DWORD Id = *(DWORD*)&packet[2];
			for(DWORD i = 0;i < Count;i++) {
				BaseSkill S;
				S.Skill = *(WORD*)&packet[6+(3*i)];
				S.Level = *(BYTE*)&packet[8+(3*i)];
				Skills[Id].push_back(S);
			}
			//for(vector<BaseSkill>::iterator it = Skills[Id].begin();  it != Skills[Id].end(); it++)
			//	PrintText(1, "Skill %d, Level %d", it->Skill, it->Level);
			break;
		}
	case 0x5b: {	//36   Player In Game      5b [WORD Packet Length] [DWORD Player Id] [BYTE Char Type] [NULLSTRING[16] Char Name] [WORD Char Lvl] [WORD Party Id] 00 00 00 00 00 00 00 00
			WORD lvl = *(WORD*)&packet[24];
			DWORD Id = *(DWORD*)&packet[3];
			char* name = (char*)&packet[8];
			UnitAny* Me = D2CLIENT_GetPlayerUnit();
			if(!Me)
				return;
			else if (!strcmp(name, Me->pPlayerData->szName))
				return;
			//if(lvl < 9)
			//	Squelch(Id, 3);
		}			//2 = mute, 3 = squelch, 4 = hostile
	}
}

void Maphack::RevealGame() {
	// Check if we have already revealed the game.
	if (revealedGame)
		return;

	// Iterate every act and reveal it.
	for (int act = 1; act <= ((*p_D2CLIENT_ExpCharFlag) ? 5 : 4); act++) {
		RevealAct(act);
	}

	revealedGame = true;
}

void Maphack::RevealAct(int act) {
	// Make sure we are given a valid act
	if (act < 1 || act > 5)
		return;

	// Check if the act is already revealed
	if (revealedAct[act])
		return;

	UnitAny* player = D2CLIENT_GetPlayerUnit();
	if (!player || !player->pAct)
		return;

	// Initalize the act incase it is isn't the act we are in.
	int actIds[6] = {1, 40, 75, 103, 109, 137};
	Act* pAct = D2COMMON_LoadAct(act - 1, player->pAct->dwMapSeed, *p_D2CLIENT_ExpCharFlag, 0, D2CLIENT_GetDifficulty(), NULL, actIds[act - 1], D2CLIENT_LoadAct_1, D2CLIENT_LoadAct_2);
	if (!pAct || !pAct->pMisc)
		return;

	// Iterate every level for the given act.
	for (int level = actIds[act - 1]; level < actIds[act]; level++) {
		Level* pLevel = GetLevel(pAct, level);
		if (!pLevel)
			continue;
		if (!pLevel->pRoom2First)
			D2COMMON_InitLevel(pLevel);
		RevealLevel(pLevel);
	}

	InitLayer(player->pPath->pRoom1->pRoom2->pLevel->dwLevelNo);
	D2COMMON_UnloadAct(pAct);
	revealedAct[act] = true;
}

void Maphack::RevealLevel(Level* level) {
	// Basic sanity checks to ensure valid level
	if (!level || level->dwLevelNo < 0 || level->dwLevelNo > 255)
		return;

	// Check if the level has been previous revealed.
	if (revealedLevel[level->dwLevelNo])
		return;

	InitLayer(level->dwLevelNo);

	// Iterate every room in the level.
	for(Room2* room = level->pRoom2First; room; room = room->pRoom2Next) {
		bool roomData = false;

		//Add Room1 Data if it is not already there.
		if (!room->pRoom1) {
			D2COMMON_AddRoomData(level->pMisc->pAct, level->dwLevelNo, room->dwPosX, room->dwPosY, room->pRoom1);
			roomData = true;
		}

		//Make sure we have Room1
		if (!room->pRoom1)
			continue;

		//Reveal the room
		D2CLIENT_RevealAutomapRoom(room->pRoom1, TRUE, *p_D2CLIENT_AutomapLayer);

		//Reveal the presets
		RevealRoom(room);

		//Remove Data if Added
		if (roomData)
			D2COMMON_RemoveRoomData(level->pMisc->pAct, level->dwLevelNo, room->dwPosX, room->dwPosY, room->pRoom1);
	}

	revealedLevel[level->dwLevelNo] = true;
}

void Maphack::RevealRoom(Room2* room) {
	//Grabs all the preset units in room.
	for (PresetUnit* preset = room->pPreset; preset; preset = preset->pPresetNext)
	{
		int cellNo = -1;
		
		// Special NPC Check
		if (preset->dwType == UNIT_MONSTER)
		{
			// Izual Check
			if (preset->dwTxtFileNo == 256)
				cellNo = 300;
			// Hephasto Check
			if (preset->dwTxtFileNo == 745)
				cellNo = 745;
		// Special Object Check
		} else if (preset->dwType == UNIT_OBJECT) {
			// Uber Chest in Lower Kurast Check
			if (preset->dwTxtFileNo == 580 && room->pLevel->dwLevelNo == MAP_A3_LOWER_KURAST)
				cellNo = 318;

			// Countess Chest Check
			if (preset->dwTxtFileNo == 371) 
				cellNo = 301;
			// Act 2 Orifice Check
			else if (preset->dwTxtFileNo == 152) 
				cellNo = 300;
			// Frozen Anya Check
			else if (preset->dwTxtFileNo == 460) 
				cellNo = 1468; 
			// Canyon / Arcane Waypoint Check
			if ((preset->dwTxtFileNo == 402) && (room->pLevel->dwLevelNo == 46))
				cellNo = 0;
			// Hell Forge Check
			if (preset->dwTxtFileNo == 376)
				cellNo = 376;

			// If it isn't special, check for a preset.
			if (cellNo == -1 && preset->dwTxtFileNo <= 572) {
				ObjectTxt *obj = D2COMMON_GetObjectTxt(preset->dwTxtFileNo);
				if (obj)
					cellNo = obj->nAutoMap;//Set the cell number then.
			}
		} else if (preset->dwType == UNIT_TILE) {
			LevelList* level = new LevelList;
			for (RoomTile* tile = room->pRoomTiles; tile; tile = tile->pNext) {
				if (*(tile->nNum) == preset->dwTxtFileNo) {
					level->levelId = tile->pRoom2->pLevel->dwLevelNo;
					break;
				}
			}
			level->x = (preset->dwPosX + (room->dwPosX * 5));
			level->y = (preset->dwPosY + (room->dwPosY * 5));
			level->act = room->pLevel->pMisc->pAct->dwAct;
			automapLevels.push_back(level);
		}

		//Draw the cell if wanted.
		if ((cellNo > 0) && (cellNo < 1258))
		{
			AutomapCell* cell = D2CLIENT_NewAutomapCell();

			cell->nCellNo = cellNo;
			int x = (preset->dwPosX + (room->dwPosX * 5));
			int y = (preset->dwPosY + (room->dwPosY * 5));
			cell->xPixel = (((x - y) * 16) / 10) + 1;
			cell->yPixel = (((y + x) * 8) / 10) - 3;

			D2CLIENT_AddAutomapCell(cell, &((*p_D2CLIENT_AutomapLayer)->pObjects));
		}

	}
	return;
}

AutomapLayer* Maphack::InitLayer(int level) {
	//Get the layer for the level.
	AutomapLayer2 *layer = D2COMMON_GetLayer(level);

	//Insure we have found the Layer.
	if (!layer)
		return false;

	//Initalize the layer!
	return (AutomapLayer*)D2CLIENT_InitAutomapLayer(layer->nLayerNo);
}

Level* Maphack::GetLevel(Act* pAct, int level)
{
	//Insure that the shit we are getting is good.
	if (level < 0 || !pAct)
		return NULL;

	//Loop all the levels in this act
	
	for(Level* pLevel = pAct->pMisc->pLevelFirst; pLevel; pLevel = pLevel->pNextLevel)
	{
		//Check if we have reached a bad level.
		if (!pLevel)
			break;

		//If we have found the level, return it!
		if (pLevel->dwLevelNo == level && pLevel->dwPosX > 0)
			return pLevel;
	}
	//Default old-way of finding level.
	return D2COMMON_GetLevel(pAct->pMisc, level);
}

int HoverMonsterColor(UnitAny *pUnit) {
	int color = White;
	if (pUnit->pMonsterData->fBoss)
		color = Gold;
	if (pUnit->pMonsterData->fChamp)
		color = Blue;
	return color;
}
int HoverObjectPatch(UnitAny* pUnit, DWORD tY, DWORD unk1, DWORD unk2, DWORD tX, wchar_t *wTxt)
{
	if (!pUnit || pUnit->dwType != UNIT_MONSTER || pUnit->pMonsterData->pMonStatsTxt->bAlign != MONSTAT_ALIGN_ENEMY)
		return 0;
	DWORD dwImmunities[] = {
		STAT_DMGREDUCTIONPCT,
		STAT_MAGICDMGREDUCTIONPCT,
		STAT_FIRERESIST,
		STAT_LIGHTNINGRESIST,
		STAT_COLDRESIST,
		STAT_POISONRESIST
	};
	int dwResistances[] = {
		0,0,0,0,0,0
	};
	for (int n = 0; n < 6; n++) {
		dwResistances[n] = D2COMMON_GetUnitStat(pUnit, dwImmunities[n], 0);
	}
	double maxhp = (double)(D2COMMON_GetUnitStat(pUnit, STAT_MAXHP, 0) >> 8);
	double hp = (double)(D2COMMON_GetUnitStat(pUnit, STAT_HP, 0) >> 8);
	POINT p = Texthook::GetTextSize(wTxt, 1);
	int center = tX + (p.x / 2);
	int y = tY - p.y;
	Texthook::Draw(center, y - 12, Center, 6, White, L"\377c7%d \377c8%d \377c1%d \377c9%d \377c3%d \377c2%d", dwResistances[0], dwResistances[1], dwResistances[2], dwResistances[3], dwResistances[4], dwResistances[5]);
	Texthook::Draw(center, y, Center, 6, White, L"\377c%d%s", HoverMonsterColor(pUnit), wTxt);
	Texthook::Draw(center, y + 8, Center, 6, White, L"%.0f%%", (hp / maxhp) * 100.0);
	return 1;
}


void __declspec(naked) Weather_Interception()
{
	__asm {
		je rainold
		xor al,al
rainold:
		ret 0x04
	}
}

BOOL __fastcall InfravisionPatch(UnitAny *unit)
{
	return false;
}

void __declspec(naked) Lighting_Interception()
{
	__asm {
		je lightold
		mov eax,0xff
		mov byte ptr [esp+4+0], al
		mov byte ptr [esp+4+1], al
		mov byte ptr [esp+4+2], al
		add dword ptr [esp], 0x72;
		ret
		lightold:
		push esi
		call D2COMMON_GetLevelIdFromRoom_I;
		ret
	}
}

void __declspec(naked) Infravision_Interception()
{
	__asm {
		mov ecx, esi
		call InfravisionPatch
		add dword ptr [esp], 0x72
		ret
	}
}

VOID __stdcall Shake_Interception(LPDWORD lpX, LPDWORD lpY)
{

	*p_D2CLIENT_xShake = 0;
	*p_D2CLIENT_yShake = 0;

}

//basically call HoverObjectPatch, if that function returns 0 execute
//the normal display code used basically for any hovered 
//object text (stash, merc, akara, etc...). if it returned 1
//that means we did our custom display text and shouldn't
//execute the draw method
void __declspec(naked) HoverObject_Interception()
{
	static DWORD rtn = 0;
	__asm {
		pop rtn
		push eax
		push ecx
		push edx
		call D2CLIENT_HoveredUnit_I
		push[esp + 0x10]
		push eax
		call HoverObjectPatch
		cmp eax, 0
		je origobjectname
		push rtn
		ret 0x28
		origobjectname:
		add esp, 0x8
		pop edx
		pop ecx
		pop eax
		call D2WIN_DrawTextBuffer
		push rtn
		ret
	}
}

//credits to https://github.com/jieaido/d2hackmap/blob/master/SkipNpcMessage.cpp
void  __declspec(naked) NPCMessageLoopPatch_ASM()
{
	__asm {
		test eax, eax
		jne noje
		mov eax, [mSkipQuestMessage]
		cmp eax, 0
		je oldje
		cmp[fSkipMessageReq], 0
		je oldje
		add[mSkipMessageTimer], 1
		cmp[mSkipMessageTimer], eax
		jle oldje
		mov[mSkipMessageTimer], 0
		mov eax, 1
		ret
	oldje :
		xor eax, eax
		add dword ptr[esp], 0xB9  // 0F84B8000000
	noje :
		ret
	}
}

void __declspec(naked) NPCQuestMessageStartPatch_ASM()
{
	__asm {
		mov[fSkipMessageReq], 1
		mov[mSkipMessageTimer], 0
		//oldcode:
		mov ecx, dword ptr[esi + 0x0C]
		movzx edx, di
		ret
	}
}

void __declspec(naked) NPCQuestMessageEndPatch1_ASM()
{
	__asm {
		mov[fSkipMessageReq], 0
		//oldcode:
		mov eax, dword ptr[esp + 0x24]
		mov ecx, dword ptr[esp + 0x20]
		ret
	}
}

void __declspec(naked) NPCQuestMessageEndPatch2_ASM()
{
	__asm {
		mov[fSkipMessageReq], 0
		//oldcode:
		mov edx, 1
		ret
	}
}

#pragma optimize( "", on)
