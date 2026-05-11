#pragma once
#include "../../D2Structs.h"
#include "../../Drawing.h"
#include "../Module.h"
#include "../../Config.h"
#include "../../Common.h"
#include "../../BitReader.h"
#include "../Item/ItemDisplay.h"
#include "../../MPQInit.h"
#include <vector>
#include <map>
#include <deque>
#include <set>
#include <string>

extern int INVENTORY_WIDTH;
extern int INVENTORY_HEIGHT;
extern int STASH_WIDTH;
extern int LOD_STASH_HEIGHT;
extern int CLASSIC_STASH_HEIGHT;
extern int CUBE_WIDTH;
extern int CUBE_HEIGHT;

extern int INVENTORY_LEFT;
extern int INVENTORY_TOP;
extern int STASH_LEFT;
extern int LOD_STASH_TOP;
extern int CLASSIC_STASH_TOP;
extern int CUBE_LEFT;
extern int CUBE_TOP;
extern int CELL_SIZE;


struct ItemPacketData {
	unsigned int itemId;
	unsigned int x;
	unsigned int y;
	ULONGLONG startTicks;
	unsigned int destination;
};

struct GoldItemData {
	DWORD itemId;
	DWORD x;
	DWORD y;
};

struct QueuedGoldPickup {
	DWORD itemId;
	DWORD x;
	DWORD y;
	ULONGLONG queueTime;
};

struct CubeRecipe {
	const char* inputCode;
	const char* outputCode;
	int maxQuantity;
};

// Stash item tracking for AutoCube stash interaction
struct StashItemRecord {
	DWORD itemId;
	char itemCode[4];  // 3-char code + null terminator
	unsigned int x;
	unsigned int y;
};

class ItemMover : public Module {
private:
	bool FirstInit;
	int *InventoryItemIds;
	int *StashItemIds;
	int *LODStashItemIds;
	int *ClassicStashItemIds;
	int *CubeItemIds;
	int tp_warn_quantity;
	InventoryLayout *stashLayout;
	InventoryLayout *inventoryLayout;
	InventoryLayout *cubeLayout;
	unsigned int TpKey;
	unsigned int HealKey;
	unsigned int ManaKey;
	unsigned int JuvKey;
	unsigned int TransmuteKey;
	unsigned int AutoCubeKey;
	ItemPacketData ActivePacket;
	CRITICAL_SECTION crit;
	Drawing::UITab* settingsTab;
	
	// Auto-cube state variables
	bool isAutoCubing;
	int currentRecipeIdx;
	int currentRecipeIteration;
	ULONGLONG lastAutoCubeTick;
	std::vector<int> validRecipeIndices;  // Indices of recipes that have matching items
	bool validRecipesScanned;  // Whether we've scanned for valid recipes this cycle
	int targetOutputCount;  // How many output items we're trying to move for current recipe
	int movedOutputCount;   // How many output items we've moved so far
	bool clearingNonRecipeItems;  // Are we currently clearing non-recipe items from cube?
	DWORD clearingItemId;  // Item ID we're currently trying to clear
	bool waitingForOutputTransmute;  // Did we just transmute output-only and are waiting for input to appear?
	bool waitingForNormalTransmute;  // Did we just transmute input+output and are waiting for it to complete?
	bool progressMadeThisCycle;  // Was any progress made in the current recipe cycle?
	int failedOutputOnlyCount;  // Count of times output-only transmute failed for this recipe
	bool processingLowerStatItem;  // Are we processing a lower stat 508 item (transmuting alone)?
	bool waitingForLowerStatTransmute;  // Waiting for lower stat transmute to complete?
	bool waitingForLowerStatOutputMove;  // Waiting for lower stat output item to be moved to inventory?
	int lowerStatTargetOutputs;  // How many outputs we need to generate to fill the highest stat item to 100
	int lowerStatOutputsGenerated;  // How many outputs we've generated so far from processing the lowest stat item
	int highestStatValue;  // The stat 508 value of the highest item we're trying to fill
	bool processingEssenceGems;  // Are we currently processing essence gems?
	int essenceGemsMoved;  // How many gems we've moved for current essence gem recipe
	bool essenceCubeInCube;  // Is the Horadric Cube (hcc) in the cube?
	bool processingEssenceRunes;  // Are we currently processing essence runes?
	int essenceRunesMoved;  // How many runes we've moved for current essence rune recipe
	bool essenceRunesCubeInCube;  // Is the catalyst (hcc) in the cube for essence runes?
	bool processingEssenceUniques;  // Are we currently processing essence uniques/sets?
	bool essenceUniquesCubeInCube;  // Is the catalyst (hcc) in the cube for essence uniques?
	bool processingEssenceHccMisc;  // Are we currently processing essence HCC misc items?
	bool essenceHccMiscCubeInCube;  // Is the catalyst (hcc) in the cube for essence HCC misc?
	ULONGLONG cursorItemStartTick;  // When did we first detect an item on cursor?
	bool cursorItemRecoveryAttempted;  // Have we tried to recover from stuck cursor?
	ULONGLONG cursorItemRecoveryTick;  // When did we last attempt recovery?
	DWORD lastMovedItemId;  // Item ID of the last item we moved
	unsigned int lastMovedDestination;  // Destination of the last item we moved (STORAGE_CUBE, STORAGE_INVENTORY, etc.)
	unsigned int lastMovedX;  // Expected X coordinate of the last moved item
	unsigned int lastMovedY;  // Expected Y coordinate of the last moved item
	bool lastMoveVerified;  // Whether the last moved item has been verified as placed
	ULONGLONG lastMoveCompleteTick;  // When did the last move complete (ActivePacket.startTicks became 0)?
	
	// Auto gold pickup members
	Toggle autoPickupGold;
	unsigned int goldPickupRange;
	ULONGLONG lastPickupTick;
	std::map<DWORD, GoldItemData> trackedGoldItems;
	std::deque<QueuedGoldPickup> goldPickupQueue;
	ULONGLONG lastQueuedPickupTick;
	DWORD previousHP;
	ULONGLONG damageTakenTick;
	
	// Auto cube settings
	Toggle autoStackItems;
	Toggle autoEssenceGems;
	Toggle autoEssenceRunes;
	Toggle autoEssenceUniques;
	Toggle autoEssenceHccMisc;
	unsigned int autoEssenceGemQuality;  // Index for gem quality dropdown
	unsigned int autoEssenceRuneQuality;  // Index for rune quality dropdown
	unsigned int autoEssenceUniqueTier;  // Index for unique/set tier dropdown
	unsigned int autoEssenceHccMiscTier;  // Index for HCC misc tier dropdown
	
	// Stash interaction state for AutoCube
	bool stashInteractionMode;           // True if AutoCube was started with stash open
	std::vector<StashItemRecord> allStashInputItems;  // ALL items found in stash to process
	std::vector<StashItemRecord> stashItemsToMove;  // Current batch of items to move
	int stashBatchStartIndex;            // Index in allStashInputItems for current batch start
	int stashMoveIndex;                  // Current index in stashItemsToMove being moved
	bool waitingForStashToInvMove;       // Waiting for stash->inventory move to complete
	bool waitingForInvToStashMove;       // Waiting for inventory->stash move to complete
	bool waitingForCubeToOpen;           // Waiting for cube to open
	bool waitingForCubeToClose;          // Waiting for cube to close after AutoCube
	bool waitingForStashToReopen;        // Waiting for stash to reopen
	bool restoringItemsToStash;          // Currently restoring items back to stash
	int stashRestoreIndex;               // Current index being restored
	DWORD savedStashUnitId;              // Unit ID of stash object to reopen
	bool processingStashBatch;           // True during autocube phase for stash items (limits recipes)
	std::set<std::string> stashMovedItemCodes;  // Item codes moved from stash for filtering recipes
public:
	ItemMover() : Module("Item Mover"),
		ActivePacket(),
		FirstInit(false),
		InventoryItemIds(NULL),
		StashItemIds(NULL),
		LODStashItemIds(NULL),
		ClassicStashItemIds(NULL),
		CubeItemIds(NULL),
		stashLayout(NULL),
		inventoryLayout(NULL),
		cubeLayout(NULL),
		tp_warn_quantity(5),
		goldPickupRange(5),
		lastPickupTick(0),
		lastQueuedPickupTick(0),
		previousHP(0),
		damageTakenTick(0),
		isAutoCubing(false),
		currentRecipeIdx(0),
		currentRecipeIteration(0),
		lastAutoCubeTick(0),
		targetOutputCount(0),
		movedOutputCount(0),
		clearingNonRecipeItems(false),
		clearingItemId(0),
		waitingForOutputTransmute(false),
		waitingForNormalTransmute(false),
		progressMadeThisCycle(false),
		failedOutputOnlyCount(0),
		processingLowerStatItem(false),
		waitingForLowerStatTransmute(false),
		waitingForLowerStatOutputMove(false),
		lowerStatTargetOutputs(0),
		lowerStatOutputsGenerated(0),
		highestStatValue(0),
		processingEssenceGems(false),
		essenceGemsMoved(0),
		essenceCubeInCube(false),
		processingEssenceRunes(false),
		essenceRunesMoved(0),
		essenceRunesCubeInCube(false),
		processingEssenceUniques(false),
		essenceUniquesCubeInCube(false),
		processingEssenceHccMisc(false),
		essenceHccMiscCubeInCube(false),
		validRecipesScanned(false),
		cursorItemStartTick(0),
		cursorItemRecoveryAttempted(false),
		cursorItemRecoveryTick(0),
		lastMovedItemId(0),
		lastMovedDestination(0),
		lastMovedX(0),
		lastMovedY(0),
		lastMoveVerified(true),
		lastMoveCompleteTick(0),
		autoEssenceGemQuality(0),
		autoEssenceRuneQuality(0),
		autoEssenceUniqueTier(0),
		autoEssenceHccMiscTier(0),
		stashInteractionMode(false),
		stashBatchStartIndex(0),
		stashMoveIndex(0),
		waitingForStashToInvMove(false),
		waitingForInvToStashMove(false),
		waitingForCubeToOpen(false),
		waitingForCubeToClose(false),
		waitingForStashToReopen(false),
		restoringItemsToStash(false),
		stashRestoreIndex(0),
		savedStashUnitId(0),
		processingStashBatch(false) {

		InitializeCriticalSection(&crit);
		// Initialize toggles to safe defaults
		autoPickupGold.toggle = 0;
		autoPickupGold.state = false;
		autoStackItems.toggle = 0;
		autoStackItems.state = false;
		autoEssenceGems.toggle = 0;
		autoEssenceGems.state = false;
		autoEssenceRunes.toggle = 0;
		autoEssenceRunes.state = false;
		autoEssenceUniques.toggle = 0;
		autoEssenceUniques.state = false;
		autoEssenceHccMisc.toggle = 0;
		autoEssenceHccMisc.state = false;
	};

	~ItemMover() {
		if (InventoryItemIds) {
			delete [] InventoryItemIds;
		}
		if (StashItemIds) {
			delete [] StashItemIds;
		}
		if (LODStashItemIds) {
			delete [] LODStashItemIds;
		}
		if (ClassicStashItemIds) {
			delete [] ClassicStashItemIds;
		}
		if (CubeItemIds) {
			delete [] CubeItemIds;
		}
		DeleteCriticalSection(&crit);
	};

	bool Init();

	void Lock() { EnterCriticalSection(&crit); };
	void Unlock() { LeaveCriticalSection(&crit); };

	bool LoadInventory(UnitAny *unit, int source, int sourceX, int sourceY, bool shiftState, bool ctrlState, int stashUI, int invUI);
	bool FindDestination(int destination, unsigned int itemId, BYTE xSize, BYTE ySize);
	void PickUpItem();
	void PutItemInContainer();
	void PutItemOnGround();

	// Auto-cube helper functions
	bool MoveItemToInventory(UnitAny* unit, UnitAny* item);
	// Returns: true if item moved, false if failed (inventory full or error)
	// Returns: -1 if inventory full, 0 if no items to clear, 1 if item being moved
	int ClearCube(UnitAny* unit);
	// Returns: -1 if inventory full, 0 if no items to clear, 1 if item being moved
	int ClearNonRecipeItemsFromCube(UnitAny* unit, const char* inputCode, const char* outputCode);
	int CountItemsInCube(UnitAny* unit, const char* code);
	int CountItemsInInventoryAndCube(UnitAny* unit, const char* code);
	bool FindAndMoveItemsToCube(UnitAny* unit, const char* code, int needed);
	bool FindAndMoveGemsToCube(UnitAny* unit, BYTE maxGemLevel, int maxCount);
	bool FindAndMoveRunesToCube(UnitAny* unit, BYTE maxRuneNumber, int maxCount);
	int GetItemTier(UnitAny* item);  // Get tier from ItemDisplay rules for unique/set items
	int GetItemTierForType(UnitAny* item);  // Get tier from ItemDisplay rules for any item type
	bool IsAugrType(UnitAny* item);  // Check if item is of type "augr" (from misc.txt category)
	int GetItemStat508(UnitAny* item);  // Get stat 508 value from an item (returns -1 if not found)
	bool IsStat508LimitedRecipe(const char* inputCode);  // Check if recipe is stat 508 limited (checks against recipes array)
	UnitAny* GetInputItemFromCube(UnitAny* unit, const char* inputCode);  // Get the input item from cube for a recipe
	int GetMinMaxAllowedFromCubeInputs(UnitAny* unit, const char* inputCode, int recipeMaxQuantity);  // Get minimum maxAllowed from all input items in cube (for stat 508)
	static const CubeRecipe* GetRecipesArray();  // Get the recipes array
	static int GetRecipesArraySize();  // Get the size of the recipes array
	bool PerformAutoCube();
	void ProcessAutoCubeStep();
	
	// Stash interaction functions for AutoCube
	void ScanStashForInputItems(UnitAny* unit);  // Scan stash for INPUT code items
	void ProcessStashInteraction();             // Main state machine for stash interaction
	void ResetStashInteractionState();          // Reset all stash interaction state
	void StopAutoCubeFromUserClick();           // While auto-cubing, any left/right click down stops the process

	void LoadConfig();

	void OnLoad();
	void OnLoop();
	void OnKey(bool up, BYTE key, LPARAM lParam, bool* block);
	void OnLeftClick(bool up, unsigned int x, unsigned int y, bool* block);
	void OnRightClick(bool up, unsigned int x, unsigned int y, bool* block);
	void OnGamePacketRecv(BYTE* packet, bool *block);
	void OnGameExit();
	Drawing::UITab* GetInteractionTab() { return settingsTab; }
};


void ParseItem(const unsigned char *data, ItemInfo *ii, bool *success);
bool ProcessStat(unsigned int statId, BitReader &reader, ItemProperty &itemProp);
