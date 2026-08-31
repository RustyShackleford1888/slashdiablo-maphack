#include "ItemMover.h"
#include "../Item/Item.h"
#include "../Gambling/Gambling.h"
#include "../Glossary/Glossary.h"
#include "../StatsPoints/StatsPoints.h"
#include "../SkillsPoints/SkillsPoints.h"
#include "../../BH.h"
#include "../../D2Ptrs.h"
#include "../../D2Stubs.h"
#include "../../D2Helpers.h"
#include "../../TableReader.h"
#include "../ScreenInfo/ScreenInfo.h"
#include <set>
#include <map>
#include <vector>
#include <string>
#include <cstring>

// This module was inspired by the RedVex plugin "Item Mover", written by kaiks.
// Thanks to kaiks for sharing his code.

#define INVENTORY_WIDTH  inventoryLayout->SlotWidth
#define INVENTORY_HEIGHT inventoryLayout->SlotHeight
#define INVENTORY_LEFT   inventoryLayout->Left
#define INVENTORY_RIGHT  inventoryLayout->Right
#define INVENTORY_TOP    inventoryLayout->Top
#define INVENTORY_BOTTOM inventoryLayout->Bottom

#define STASH_WIDTH  stashLayout->SlotWidth
#define STASH_HEIGHT stashLayout->SlotHeight
#define STASH_LEFT   stashLayout->Left
#define STASH_RIGHT  stashLayout->Right
#define STASH_TOP    stashLayout->Top
#define STASH_BOTTOM stashLayout->Bottom

#define CUBE_WIDTH  cubeLayout->SlotWidth
#define CUBE_HEIGHT cubeLayout->SlotHeight
#define CUBE_LEFT   cubeLayout->Left
#define CUBE_RIGHT  cubeLayout->Right
#define CUBE_TOP    cubeLayout->Top
#define CUBE_BOTTOM cubeLayout->Bottom

#define CELL_SIZE inventoryLayout->SlotPixelHeight

std::string POTIONS[] = { "hp", "mp", "rv" };

DWORD idBookId;
DWORD unidItemId;

bool ItemMover::Init() {
	BnetData* pData = (*p_D2LAUNCH_BnData);
	if (!pData) { return false; }
	int xpac = pData->nCharFlags & PLAYER_TYPE_EXPANSION;

	if (xpac) {
		stashLayout = p_D2CLIENT_StashLayout;
		StashItemIds = LODStashItemIds;
	}
	else {
		stashLayout = p_D2CLIENT_ClassicStashLayout;
		StashItemIds = ClassicStashItemIds;
	}
	inventoryLayout = p_D2CLIENT_InventoryLayout;
	cubeLayout = p_D2CLIENT_CubeLayout;

	if (!InventoryItemIds) {
		InventoryItemIds = new int[INVENTORY_WIDTH * INVENTORY_HEIGHT];
	}
	if (!StashItemIds) {
		StashItemIds = new int[STASH_WIDTH * STASH_HEIGHT];
	}
	if (!CubeItemIds) {
		CubeItemIds = new int[CUBE_WIDTH * CUBE_HEIGHT];
	}

	FirstInit = true;

	//PrintText(1, "Got positions: %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d",
	//	INVENTORY_WIDTH,
	//	INVENTORY_HEIGHT,
	//	STASH_WIDTH,
	//	STASH_HEIGHT,
	//	CUBE_WIDTH,
	//	CUBE_HEIGHT,
	//	INVENTORY_LEFT,
	//	INVENTORY_TOP,
	//	STASH_LEFT,
	//	STASH_TOP,
	//	CUBE_LEFT,
	//	CUBE_TOP,
	//	CELL_SIZE
	//);

	return true;
}

bool ItemMover::LoadInventory(UnitAny *unit, int source, int sourceX, int sourceY, bool shiftState, bool ctrlState, int stashUI, int invUI) {
	bool returnValue = false;

	memset(InventoryItemIds, 0, INVENTORY_WIDTH * INVENTORY_HEIGHT * sizeof(int));
	memset(StashItemIds, 0, STASH_WIDTH * STASH_HEIGHT * sizeof(int));
	memset(CubeItemIds, 0, CUBE_WIDTH * CUBE_HEIGHT * sizeof(int));

	unsigned int itemId = 0;
	BYTE itemXSize, itemYSize;
	bool cubeInInventory = false, cubeAnywhere = false;
	for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
		int *p, width;
		if (pItem->pItemData->ItemLocation == STORAGE_INVENTORY) {
			p = InventoryItemIds;
			width = INVENTORY_WIDTH;
		} else if (pItem->pItemData->ItemLocation == STORAGE_STASH) {
			p = StashItemIds;
			width = STASH_WIDTH;
		} else if (pItem->pItemData->ItemLocation == STORAGE_CUBE) {
			p = CubeItemIds;
			width = CUBE_WIDTH;
		} else {
			continue;
		}

		bool box = false;
		char *code = D2COMMON_GetItemText(pItem->dwTxtFileNo)->szCode;
		if (code[0] == 'b' && code[1] == 'o' && code[2] == 'x') {
			if (pItem->pItemData->ItemLocation == STORAGE_INVENTORY) {
				cubeInInventory = true;
				cubeAnywhere = true;
			}
			if (pItem->pItemData->ItemLocation == STORAGE_STASH) {
				cubeAnywhere = true;
			}
			box = true;
		}

		int xStart = pItem->pObjectPath->dwPosX;
		int yStart = pItem->pObjectPath->dwPosY;
		BYTE xSize = D2COMMON_GetItemText(pItem->dwTxtFileNo)->xSize;
		BYTE ySize = D2COMMON_GetItemText(pItem->dwTxtFileNo)->ySize;
		for (int x = xStart; x < xStart + xSize; x++) {
			for (int y = yStart; y < yStart + ySize; y++) {
				p[y*width + x] = pItem->dwUnitId;

				// If you click to move the cube into itself, your character ends up in
				// the amusing (and apparently permanent) state where he has no visible
				// cube and yet is unable to pick one up. Logging out does not fix it.
				// So we disable all cube movements to be on the safe side.
				if (x == sourceX && y == sourceY && pItem->pItemData->ItemLocation == source && !box) {
					// This is the item we want to move
					itemId = pItem->dwUnitId;
					itemXSize = xSize;
					itemYSize = ySize;
				}
			}
		}
	}

	int destination;
	if (ctrlState && shiftState && ((stashUI && cubeAnywhere) || (invUI && cubeInInventory)) && source != STORAGE_CUBE) {
		destination = STORAGE_CUBE;
	} else if (ctrlState) {
		destination = STORAGE_NULL;  // i.e. the ground
	} else if (source == STORAGE_STASH || source == STORAGE_CUBE) {
		destination = STORAGE_INVENTORY;
	} else if (source == STORAGE_INVENTORY && D2CLIENT_GetUIState(UI_STASH)) {
		destination = STORAGE_STASH;
	} else if (source == STORAGE_INVENTORY && D2CLIENT_GetUIState(UI_CUBE)) {
		destination = STORAGE_CUBE;
	} else {
		return false;
	}

	// Find a spot for the item in the destination container
	if (itemId > 0) {
		returnValue = FindDestination(destination, itemId, itemXSize, itemYSize);
	}

	FirstInit = true;
	return returnValue;
}

bool ItemMover::FindDestination(int destination, unsigned int itemId, BYTE xSize, BYTE ySize) {
	int *p, width = 0, height = 0;
	if (destination == STORAGE_INVENTORY) {
		p = InventoryItemIds;
		width = INVENTORY_WIDTH;
		height = INVENTORY_HEIGHT;
	} else if (destination == STORAGE_STASH) {
		p = StashItemIds;
		width = STASH_WIDTH;
		height = STASH_HEIGHT;
	} else if (destination == STORAGE_CUBE) {
		p = CubeItemIds;
		width = CUBE_WIDTH;
		height = CUBE_HEIGHT;
	}

	bool found = false;
	int destX = 0, destY = 0;
	if (width) {
		bool first_y = true;
		for (int x = 0; x < width; x++) {
			for (int y = 0; y < height; y++) {
				bool abort = false;
				int vacancies = 0;
				for (int testx = x; testx < x + xSize && testx < width; testx++) {
					for (int testy = y; testy < y + ySize && testy < height; testy++) {
						if (p[testy*width + testx]) {
							abort = true;
							break;
						} else {
							vacancies++;
						}
					}
					if (abort) {
						break;
					}
				}
				if (vacancies == xSize * ySize) {
					// Found an empty spot that's big enough for the item
					found = true;
					destX = x;
					destY = y;
					break;
				}
				if (xSize == 1) {
					if (first_y) {
						if (x + 1 < width) {
							x++;
							y--;
							first_y = false;
						}
					} else {
						first_y = true;
						x--;
					}
				}
			} // end y loop
			if (found) {
				break;
			}
			if (xSize == 2 && x % 2 == 0 && x + 2 >= width) {
				x = 0;
			} else {
				x++;
			}
		} // end x loop
	} else {
		found = true;
	}

		if (found) {
		Lock();
		if (ActivePacket.startTicks == 0) {
			ActivePacket.itemId = itemId;
			ActivePacket.x = destX;
			ActivePacket.y = destY;
			ActivePacket.startTicks = BHGetTickCount();
			ActivePacket.destination = destination;
		}
		Unlock();
	}

	return found;
}

void ItemMover::PickUpItem() {
	BYTE PacketData[5] = {0x19,0,0,0,0};
	*reinterpret_cast<int*>(PacketData + 1) = ActivePacket.itemId;
	D2NET_SendPacket(5, 1, PacketData);
}

void ItemMover::PutItemInContainer() {
	BYTE PacketData[17] = {0x18,0,0,0,0};
	*reinterpret_cast<int*>(PacketData + 1) = ActivePacket.itemId;
	*reinterpret_cast<int*>(PacketData + 5) = ActivePacket.x;
	*reinterpret_cast<int*>(PacketData + 9) = ActivePacket.y;
	*reinterpret_cast<int*>(PacketData + 13)= ActivePacket.destination;
	D2NET_SendPacket(17, 1, PacketData);
}

void ItemMover::PutItemOnGround() {
	BYTE PacketData[5] = {0x17,0,0,0,0};
	*reinterpret_cast<int*>(PacketData + 1) = ActivePacket.itemId;
	D2NET_SendPacket(5, 1, PacketData);
}

void ItemMover::StackCursorItemOnTarget(DWORD cursorItemId, DWORD targetItemId) {
	if (cursorItemId == 0 || targetItemId == 0 || cursorItemId == targetItemId) {
		return;
	}
	BYTE PacketData[9] = {0x21,0,0,0,0,0,0,0,0};
	*reinterpret_cast<int*>(PacketData + 1) = cursorItemId;
	*reinterpret_cast<int*>(PacketData + 5) = targetItemId;
	D2NET_SendPacket(9, 1, PacketData);
}

void ItemMover::OnLeftClick(bool up, unsigned int x, unsigned int y, bool* block) {
	if (!up) {
		StopAutoCubeFromUserClick();
	}
	UnitAny *unit = D2CLIENT_GetPlayerUnit();
	bool shiftState = ((GetKeyState(VK_LSHIFT) & 0x80) || (GetKeyState(VK_RSHIFT) & 0x80));
	
	// Don't allow movement if there's already an item being moved (same check as auto-cube uses)
	Lock();
	bool itemInProgress = (ActivePacket.startTicks > 0);
	Unlock();
	
	if (up || !unit || !shiftState || D2CLIENT_GetCursorItem()>0 ||
		(!D2CLIENT_GetUIState(UI_INVENTORY) && !D2CLIENT_GetUIState(UI_STASH)
			&& !D2CLIENT_GetUIState(UI_CUBE) && !D2CLIENT_GetUIState(UI_NPCSHOP)) ||
		!Init() || itemInProgress) {
		return;
	}

	unidItemId = 0;
	idBookId = 0;
	
	int mouseX,mouseY;	

	for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
		char *code = D2COMMON_GetItemText(pItem->dwTxtFileNo)->szCode;
		if ((pItem->pItemData->dwFlags & ITEM_IDENTIFIED) <= 0) {
			int xStart = pItem->pObjectPath->dwPosX;
			int yStart = pItem->pObjectPath->dwPosY;
			BYTE xSize = D2COMMON_GetItemText(pItem->dwTxtFileNo)->xSize;
			BYTE ySize = D2COMMON_GetItemText(pItem->dwTxtFileNo)->ySize;
			if (pItem->pItemData->ItemLocation == STORAGE_INVENTORY) {
				mouseX = (*p_D2CLIENT_MouseX - INVENTORY_LEFT) / CELL_SIZE;
				mouseY = (*p_D2CLIENT_MouseY - INVENTORY_TOP) / CELL_SIZE;
			} else if(pItem->pItemData->ItemLocation == STORAGE_STASH) {
				mouseX = (*p_D2CLIENT_MouseX - STASH_LEFT) / CELL_SIZE;
				mouseY = (*p_D2CLIENT_MouseY - STASH_TOP) / CELL_SIZE;
			} else if(pItem->pItemData->ItemLocation == STORAGE_CUBE) {
				mouseX = (*p_D2CLIENT_MouseX - CUBE_LEFT) / CELL_SIZE;
				mouseY = (*p_D2CLIENT_MouseY - CUBE_TOP) / CELL_SIZE;
			}
			for (int x = xStart; x < xStart + xSize; x++) {
				for (int y = yStart; y < yStart + ySize; y++) {
					if (x == mouseX && y == mouseY) {
						if ((pItem->pItemData->ItemLocation == STORAGE_STASH && !D2CLIENT_GetUIState(UI_STASH)) || (pItem->pItemData->ItemLocation == STORAGE_CUBE && !D2CLIENT_GetUIState(UI_CUBE))) {
							return;
						}
						unidItemId = pItem->dwUnitId;								
					}
				}
			}
		}
		if (code[0] == 'i' && code[1] == 'b' && code[2] == 'k' && pItem->pItemData->ItemLocation == STORAGE_INVENTORY && D2COMMON_GetUnitStat(pItem, STAT_AMMOQUANTITY, 0)>0) {
			idBookId = pItem->dwUnitId;
		}
		if (unidItemId > 0 && idBookId > 0) {
			BYTE PacketData[13] = { 0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
			*reinterpret_cast<int*>(PacketData + 1) = idBookId;
			*reinterpret_cast<WORD*>(PacketData + 5) = (WORD)unit->pPath->xPos;
			*reinterpret_cast<WORD*>(PacketData + 9) = (WORD)unit->pPath->yPos;
			D2NET_SendPacket(13, 0, PacketData);
			*block = true;
			return;
		}
	}
}

void ItemMover::OnRightClick(bool up, unsigned int x, unsigned int y, bool* block) {
	if (!up) {
		StopAutoCubeFromUserClick();
	}
	UnitAny *unit = D2CLIENT_GetPlayerUnit();
	bool shiftState = ((GetKeyState(VK_LSHIFT) & 0x80) || (GetKeyState(VK_RSHIFT) & 0x80));
	bool ctrlState = ((GetKeyState(VK_LCONTROL) & 0x80) || (GetKeyState(VK_RCONTROL) & 0x80));
	
	// Don't allow movement if there's already an item being moved (same check as auto-cube uses)
	Lock();
	bool itemInProgress = (ActivePacket.startTicks > 0);
	Unlock();
	
	if (up || !unit || !(shiftState || ctrlState) || !Init() || itemInProgress) {
		return;
	}

	int source, sourceX, sourceY;
	int invUI = D2CLIENT_GetUIState(UI_INVENTORY);
	int stashUI = D2CLIENT_GetUIState(UI_STASH);
	int cubeUI = D2CLIENT_GetUIState(UI_CUBE);
	if ((invUI || stashUI || cubeUI) && x >= INVENTORY_LEFT && x <= INVENTORY_RIGHT && y >= INVENTORY_TOP && y <= INVENTORY_BOTTOM) {
		source = STORAGE_INVENTORY;
		sourceX = (x - INVENTORY_LEFT) / CELL_SIZE;
		sourceY = (y - INVENTORY_TOP) / CELL_SIZE;
	} else if (stashUI && x >= STASH_LEFT && x <= STASH_RIGHT && y >= STASH_TOP && y <= STASH_BOTTOM) {
		source = STORAGE_STASH;
		sourceX = (x - STASH_LEFT) / CELL_SIZE;
		sourceY = (y - STASH_TOP) / CELL_SIZE;
	} else if (cubeUI && x >= CUBE_LEFT && x <= CUBE_RIGHT && y >= CUBE_TOP && y <= CUBE_BOTTOM) {
		source = STORAGE_CUBE;
		sourceX = (x - CUBE_LEFT) / CELL_SIZE;
		sourceY = (y - CUBE_TOP) / CELL_SIZE;
	} else {
		return;
	}

	bool moveItem = LoadInventory(unit, source, sourceX, sourceY, shiftState, ctrlState, stashUI, invUI);
	if (moveItem) {
		PickUpItem();
	}
	*block = true;
}

void ItemMover::LoadConfig() {
	BH::config->ReadKey("Use TP Tome", "VK_NUMPADADD", TpKey);
	BH::config->ReadKey("Use Healing Potion", "VK_NUMPADMULTIPLY", HealKey);
	BH::config->ReadKey("Use Mana Potion", "VK_NUMPADSUBTRACT", ManaKey);
	BH::config->ReadKey("Use Rejuv Potion", "VK_NUMPADDIVIDE", JuvKey);
	BH::config->ReadKey("Cube Transmute", "None", TransmuteKey);
	BH::config->ReadKey("Auto Cube", "None", AutoCubeKey);

	BH::config->ReadInt("Low TP Warning", tp_warn_quantity);
	
	// Auto gold pickup config
	BH::config->ReadToggle("Auto Pickup Gold", "None", false, autoPickupGold);
	
	// Auto cube config
	BH::config->ReadToggle("Auto Stack Items", "None", false, autoStackItems);
	BH::config->ReadToggle("Auto Essence Gems", "None", false, autoEssenceGems);
	BH::config->ReadToggle("Auto Essence Runes", "None", false, autoEssenceRunes);
	BH::config->ReadToggle("Auto Essence Uniques", "None", false, autoEssenceUniques);
	BH::config->ReadToggle("Auto Essence Messages", "None", false, autoEssenceHccMisc);
	BH::config->ReadInt("Auto Essence Gem Quality", autoEssenceGemQuality);
	BH::config->ReadInt("Auto Essence Rune Quality", autoEssenceRuneQuality);
	BH::config->ReadInt("Auto Essence Unique Tier", autoEssenceUniqueTier);
	BH::config->ReadInt("Auto Essence Messages Tier", autoEssenceHccMiscTier);
}

void ItemMover::OnLoad() {
	LoadConfig();
	Drawing::Texthook* colored_text;

	settingsTab = new Drawing::UITab("Interaction", BH::settingsUI);

	unsigned int x = 8;
	unsigned int x2 = 280;  // Second column position (moved further right)
	unsigned int y = 7;
	unsigned int y2 = 7;    // Second column Y position
	new Drawing::Texthook(settingsTab, x, y, "Keys (esc to clear)");
	
	// First column
	new Drawing::Keyhook(settingsTab, x, (y += 15), &TpKey ,       "Quick Town Portal:     ");
	new Drawing::Keyhook(settingsTab, x, (y += 15), &HealKey,     "Use Healing Potion:    ");
	new Drawing::Keyhook(settingsTab, x, (y += 15), &ManaKey,     "Use Mana Potion:       ");
	new Drawing::Keyhook(settingsTab, x, (y += 15), &JuvKey,      "Use Rejuv Potion:      ");
	
	// Second column
	new Drawing::Keyhook(settingsTab, x2, (y2 += 15), &TransmuteKey,"Cube Transmute:        ");
	
	// Add Gambling Refresh keyhook if Gambling module is loaded (second column)
	Gambling* gambling = (Gambling*)BH::moduleManager->Get("gambling");
	if (gambling) {
		new Drawing::Keyhook(settingsTab, x2, (y2 += 15), gambling->GetRefreshKeyPtr(), "Gambling Refresh:     ");
		colored_text = new Drawing::Texthook(settingsTab, x2, (y2 += 15), "Warning: Flashing effects - Epilepsy risk");
		colored_text->SetColor(Red);
	}
	
	// Add Glossary Toggle keyhook if Glossary module is loaded (second column)
	Glossary* glossary = (Glossary*)BH::moduleManager->Get("glossary");
	if (glossary) {
		new Drawing::Keyhook(settingsTab, x2, (y2 += 15), glossary->GetGlossaryKeyPtr(), "Glossary Toggle:      ");
	}

	// Find the maximum Y position to start the bottom section
	unsigned int bottomStartY = (y > y2) ? y : y2;
	bottomStartY += 7;

	// Split bottom section into two halves
	unsigned int leftX = x;
	unsigned int rightX = x2;
	unsigned int bottomY = bottomStartY;
	unsigned int bottomY2 = bottomStartY;

	// Left side: Auto Cube settings
	new Drawing::Texthook(settingsTab, leftX, (bottomY += 15), "Auto Cube");
	colored_text = new Drawing::Texthook(settingsTab, leftX, (bottomY += 15), "Warning - experimental feature");
	colored_text->SetColor(Red);
	new Drawing::Keyhook(settingsTab, leftX, (bottomY += 15), &AutoCubeKey, "Auto Cube:            ");
	
	new Drawing::Checkhook(settingsTab, leftX, (bottomY += 15), &autoStackItems.state, "Auto Stack Items");
	
	// Auto Essence Gems checkbox with dropdown (dropdown on next line with extra spacing)
	new Drawing::Checkhook(settingsTab, leftX, (bottomY += 15), &autoEssenceGems.state, "Auto Essence Gems <=");
	std::vector<std::string> gemQualities = {"Chipped", "Flawed", "Normal", "Flawless", "Perfect"};
	new Drawing::Combohook(settingsTab, leftX + 20, (bottomY += 15), 100, &autoEssenceGemQuality, gemQualities);
	bottomY += 5;  // Extra spacing to prevent overlap
	
	// Auto Essence Runes checkbox with dropdown (dropdown on next line with extra spacing, 2 columns)
	new Drawing::Checkhook(settingsTab, leftX, (bottomY += 15), &autoEssenceRunes.state, "Auto Essence Runes <=");
	std::vector<std::string> runeQualities = {"El", "Eld", "Tir", "Nef", "Eth", "Ith", "Tal", "Ral", "Ort", "Thul", 
		"Amn", "Sol", "Shael", "Dol", "Hel", "Io", "Lum", "Ko", "Fal", "Lem", "Pul", "Um", "Mal", "Ist", "Gul", "Vex", "Ohm", "Lo", "Sur", "Ber", "Jah", "Cham", "Zod"};
	// Use 2-column dropdown for runes to fit all 33 runes
	new Drawing::Combohook(settingsTab, leftX + 20, (bottomY += 15), 180, &autoEssenceRuneQuality, runeQualities, 2);
	bottomY += 5;  // Extra spacing to prevent overlap
	
	// Auto Essence Uniques/Sets checkbox with dropdown (dropdown on next line with extra spacing)
	new Drawing::Checkhook(settingsTab, leftX, (bottomY += 15), &autoEssenceUniques.state, "Auto Essence Uniques/Sets >=");
	std::vector<std::string> uniqueTiers = {"Tier 1", "Tier 2", "Tier 3", "Tier 4", "Tier 5", "Tier 6"};
	new Drawing::Combohook(settingsTab, leftX + 20, (bottomY += 15), 100, &autoEssenceUniqueTier, uniqueTiers);
	bottomY += 5;  // Extra spacing to prevent overlap
	
	// Auto Essence Messages checkbox with dropdown (dropdown on next line with extra spacing)
	new Drawing::Checkhook(settingsTab, leftX, (bottomY += 15), &autoEssenceHccMisc.state, "Auto Essence Messages >=");
	std::vector<std::string> hccMiscTiers = {"Tier 1", "Tier 2", "Tier 3", "Tier 4", "Tier 5", "Tier 6"};
	new Drawing::Combohook(settingsTab, leftX + 20, (bottomY += 15), 100, &autoEssenceHccMiscTier, hccMiscTiers);
	bottomY += 5;  // Extra spacing to prevent overlap

	// Right side: QoL features and Auto Pickup
	new Drawing::Texthook(settingsTab, rightX, (bottomY2 += 15), "QoL features");
	colored_text = new Drawing::Texthook(settingsTab, rightX, (bottomY2 += 15),
			"Shift-rightclick moves between stash/open");
	colored_text->SetColor(Gold);
	colored_text = new Drawing::Texthook(settingsTab, rightX, (bottomY2 += 15),
			"cube and inventory");
	colored_text->SetColor(Gold);
	colored_text = new Drawing::Texthook(settingsTab, rightX, (bottomY2 += 15),
			"Ctrl-rightclick moves item to ground");
	colored_text->SetColor(Gold);
	colored_text = new Drawing::Texthook(settingsTab, rightX, (bottomY2 += 15),
			"Ctrl-shift-rightclick moves item into");
	colored_text->SetColor(Gold);
	colored_text = new Drawing::Texthook(settingsTab, rightX, (bottomY2 += 15),
			"closed cube");
	colored_text->SetColor(Gold);
	colored_text = new Drawing::Texthook(settingsTab, rightX, (bottomY2 += 15),
			"Ctrl+shift+click stats assigns all");
	colored_text->SetColor(Gold);

	bottomY2 += 7;
	interactionRightColumnX = rightX;
	interactionRightColumnY = bottomY2;
}

void ItemMover::BuildShiftClickSettingsSection() {
	StatsPoints* statsPoints = (StatsPoints*)BH::moduleManager->Get("statspoints");
	SkillsPoints* skillsPoints = (SkillsPoints*)BH::moduleManager->Get("skillspoints");

	if (statsPoints)
		statsPoints->BuildSettingsUI();
	if (skillsPoints)
		skillsPoints->BuildSettingsUI();

	BuildAutoPickupSection();
}

void ItemMover::BuildAutoPickupSection() {
	if (!settingsTab)
		return;

	unsigned int x = interactionRightColumnX;
	unsigned int& y = interactionRightColumnY;

	y += 7;
	new Drawing::Texthook(settingsTab, x, (y += 15), "Auto Pickup");
	new Drawing::Checkhook(settingsTab, x, (y += 15), &autoPickupGold.state, "Auto Pickup Gold");
}

void ItemMover::OnLoop() {
	// Handle auto-cubing incrementally (non-blocking)
	if (isAutoCubing) {
		if (stashInteractionMode) {
			ProcessStashInteraction();
		} else {
			ProcessAutoCubeStep();
		}
	}
	
	if (!autoPickupGold.state) {
		// Clear queue when auto pickup is disabled
		goldPickupQueue.clear();
		return;
	}

	UnitAny* player = D2CLIENT_GetPlayerUnit();
	if (!player || !player->pPath || !player->pAct) {
		// Clear queue if player is invalid
		goldPickupQueue.clear();
		return;
	}

	ULONGLONG currentTick = BHGetTickCount();

	// Check if inventory gold is at max capacity
	// Max gold formula: 10,000 per level (level 1 = 10,000, level 99 = 990,000)
	DWORD currentGold = (DWORD)D2COMMON_GetUnitStat(player, STAT_GOLD, 0);
	DWORD playerLevel = (DWORD)D2COMMON_GetUnitStat(player, STAT_LEVEL, 0);
	
	// Safety check: ensure playerLevel is valid (1-99)
	if (playerLevel == 0 || playerLevel > 99) {
		return;
	}
	
	DWORD maxGold = playerLevel * 10000;
	
	// If inventory gold is at max capacity, clear queue and disable auto pickup
	if (currentGold >= maxGold) {
		goldPickupQueue.clear();
		return;
	}
	
	// Get player position for distance checks
	DWORD playerX = player->pPath->xPos;
	DWORD playerY = player->pPath->yPos;
	
	// Remove items from queue that are too far away (more than 5 yards)
	// Use a threshold slightly larger than pickup range to account for movement
	const DWORD MAX_DISTANCE = 7;  // 7 yards threshold (pickup range is 5)
	for (auto it = goldPickupQueue.begin(); it != goldPickupQueue.end();) {
		DWORD distance = GetDistanceSquared(playerX, playerY, it->x, it->y);
		if (distance > MAX_DISTANCE) {
			it = goldPickupQueue.erase(it);  // Remove if too far
		} else {
			++it;
		}
	}
	
	// Remove stale entries from queue (older than 2 seconds)
	for (auto it = goldPickupQueue.begin(); it != goldPickupQueue.end();) {
		if ((currentTick - it->queueTime) > 2000) {
			it = goldPickupQueue.erase(it);  // Remove if too old
		} else {
			++it;
		}
	}
	
	// Process the queue: send one pickup packet if enough time has passed (75ms delay between pickups)
	if (!goldPickupQueue.empty()) {
		QueuedGoldPickup& queuedItem = goldPickupQueue.front();
		
		// Double-check distance before processing (in case player moved)
		DWORD distance = GetDistanceSquared(playerX, playerY, queuedItem.x, queuedItem.y);
		if (distance <= 5) {
			// Check if enough time has passed since last pickup (75ms delay)
			if (currentTick - lastPickupTick >= 75) {
				// Send packet 0x13 - player action on unit
				BYTE PacketData[9] = {0x13, 0, 0, 0, 0, 0, 0, 0, 0};
				*reinterpret_cast<DWORD*>(PacketData + 1) = 4;  // Action type 4
				*reinterpret_cast<DWORD*>(PacketData + 5) = queuedItem.itemId;
				D2NET_SendPacket(9, 1, PacketData);

				lastPickupTick = currentTick;
				goldPickupQueue.pop_front();  // Remove from queue after sending
			}
		} else {
			// Too far away, remove from queue
			goldPickupQueue.pop_front();
		}
	}

	// Limit queue size to prevent it from growing too large (max 20 items)
	const size_t MAX_QUEUE_SIZE = 20;
	if (goldPickupQueue.size() >= MAX_QUEUE_SIZE) {
		return;  // Don't add more items if queue is full
	}

	// Scan for new gold piles and add them to the queue

	if (!player->pAct || !player->pAct->pRoom1) {
		return;
	}

	// Create a set to track already queued item IDs to avoid duplicates
	std::set<DWORD> queuedItemIds;
	for (const auto& queued : goldPickupQueue) {
		queuedItemIds.insert(queued.itemId);
	}

	// Iterate through all rooms and ground items to find gold
	for (Room1* room1 = player->pAct->pRoom1; room1; room1 = room1->pRoomNext) {
		if (!room1->pUnitFirst) {
			continue;
		}

		for (UnitAny* pUnit = room1->pUnitFirst; pUnit; pUnit = pUnit->pListNext) {
			// Check if this is an item on the ground
			if (pUnit->dwType != UNIT_ITEM || !pUnit->pItemPath) {
				continue;
			}

			// Skip if already in queue
			if (queuedItemIds.find(pUnit->dwUnitId) != queuedItemIds.end()) {
				continue;
			}

			// Check if it's gold - CRITICAL: GetItemText can return NULL!
			ItemText* pItemText = D2COMMON_GetItemText(pUnit->dwTxtFileNo);
			if (!pItemText || !pItemText->szCode) {
				continue;
			}
			
			char* code = pItemText->szCode;
			if (code[0] != 'g' || code[1] != 'l' || code[2] != 'd') {
				continue;
			}

			// Calculate distance to gold
			DWORD goldX = pUnit->pItemPath->dwPosX;
			DWORD goldY = pUnit->pItemPath->dwPosY;
			DWORD distance = GetDistanceSquared(playerX, playerY, goldX, goldY);

			// If gold is within 5 yards, add it to the queue
			if (distance <= 5) {
				QueuedGoldPickup newPickup;
				newPickup.itemId = pUnit->dwUnitId;
				newPickup.x = goldX;
				newPickup.y = goldY;
				newPickup.queueTime = currentTick;
				goldPickupQueue.push_back(newPickup);
				queuedItemIds.insert(pUnit->dwUnitId);  // Track it to avoid duplicates
				
				// Limit how many we add per loop iteration
				if (goldPickupQueue.size() >= MAX_QUEUE_SIZE) {
					break;
				}
			}
		}
		
		// Break outer loop if queue is full
		if (goldPickupQueue.size() >= MAX_QUEUE_SIZE) {
			break;
		}
	}
}

void ItemMover::OnKey(bool up, BYTE key, LPARAM lParam, bool* block)  {
	UnitAny *unit = D2CLIENT_GetPlayerUnit();
	if (!unit)
		return;

	if (!up && (key == HealKey || key == ManaKey || key == JuvKey)) {
		int idx = key == JuvKey ? 2 : key == ManaKey ? 1 : 0;
		std::string startChars = POTIONS[idx];
		char minPotion = 127;
		DWORD minItemId = 0;
		bool isBelt = false;
		for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
			if (pItem->pItemData->ItemLocation == STORAGE_INVENTORY ||
				pItem->pItemData->ItemLocation == STORAGE_NULL && pItem->pItemData->NodePage == NODEPAGE_BELTSLOTS) {
				char* code = D2COMMON_GetItemText(pItem->dwTxtFileNo)->szCode;
				if (code[0] == startChars[0] && code[1] == startChars[1] && code[2] < minPotion) {
					minPotion = code[2];
					minItemId = pItem->dwUnitId;
					isBelt = pItem->pItemData->NodePage == NODEPAGE_BELTSLOTS;
				}
			}
			//char *code = D2COMMON_GetItemText(pItem->dwTxtFileNo)->szCode;
			//if (code[0] == 'b' && code[1] == 'o' && code[2] == 'x') {
			//	// Hack to pick up cube to fix cube-in-cube problem
			//	BYTE PacketDataCube[5] = {0x19,0,0,0,0};
			//	*reinterpret_cast<int*>(PacketDataCube + 1) = pItem->dwUnitId;
			//	D2NET_SendPacket(5, 1, PacketDataCube);
			//	break;
			//}
		}
		if (minItemId > 0) {
			if (isBelt){
				BYTE PacketData[13] = { 0x26, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
				*reinterpret_cast<int*>(PacketData + 1) = minItemId;
				D2NET_SendPacket(13, 0, PacketData);
			}
			else{
				//PrintText(1, "Sending packet %d, %d, %d", minItemId, unit->pPath->xPos, unit->pPath->yPos);
				BYTE PacketData[13] = { 0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
				*reinterpret_cast<int*>(PacketData + 1) = minItemId;
				*reinterpret_cast<WORD*>(PacketData + 5) = (WORD)unit->pPath->xPos;
				*reinterpret_cast<WORD*>(PacketData + 9) = (WORD)unit->pPath->yPos;
				D2NET_SendPacket(13, 0, PacketData);
			}
			*block = true;
		}
	}
	if (!up && (key == TpKey)) {
		DWORD tpId = 0;
		int tp_quantity = 0;
		bool isTome = false;
		// First, search for individual TP scrolls (prefer scrolls)
		for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
			if (pItem->pItemData->ItemLocation == STORAGE_INVENTORY) {
				char* code = D2COMMON_GetItemText(pItem->dwTxtFileNo)->szCode;
				if (code[0] == 't' && code[1] == 's' && code[2] == 'c') {
					tpId = pItem->dwUnitId;
					tp_quantity = 1; // Individual scrolls have quantity 1
					isTome = false;
					break;
				}
			}
		}
		// If no scroll found, search for TP tomes
		if (tpId == 0) {
			for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
				if (pItem->pItemData->ItemLocation == STORAGE_INVENTORY) {
					char* code = D2COMMON_GetItemText(pItem->dwTxtFileNo)->szCode;
					if (code[0] == 't' && code[1] == 'b' && code[2] =='k') {
						tp_quantity = D2COMMON_GetUnitStat(pItem, STAT_AMMOQUANTITY, 0);
						tpId = pItem->dwUnitId;
						isTome = true;
						break;
					}
				}
			}
		}
		if (tpId > 0) {
			BYTE PacketData[13] = { 0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
			*reinterpret_cast<int*>(PacketData + 1) = tpId;
			*reinterpret_cast<WORD*>(PacketData + 5) = (WORD)unit->pPath->xPos;
			*reinterpret_cast<WORD*>(PacketData + 9) = (WORD)unit->pPath->yPos;
			if (isTome) {
				if (tp_quantity > 1 && tp_quantity <= tp_warn_quantity) {
					PrintText(Red, "TP tome is running low!");					
				} else if (tp_quantity <= 1) {
					PrintText(Red, "TP tome is empty");					
				}
			}
			D2NET_SendPacket(13, 0, PacketData);
			*block = true;
		}
	}
	if (up && (key == TransmuteKey)) {
		// Only transmute when key is released (once per press)
		// Only transmute if the cube UI is open
		if (D2CLIENT_GetUIState(UI_CUBE)) {
			// Check if there's an item on the cursor
			UnitAny* pCursorItem = D2CLIENT_GetCursorItem();
			if (pCursorItem != NULL) {
				PrintText(Red, "Cannot transmute while holding an item!");
				*block = true;
			} else {
				// No item on cursor, transmute normally
				D2CLIENT_Transmute();
				*block = true;
			}
		}
	}
	if (!up && (key == AutoCubeKey)) {
		// Start auto-cubing process (non-blocking)
		if (!isAutoCubing) {
			UnitAny* unit = D2CLIENT_GetPlayerUnit();
			if (!unit || D2CLIENT_GetCursorItem() != NULL) {
				PrintText(Red, "Auto Cube: No item on cursor required");
			} else {
				bool cubeOpen = D2CLIENT_GetUIState(UI_CUBE);
				bool stashOpen = D2CLIENT_GetUIState(UI_STASH);
				
				if (!cubeOpen && !stashOpen) {
					PrintText(Red, "Auto Cube: Cube or Stash must be open");
				} else if (stashOpen && !cubeOpen) {
					// Stash mode - combine stacks in place, then cube essence recipes if enabled
					isAutoCubing = true;
					stashInteractionMode = true;
					ResetStashInteractionState();
					lastAutoCubeTick = BHGetTickCount();
					PrintText(White, "Auto Cube: Started (Stash Mode)");
				} else {
					// Normal cube mode (cube already open)
					isAutoCubing = true;
					stashInteractionMode = false;
					lastAutoCubeTick = BHGetTickCount();
					PrintText(White, "Auto Cube: Started");
				}
			}
		} else {
			// Stop auto-cubing if already running
			isAutoCubing = false;
			stashInteractionMode = false;
			ResetStashInteractionState();
			PrintText(White, "Auto Cube: Stopped");
		}
		*block = true;
	}
}

void ItemMover::OnGamePacketRecv(BYTE* packet, bool* block) {
	switch (packet[0])
	{
	case 0x3F:
		{
			// We get this packet after our cursor change. Will only ID if we found book and item previously. packet[1] = 0 guarantees the cursor is changing to "id ready" state.
			if (packet[1] == 0 && idBookId > 0 && unidItemId > 0) {
				BYTE PacketData[9] = {0x27,0,0,0,0,0,0,0,0};
				*reinterpret_cast<int*>(PacketData + 1) = unidItemId;
				*reinterpret_cast<int*>(PacketData + 5) = idBookId;
				D2NET_SendPacket(9, 0, PacketData);
				*block = true;
				// Reseting variables after we ID an item so the next ID works.
				unidItemId = 0;
				idBookId = 0;
			}
			break;
		}
		case 0x9c:
		{
			// We get this packet after placing an item in a container or on the ground
			if (FirstInit) {
				BYTE action = packet[1];
				unsigned int itemId = *(unsigned int*)&packet[4];
				
				// Parse the item packet to get container information
				bool parseSuccess = true;
				ItemInfo item = {};
				ParseItem((unsigned char*)packet, &item, &parseSuccess);
				
				Lock();
				// Clear ActivePacket if itemId matches
				if (itemId == ActivePacket.itemId && ActivePacket.startTicks > 0) {
					//PrintText(1, "Placed item id %d", itemId);
					ActivePacket.itemId = 0;
					ActivePacket.x = 0;
					ActivePacket.y = 0;
					ActivePacket.startTicks = 0;
					ActivePacket.destination = 0;
				}
				Unlock();
			}

			if ((*BH::MiscToggles2)["Advanced Item Display"].state) {
				bool success = true;
				ItemInfo item = {};
				ParseItem((unsigned char*)packet, &item, &success);
				//PrintText(1, "Item packet: %s, %s, %X, %d, %d", item.name.c_str(), item.code, item.attrs->flags, item.sockets, GetDefense(&item));
				if ((item.action == ITEM_ACTION_NEW_GROUND || item.action == ITEM_ACTION_OLD_GROUND) && success) {
					bool showOnMap = false;
					bool nameWhitelisted = false;
					bool noTracking = false;
					auto pingLevel = -1;
					auto color = UNDEFINED_COLOR;

					for (vector<Rule*>::iterator it = MapRuleList.begin(); it != MapRuleList.end(); it++) {
						if ((*it)->Evaluate(NULL, &item)) {
							nameWhitelisted = true;
							// skip map and notification if ping level requirement is not met
							if ((*it)->action.pingLevel > Item::GetPingLevel()) continue;
							auto action_color = (*it)->action.notifyColor;
							// never overwrite color with an undefined color. never overwrite a defined color with dead color.
							if (action_color != UNDEFINED_COLOR && (action_color != DEAD_COLOR || color == UNDEFINED_COLOR))
								color = action_color;
							showOnMap = true;
							noTracking = (*it)->action.noTracking;
							pingLevel = (*it)->action.pingLevel;
							// break unless %CONTINUE% is used
							if ((*it)->action.stopProcessing) break;
						}
					}
					// Don't block items that have a white-listed name
					for (vector<Rule*>::iterator it = DoNotBlockRuleList.begin(); it != DoNotBlockRuleList.end(); it++) {
						if ((*it)->Evaluate(NULL, &item)) {
							nameWhitelisted = true;
							break;
						}
					}
					//PrintText(1, "Item on ground: %s, %s, %s, %X", item.name.c_str(), item.code, item.attrs->category.c_str(), item.attrs->flags);
					if(showOnMap && !(*BH::MiscToggles2)["Item Detailed Notifications"].state) {
						if (!noTracking && !IsTown(GetPlayerArea()) && pingLevel >= 0 && (unsigned int)pingLevel <= Item::GetTrackerPingLevel()) {
							ScreenInfo::AddDrop(item.name.c_str(), item.x, item.y);
						}
						if (color == UNDEFINED_COLOR) {
							color = ItemColorFromQuality(item.quality);
						}
						if ((*BH::MiscToggles2)["Item Drop Notifications"].state &&
								item.action == ITEM_ACTION_NEW_GROUND &&
								color != DEAD_COLOR
							 ) {
							PrintText(color, "%s%s",
									item.name.c_str(),
									(*BH::MiscToggles2)["Verbose Notifications"].state ? " \377c5drop" : ""
									);
						}
						if ((*BH::MiscToggles2)["Item Close Notifications"].state &&
								item.action == ITEM_ACTION_OLD_GROUND &&
								color != DEAD_COLOR
							 ) {
							PrintText(color, "%s%s",
									item.name.c_str(),
									(*BH::MiscToggles2)["Verbose Notifications"].state ? " \377c5close" : ""
									);
						}
					}
					else if (!showOnMap && !nameWhitelisted) {
						for (vector<Rule*>::iterator it = IgnoreRuleList.begin(); it != IgnoreRuleList.end(); it++) {
							if ((*it)->Evaluate(NULL, &item)) {
								*block = true;
								//PrintText(1, "Blocking item: %s, %s, %d", item.name.c_str(), item.code, item.amount);
								break;
							}
						}
					}
				}
			}
			break;
		}
	case 0x9d:
		{
			// We get this packet after picking up an item
			if (FirstInit) {
				BYTE action = packet[1];
				unsigned int itemId = *(unsigned int*)&packet[4];
				Lock();
				if (itemId == ActivePacket.itemId) {
					//PrintText(2, "Picked up item id %d", itemId);
					if (ActivePacket.destination == STORAGE_NULL) {
						PutItemOnGround();
					} else if (!stackDropOnItem) {
						PutItemInContainer();
					}
				}
				Unlock();
			}
			break;
		}
	default:
		break;
	}
	return;
}

// Auto-cube helper functions
bool ItemMover::MoveItemToInventory(UnitAny* unit, UnitAny* item) {
	if (!unit || !item || !item->pItemData) {
		return false;
	}
	
	if (!Init()) {
		return false;
	}
	
	// Check if item is already in inventory
	if (item->pItemData->ItemLocation == STORAGE_INVENTORY) {
		return true; // Already in inventory
	}
	
	// Get item's grid position and size
	int itemGridX = item->pObjectPath->dwPosX;
	int itemGridY = item->pObjectPath->dwPosY;
	BYTE xSize = D2COMMON_GetItemText(item->dwTxtFileNo)->xSize;
	BYTE ySize = D2COMMON_GetItemText(item->dwTxtFileNo)->ySize;
	
	// Load inventory state to populate InventoryItemIds array
	// This is necessary for FindDestination to work correctly
	int invUI = D2CLIENT_GetUIState(UI_INVENTORY);
	int stashUI = D2CLIENT_GetUIState(UI_STASH);
	int sourceLocation = item->pItemData->ItemLocation;
	
	// Load inventory to populate the item ID arrays
	LoadInventory(unit, sourceLocation, itemGridX, itemGridY, false, false, stashUI, invUI);
	
	// Find destination in inventory
	bool found = FindDestination(STORAGE_INVENTORY, item->dwUnitId, xSize, ySize);
	if (found) {
		PickUpItem();
		return true;
	}
	return false;
}

int ItemMover::ClearCube(UnitAny* unit) {
	if (!unit || !unit->pInventory) {
		return 0;
	}
	
	// Count items in cube
	int cubeItemCount = 0;
	for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
		if (pItem->pItemData->ItemLocation == STORAGE_CUBE) {
			cubeItemCount++;
		}
	}
	
	if (cubeItemCount == 0) {
		return 0; // Cube is already empty
	}
	
	// Move all items from cube to inventory
	// Create a list first to avoid modifying the list while iterating
	std::vector<DWORD> cubeItemIds;
	for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
		if (pItem->pItemData->ItemLocation == STORAGE_CUBE) {
			cubeItemIds.push_back(pItem->dwUnitId);
		}
	}
	
	// Move each item
	for (size_t i = 0; i < cubeItemIds.size(); i++) {
		// Find the item again (in case it was moved)
		UnitAny* pItem = NULL;
		for (UnitAny *item = unit->pInventory->pFirstItem; item; item = item->pItemData->pNextInvItem) {
			if (item->dwUnitId == cubeItemIds[i] && item->pItemData->ItemLocation == STORAGE_CUBE) {
				pItem = item;
				break;
			}
		}
		
		if (pItem) {
			if (!MoveItemToInventory(unit, pItem)) {
				// Can't move item (inventory full?), return error
				return -1;
			}
			
			// Item is being moved, return 1 to indicate we're waiting
			return 1;
		}
	}
	
	return 0; // All items cleared
}

int ItemMover::CountItemsInCube(UnitAny* unit, const char* code) {
	if (!unit || !unit->pInventory || !code) {
		return 0;
	}
	
	int count = 0;
	for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
		if (pItem->pItemData->ItemLocation == STORAGE_CUBE) {
			ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
			if (pItemText && pItemText->szCode) {
				char* itemCode = pItemText->szCode;
				// Exact 3-character code match
				if (itemCode && code && strlen(code) >= 3) {
					if (itemCode[0] == code[0] && itemCode[1] == code[1] && itemCode[2] == code[2]) {
						count++;
					}
				}
			}
		}
	}
	return count;
}

bool ItemMover::FindAndMoveItemsToCube(UnitAny* unit, const char* code, int needed) {
	if (!unit || !unit->pInventory || !code || needed <= 0) {
		return false;
	}
	
	if (!Init()) {
		return false;
	}
	
	// First check if cube UI is open
	if (!D2CLIENT_GetUIState(UI_CUBE)) {
		return false;
	}
	
	int moved = 0;

	// First, build a list of matching items to move
	std::vector<UnitAny*> itemsToMove;
	for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
		if (pItem->pItemData->ItemLocation == STORAGE_INVENTORY) {
			ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
			if (pItemText && pItemText->szCode) {
				char* itemCode = pItemText->szCode;
				if (itemCode && code && strlen(code) >= 3) {
					if (itemCode[0] == code[0] && itemCode[1] == code[1] && itemCode[2] == code[2]) {
						itemsToMove.push_back(pItem);
					}
				}
			}
		}
	}

	// Limit to the number needed
	if ((int)itemsToMove.size() > needed) {
		itemsToMove.resize(needed);
	}
	
	// Now move items one at a time using the existing shift-click logic
	for (size_t i = 0; i < itemsToMove.size() && moved < needed; i++) {
		UnitAny* pItem = itemsToMove[i];
		
		// Make sure item is still in inventory (might have been moved already)
		if (pItem->pItemData->ItemLocation != STORAGE_INVENTORY) {
			continue;
		}
		
		ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
		if (!pItemText || !pItemText->szCode) {
			continue;
		}
		
		// Get item's grid position
		int itemGridX = pItem->pObjectPath->dwPosX;
		int itemGridY = pItem->pObjectPath->dwPosY;
		
		// Use the existing shift-click logic to move item to cube
		// LoadInventory will find the item and set up the move to cube
		int invUI = D2CLIENT_GetUIState(UI_INVENTORY);
		int stashUI = D2CLIENT_GetUIState(UI_STASH);
		bool moveItem = LoadInventory(unit, STORAGE_INVENTORY, itemGridX, itemGridY, true, false, stashUI, invUI);
		
		if (moveItem) {
			PickUpItem();
			moved++;
		} else {
			// Couldn't find destination (cube full?), stop trying
			break;
		}
	}
	
	return moved > 0;
}

bool ItemMover::FindAndMoveGemsToCube(UnitAny* unit, BYTE maxGemLevel, int maxCount) {
	if (!unit || !unit->pInventory || maxGemLevel == 0 || maxCount <= 0) {
		return false;
	}
	
	if (!Init()) {
		return false;
	}
	
	// First check if cube UI is open
	if (!D2CLIENT_GetUIState(UI_CUBE)) {
		return false;
	}
	
	int moved = 0;
	// First, build a list of matching gems to move
	std::vector<UnitAny*> gemsToMove;
	for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem && (int)gemsToMove.size() < maxCount; pItem = pItem->pItemData->pNextInvItem) {
			if (pItem->pItemData->ItemLocation == STORAGE_INVENTORY) {
				ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
				if (pItemText && pItemText->szCode) {
					// Convert item code to string for lookup
					std::string itemCodeStr(pItemText->szCode, 3); // First 3 characters
					// Get item attributes to check if it's a gem
					std::map<std::string, ItemAttributes*>::iterator it = ItemAttributeMap.find(itemCodeStr);
					if (it != ItemAttributeMap.end()) {
						ItemAttributes* attrs = it->second;
						// Check if it's a gem using flags
						if (attrs->flags2 & ITEM_GROUP_GEM) {
						BYTE gemLevel = 0;
						// Get gem level from flags (1=Chipped, 2=Flawed, 3=Normal, 4=Flawless, 5=Perfect)
						if (attrs->flags2 & ITEM_GROUP_CHIPPED) {
							gemLevel = 1;
						} else if (attrs->flags2 & ITEM_GROUP_FLAWED) {
							gemLevel = 2;
						} else if (attrs->flags2 & ITEM_GROUP_REGULAR) {
							gemLevel = 3;
						} else if (attrs->flags2 & ITEM_GROUP_FLAWLESS) {
							gemLevel = 4;
						} else if (attrs->flags2 & ITEM_GROUP_PERFECT) {
							gemLevel = 5;
						}
						// Check if gem level is <= maxGemLevel
						if (gemLevel > 0 && gemLevel <= maxGemLevel) {
							gemsToMove.push_back(pItem);
						}
					}
				}
			}
		}
	}
	
	// Now move gems one at a time using the existing shift-click logic
	for (size_t i = 0; i < gemsToMove.size() && moved < maxCount; i++) {
		UnitAny* pItem = gemsToMove[i];
		
		// Make sure item is still in inventory (might have been moved already)
		if (pItem->pItemData->ItemLocation != STORAGE_INVENTORY) {
			continue;
		}
		
		ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
		if (!pItemText || !pItemText->szCode) {
			continue;
		}
		
		// Get item's grid position
		int itemGridX = pItem->pObjectPath->dwPosX;
		int itemGridY = pItem->pObjectPath->dwPosY;
		
		// Use the existing shift-click logic to move item to cube
		int invUI = D2CLIENT_GetUIState(UI_INVENTORY);
		int stashUI = D2CLIENT_GetUIState(UI_STASH);
		bool moveItem = LoadInventory(unit, STORAGE_INVENTORY, itemGridX, itemGridY, true, false, stashUI, invUI);
		
		if (moveItem) {
			PickUpItem();
			moved++;
		} else {
			// Couldn't find destination (cube full?), stop trying
			break;
		}
	}
	
	return moved > 0;
}

bool ItemMover::FindAndMoveRunesToCube(UnitAny* unit, BYTE maxRuneNumber, int maxCount) {
	if (!unit || !unit->pInventory || maxRuneNumber == 0 || maxCount <= 0) {
		return false;
	}
	
	if (!Init()) {
		return false;
	}
	
	// First check if cube UI is open
	if (!D2CLIENT_GetUIState(UI_CUBE)) {
		return false;
	}
	
	int moved = 0;
	// First, build a list of matching runes to move
	// If we're trying to move a high rune (> r17), only move 1 at a time
	std::vector<UnitAny*> runesToMove;
	for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem && (int)runesToMove.size() < maxCount; pItem = pItem->pItemData->pNextInvItem) {
		if (pItem->pItemData->ItemLocation == STORAGE_INVENTORY) {
			ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
			if (pItemText && pItemText->szCode) {
				// Convert item code to string for lookup
				std::string itemCodeStr(pItemText->szCode, 3); // First 3 characters
				// Get item attributes to check if it's a rune
				std::map<std::string, ItemAttributes*>::iterator it = ItemAttributeMap.find(itemCodeStr);
				if (it != ItemAttributeMap.end()) {
					ItemAttributes* attrs = it->second;
					// Check if it's a rune using flags
					if (attrs->flags2 & ITEM_GROUP_RUNE) {
						// Get rune number from item code (e.g., "r01" = 1, "r33" = 33)
						BYTE runeNumber = (BYTE)(((pItemText->szCode[1] - '0') * 10) + pItemText->szCode[2] - '0');
						// Check if rune number is <= maxRuneNumber
						if (runeNumber > 0 && runeNumber <= maxRuneNumber) {
							// If this is a high rune (> r17), only allow moving 1 at a time
							if (runeNumber > 17) {
								// High rune - only move 1, and stop looking for more
								if (runesToMove.empty()) {
									runesToMove.push_back(pItem);
								}
								break; // Stop after finding first high rune
							} else {
								// Low rune (<= r17) - can move up to maxCount
								runesToMove.push_back(pItem);
							}
						}
					}
				}
			}
		}
	}
	
	// Now move runes one at a time using the existing shift-click logic
	for (size_t i = 0; i < runesToMove.size() && moved < maxCount; i++) {
		UnitAny* pItem = runesToMove[i];
		
		// Make sure item is still in inventory (might have been moved already)
		if (pItem->pItemData->ItemLocation != STORAGE_INVENTORY) {
			continue;
		}
		
		ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
		if (!pItemText || !pItemText->szCode) {
			continue;
		}
		
		// Get item's grid position
		int itemGridX = pItem->pObjectPath->dwPosX;
		int itemGridY = pItem->pObjectPath->dwPosY;
		
		// Use the existing shift-click logic to move item to cube
		int invUI = D2CLIENT_GetUIState(UI_INVENTORY);
		int stashUI = D2CLIENT_GetUIState(UI_STASH);
		bool moveItem = LoadInventory(unit, STORAGE_INVENTORY, itemGridX, itemGridY, true, false, stashUI, invUI);
		
		if (moveItem) {
			PickUpItem();
			moved++;
		} else {
			// Couldn't find destination (cube full?), stop trying
			break;
		}
	}
	
	return moved > 0;
}

// Helper function to get the tier of a unique/set item from ItemDisplay rules
// Uses the same evaluation pattern as existing ping/tier functionality
int ItemMover::GetItemTier(UnitAny* item) {
	if (!item || !item->pItemData) {
		return 0;
	}
	
	// Only check unique and set items
	if (item->pItemData->dwQuality != ITEM_QUALITY_UNIQUE && 
	    item->pItemData->dwQuality != ITEM_QUALITY_SET) {
		return 0;
	}
	
	// Initialize ItemDisplay rules if needed
	ItemDisplay::InitializeItemRules();
	
	// Create UnitItemInfo from the item (same pattern as map_action_cache uses)
	UnitItemInfo uInfo;
	if (CreateUnitItemInfo(&uInfo, item) != 0) {
		return 0; // Failed to create UnitItemInfo
	}
	
	// Use map_action_cache to get actions (same as existing ping/tier code)
	// This evaluates MapRuleList which should contain tier information
	const vector<Action> actions = map_action_cache.Get(&uInfo);
	for (auto &action : actions) {
		// Return the first matching action's tier (pingLevel)
		// Tier is stored in pingLevel (0 = no tier, 1-6 = Tier 1-6)
		if (action.pingLevel > 0) {
			return action.pingLevel;
		}
	}
	
	// If no tier found in map rules, check all rules as fallback
	for (vector<Rule*>::iterator it = RuleList.begin(); it != RuleList.end(); it++) {
		if ((*it)->Evaluate(&uInfo, NULL)) {
			if ((*it)->action.pingLevel > 0) {
				return (*it)->action.pingLevel;
			}
		}
	}
	
	// No matching rule found, return 0 (no tier)
	return 0;
}

// Helper function to get the tier of any item from ItemDisplay rules (without quality restriction)
int ItemMover::GetItemTierForType(UnitAny* item) {
	if (!item || !item->pItemData) {
		return 0;
	}
	
	// Initialize ItemDisplay rules if needed
	ItemDisplay::InitializeItemRules();
	
	// Create UnitItemInfo from the item (same pattern as map_action_cache uses)
	UnitItemInfo uInfo;
	if (CreateUnitItemInfo(&uInfo, item) != 0) {
		return 0; // Failed to create UnitItemInfo
	}
	
	// Use map_action_cache to get actions (same as existing ping/tier code)
	// This evaluates MapRuleList which should contain tier information
	const vector<Action> actions = map_action_cache.Get(&uInfo);
	for (auto &action : actions) {
		// Return the first matching action's tier (pingLevel)
		// Tier is stored in pingLevel (0 = no tier, 1-6 = Tier 1-6)
		if (action.pingLevel > 0) {
			return action.pingLevel;
		}
	}
	
	// If no tier found in map rules, check all rules as fallback
	for (vector<Rule*>::iterator it = RuleList.begin(); it != RuleList.end(); it++) {
		if ((*it)->Evaluate(&uInfo, NULL)) {
			if ((*it)->action.pingLevel > 0) {
				return (*it)->action.pingLevel;
			}
		}
	}
	
	// No matching rule found, return 0 (no tier)
	return 0;
}

// Check if item is of type "augr"
bool ItemMover::IsAugrType(UnitAny* item) {
	if (!item || item->dwType != UNIT_ITEM) {
		return false;
	}
	
	// Get item code from ItemText
	ItemText* pItemText = D2COMMON_GetItemText(item->dwTxtFileNo);
	if (!pItemText || !pItemText->szCode) {
		return false;
	}
	
	// Convert item code to string for lookup
	std::string itemCodeStr(pItemText->szCode, 3); // First 3 characters
	
	// Look up item in ItemAttributeMap (loaded from misc.txt, weapons.txt, armor.txt)
	std::map<std::string, ItemAttributes*>::iterator it = ItemAttributeMap.find(itemCodeStr);
	if (it != ItemAttributeMap.end()) {
		ItemAttributes* attrs = it->second;
		// Check if category (type from misc.txt) is "augr"
		if (attrs->category == "augr") {
			return true;
		}
	}
	
	return false;
}

static const int AUTO_STACK_MAX = 500;

bool ItemMover::IsKnownStackableCode(const char* code) {
	if (!code || strlen(code) < 3) {
		return false;
	}
	static const char* stackableCodes[] = {
		"cf4", "cf6", "cf2", "cf8", "%c7", "%c6",
		"cx2", "cx5", "cx6", "%c5", "%c4", "cx3",
		"czd", "cze", "czf", "czg", "%c3", "%c0",
		"%c1", "%c2", "cx1", "cz0", "cd0", "cz1",
		"nvz"
	};
	const int count = sizeof(stackableCodes) / sizeof(stackableCodes[0]);
	for (int i = 0; i < count; i++) {
		if (code[0] == stackableCodes[i][0] &&
		    code[1] == stackableCodes[i][1] &&
		    code[2] == stackableCodes[i][2]) {
			return true;
		}
	}
	return false;
}

int ItemMover::GetItemStackAmount(UnitAny* item) {
	if (!item) {
		return -1;
	}

	int qty = (int)D2COMMON_GetUnitStat(item, STAT_AMMOQUANTITY, 0);
	if (qty > 0) {
		return qty;
	}

	ItemText* txt = D2COMMON_GetItemText(item->dwTxtFileNo);
	if (txt && txt->szCode && IsKnownStackableCode(txt->szCode)) {
		return 1;
	}
	return -1;
}

bool ItemMover::IsAutoStackableItem(UnitAny* item) {
	if (!item || !item->pItemData) {
		return false;
	}
	BYTE loc = item->pItemData->ItemLocation;
	if (loc != STORAGE_INVENTORY && loc != STORAGE_STASH && loc != STORAGE_CUBE) {
		return false;
	}
	ItemText* txt = D2COMMON_GetItemText(item->dwTxtFileNo);
	if (!txt || !txt->szCode) {
		return false;
	}
	return IsKnownStackableCode(txt->szCode);
}

bool ItemMover::MoveItemOntoStack(UnitAny* source, UnitAny* target) {
	if (!source || !target || !source->pObjectPath || !target->pObjectPath || !target->pItemData) {
		return false;
	}
	if (!Init()) {
		return false;
	}

	Lock();
	if (ActivePacket.startTicks != 0) {
		Unlock();
		return false;
	}
	ActivePacket.itemId = source->dwUnitId;
	ActivePacket.x = target->pObjectPath->dwPosX;
	ActivePacket.y = target->pObjectPath->dwPosY;
	ActivePacket.startTicks = BHGetTickCount();
	ActivePacket.destination = target->pItemData->ItemLocation;
	Unlock();

	stackTargetItemId = target->dwUnitId;
	stackDropOnItem = true;
	PickUpItem();
	return true;
}

bool ItemMover::ProcessAutoStackStep() {
	UnitAny* unit = D2CLIENT_GetPlayerUnit();
	if (!unit || !unit->pInventory) {
		return false;
	}

	ULONGLONG currentTick = BHGetTickCount();

	Lock();
	bool busy = (ActivePacket.startTicks > 0);
	ULONGLONG packetStart = ActivePacket.startTicks;
	DWORD packetItemId = ActivePacket.itemId;
	Unlock();

	if (busy) {
		UnitAny* cursorItem = D2CLIENT_GetCursorItem();
		if (cursorItem != NULL && cursorItem->dwUnitId == packetItemId && stackDropOnItem) {
			if ((currentTick - packetStart) < 50) {
				return true;
			}
			DWORD targetId = stackTargetItemId;
			stackDropOnItem = false;
			StackCursorItemOnTarget(cursorItem->dwUnitId, targetId);
			Lock();
			ActivePacket.itemId = 0;
			ActivePacket.x = 0;
			ActivePacket.y = 0;
			ActivePacket.startTicks = 0;
			ActivePacket.destination = 0;
			Unlock();
			return true;
		}
		bool mergeSettled = (currentTick - packetStart) > 150;
		if (mergeSettled && cursorItem == NULL) {
			Lock();
			ActivePacket.itemId = 0;
			ActivePacket.x = 0;
			ActivePacket.y = 0;
			ActivePacket.startTicks = 0;
			ActivePacket.destination = 0;
			Unlock();
			return true;
		}
		if ((currentTick - packetStart) > 3000) {
			Lock();
			ActivePacket.itemId = 0;
			ActivePacket.x = 0;
			ActivePacket.y = 0;
			ActivePacket.startTicks = 0;
			ActivePacket.destination = 0;
			Unlock();
		}
		return true;
	}

	bool stashOpen = D2CLIENT_GetUIState(UI_STASH) != 0 || stashInteractionMode;
	bool cubeOpen = D2CLIENT_GetUIState(UI_CUBE) != 0;

	UnitAny* cursorItem = D2CLIENT_GetCursorItem();
	if (cursorItem != NULL) {
		if (!Init()) {
			return true;
		}

		ItemText* cursorText = D2COMMON_GetItemText(cursorItem->dwTxtFileNo);
		if (cursorText && cursorText->szCode && IsKnownStackableCode(cursorText->szCode)) {
			UnitAny* dropTarget = NULL;
			int dropTargetAmount = -1;
			for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
				if (pItem == cursorItem || !pItem->pObjectPath || !pItem->pItemData) {
					continue;
				}
				int loc = pItem->pItemData->ItemLocation;
				if (stashOpen) {
					if (loc != STORAGE_STASH) {
						continue;
					}
				} else if (loc != STORAGE_INVENTORY && loc != STORAGE_CUBE) {
					continue;
				}
				ItemText* txt = D2COMMON_GetItemText(pItem->dwTxtFileNo);
				if (!txt || !txt->szCode ||
				    txt->szCode[0] != cursorText->szCode[0] ||
				    txt->szCode[1] != cursorText->szCode[1] ||
				    txt->szCode[2] != cursorText->szCode[2]) {
					continue;
				}
				int amount = GetItemStackAmount(pItem);
				if (amount < 1 || amount >= AUTO_STACK_MAX) {
					continue;
				}
				if (amount > dropTargetAmount) {
					dropTargetAmount = amount;
					dropTarget = pItem;
				}
			}
			if (dropTarget) {
				stackDropOnItem = false;
				StackCursorItemOnTarget(cursorItem->dwUnitId, dropTarget->dwUnitId);
				return true;
			}
		}

		int invUI = D2CLIENT_GetUIState(UI_INVENTORY);
		int stashUI = D2CLIENT_GetUIState(UI_STASH);
		if (!cursorText) {
			return true;
		}
		BYTE xSize = cursorText->xSize;
		BYTE ySize = cursorText->ySize;
		LoadInventory(unit, STORAGE_INVENTORY, -1, -1, false, false, stashUI, invUI);
		bool found = false;
		if (stashOpen) {
			found = FindDestination(STORAGE_STASH, cursorItem->dwUnitId, xSize, ySize);
		}
		if (!found) {
			found = FindDestination(STORAGE_INVENTORY, cursorItem->dwUnitId, xSize, ySize);
		}
		if (found) {
			PutItemInContainer();
		}
		return true;
	}

	struct StackRef {
		UnitAny* item;
		int amount;
		int location;
	};
	std::map<std::string, std::vector<StackRef> > groups;
	std::map<std::string, bool> codeHasStashStack;

	for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
		if (!IsAutoStackableItem(pItem) || !pItem->pObjectPath) {
			continue;
		}
		int loc = pItem->pItemData->ItemLocation;
		if (loc == STORAGE_STASH && !stashOpen) {
			continue;
		}
		if (loc == STORAGE_CUBE && !cubeOpen) {
			continue;
		}
		ItemText* txt = D2COMMON_GetItemText(pItem->dwTxtFileNo);
		if (!txt || !txt->szCode) {
			continue;
		}
		std::string code(txt->szCode, 3);
		if (loc == STORAGE_STASH) {
			codeHasStashStack[code] = true;
		}
		int amount = GetItemStackAmount(pItem);
		if (amount < 1 || amount >= AUTO_STACK_MAX) {
			continue;
		}
		StackRef ref;
		ref.item = pItem;
		ref.amount = amount;
		ref.location = loc;
		groups[code].push_back(ref);
	}

	UnitAny* source = NULL;
	UnitAny* target = NULL;
	int bestTargetAmount = -1;
	int bestSourceAmount = AUTO_STACK_MAX + 1;

	auto considerDirected = [&](const StackRef& src, const StackRef& tgt) {
		if (src.item == tgt.item) {
			return;
		}
		if (tgt.amount > bestTargetAmount ||
		    (tgt.amount == bestTargetAmount && src.amount < bestSourceAmount)) {
			bestTargetAmount = tgt.amount;
			bestSourceAmount = src.amount;
			source = src.item;
			target = tgt.item;
		}
	};

	auto considerPair = [&](const StackRef& a, const StackRef& b) {
		if (a.amount <= b.amount) {
			considerDirected(a, b);
		} else {
			considerDirected(b, a);
		}
	};

	// Stash open: every inventory stack goes onto the matching stash stack.
	if (stashOpen) {
		for (std::map<std::string, std::vector<StackRef> >::iterator it = groups.begin(); it != groups.end(); ++it) {
			const std::vector<StackRef>& items = it->second;
			for (size_t i = 0; i < items.size(); i++) {
				if (items[i].location != STORAGE_INVENTORY) {
					continue;
				}
				for (size_t j = 0; j < items.size(); j++) {
					if (items[j].location == STORAGE_STASH) {
						considerDirected(items[i], items[j]);
					}
				}
			}
		}
	}

	if (!source) {
		for (std::map<std::string, std::vector<StackRef> >::iterator it = groups.begin(); it != groups.end(); ++it) {
			const std::vector<StackRef>& items = it->second;
			if (items.size() < 2) {
				continue;
			}
			bool hasStash = (codeHasStashStack.find(it->first) != codeHasStashStack.end());
			for (size_t i = 0; i < items.size(); i++) {
				for (size_t j = i + 1; j < items.size(); j++) {
					if (items[i].location != items[j].location) {
						continue;
					}
					// With stash open, do not combine inventory stacks of a type that already has a stash stack.
					if (stashOpen && hasStash && items[i].location == STORAGE_INVENTORY) {
						continue;
					}
					considerPair(items[i], items[j]);
				}
			}
		}
	}

	if (!source) {
		for (std::map<std::string, std::vector<StackRef> >::iterator it = groups.begin(); it != groups.end(); ++it) {
			const std::vector<StackRef>& items = it->second;
			if (items.size() < 2) {
				continue;
			}
			for (size_t i = 0; i < items.size(); i++) {
				for (size_t j = i + 1; j < items.size(); j++) {
					if (items[i].location == items[j].location) {
						continue;
					}
					// Never pull a stash stack back into inventory.
					if ((items[i].location == STORAGE_STASH && items[j].location == STORAGE_INVENTORY) ||
					    (items[j].location == STORAGE_STASH && items[i].location == STORAGE_INVENTORY)) {
						continue;
					}
					considerPair(items[i], items[j]);
				}
			}
		}
	}

	if (!source || !target) {
		return false;
	}

	return MoveItemOntoStack(source, target);
}


// Reset all stash interaction state
void ItemMover::ResetStashInteractionState() {
	waitingForCubeToOpen = false;
}

void ItemMover::StopAutoCubeFromUserClick() {
	if (!isAutoCubing) {
		return;
	}
	isAutoCubing = false;
	stashInteractionMode = false;
	stackDropOnItem = false;
	ResetStashInteractionState();
	PrintText(White, "Auto Cube: Stopped");
}

void ItemMover::ProcessStashInteraction() {
	UnitAny* unit = D2CLIENT_GetPlayerUnit();
	if (!unit || !unit->pInventory) {
		isAutoCubing = false;
		stashInteractionMode = false;
		return;
	}

	ULONGLONG currentTick = BHGetTickCount();

	if (waitingForCubeToOpen) {
		if (D2CLIENT_GetUIState(UI_CUBE)) {
			waitingForCubeToOpen = false;
			stashInteractionMode = false;
			lastAutoCubeTick = currentTick;
			processingEssenceGems = false;
			processingEssenceRunes = false;
			processingEssenceUniques = false;
			processingEssenceHccMisc = false;
			PrintText(White, "Auto Cube: Processing...");
			return;
		}

		if (currentTick - lastAutoCubeTick > 500) {
			DWORD cubeId = 0;
			for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
				if (pItem->pItemData->ItemLocation == STORAGE_INVENTORY) {
					ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
					if (pItemText && pItemText->szCode) {
						char* code = pItemText->szCode;
						if (code[0] == 'b' && code[1] == 'o' && code[2] == 'x') {
							cubeId = pItem->dwUnitId;
							break;
						}
					}
				}
			}
			if (cubeId > 0) {
				BYTE PacketData[13] = { 0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
				*reinterpret_cast<int*>(PacketData + 1) = cubeId;
				*reinterpret_cast<WORD*>(PacketData + 5) = (WORD)unit->pPath->xPos;
				*reinterpret_cast<WORD*>(PacketData + 9) = (WORD)unit->pPath->yPos;
				D2NET_SendPacket(13, 0, PacketData);
			}
			lastAutoCubeTick = currentTick;
		}
		return;
	}

	if (currentTick - lastAutoCubeTick < 100) {
		return;
	}

	if (autoStackItems.state) {
		if (ProcessAutoStackStep()) {
			lastAutoCubeTick = currentTick;
			return;
		}
	}

	bool essenceEnabled = autoEssenceGems.state || autoEssenceRunes.state ||
	                      autoEssenceUniques.state || autoEssenceHccMisc.state;
	if (!essenceEnabled) {
		PrintText(White, autoStackItems.state ? "Auto Cube: Finished stacking" : "Auto Cube: Finished");
		isAutoCubing = false;
		stashInteractionMode = false;
		ResetStashInteractionState();
		return;
	}

	if (D2CLIENT_GetCursorItem() != NULL) {
		return;
	}

	if (!D2CLIENT_GetUIState(UI_STASH)) {
		isAutoCubing = false;
		stashInteractionMode = false;
		PrintText(Red, "Auto Cube: Stash closed unexpectedly");
		return;
	}

	DWORD cubeId = 0;
	for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
		if (pItem->pItemData->ItemLocation == STORAGE_INVENTORY) {
			ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
			if (pItemText && pItemText->szCode) {
				char* code = pItemText->szCode;
				if (code[0] == 'b' && code[1] == 'o' && code[2] == 'x') {
					cubeId = pItem->dwUnitId;
					break;
				}
			}
		}
	}

	if (cubeId > 0) {
		BYTE PacketData[13] = { 0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		*reinterpret_cast<int*>(PacketData + 1) = cubeId;
		*reinterpret_cast<WORD*>(PacketData + 5) = (WORD)unit->pPath->xPos;
		*reinterpret_cast<WORD*>(PacketData + 9) = (WORD)unit->pPath->yPos;
		D2NET_SendPacket(13, 0, PacketData);
		PrintText(White, "Auto Cube: Opening cube...");
		waitingForCubeToOpen = true;
		lastAutoCubeTick = currentTick;
	} else {
		PrintText(Red, "Auto Cube: Horadric Cube not found in inventory");
		isAutoCubing = false;
		stashInteractionMode = false;
	}
}

bool ItemMover::PerformAutoCube() {
	// Non-blocking version - just validates and starts the process
	// Actual processing happens in ProcessAutoCubeStep() via OnLoop()
	UnitAny* unit = D2CLIENT_GetPlayerUnit();
	if (!unit || !unit->pInventory) {
		PrintText(Red, "Auto Cube: Invalid game state");
		return false;
	}
	
	// Check if cursor has an item
	if (D2CLIENT_GetCursorItem() != NULL) {
		PrintText(Red, "Auto Cube: Cannot run while holding item");
		return false;
	}
	
	// Check if cube UI is open
	if (!D2CLIENT_GetUIState(UI_CUBE)) {
		PrintText(Red, "Auto Cube: Cube UI must be open");
		return false;
	}
	
	if (!Init()) {
		PrintText(Red, "Auto Cube: Initialization failed");
		return false;
	}
	
	// Start the auto-cube process (non-blocking)
	isAutoCubing = true;
	lastAutoCubeTick = BHGetTickCount();
	processingEssenceGems = false; // Reset essence gem processing
	essenceGemsMoved = 0; // Reset essence gem count
	essenceCubeInCube = false; // Reset cube in cube flag
	processingEssenceRunes = false; // Reset essence rune processing
	essenceRunesMoved = 0; // Reset essence rune count
	essenceRunesCubeInCube = false; // Reset cube in cube flag for runes
	processingEssenceUniques = false; // Reset essence unique processing
	essenceUniquesCubeInCube = false; // Reset cube in cube flag for uniques
	cursorItemStartTick = 0; // Reset cursor item tracking
	cursorItemRecoveryAttempted = false; // Reset recovery flag
	cursorItemRecoveryTick = 0; // Reset recovery tick
	lastMovedItemId = 0; // Reset last moved item tracking
	lastMovedDestination = 0; // Reset last moved destination
	lastMovedX = 0;
	lastMovedY = 0;
	lastMoveVerified = true; // Reset verification flag
	PrintText(White, "Auto Cube: Started");
	return true;
}

void ItemMover::ProcessAutoCubeStep() {
	// Process one step of auto-cubing per call (non-blocking)
	// This is called from OnLoop() repeatedly until done
	UnitAny* unit = D2CLIENT_GetPlayerUnit();
	if (!unit || !unit->pInventory || !unit->pPath || !unit->pAct || 
	    !unit->pPath->pRoom1 || !unit->pPath->pRoom1->pRoom2 || 
	    !unit->pPath->pRoom1->pRoom2->pLevel) {
		isAutoCubing = false;
		return;
	}
	
	// Check if cube UI is still open
	if (!D2CLIENT_GetUIState(UI_CUBE)) {
		isAutoCubing = false;
		PrintText(Red, "Auto Cube: Stopped - cube UI closed");
		return;
	}
	
	ULONGLONG currentTick = BHGetTickCount();

	// Place leftover stack remainder immediately instead of waiting on the generic cursor timeout
	if (autoStackItems.state &&
	    !processingEssenceGems && !processingEssenceRunes &&
	    !processingEssenceUniques && !processingEssenceHccMisc &&
	    D2CLIENT_GetCursorItem() != NULL) {
		if (ProcessAutoStackStep()) {
			lastAutoCubeTick = currentTick;
			return;
		}
	}
	
	// Check if cursor has an item (wait for it to clear, but with timeout)
	UnitAny* cursorItem = D2CLIENT_GetCursorItem();
	if (cursorItem != NULL) {
		// Track when we first detected the cursor item
		if (cursorItemStartTick == 0) {
			cursorItemStartTick = currentTick;
			cursorItemRecoveryAttempted = false;
			cursorItemRecoveryTick = 0;
		}
		
		// Also check if ActivePacket is stuck (item was picked up but never placed)
		Lock();
		bool activePacketStuck = (ActivePacket.startTicks > 0 && 
		                          (currentTick - ActivePacket.startTicks > 3000));
		bool activePacketProcessing = (ActivePacket.startTicks > 0 && 
		                               ActivePacket.itemId == cursorItem->dwUnitId &&
		                               (currentTick - ActivePacket.startTicks < 2000));
		Unlock();
		
		// If recovery is in progress (ActivePacket is processing this item), wait for it to complete
		if (activePacketProcessing && cursorItemRecoveryAttempted) {
			return; // Wait for recovery to complete
		}
		
		// If item has been on cursor for more than 1 second, or ActivePacket is stuck, try to recover
		// But only try once every 3 seconds
		bool shouldRecover = false;
		if (!cursorItemRecoveryAttempted) {
			// First time - try after 1 second
			if (currentTick - cursorItemStartTick > 1000) {
				shouldRecover = true;
			} else if (activePacketStuck) {
				shouldRecover = true;
			}
		} else {
			// Already attempted recovery - if item still stuck after 3 seconds, try dropping it
			if (cursorItemRecoveryTick > 0 && (currentTick - cursorItemRecoveryTick > 3000)) {
				// Recovery failed, try dropping on ground
				shouldRecover = true;
			}
		}
		
		if (shouldRecover) {
			// Clear stuck ActivePacket first
			if (activePacketStuck) {
				Lock();
				ActivePacket.itemId = 0;
				ActivePacket.x = 0;
				ActivePacket.y = 0;
				ActivePacket.startTicks = 0;
				ActivePacket.destination = 0;
				Unlock();
			}
			
			// If we already attempted recovery once and item is still stuck, drop it
			if (cursorItemRecoveryAttempted && cursorItemRecoveryTick > 0) {
				// Recovery failed, drop on ground
				if (Init()) {
					Lock();
					ActivePacket.itemId = cursorItem->dwUnitId;
					ActivePacket.x = 0;
					ActivePacket.y = 0;
					ActivePacket.destination = STORAGE_NULL;
					ActivePacket.startTicks = currentTick;
					Unlock();
					
					PutItemOnGround();
					PrintText(Red, "Auto Cube: Cursor item stuck, dropped on ground");
					isAutoCubing = false;
					cursorItemStartTick = 0;
					cursorItemRecoveryAttempted = false;
					cursorItemRecoveryTick = 0;
					return;
				}
			} else {
				// First recovery attempt - try to put item back in inventory
				cursorItemRecoveryAttempted = true;
				cursorItemRecoveryTick = currentTick;
				
				if (Init()) {
					// Try to find a spot in inventory for the item
					BYTE xSize = D2COMMON_GetItemText(cursorItem->dwTxtFileNo)->xSize;
					BYTE ySize = D2COMMON_GetItemText(cursorItem->dwTxtFileNo)->ySize;
					
					// Try to find destination in inventory
					bool found = FindDestination(STORAGE_INVENTORY, cursorItem->dwUnitId, xSize, ySize);
					if (found) {
						// FindDestination already set up ActivePacket, just put item back
						PutItemInContainer();
						PrintText(White, "Auto Cube: Recovering stuck cursor item");
						lastAutoCubeTick = currentTick;
						return; // Wait for item to be placed
					} else {
						// Can't find spot, try to drop on ground as last resort
						Lock();
						ActivePacket.itemId = cursorItem->dwUnitId;
						ActivePacket.x = 0;
						ActivePacket.y = 0;
						ActivePacket.destination = STORAGE_NULL;
						ActivePacket.startTicks = currentTick;
						Unlock();
						
						PutItemOnGround();
						PrintText(Red, "Auto Cube: Cursor item stuck, dropped on ground");
						isAutoCubing = false;
						cursorItemStartTick = 0;
						cursorItemRecoveryAttempted = false;
						cursorItemRecoveryTick = 0;
						return;
					}
				}
			}
		}
		
		return; // Don't process while cursor has item
	} else {
		// Cursor is clear, reset tracking
		cursorItemStartTick = 0;
		cursorItemRecoveryAttempted = false;
		cursorItemRecoveryTick = 0;
	}
	
	// Rate limit operations - minimum 150ms between operations
	if (currentTick - lastAutoCubeTick < 100) {
		return;
	}
	
	if (!Init()) {
		isAutoCubing = false;
		return;
	}

	// Combine stackable items by dropping one stack onto another (same item code).
	// Skip while an essence recipe is using the cube.
	if (autoStackItems.state &&
	    !processingEssenceGems && !processingEssenceRunes &&
	    !processingEssenceUniques && !processingEssenceHccMisc) {
		if (ProcessAutoStackStep()) {
			lastAutoCubeTick = currentTick;
			return;
		}
	}
	
	// Process Auto Essence Gems if enabled
	// Only process essence gems if the checkbox is enabled
	if (autoEssenceGems.state) {
		// Convert dropdown index to gem level (0=Chipped=1, 1=Flawed=2, 2=Normal=3, 3=Flawless=4, 4=Perfect=5)
		BYTE maxGemLevel = (BYTE)(autoEssenceGemQuality + 1);
		
		// First, check if there are any matching gems in inventory before moving catalyst
		int matchingGemsInInventory = 0;
		for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
			if (pItem->pItemData->ItemLocation == STORAGE_INVENTORY) {
				ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
				if (pItemText && pItemText->szCode) {
					// Convert item code to string for lookup
					std::string itemCodeStr(pItemText->szCode, 3); // First 3 characters
					std::map<std::string, ItemAttributes*>::iterator it = ItemAttributeMap.find(itemCodeStr);
					if (it != ItemAttributeMap.end()) {
						ItemAttributes* attrs = it->second;
						if (attrs->flags2 & ITEM_GROUP_GEM) {
							BYTE gemLevel = 0;
							if (attrs->flags2 & ITEM_GROUP_CHIPPED) {
								gemLevel = 1;
							} else if (attrs->flags2 & ITEM_GROUP_FLAWED) {
								gemLevel = 2;
							} else if (attrs->flags2 & ITEM_GROUP_REGULAR) {
								gemLevel = 3;
							} else if (attrs->flags2 & ITEM_GROUP_FLAWLESS) {
								gemLevel = 4;
							} else if (attrs->flags2 & ITEM_GROUP_PERFECT) {
								gemLevel = 5;
							}
							if (gemLevel > 0 && gemLevel <= maxGemLevel) {
								matchingGemsInInventory++;
							}
						}
					}
				}
			}
		}
		
		// Only process essence gems if we have matching gems OR if we're already processing (catalyst in cube with gems)
		int catalystInCube = CountItemsInCube(unit, "hcc");
		int gemsInCube = 0;
		if (catalystInCube > 0) {
			// Count gems already in cube
			for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
				if (pItem->pItemData->ItemLocation == STORAGE_CUBE) {
					ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
					if (pItemText && pItemText->szCode) {
						// Convert item code to string for lookup
						std::string itemCodeStr(pItemText->szCode, 3); // First 3 characters
						std::map<std::string, ItemAttributes*>::iterator it = ItemAttributeMap.find(itemCodeStr);
						if (it != ItemAttributeMap.end()) {
							ItemAttributes* attrs = it->second;
							if (attrs->flags2 & ITEM_GROUP_GEM) {
								gemsInCube++;
							}
						}
					}
				}
			}
		}
		
		// Only process essence gems if we have gems to process (in inventory or already in cube)
		if (matchingGemsInInventory > 0 || gemsInCube > 0) {
			if (catalystInCube == 0 && !essenceCubeInCube) {
				// Need to move catalyst to cube first
				if (FindAndMoveItemsToCube(unit, "hcc", 1)) {
					essenceCubeInCube = true;
					processingEssenceGems = true;
					lastAutoCubeTick = currentTick;
					return; // Wait for catalyst to be moved
				} else {
					// Can't find catalyst, skip essence gems
					processingEssenceGems = false;
					essenceCubeInCube = false;
				}
			} else if (catalystInCube > 0) {
				essenceCubeInCube = true;
				processingEssenceGems = true;
			}
		} else {
			// No matching gems available, skip essence gems
			processingEssenceGems = false;
			essenceCubeInCube = false;
		}
		
		if (essenceCubeInCube && processingEssenceGems) {
			// Count gems in cube
			int gemsInCube = 0;
			for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
			if (pItem->pItemData->ItemLocation == STORAGE_CUBE) {
				ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
				if (pItemText && pItemText->szCode) {
					// Convert item code to string for lookup
					std::string itemCodeStr(pItemText->szCode, 3); // First 3 characters
					std::map<std::string, ItemAttributes*>::iterator it = ItemAttributeMap.find(itemCodeStr);
					if (it != ItemAttributeMap.end()) {
						ItemAttributes* attrs = it->second;
						// Check if it's a gem using flags
						if (attrs->flags2 & ITEM_GROUP_GEM) {
							gemsInCube++;
						}
					}
				}
			}
			}
			
			// Clear any non-gem, non-cube items first
			for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
				if (pItem->pItemData->ItemLocation == STORAGE_CUBE) {
					ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
					if (pItemText && pItemText->szCode) {
						char* itemCode = pItemText->szCode;
						bool isCatalyst = (itemCode[0] == 'h' && itemCode[1] == 'c' && itemCode[2] == 'c');
						if (!isCatalyst) {
							// Convert item code to string for lookup
							std::string itemCodeStr(itemCode, 3); // First 3 characters
							std::map<std::string, ItemAttributes*>::iterator it = ItemAttributeMap.find(itemCodeStr);
							if (it != ItemAttributeMap.end()) {
							ItemAttributes* attrs = it->second;
							// Check if it's NOT a gem (clear non-gem items)
							if (!(attrs->flags2 & ITEM_GROUP_GEM)) {
								if (!MoveItemToInventory(unit, pItem)) {
									// Inventory full, stop auto-cubing
									isAutoCubing = false;
									processingEssenceGems = false;
									PrintText(Red, "Auto Cube: Stopped - inventory full (cannot move items from cube)");
									return;
								}
								lastAutoCubeTick = currentTick;
								return; // Wait for item to be moved
							}
							}
						}
					}
				}
			}
			
			// If we have less than 3 gems in cube, try to move more
			if (gemsInCube < 3) {
				int needed = 3 - gemsInCube;
				if (FindAndMoveGemsToCube(unit, maxGemLevel, needed)) {
					lastAutoCubeTick = currentTick;
					return; // Wait for gems to be moved
				} else {
					// No more matching gems available
					if (gemsInCube > 0) {
						// Transmute what we have (even if less than 3)
						D2CLIENT_Transmute();
						essenceGemsMoved = 0;
						lastAutoCubeTick = currentTick;
						return; // Wait for transmute
					} else {
						// No gems at all, done with essence gems - clear cube and reset state
						processingEssenceGems = false;
						essenceCubeInCube = false;
						essenceGemsMoved = 0;
						
						// Clear the cube completely
						int clearResult = ClearCube(unit);
						if (clearResult == -1) {
							// Inventory full, stop auto-cubing
							isAutoCubing = false;
							PrintText(Red, "Auto Cube: Stopped - inventory full (cannot move items from cube)");
							return;
						} else if (clearResult == 1) {
							// Item is being moved, wait for it
							lastAutoCubeTick = currentTick;
							return;
						}
					}
				}
			} else {
				// We have 3 gems (or more), transmute
				D2CLIENT_Transmute();
				essenceGemsMoved = 0;
				lastAutoCubeTick = currentTick;
				return; // Wait for transmute, then continue processing essence gems
			}
		}
	} else {
		// Auto Essence Gems is disabled, reset state (but leave catalyst in cube for other essence recipes)
		if (processingEssenceGems || essenceCubeInCube) {
			// We were processing essence gems, now switching to other recipes
			processingEssenceGems = false;
			essenceCubeInCube = false;
			essenceGemsMoved = 0;
		}
	}
	
	// If we're still processing essence gems, don't continue yet
	if (processingEssenceGems) {
		return;
	}
	
	// Process Auto Essence Runes recipe if enabled (process after essence gems, before essence uniques)
	// Only process essence runes if the checkbox is enabled
	if (autoEssenceRunes.state) {
		// Convert dropdown index to rune number (0=El=1, 1=Eld=2, ..., 32=Zod=33)
		BYTE maxRuneNumber = (BYTE)(autoEssenceRuneQuality + 1);
		
		// First, check if there are any matching runes in inventory before moving catalyst
		int matchingRunesInInventory = 0;
		for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
			if (pItem->pItemData->ItemLocation == STORAGE_INVENTORY) {
				ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
				if (pItemText && pItemText->szCode) {
					// Convert item code to string for lookup
					std::string itemCodeStr(pItemText->szCode, 3); // First 3 characters
					std::map<std::string, ItemAttributes*>::iterator it = ItemAttributeMap.find(itemCodeStr);
					if (it != ItemAttributeMap.end()) {
						ItemAttributes* attrs = it->second;
						if (attrs->flags2 & ITEM_GROUP_RUNE) {
							// Get rune number from item code (e.g., "r01" = 1, "r33" = 33)
							BYTE runeNumber = (BYTE)(((pItemText->szCode[1] - '0') * 10) + pItemText->szCode[2] - '0');
							if (runeNumber > 0 && runeNumber <= maxRuneNumber) {
								matchingRunesInInventory++;
							}
						}
					}
				}
			}
		}
		
		// Only process essence runes if we have matching runes OR if we're already processing (catalyst in cube with runes)
		int catalystInCube = CountItemsInCube(unit, "hcc");
		int runesInCube = 0;
		if (catalystInCube > 0) {
			// Count runes already in cube
			for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
				if (pItem->pItemData->ItemLocation == STORAGE_CUBE) {
					ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
					if (pItemText && pItemText->szCode) {
						// Convert item code to string for lookup
						std::string itemCodeStr(pItemText->szCode, 3); // First 3 characters
						std::map<std::string, ItemAttributes*>::iterator it = ItemAttributeMap.find(itemCodeStr);
						if (it != ItemAttributeMap.end()) {
							ItemAttributes* attrs = it->second;
							if (attrs->flags2 & ITEM_GROUP_RUNE) {
								runesInCube++;
							}
						}
					}
				}
			}
		}
		
		// Only process essence runes if we have runes to process (in inventory or already in cube)
		if (matchingRunesInInventory > 0 || runesInCube > 0) {
			if (catalystInCube == 0 && !essenceRunesCubeInCube) {
				// Need to move catalyst to cube first
				if (FindAndMoveItemsToCube(unit, "hcc", 1)) {
					essenceRunesCubeInCube = true;
					processingEssenceRunes = true;
					lastAutoCubeTick = currentTick;
					return; // Wait for catalyst to be moved
				} else {
					// Can't find catalyst, skip essence runes and continue to other recipes
					processingEssenceRunes = false;
					essenceRunesCubeInCube = false;
				}
			} else if (catalystInCube > 0) {
				// Catalyst already in cube, use it directly
				essenceRunesCubeInCube = true;
				processingEssenceRunes = true;
			}
		} else {
			// No matching runes available, skip essence runes
			processingEssenceRunes = false;
			essenceRunesCubeInCube = false;
		}
		
		if (essenceRunesCubeInCube && processingEssenceRunes) {
			// Count runes in cube and check for high runes (> r17)
			int runesInCube = 0;
			bool hasHighRune = false; // Rune number > 17
			for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
			if (pItem->pItemData->ItemLocation == STORAGE_CUBE) {
				ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
				if (pItemText && pItemText->szCode) {
					// Convert item code to string for lookup
					std::string itemCodeStr(pItemText->szCode, 3); // First 3 characters
					std::map<std::string, ItemAttributes*>::iterator it = ItemAttributeMap.find(itemCodeStr);
					if (it != ItemAttributeMap.end()) {
						ItemAttributes* attrs = it->second;
						// Check if it's a rune using flags
						if (attrs->flags2 & ITEM_GROUP_RUNE) {
							runesInCube++;
							// Check if this is a high rune (> r17, rune number > 17)
							BYTE runeNumber = (BYTE)(((pItemText->szCode[1] - '0') * 10) + pItemText->szCode[2] - '0');
							if (runeNumber > 17) {
								hasHighRune = true;
							}
						}
					}
				}
			}
			}
			
			// Clear any non-rune, non-cube items first
			for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
				if (pItem->pItemData->ItemLocation == STORAGE_CUBE) {
					ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
					if (pItemText && pItemText->szCode) {
						char* itemCode = pItemText->szCode;
						bool isCatalyst = (itemCode[0] == 'h' && itemCode[1] == 'c' && itemCode[2] == 'c');
						if (!isCatalyst) {
							// Convert item code to string for lookup
							std::string itemCodeStr(itemCode, 3); // First 3 characters
							std::map<std::string, ItemAttributes*>::iterator it = ItemAttributeMap.find(itemCodeStr);
							if (it != ItemAttributeMap.end()) {
							ItemAttributes* attrs = it->second;
							// Check if it's NOT a rune (clear non-rune items)
							if (!(attrs->flags2 & ITEM_GROUP_RUNE)) {
								if (!MoveItemToInventory(unit, pItem)) {
									// Inventory full, stop auto-cubing
									isAutoCubing = false;
									processingEssenceRunes = false;
									PrintText(Red, "Auto Cube: Stopped - inventory full (cannot move items from cube)");
									return;
								}
								lastAutoCubeTick = currentTick;
								return; // Wait for item to be moved
							}
							}
						}
					}
				}
			}
			
			// If we have a high rune (> r17) in cube, it can only be cubed by itself
			// So transmute immediately if we have 1 rune (the high rune)
			if (hasHighRune) {
				if (runesInCube == 1) {
					// We have exactly 1 high rune, transmute it
					D2CLIENT_Transmute();
					essenceRunesMoved = 0;
					lastAutoCubeTick = currentTick;
					return; // Wait for transmute
				} else if (runesInCube > 1) {
					// We have multiple runes but one is high - this shouldn't happen, clear non-high runes
					// Actually, if we have a high rune, we should only have 1 rune total
					// Clear any extra runes (shouldn't happen, but handle it)
					for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
						if (pItem->pItemData->ItemLocation == STORAGE_CUBE) {
							ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
							if (pItemText && pItemText->szCode) {
								std::string itemCodeStr(pItemText->szCode, 3);
								std::map<std::string, ItemAttributes*>::iterator it = ItemAttributeMap.find(itemCodeStr);
								if (it != ItemAttributeMap.end()) {
									ItemAttributes* attrs = it->second;
									if (attrs->flags2 & ITEM_GROUP_RUNE) {
										BYTE runeNumber = (BYTE)(((pItemText->szCode[1] - '0') * 10) + pItemText->szCode[2] - '0');
										if (runeNumber <= 17) {
											// This is a low rune, remove it
											if (!MoveItemToInventory(unit, pItem)) {
												isAutoCubing = false;
												processingEssenceRunes = false;
												PrintText(Red, "Auto Cube: Stopped - inventory full (cannot move items from cube)");
												return;
											}
											lastAutoCubeTick = currentTick;
											return; // Wait for item to be moved
										}
									}
								}
							}
						}
					}
				}
			}
			
			// For low runes (<= r17), we can cube up to 3 at a time
			// If we have less than 3 runes in cube, try to move more
			if (!hasHighRune && runesInCube < 3) {
				int needed = 3 - runesInCube;
				// Check if there are high runes in inventory - if so, prioritize them first
				// High runes must be cubed one at a time, so handle them separately
				bool hasHighRuneInInventory = false;
				for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
					if (pItem->pItemData->ItemLocation == STORAGE_INVENTORY) {
						ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
						if (pItemText && pItemText->szCode) {
							std::string itemCodeStr(pItemText->szCode, 3);
							std::map<std::string, ItemAttributes*>::iterator it = ItemAttributeMap.find(itemCodeStr);
							if (it != ItemAttributeMap.end()) {
								ItemAttributes* attrs = it->second;
								if (attrs->flags2 & ITEM_GROUP_RUNE) {
									BYTE runeNumber = (BYTE)(((pItemText->szCode[1] - '0') * 10) + pItemText->szCode[2] - '0');
									if (runeNumber > 17 && runeNumber <= maxRuneNumber) {
										hasHighRuneInInventory = true;
										break;
									}
								}
							}
						}
					}
				}
				// If we have high runes in inventory, move them first (one at a time)
				// Clear low runes from cube first if we have high runes to process
				if (hasHighRuneInInventory && runesInCube > 0) {
					// We have low runes in cube but high runes in inventory
					// Transmute the low runes first (even if less than 3)
					D2CLIENT_Transmute();
					essenceRunesMoved = 0;
					lastAutoCubeTick = currentTick;
					return; // Wait for transmute
				}
				// Only move low runes (<= r17) - FindAndMoveRunesToCube will handle high runes separately
				if (FindAndMoveRunesToCube(unit, maxRuneNumber, needed)) {
					lastAutoCubeTick = currentTick;
					return; // Wait for runes to be moved
				} else {
					// No more matching runes available
					if (runesInCube > 0) {
						// Transmute what we have (even if less than 3)
						D2CLIENT_Transmute();
						essenceRunesMoved = 0;
						lastAutoCubeTick = currentTick;
						return; // Wait for transmute
					} else {
						// No runes at all, done with essence runes - clear cube and reset state
						processingEssenceRunes = false;
						essenceRunesCubeInCube = false;
						essenceRunesMoved = 0;
						
						// Clear the cube completely
						int clearResult = ClearCube(unit);
						if (clearResult == -1) {
							// Inventory full, stop auto-cubing
							isAutoCubing = false;
							PrintText(Red, "Auto Cube: Stopped - inventory full (cannot move items from cube)");
							return;
						} else if (clearResult == 1) {
							// Item is being moved, wait for it
							lastAutoCubeTick = currentTick;
							return;
						}
					}
				}
			} else if (!hasHighRune && runesInCube >= 3) {
				// We have 3 or more low runes, transmute
				D2CLIENT_Transmute();
				essenceRunesMoved = 0;
				lastAutoCubeTick = currentTick;
				return; // Wait for transmute, then continue processing essence runes
			}
		}
	} else {
		// Auto Essence Runes is disabled, reset state (but leave catalyst in cube for other essence recipes)
		if (processingEssenceRunes || essenceRunesCubeInCube) {
			// We were processing essence runes, now switching to other recipes
			processingEssenceRunes = false;
			essenceRunesCubeInCube = false;
			essenceRunesMoved = 0;
		}
	}
	
	// If we're still processing essence runes, don't continue yet
	if (processingEssenceRunes) {
		return;
	}
	
	// Process Auto Essence HCC Misc recipe if enabled (process after essence gems and runes, before uniques)
	// Only process essence HCC misc if the checkbox is enabled
	if (autoEssenceHccMisc.state) {
		// Convert dropdown index to tier (0=Tier 1, 1=Tier 2, 2=Tier 3, 3=Tier 4, 4=Tier 5, 5=Tier 6)
		// We process items with tier >= selected tier (selected tier is autoEssenceHccMiscTier + 1)
		int minTier = (int)(autoEssenceHccMiscTier + 1);
		
		// First, check if there are any matching HCC misc items (type "augr") in inventory before moving catalyst
		int matchingHccMiscInInventory = 0;
		for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
			if (pItem->pItemData->ItemLocation == STORAGE_INVENTORY) {
				// Check if it's an "augr" type item
				if (IsAugrType(pItem)) {
					int itemTier = GetItemTierForType(pItem);
					// Process items with tier >= minTier (if tier is 0, it means no tier assigned, skip it)
					if (itemTier > 0 && itemTier >= minTier) {
						matchingHccMiscInInventory++;
					}
				}
			}
		}
		
		// Only process essence HCC misc if we have matching items OR if we're already processing (catalyst in cube with HCC misc)
		int catalystInCube = CountItemsInCube(unit, "hcc");
		int hccMiscInCube = 0;
		if (catalystInCube > 0) {
			// Check if there's an HCC misc item already in cube
			for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
				if (pItem->pItemData->ItemLocation == STORAGE_CUBE) {
					if (IsAugrType(pItem)) {
						int itemTier = GetItemTierForType(pItem);
						if (itemTier > 0 && itemTier >= minTier) {
							hccMiscInCube++;
						}
					}
				}
			}
		}
		
		// Only process essence HCC misc if we have items to process (in inventory or already in cube)
		if (matchingHccMiscInInventory > 0 || hccMiscInCube > 0) {
			if (catalystInCube == 0 && !essenceHccMiscCubeInCube) {
				// Need to move catalyst to cube first
				if (FindAndMoveItemsToCube(unit, "hcc", 1)) {
					essenceHccMiscCubeInCube = true;
					processingEssenceHccMisc = true;
					lastAutoCubeTick = currentTick;
					return; // Wait for catalyst to be moved
				} else {
					// Can't find catalyst, skip essence HCC misc and continue to other recipes
					processingEssenceHccMisc = false;
					essenceHccMiscCubeInCube = false;
				}
			} else if (catalystInCube > 0) {
				// Catalyst already in cube, use it directly
				essenceHccMiscCubeInCube = true;
				processingEssenceHccMisc = true;
			}
		} else {
			// No matching items available, skip essence HCC misc
			processingEssenceHccMisc = false;
			essenceHccMiscCubeInCube = false;
		}
		
		if (essenceHccMiscCubeInCube && processingEssenceHccMisc) {
			// Check if we have an HCC misc item in cube
			bool hasHccMiscInCube = false;
			UnitAny* hccMiscInCubeItem = NULL;
			for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
				if (pItem->pItemData->ItemLocation == STORAGE_CUBE) {
					if (IsAugrType(pItem)) {
						int itemTier = GetItemTierForType(pItem);
						if (itemTier > 0 && itemTier >= minTier) {
							hasHccMiscInCube = true;
							hccMiscInCubeItem = pItem;
							break;
						}
					}
				}
			}
			
			// Clear any non-catalyst, non-HCC misc items first
			for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
				if (pItem->pItemData->ItemLocation == STORAGE_CUBE) {
					ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
					if (pItemText && pItemText->szCode) {
						char* itemCode = pItemText->szCode;
						bool isCatalyst = (itemCode[0] == 'h' && itemCode[1] == 'c' && itemCode[2] == 'c');
						if (!isCatalyst) {
							// Check if it's an HCC misc item with matching tier
							bool isMatchingHccMisc = false;
							if (IsAugrType(pItem)) {
								int itemTier = GetItemTierForType(pItem);
								if (itemTier > 0 && itemTier >= minTier) {
									isMatchingHccMisc = true;
								}
							}
							if (!isMatchingHccMisc) {
								// Not a matching HCC misc item, clear it
								if (!MoveItemToInventory(unit, pItem)) {
									// Inventory full, stop auto-cubing
									isAutoCubing = false;
									processingEssenceHccMisc = false;
									PrintText(Red, "Auto Cube: Stopped - inventory full (cannot move items from cube)");
									return;
								}
								lastAutoCubeTick = currentTick;
								return; // Wait for item to be moved
							}
						}
					}
				}
			}
			
			// If we don't have an HCC misc item in cube, try to move one
			if (!hasHccMiscInCube) {
				// Find a matching HCC misc item in inventory
				UnitAny* itemToMove = NULL;
				for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
					if (pItem->pItemData->ItemLocation == STORAGE_INVENTORY) {
						if (IsAugrType(pItem)) {
							int itemTier = GetItemTierForType(pItem);
							if (itemTier > 0 && itemTier >= minTier) {
								itemToMove = pItem;
								break;
							}
						}
					}
				}
				
				if (itemToMove) {
					// Move the HCC misc item to cube
					ItemText* pItemText = D2COMMON_GetItemText(itemToMove->dwTxtFileNo);
					if (pItemText && pItemText->szCode) {
						int itemGridX = itemToMove->pObjectPath->dwPosX;
						int itemGridY = itemToMove->pObjectPath->dwPosY;
						
						int invUI = D2CLIENT_GetUIState(UI_INVENTORY);
						int stashUI = D2CLIENT_GetUIState(UI_STASH);
						bool moveItem = LoadInventory(unit, STORAGE_INVENTORY, itemGridX, itemGridY, true, false, stashUI, invUI);
						
						if (moveItem) {
							PickUpItem();
							lastAutoCubeTick = currentTick;
							return; // Wait for item to be moved
						}
					}
				} else {
					// No more matching HCC misc items available
					// Done with essence HCC misc - clear cube and reset state
					processingEssenceHccMisc = false;
					essenceHccMiscCubeInCube = false;
					
					// Clear the cube completely
					int clearResult = ClearCube(unit);
					if (clearResult == -1) {
						// Inventory full, stop auto-cubing
						isAutoCubing = false;
						PrintText(Red, "Auto Cube: Stopped - inventory full (cannot move items from cube)");
						return;
					} else if (clearResult == 1) {
						// Item is being moved, wait for it
						lastAutoCubeTick = currentTick;
						return;
					}
				}
			} else {
				// We have an HCC misc item in cube with catalyst, transmute
				D2CLIENT_Transmute();
				lastAutoCubeTick = currentTick;
				return; // Wait for transmute, then continue processing essence HCC misc
			}
		}
	} else {
		// Auto Essence HCC Misc is disabled, reset state (but leave catalyst in cube for other essence recipes)
		if (processingEssenceHccMisc || essenceHccMiscCubeInCube) {
			// We were processing essence HCC misc, now switching to other recipes
			processingEssenceHccMisc = false;
			essenceHccMiscCubeInCube = false;
		}
	}
	
	// If we're still processing essence HCC misc, don't continue yet
	if (processingEssenceHccMisc) {
		return;
	}
	
	// Process Auto Essence Uniques/Sets if enabled (after essence gems, runes, and HCC misc)
	// Only process essence uniques if the checkbox is enabled
	if (autoEssenceUniques.state) {
		// Convert dropdown index to tier (0=Tier 1, 1=Tier 2, 2=Tier 3, 3=Tier 4, 4=Tier 5, 5=Tier 6)
		// We process items with tier >= selected tier (selected tier is autoEssenceUniqueTier + 1)
		int minTier = (int)(autoEssenceUniqueTier + 1);
		
		// First, check if there are any matching unique/set items in inventory before moving catalyst
		int matchingUniquesInInventory = 0;
		for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
			if (pItem->pItemData->ItemLocation == STORAGE_INVENTORY) {
				// Check if it's a unique or set item
				if (pItem->pItemData->dwQuality == ITEM_QUALITY_UNIQUE || 
				    pItem->pItemData->dwQuality == ITEM_QUALITY_SET) {
					// Skip charms (cm1, cm2, cm3, nva, nvb, nvc, nvd, nve, nvf, nvg)
					ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
					if (pItemText && pItemText->szCode) {
						char* itemCode = pItemText->szCode;
						bool isCharm = ((itemCode[0] == 'c' && itemCode[1] == 'm' && 
						                (itemCode[2] == '1' || itemCode[2] == '2' || itemCode[2] == '3')) ||
						               (itemCode[0] == 'n' && itemCode[1] == 'v' &&
						                (itemCode[2] == 'a' || itemCode[2] == 'b' || itemCode[2] == 'c' ||
						                 itemCode[2] == 'd' || itemCode[2] == 'e' || itemCode[2] == 'f' || itemCode[2] == 'g')));
						if (isCharm) {
							continue; // Skip charms
						}
					}
					int itemTier = GetItemTier(pItem);
					// Process items with tier >= minTier (if tier is 0, it means no tier assigned, skip it)
					if (itemTier > 0 && itemTier >= minTier) {
						matchingUniquesInInventory++;
					}
				}
			}
		}
		
		// Only process essence uniques if we have matching items OR if we're already processing (catalyst in cube with unique)
		int catalystInCube = CountItemsInCube(unit, "hcc");
		int uniqueInCube = 0;
		if (catalystInCube > 0) {
			// Check if there's a unique/set item already in cube
			for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
				if (pItem->pItemData->ItemLocation == STORAGE_CUBE) {
					if (pItem->pItemData->dwQuality == ITEM_QUALITY_UNIQUE || 
					    pItem->pItemData->dwQuality == ITEM_QUALITY_SET) {
						// Skip charms (cm1, cm2, cm3, nva, nvb, nvc, nvd, nve, nvf, nvg)
						ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
						if (pItemText && pItemText->szCode) {
							char* itemCode = pItemText->szCode;
							bool isCharm = ((itemCode[0] == 'c' && itemCode[1] == 'm' && 
							                (itemCode[2] == '1' || itemCode[2] == '2' || itemCode[2] == '3')) ||
							               (itemCode[0] == 'n' && itemCode[1] == 'v' &&
							                (itemCode[2] == 'a' || itemCode[2] == 'b' || itemCode[2] == 'c' ||
							                 itemCode[2] == 'd' || itemCode[2] == 'e' || itemCode[2] == 'f' || itemCode[2] == 'g')));
							if (isCharm) {
								continue; // Skip charms
							}
						}
						int itemTier = GetItemTier(pItem);
						if (itemTier > 0 && itemTier >= minTier) {
							uniqueInCube++;
						}
					}
				}
			}
		}
		
		// Only process essence uniques if we have items to process (in inventory or already in cube)
		if (matchingUniquesInInventory > 0 || uniqueInCube > 0) {
			if (catalystInCube == 0 && !essenceUniquesCubeInCube) {
				// Need to move catalyst to cube first
				if (FindAndMoveItemsToCube(unit, "hcc", 1)) {
					essenceUniquesCubeInCube = true;
					processingEssenceUniques = true;
					lastAutoCubeTick = currentTick;
					return; // Wait for catalyst to be moved
				} else {
					// Can't find catalyst, skip essence uniques
					processingEssenceUniques = false;
					essenceUniquesCubeInCube = false;
				}
			} else if (catalystInCube > 0) {
				// Catalyst already in cube, use it directly
				essenceUniquesCubeInCube = true;
				processingEssenceUniques = true;
			}
		} else {
			// No matching items available, skip essence uniques
			processingEssenceUniques = false;
			essenceUniquesCubeInCube = false;
		}
		
		if (essenceUniquesCubeInCube && processingEssenceUniques) {
			// Check if we have a unique/set item in cube
			bool hasUniqueInCube = false;
			UnitAny* uniqueInCubeItem = NULL;
			for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
				if (pItem->pItemData->ItemLocation == STORAGE_CUBE) {
					if (pItem->pItemData->dwQuality == ITEM_QUALITY_UNIQUE || 
					    pItem->pItemData->dwQuality == ITEM_QUALITY_SET) {
						// Skip charms (cm1, cm2, cm3, nva, nvb, nvc, nvd, nve, nvf, nvg)
						ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
						if (pItemText && pItemText->szCode) {
							char* itemCode = pItemText->szCode;
							bool isCharm = ((itemCode[0] == 'c' && itemCode[1] == 'm' && 
							                (itemCode[2] == '1' || itemCode[2] == '2' || itemCode[2] == '3')) ||
							               (itemCode[0] == 'n' && itemCode[1] == 'v' &&
							                (itemCode[2] == 'a' || itemCode[2] == 'b' || itemCode[2] == 'c' ||
							                 itemCode[2] == 'd' || itemCode[2] == 'e' || itemCode[2] == 'f' || itemCode[2] == 'g')));
							if (isCharm) {
								continue; // Skip charms
							}
						}
						int itemTier = GetItemTier(pItem);
						if (itemTier > 0 && itemTier >= minTier) {
							hasUniqueInCube = true;
							uniqueInCubeItem = pItem;
							break;
						}
					}
				}
			}
			
			// Clear any non-catalyst, non-unique/set items first
			for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
				if (pItem->pItemData->ItemLocation == STORAGE_CUBE) {
					ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
					if (pItemText && pItemText->szCode) {
						char* itemCode = pItemText->szCode;
						bool isCatalyst = (itemCode[0] == 'h' && itemCode[1] == 'c' && itemCode[2] == 'c');
						if (!isCatalyst) {
							// Check if it's a unique/set item with matching tier
							bool isMatchingUnique = false;
							if (pItem->pItemData->dwQuality == ITEM_QUALITY_UNIQUE || 
							    pItem->pItemData->dwQuality == ITEM_QUALITY_SET) {
								// Skip charms (cm1, cm2, cm3, nva, nvb, nvc, nvd, nve, nvf, nvg)
								bool isCharm = ((itemCode[0] == 'c' && itemCode[1] == 'm' && 
								                (itemCode[2] == '1' || itemCode[2] == '2' || itemCode[2] == '3')) ||
								               (itemCode[0] == 'n' && itemCode[1] == 'v' &&
								                (itemCode[2] == 'a' || itemCode[2] == 'b' || itemCode[2] == 'c' ||
								                 itemCode[2] == 'd' || itemCode[2] == 'e' || itemCode[2] == 'f' || itemCode[2] == 'g')));
								if (!isCharm) {
									int itemTier = GetItemTier(pItem);
									if (itemTier > 0 && itemTier >= minTier) {
										isMatchingUnique = true;
									}
								}
							}
							if (!isMatchingUnique) {
								// Not a matching unique/set, clear it
								if (!MoveItemToInventory(unit, pItem)) {
									// Inventory full, stop auto-cubing
									isAutoCubing = false;
									processingEssenceUniques = false;
									PrintText(Red, "Auto Cube: Stopped - inventory full (cannot move items from cube)");
									return;
								}
								lastAutoCubeTick = currentTick;
								return; // Wait for item to be moved
							}
						}
					}
				}
			}
			
			// If we don't have a unique/set item in cube, try to move one
			if (!hasUniqueInCube) {
				// Find a matching unique/set item in inventory
				UnitAny* itemToMove = NULL;
				for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
					if (pItem->pItemData->ItemLocation == STORAGE_INVENTORY) {
						if (pItem->pItemData->dwQuality == ITEM_QUALITY_UNIQUE || 
						    pItem->pItemData->dwQuality == ITEM_QUALITY_SET) {
							// Skip charms (cm1, cm2, cm3, nva, nvb, nvc, nvd, nve, nvf, nvg)
							ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
							if (pItemText && pItemText->szCode) {
								char* itemCode = pItemText->szCode;
								bool isCharm = ((itemCode[0] == 'c' && itemCode[1] == 'm' && 
								                (itemCode[2] == '1' || itemCode[2] == '2' || itemCode[2] == '3')) ||
								               (itemCode[0] == 'n' && itemCode[1] == 'v' &&
								                (itemCode[2] == 'a' || itemCode[2] == 'b' || itemCode[2] == 'c' ||
								                 itemCode[2] == 'd' || itemCode[2] == 'e' || itemCode[2] == 'f' || itemCode[2] == 'g')));
								if (isCharm) {
									continue; // Skip charms
								}
							}
							int itemTier = GetItemTier(pItem);
							if (itemTier > 0 && itemTier >= minTier) {
								itemToMove = pItem;
								break;
							}
						}
					}
				}
				
				if (itemToMove) {
					// Move the unique/set item to cube
					ItemText* pItemText = D2COMMON_GetItemText(itemToMove->dwTxtFileNo);
					if (pItemText && pItemText->szCode) {
						int itemGridX = itemToMove->pObjectPath->dwPosX;
						int itemGridY = itemToMove->pObjectPath->dwPosY;
						
						int invUI = D2CLIENT_GetUIState(UI_INVENTORY);
						int stashUI = D2CLIENT_GetUIState(UI_STASH);
						bool moveItem = LoadInventory(unit, STORAGE_INVENTORY, itemGridX, itemGridY, true, false, stashUI, invUI);
						
						if (moveItem) {
							PickUpItem();
							lastAutoCubeTick = currentTick;
							return; // Wait for item to be moved
						}
					}
				} else {
					// No more matching unique/set items available
					// Done with essence uniques - clear cube and reset state
					processingEssenceUniques = false;
					essenceUniquesCubeInCube = false;
					
					// Clear the cube completely
					int clearResult = ClearCube(unit);
					if (clearResult == -1) {
						// Inventory full, stop auto-cubing
						isAutoCubing = false;
						PrintText(Red, "Auto Cube: Stopped - inventory full (cannot move items from cube)");
						return;
					} else if (clearResult == 1) {
						// Item is being moved, wait for it
						lastAutoCubeTick = currentTick;
						return;
					}
				}
			} else {
				// We have a unique/set item in cube with catalyst, transmute
				D2CLIENT_Transmute();
				lastAutoCubeTick = currentTick;
				return; // Wait for transmute, then continue processing essence uniques
			}
		}
	} else {
		// Auto Essence Uniques is disabled, reset state (but leave catalyst in cube for other essence recipes)
		if (processingEssenceUniques || essenceUniquesCubeInCube) {
			// We were processing essence uniques, now switching to other recipes
			processingEssenceUniques = false;
			essenceUniquesCubeInCube = false;
		}
	}
	
	// If we're still processing any essence recipe, don't continue yet
	// Process all essence recipes first (gems, runes, HCC misc, uniques)
	if (processingEssenceGems || processingEssenceRunes || processingEssenceHccMisc || processingEssenceUniques) {
		return;
	}
	
	// All essence recipes are done. If catalyst is still in cube and no essence recipes are enabled, move it back
	// But only if NO essence recipes are enabled at all
	if (!autoEssenceGems.state && !autoEssenceRunes.state && !autoEssenceHccMisc.state && !autoEssenceUniques.state) {
		int catalystInCube = CountItemsInCube(unit, "hcc");
		if (catalystInCube > 0) {
			// Check if cube has any items that might be from essence recipes
			bool hasEssenceItemInCube = false;
			for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
				if (pItem->pItemData->ItemLocation == STORAGE_CUBE) {
					ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
					if (pItemText && pItemText->szCode) {
						char* itemCode = pItemText->szCode;
						bool isCatalyst = (itemCode[0] == 'h' && itemCode[1] == 'c' && itemCode[2] == 'c');
						if (!isCatalyst) {
							// Check if it's a gem, rune, unique/set, or HCC misc item
							std::string itemCodeStr(itemCode, 3);
							std::map<std::string, ItemAttributes*>::iterator it = ItemAttributeMap.find(itemCodeStr);
							if (it != ItemAttributeMap.end()) {
								ItemAttributes* attrs = it->second;
								if (attrs->flags2 & ITEM_GROUP_GEM || attrs->flags2 & ITEM_GROUP_RUNE) {
									hasEssenceItemInCube = true;
									break;
								}
							}
							if (pItem->pItemData->dwQuality == ITEM_QUALITY_UNIQUE || 
							    pItem->pItemData->dwQuality == ITEM_QUALITY_SET ||
							    IsAugrType(pItem)) {
								hasEssenceItemInCube = true;
								break;
							}
						}
					}
				}
			}
			
			// Only move catalyst back if there's no essence-related item in cube
			if (!hasEssenceItemInCube) {
				// Find and move catalyst back to inventory
				for (UnitAny *pItem = unit->pInventory->pFirstItem; pItem; pItem = pItem->pItemData->pNextInvItem) {
					if (pItem->pItemData->ItemLocation == STORAGE_CUBE) {
						ItemText* pItemText = D2COMMON_GetItemText(pItem->dwTxtFileNo);
						if (pItemText && pItemText->szCode) {
							char* itemCode = pItemText->szCode;
							bool isCatalyst = (itemCode[0] == 'h' && itemCode[1] == 'c' && itemCode[2] == 'c');
							if (isCatalyst) {
								if (MoveItemToInventory(unit, pItem)) {
									lastAutoCubeTick = currentTick;
									return; // Wait for catalyst to be moved back
								}
								break;
							}
						}
					}
				}
			}
		}
	}
	
	// Combine any remaining stacks after essence cubing, then finish.
	if (autoStackItems.state) {
		if (ProcessAutoStackStep()) {
			lastAutoCubeTick = currentTick;
			return;
		}
	}

	isAutoCubing = false;
	PrintText(White, "Auto Cube: Finished");
}

void ItemMover::OnGameExit() {
	ActivePacket.itemId = 0;
	ActivePacket.x = 0;
	ActivePacket.y = 0;
	ActivePacket.startTicks = 0;
	ActivePacket.destination = 0;
	stackDropOnItem = false;
	goldPickupQueue.clear();
	previousHP = 0;
	damageTakenTick = 0;
}

// Code for reading the 0x9c bitstream (borrowed from heroin_glands)
void ParseItem(const unsigned char *data, ItemInfo *item, bool *success) {
	*success = true;
	try {
		BitReader reader(data);
		unsigned long packet = reader.read(8);
		item->action = reader.read(8);
		unsigned long messageSize = reader.read(8);
		item->category = reader.read(8); // item type
		item->id = reader.read(32);

		if (packet == 0x9d) {
			reader.read(32);
			reader.read(8);
		}

		item->equipped = reader.readBool();
		reader.readBool();
		reader.readBool();
		item->inSocket = reader.readBool();
		item->identified = reader.readBool();
		reader.readBool();
		item->switchedIn = reader.readBool();
		item->switchedOut = reader.readBool();

		item->broken = reader.readBool();
		reader.readBool();
		item->potion = reader.readBool();
		item->hasSockets = reader.readBool();
		reader.readBool();
		item->inStore = reader.readBool();
		item->notInSocket = reader.readBool();
		reader.readBool();

		item->ear = reader.readBool();
		item->startItem = reader.readBool();
		reader.readBool();
		reader.readBool();
		reader.readBool();
		item->simpleItem = reader.readBool();
		item->ethereal = reader.readBool();
		reader.readBool();

		item->personalized = reader.readBool();
		item->gambling = reader.readBool();
		item->runeword = reader.readBool();
		reader.read(5);

		item->version = static_cast<unsigned int>(reader.read(8));

		reader.read(2);
		unsigned long destination = reader.read(3);

		item->ground = (destination == 0x03);

		if (item->ground) {
			item->x = reader.read(16);
			item->y = reader.read(16);
		} else {
			item->directory = reader.read(4);
			item->x = reader.read(4);
			item->y = reader.read(3);
			item->container = static_cast<unsigned int>(reader.read(4));
		}

		item->unspecifiedDirectory = false;

		if (item->action == ITEM_ACTION_TO_STORE || item->action == ITEM_ACTION_FROM_STORE) {
			long container = static_cast<long>(item->container);
			container |= 0x80;
			if (container & 1) {
				container--; //remove first bit
				item->y += 8;
			}
			item->container = static_cast<unsigned int>(container);
		} else if (item->container == CONTAINER_UNSPECIFIED) {
			if (item->directory == EQUIP_NONE) {
				if (item->inSocket) {
					//y is ignored for this container type, x tells you the index
					item->container = CONTAINER_ITEM;
				} else if (item->action == ITEM_ACTION_PLACE_BELT || item->action == ITEM_ACTION_REMOVE_BELT) {
					item->container = CONTAINER_BELT;
					item->y = item->x / 4;
					item->x %= 4;
				}
			} else {
				item->unspecifiedDirectory = true;
			}
		}

		if (item->ear) {
			item->earClass = static_cast<BYTE>(reader.read(3));
			item->earLevel = reader.read(7);
			item->code[0] = 'e';
			item->code[1] = 'a';
			item->code[2] = 'r';
			item->code[3] = 0;
			for (std::size_t i = 0; i < 16; i++) {
				char letter = static_cast<char>(reader.read(7));
				if (letter == 0) {
					break;
				}
				item->earName.push_back(letter);
			}
			item->attrs = ItemAttributeMap[item->code];
			item->name = item->attrs->name;
			item->width = item->attrs->width;
			item->height = item->attrs->height;
			//PrintText(1, "Ear packet: %s, %s, %d, %d", item->earName.c_str(), item->code, item->earClass, item->earLevel);
			return;
		}

		for (std::size_t i = 0; i < 4; i++) {
			item->code[i] = static_cast<char>(reader.read(8));
		}
		item->code[3] = 0;

		if (ItemAttributeMap.find(item->code) == ItemAttributeMap.end()) {
			HandleUnknownItemCode(item->code, "from packet");
			*success = false;
			return;
		}
		item->attrs = ItemAttributeMap[item->code];
		item->name = item->attrs->name;
		item->width = item->attrs->width;
		item->height = item->attrs->height;

		item->isGold = (item->code[0] == 'g' && item->code[1] == 'l' && item->code[2] == 'd');

		if (item->isGold) {
			bool big_pile = reader.readBool();
			if (big_pile) {
				item->amount = reader.read(32);
			} else {
				item->amount = reader.read(12);
			}
			return;
		}

		item->usedSockets = (BYTE)reader.read(3);

		if (item->simpleItem || item->gambling) {
			return;
		}

		item->level = (BYTE)reader.read(7);
		item->quality = static_cast<unsigned int>(reader.read(4));

		item->hasGraphic = reader.readBool();;
		if (item->hasGraphic) {
			item->graphic = reader.read(3);
		}

		item->hasColor = reader.readBool();
		if (item->hasColor) {
			item->color = reader.read(11);
		}

		if (item->identified) {
			switch(item->quality) {
			case ITEM_QUALITY_INFERIOR:
				item->prefix = reader.read(3);
				break;

			case ITEM_QUALITY_SUPERIOR:
				item->superiority = static_cast<unsigned int>(reader.read(3));
				break;

			case ITEM_QUALITY_MAGIC:
				item->prefix = reader.read(11);
				item->suffix = reader.read(11);
				break;

			case ITEM_QUALITY_CRAFT:
			case ITEM_QUALITY_RARE:
				item->prefix = reader.read(8) - 156;
				item->suffix = reader.read(8) - 1;
				break;

			case ITEM_QUALITY_SET:
				item->setCode = reader.read(12);
				break;

			case ITEM_QUALITY_UNIQUE:
				if (item->code[0] != 's' || item->code[1] != 't' || item->code[2] != 'd') { //standard of heroes exception?
					item->uniqueCode = reader.read(12);
				}
				break;
			}
		}

		if (item->quality == ITEM_QUALITY_RARE || item->quality == ITEM_QUALITY_CRAFT) {
			for (unsigned long i = 0; i < 3; i++) {
				if (reader.readBool()) {
					item->prefixes.push_back(reader.read(11));
				}
				if (reader.readBool()) {
					item->suffixes.push_back(reader.read(11));
				}
			}
		}

		if (item->runeword) {
			item->runewordId = reader.read(12);
			item->runewordParameter = reader.read(4);
		}

		if (item->personalized) {
			for (std::size_t i = 0; i < 16; i++) {
				char letter = static_cast<char>(reader.read(7));
				if (letter == 0) {
					break;
				}
				item->personalizedName.push_back(letter);
			}
			//PrintText(1, "Personalized packet: %s, %s", item->personalizedName.c_str(), item->code);
		}

		item->isArmor = (item->attrs->flags & ITEM_GROUP_ALLARMOR) > 0;
		item->isWeapon = (item->attrs->flags & ITEM_GROUP_ALLWEAPON) > 0;

		if (item->isArmor) {
			item->defense = reader.read(11) - 10;
		}

		/*if(entry.throwable)
		{
			reader.read(9);
			reader.read(17);
		} else */
		//special case: indestructible phase blade
		if (item->code[0] == '7' && item->code[1] == 'c' && item->code[2] == 'r') {
			reader.read(8);
		} else if (item->isArmor || item->isWeapon) {
			item->maxDurability = reader.read(8);
			item->indestructible = item->maxDurability == 0;
			/*if (!item->indestructible) {
				item->durability = reader.read(8);
				reader.readBool();
			}*/
			//D2Hackit always reads it, hmmm. Appears to work.
			item->durability = reader.read(8);
			reader.readBool();
		}

		if (item->hasSockets) {
			item->sockets = (BYTE)reader.read(4);
		}

		if (!item->identified) {
			return;
		}

		if (item->attrs->stackable) {
			if (item->attrs->useable) {
				reader.read(5);
			}
			item->amount = reader.read(9);
		}

		if (item->quality == ITEM_QUALITY_SET) {
			unsigned long set_mods = reader.read(5);
		}

		while (true) {
			unsigned long stat_id = reader.read(9);
			if (stat_id == 0x1ff) {
				break;
			}
			ItemProperty prop = {};
			if (!ProcessStat(stat_id, reader, prop) &&
					!(*BH::MiscToggles2)["Suppress Invalid Stats"].state) {
				PrintText(1, "Invalid stat: %d, %c%c%c", stat_id, item->code[0], item->code[1], item->code[2]);
				*success = false;
				break;
			}
			item->properties.push_back(prop);
		}
	} catch (int e) {
		PrintText(1, "Int exception parsing item: %c%c%c, %d", item->code[0], item->code[1], item->code[2], e);
	} catch (std::exception const & ex) {
		PrintText(1, "Exception parsing item: %c%c%c, %s", item->code[0], item->code[1], item->code[2], ex.what());
	} catch(...) {
		PrintText(1, "Miscellaneous exception parsing item: %c%c%c", item->code[0], item->code[1], item->code[2]);
		*success = false;
	}
	return;
}

bool ProcessStat(unsigned int stat, BitReader &reader, ItemProperty &itemProp) {
	if (stat > STAT_MAX) {
		return false;
	}

	StatProperties *bits = GetStatProperties(stat);
	unsigned int saveBits = bits->saveBits;
	unsigned int saveParamBits = bits->saveParamBits;
	unsigned int saveAdd = bits->saveAdd;
	itemProp.stat = stat;

	if (saveParamBits > 0) {
		switch (stat) {
			case STAT_CLASSSKILLS:
			{
				itemProp.characterClass = reader.read(saveParamBits);
				itemProp.value = reader.read(saveBits);
				return true;
			}
			case STAT_NONCLASSSKILL:
			case STAT_SINGLESKILL:
			{
				itemProp.skill = reader.read(saveParamBits);
				itemProp.value = reader.read(saveBits);
				return true;
			}
			case STAT_ELEMENTALSKILLS:
			{
				ulong element = reader.read(saveParamBits);
				itemProp.value = reader.read(saveBits);
				return true;
			}
			case STAT_AURA:
			{
				itemProp.skill = reader.read(saveParamBits);
				itemProp.value = reader.read(saveBits);
				return true;
			}
			case STAT_REANIMATE:
			{
				itemProp.monster = reader.read(saveParamBits);
				itemProp.value = reader.read(saveBits);
				return true;
			}
			case STAT_SKILLTAB:
			{
				itemProp.tab = reader.read(3);
				itemProp.characterClass = reader.read(3);
				ulong unknown = reader.read(10);
				itemProp.value = reader.read(saveBits);
				return true;
			}
			case STAT_SKILLONDEATH:
			case STAT_SKILLONHIT:
			case STAT_SKILLONKILL:
			case STAT_SKILLONLEVELUP:
			case STAT_SKILLONSTRIKING:
			case STAT_SKILLWHENSTRUCK:
			{
				itemProp.level = reader.read(6);
				itemProp.skill = reader.read(10);
				itemProp.skillChance = reader.read(saveBits);
				return true;
			}
			case STAT_CHARGED:
			{
				itemProp.level = reader.read(6);
				itemProp.skill = reader.read(10);
				itemProp.charges = reader.read(8);
				itemProp.maximumCharges = reader.read(8);
				return true;
			}
			case STAT_STATE:
			case STAT_ATTCKRTNGVSMONSTERTYPE:
			case STAT_DAMAGETOMONSTERTYPE:
			{
				// For some reason heroin_glands doesn't read these, even though
				// they have saveParamBits; maybe they don't occur in practice?
				itemProp.value = reader.read(saveBits) - saveAdd;
				return true;
			}
			default:
				reader.read(saveParamBits);
				reader.read(saveBits);
				return true;
		}
	}

	if (bits->op >= 2 && bits->op <= 5) {
		itemProp.perLevel = reader.read(saveBits);
		return true;
	}

	switch (stat) {
		case STAT_ENHANCEDMAXIMUMDAMAGE:
		case STAT_ENHANCEDMINIMUMDAMAGE:
		{
			itemProp.minimum = reader.read(saveBits);
			itemProp.maximum = reader.read(saveBits);
			return true;
		}
		case STAT_MINIMUMFIREDAMAGE:
		{
			itemProp.minimum = reader.read(saveBits);
			itemProp.maximum = reader.read(GetStatProperties(STAT_MAXIMUMFIREDAMAGE)->saveBits);
			return true;
		}
		case STAT_MINIMUMLIGHTNINGDAMAGE:
		{
			itemProp.minimum = reader.read(saveBits);
			itemProp.maximum = reader.read(GetStatProperties(STAT_MAXIMUMLIGHTNINGDAMAGE)->saveBits);
			return true;
		}
		case STAT_MINIMUMMAGICALDAMAGE:
		{
			itemProp.minimum = reader.read(saveBits);
			itemProp.maximum = reader.read(GetStatProperties(STAT_MAXIMUMMAGICALDAMAGE)->saveBits);
			return true;
		}
		case STAT_MINIMUMCOLDDAMAGE:
		{
			itemProp.minimum = reader.read(saveBits);
			itemProp.maximum = reader.read(GetStatProperties(STAT_MAXIMUMCOLDDAMAGE)->saveBits);
			itemProp.length = reader.read(GetStatProperties(STAT_COLDDAMAGELENGTH)->saveBits);
			return true;
		}
		case STAT_MINIMUMPOISONDAMAGE:
		{
			itemProp.minimum = reader.read(saveBits);
			itemProp.maximum = reader.read(GetStatProperties(STAT_MAXIMUMPOISONDAMAGE)->saveBits);
			itemProp.length = reader.read(GetStatProperties(STAT_POISONDAMAGELENGTH)->saveBits);
			return true;
		}
		case STAT_REPAIRSDURABILITY:
		case STAT_REPLENISHESQUANTITY:
		{
			itemProp.value = reader.read(saveBits);
			return true;
		}
		default:
		{
			itemProp.value = reader.read(saveBits) - saveAdd;
			return true;
		}
	}
}
