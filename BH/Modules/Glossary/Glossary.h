#pragma once
#include "../Module.h"
#include "../../D2Ptrs.h"
#include "../../CommonStructs.h"
#include "../../Constants.h"
#include "../../Drawing.h"
#include "../../JSONObject.h"
#include <string>
#include <vector>
#include <windows.h>

class Glossary : public Module {
private:
    // Menu option Y position (centered horizontally, positioned closer to other options)
    static const int MENU_Y = 100;
    static const int MENU_HEIGHT = 20;  // Height for click detection (font 3 is 24px, add 6px margin)
    
    // Thread safety for data access
    CRITICAL_SECTION dataCrit;
    
    // UI window
    Drawing::UI* glossaryUI;
    Drawing::UITab* corruptionsTab;
    Drawing::UITab* aurasTab;
    Drawing::UITab* gainEffectsTab;
    Drawing::UITab* charmsTab;
    Drawing::Texthook* closeButton;  // Close button [X] in upper right corner
    
    // Corruptions data
    JSONArray* corruptionsData;
    bool corruptionsDataLoaded;
    bool corruptionsDisplayed;  // Track if hooks have been created
    std::vector<Drawing::Texthook*> corruptionsTextHooks;  // Store text hooks to prevent leaks
    
    // Auras data
    JSONArray* aurasData;
    bool aurasDataLoaded;
    bool aurasDisplayed;  // Track if hooks have been created
    std::vector<Drawing::Texthook*> aurasTextHooks;  // Store text hooks to prevent leaks
    
    // Effects data
    JSONArray* effectsData;
    bool effectsDataLoaded;
    bool effectsDisplayed;  // Track if hooks have been created
    std::vector<Drawing::Texthook*> effectsTextHooks;  // Store text hooks to prevent leaks
    
    // Charms display
    bool charmsDisplayed;  // Track if hooks have been created
    std::vector<Drawing::Texthook*> charmsTextHooks;  // Store text hooks to prevent leaks
    
    // Hotkey for opening/closing glossary
    unsigned int glossaryKey;
    
    static bool IsEscMenuOpen();
    static bool IsMouseInRect(int x, int y, int w, int h);
    static int GetTextWidth(const wchar_t* text, int font);
    static void DrawGlossaryOption();
    void InitializeUI();
    void CenterGlossaryWindow();
    void OnGlossaryClick();
    
    // JSON download and parsing
    std::string DownloadJSON(const std::string& url);
    JSONArray* ParseJSON(const std::string& jsonString);
    JSONObject* ParseJSONObject(const std::string& objStr);
    JSONArray* ParseJSONArray(const std::string& arrStr);
    void LoadCorruptionsData();
    void DisplayCorruptions();
    void LoadAurasData();
    void DisplayAuras();
    void LoadEffectsData();
    void DisplayEffects();
    void DisplayCharms();
    
    // Helper functions for text wrapping
    std::vector<std::string> WrapText(const std::string& text, unsigned int maxWidth, unsigned int font);
    int CalculateSlotHeight(JSONObject* item, unsigned int columnWidth, unsigned int font, unsigned int lineHeight);
    int CalculateAuraHeight(JSONObject* item, unsigned int columnWidth, unsigned int font, unsigned int lineHeight);
    int CalculateEffectsHeight(JSONObject* item, unsigned int columnWidth, unsigned int font, unsigned int lineHeight);

public:
    Glossary() : Module("Glossary"), glossaryUI(nullptr), 
                 corruptionsTab(nullptr), aurasTab(nullptr), 
                 gainEffectsTab(nullptr), charmsTab(nullptr),
                 corruptionsData(nullptr), corruptionsDataLoaded(false),
                 corruptionsDisplayed(false), closeButton(nullptr),
                 aurasData(nullptr), aurasDataLoaded(false),
                 aurasDisplayed(false), effectsData(nullptr),
                 effectsDataLoaded(false), effectsDisplayed(false),
                 charmsDisplayed(false), glossaryKey(0) {
        InitializeCriticalSection(&dataCrit);
    };
    
    ~Glossary() {
        DeleteCriticalSection(&dataCrit);
    }

    void OnLoad();
    void OnUnload();
    void LoadConfig();
    void OnDraw();
    void OnKey(bool up, BYTE key, LPARAM lParam, bool* block);
    void OnLeftClick(bool up, unsigned int x, unsigned int y, bool* block);
    void OnRightClick(bool up, unsigned int x, unsigned int y, bool* block);
    unsigned int* GetGlossaryKeyPtr() { return &glossaryKey; }
};

