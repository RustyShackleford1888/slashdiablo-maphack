#include "Gambling.h"
#include "../../D2Ptrs.h"
#include "../../D2Stubs.h"
#include "../../BH.h"
#include "../../D2Helpers.h"
#include "../../Constants.h"
#include "../ItemMover/ItemMover.h"

using namespace std;

void Gambling::LoadConfig() {
    BH::config->ReadKey("Gambling Refresh", "None", refreshKey);
}

void Gambling::OnLoad() {
    LoadConfig();
    lastRefreshTime = 0;
    pendingRefresh = false;
    pendingGambleTime = 0;
    inGamblingScreen = false;

    Toggles["Enabled"].toggle = 0;
    Toggles["Enabled"].state = true;
}

void Gambling::OnUnload() {
}

// ------------------------------------------------------------
// Key handler — FLAGS ONLY (NO GAME CALLS)
// ------------------------------------------------------------
void Gambling::OnKey(bool up, BYTE key, LPARAM lParam, bool* block) {
    // Only process key up events
    if (!up)
        return;

    // Only process the refresh key (skip if no key set)
    if (refreshKey == 0 || key != (BYTE)refreshKey)
        return;

    // Only allow when gambling UI is open
    if (!IsGambling())
        return;

    pendingRefresh = true;
    *block = true;
}

// ------------------------------------------------------------
// Safe UI execution point (game thread)
// ------------------------------------------------------------
void Gambling::OnDraw() {
    DWORD now = GetTickCount();
    
    // If we're waiting for the NPC menu to open before calling StartGamble
    if (pendingGambleTime > 0) {
        DWORD npcMenuState = D2CLIENT_GetUIState(UI_NPCMENU);
        DWORD npcShopState = D2CLIENT_GetUIState(UI_NPCSHOP);
        UnitAny* pNPC = D2CLIENT_GetCurrentInteractingNPC();
        
        // Check if NPC menu is open (but shop is NOT yet)
        if (npcMenuState != 0 && npcShopState == 0 && pNPC != NULL) {
            // Menu is open, try to open gamble screen
            __try {
                D2CLIENT_StartGamble();
                // If StartGamble succeeds, we're opening a gambling screen
                inGamblingScreen = true;
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                // If StartGamble crashes, user can click Gamble manually
                inGamblingScreen = false;
            }
            pendingGambleTime = 0;
            pendingRefresh = false;
            return;
        } else if (now - pendingGambleTime > 1000) {
            // Timeout after 1 second - check if shop opened (which means StartGamble worked)
            if (D2CLIENT_GetUIState(UI_NPCSHOP) != 0 && D2CLIENT_GetUIState(UI_TRADE) == 0) {
                inGamblingScreen = true;
            } else {
                inGamblingScreen = false;
            }
            pendingGambleTime = 0;
            pendingRefresh = false;
            return;
        }
        // Keep waiting for menu to open
        return;
    }
    
    // Check if we're in a gambling screen (shop open, trade not open)
    // This detects manually opened gambling screens too
    DWORD checkMenuState = D2CLIENT_GetUIState(UI_NPCMENU);
    DWORD checkShopState = D2CLIENT_GetUIState(UI_NPCSHOP);
    DWORD checkTradeState = D2CLIENT_GetUIState(UI_TRADE);
    if (checkMenuState != 0 && checkShopState != 0 && checkTradeState == 0) {
        // We're in a shop that's not a trade screen - assume it's gambling
        inGamblingScreen = true;
    } else if (checkShopState == 0 || checkTradeState != 0) {
        // Shop closed or trade open - not gambling
        inGamblingScreen = false;
    }
    
    if (!pendingRefresh)
        return;

    if (now - lastRefreshTime < REFRESH_COOLDOWN)
        return;

    // Must still be in NPC gamble and have the flag set
    DWORD npcMenuState = D2CLIENT_GetUIState(UI_NPCMENU);
    DWORD npcShopState = D2CLIENT_GetUIState(UI_NPCSHOP);
    DWORD tradeState = D2CLIENT_GetUIState(UI_TRADE);
    
    // If trade screen is active, we're not gambling
    if (tradeState != 0) {
        inGamblingScreen = false;
        pendingRefresh = false;
        return;
    }
    
    if (!npcMenuState || !npcShopState || !inGamblingScreen) {
        pendingRefresh = false;
        return;
    }

    // Get NPC info BEFORE closing UI (since closing destroys NPC context)
    UnitAny* pNPC = D2CLIENT_GetCurrentInteractingNPC();
    if (!pNPC) {
        pendingRefresh = false;
        return;
    }
    
    DWORD npcUnitId = pNPC->dwUnitId;
    DWORD npcUnitType = pNPC->dwType;
    
    // Close the shop UI
    D2CLIENT_SetUIVar(UI_NPCSHOP, 1, 0);
    
    // Re-interact with NPC to reopen the NPC menu
    Interact(npcUnitId, npcUnitType);
    
    // Set flag to wait for menu to open, then call StartGamble
    pendingGambleTime = now;
    
    lastRefreshTime = now;
}

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------
bool Gambling::IsGambling() {
    // Check if NPC menu and shop are open
    DWORD npcMenuState = D2CLIENT_GetUIState(UI_NPCMENU);
    DWORD npcShopState = D2CLIENT_GetUIState(UI_NPCSHOP);
    DWORD tradeState = D2CLIENT_GetUIState(UI_TRADE);
    
    // If trade screen is active, we're not gambling
    if (tradeState != 0)
        return false;
    
    // Check if NPC menu and shop are open
    if (npcMenuState == 0 || npcShopState == 0) {
        inGamblingScreen = false;
        return false;
    }
    
    // Only allow if we have the flag set (meaning we successfully opened a gambling screen)
    return inGamblingScreen;
}
