#include "Glossary.h"
#include "../../BH.h"
#include "../../D2Intercepts.h"
#include "../../D2Handlers.h"
#include "../../D2Stubs.h"
#include "../../Common.h"
#include "../../Patch.h"
#include "../../Drawing/UI/UI.h"
#include "../../Drawing/UI/UITab.h"
#include "../../Drawing/Hook.h"
#include "../../Drawing/Basic/Texthook/Texthook.h"
#include "../../Drawing/Basic/Linehook/Linehook.h"
#include "../../JSONObject.h"
#include "../../Task.h"
#include <cstring>
#include <windows.h>
#include <winhttp.h>
#include <sstream>
#include <vector>
#pragma comment(lib, "winhttp.lib")

using namespace std;
using namespace Drawing;

// Check if escape menu is open
bool Glossary::IsEscMenuOpen() {
    return D2CLIENT_GetUIState(UI_ESCMENU_MAIN) != 0;
}

// Check if mouse cursor is within the specified rectangle
bool Glossary::IsMouseInRect(int x, int y, int w, int h) {
    int mouseX = *p_D2CLIENT_MouseX;
    int mouseY = *p_D2CLIENT_MouseY;
    return mouseX >= x && mouseX <= x + w &&
           mouseY >= y && mouseY <= y + h;
}

// Get text width for click detection
int Glossary::GetTextWidth(const wchar_t* text, int font) {
    DWORD width, fileNo;
    DWORD oldFont = D2WIN_SetTextSize(font);
    D2WIN_GetTextWidthFileNo(const_cast<wchar_t*>(text), &width, &fileNo);
    D2WIN_SetTextSize(oldFont);
    return (int)width;
}

// Draw the Glossary menu option
void Glossary::DrawGlossaryOption() {
    if (!IsEscMenuOpen()) {
        return;
    }

    // Use font 3 (24px height) for larger text, matching escape menu options better
    // Font heights: {10,11,18,24,10,13,7,13,10,12,8,8,7,12}
    const int MENU_FONT = 3;
    const int fontHeight = 24;  // Font 3 height
    
    // Center horizontally on screen
    int screenCenterX = *p_D2CLIENT_ScreenSizeX / 2;
    
    // Set font size before drawing
    DWORD oldFont = D2WIN_SetTextSize(MENU_FONT);
    
    // Get text width for centering and click detection
    int textWidth = GetTextWidth(L"GLOSSARY", MENU_FONT);
    
    // Calculate centered text X position
    int textX = screenCenterX - (textWidth / 2);
    
    // Calculate clickable rectangle (centered around text)
    // Text is drawn at MENU_Y + fontHeight, so check hover at that Y position
    int textY = MENU_Y + fontHeight;
    int clickX = textX;
    int clickW = textWidth;
    int clickY = textY - fontHeight;  // Start from top of text area
    int clickH = fontHeight;  // Height of text
    
    // Check if mouse is hovering over the option (use actual text drawing position)
    bool isHovered = IsMouseInRect(clickX, clickY, clickW, clickH);
    
    // Use Gold color to match other escape menu options, White when hovered
    DWORD color = isHovered ? White : Gold;
    
    // Use DrawText (no background) and center manually
    // DrawText parameters: (text, xPos, yPos, color, dwUnk)
    // yPos should account for font height
    D2WIN_DrawText(L"GLOSSARY", textX, textY, color, 0);
    
    // Restore original font size
    D2WIN_SetTextSize(oldFont);
}

// Initialize UI window lazily (only when first needed)
void Glossary::InitializeUI() {
    if (glossaryUI) {
        return;  // Already initialized
    }
    
    // Calculate window size - 90% of screen width, 550px height
    int screenWidth = Hook::GetScreenWidth();
    int windowWidth = (int)(screenWidth * 0.9f);
    
    // Create Glossary UI window - 90% screen width x 550px
    glossaryUI = new UI("Glossary", windowWidth, 550);
    
    // Always center the window on screen
    CenterGlossaryWindow();
    
    // Create tabs (first tab will automatically become currentTab)
    corruptionsTab = new UITab("Corruptions", glossaryUI);
    aurasTab = new UITab("Auras", glossaryUI);
    gainEffectsTab = new UITab("Effects", glossaryUI);
    charmsTab = new UITab("Charms", glossaryUI);
    
    // Create close button [X] in upper right corner
    // Position will be updated in OnDraw when window is centered
    // Use Perm visibility so it's always visible when active
    closeButton = new Texthook(Perm, 0, 0, "[X]");
    if (closeButton) {
        closeButton->SetColor(White);
        closeButton->SetHoverColor(Silver);
        closeButton->SetFont(0);
    }
    
    // Initially hide the window (set minimized and invisible)
    glossaryUI->Lock();
    glossaryUI->SetMinimized(true);  // Set to minimized so it's hidden
    glossaryUI->SetVisible(false);
    glossaryUI->SetActive(false);
    glossaryUI->Unlock();
}

// Center the Glossary window on screen
void Glossary::CenterGlossaryWindow() {
    if (!glossaryUI) {
        return;
    }
    
    int screenWidth = Hook::GetScreenWidth();
    int screenHeight = Hook::GetScreenHeight();
    int windowWidth = glossaryUI->GetXSize();
    int windowHeight = glossaryUI->GetYSize();
    
    if (windowWidth > 0 && windowHeight > 0) {
        glossaryUI->Lock();
        glossaryUI->SetX((screenWidth - windowWidth) / 2);
        glossaryUI->SetY((screenHeight - windowHeight) / 2);
        glossaryUI->Unlock();
    }
}

// Handle click on Glossary option
void Glossary::OnGlossaryClick() {
    // Only allow opening if we're in a game
    if (!D2CLIENT_GetPlayerUnit()) {
        return;
    }
    
    // Close the escape menu before opening the glossary
    if (D2CLIENT_GetUIState(UI_ESCMENU_MAIN) != 0) {
        D2CLIENT_SetUIVar(UI_ESCMENU_MAIN, 1, 0);
    }
    
    // Initialize UI if not already created (lazy initialization)
    InitializeUI();
    
    if (!glossaryUI) {
        return;
    }
    
    // Open the glossary window (similar to how settings UI works)
    glossaryUI->Lock();
    // First ensure it's not minimized (this will also set visible=true and currentTab if needed)
    if (glossaryUI->IsMinimized()) {
        glossaryUI->SetMinimized(false);
    }
    // Explicitly ensure it's visible and active
    glossaryUI->SetVisible(true);
    glossaryUI->SetActive(true);
    // Ensure currentTab is set
    if (!glossaryUI->GetActiveTab() && !glossaryUI->Tabs.empty()) {
        glossaryUI->SetCurrentTab(*(glossaryUI->Tabs.begin()));
    }
    // Center the window
    CenterGlossaryWindow();
    UI::Sort(glossaryUI);  // Bring to front
    glossaryUI->Unlock();
    
    // Display data for active tab if it's loaded
    EnterCriticalSection(&dataCrit);
    bool corruptionsReady = corruptionsDataLoaded && corruptionsData != nullptr;
    bool aurasReady = aurasDataLoaded && aurasData != nullptr;
    bool effectsReady = effectsDataLoaded && effectsData != nullptr;
    LeaveCriticalSection(&dataCrit);
    
    if (corruptionsReady && corruptionsTab && corruptionsTab->IsActive()) {
        DisplayCorruptions();
    }
    
    if (aurasReady && aurasTab && aurasTab->IsActive()) {
        DisplayAuras();
    }
    
    if (effectsReady && gainEffectsTab && gainEffectsTab->IsActive()) {
        DisplayEffects();
    }
}

void Glossary::OnLoad() {
    // Don't create UI here - defer until first use to avoid startup delay
    // UI will be created lazily when Glossary option is clicked
    
    // Load corruptions, auras, and effects data asynchronously
    LoadCorruptionsData();
    LoadAurasData();
    LoadEffectsData();
}

void Glossary::OnUnload() {
    // Clean up corruptions text hooks
    for (auto hook : corruptionsTextHooks) {
        if (hook) delete hook;
    }
    corruptionsTextHooks.clear();
    
    // Clean up auras text hooks
    for (auto hook : aurasTextHooks) {
        if (hook) delete hook;
    }
    aurasTextHooks.clear();
    
    // Clean up effects text hooks
    for (auto hook : effectsTextHooks) {
        if (hook) delete hook;
    }
    effectsTextHooks.clear();
    
    // Clean up charms text hooks
    for (auto hook : charmsTextHooks) {
        if (hook) delete hook;
    }
    charmsTextHooks.clear();
    
    // Clean up close button
    if (closeButton) {
        delete closeButton;
        closeButton = nullptr;
    }
    
    // Clean up JSON data (with lock)
    EnterCriticalSection(&dataCrit);
    if (corruptionsData) {
        delete corruptionsData;
        corruptionsData = nullptr;
    }
    
    if (aurasData) {
        delete aurasData;
        aurasData = nullptr;
    }
    
    if (effectsData) {
        delete effectsData;
        effectsData = nullptr;
    }
    LeaveCriticalSection(&dataCrit);
    
    // Clean up UI (tabs will be cleaned up automatically when UI is destroyed)
    if (glossaryUI) {
        delete glossaryUI;
        glossaryUI = nullptr;
        corruptionsTab = nullptr;
        aurasTab = nullptr;
        gainEffectsTab = nullptr;
        charmsTab = nullptr;
    }
}

void Glossary::OnDraw() {
    // Only draw if we're in a game (prevents crashes)
    if (!D2CLIENT_GetPlayerUnit()) {
        // Hide the Glossary window if not in game
        if (glossaryUI && glossaryUI->IsVisible()) {
            glossaryUI->Lock();
            glossaryUI->SetVisible(false);
            glossaryUI->SetActive(false);
            glossaryUI->Unlock();
        }
        return;
    }
    
    // Draw the Glossary option when escape menu is open
    DrawGlossaryOption();
    
    // Keep the Glossary window centered and prevent dragging if it's visible
    if (glossaryUI && glossaryUI->IsVisible() && !glossaryUI->IsMinimized()) {
        // Keep the window active - prevents clicking outside from making it transparent
        // Do this BEFORE locking to avoid recursive lock issues (SetActive locks internally)
        if (!glossaryUI->IsActive()) {
            glossaryUI->SetActive(true);
        }
        
        glossaryUI->Lock();
        
        // Always prevent dragging - clear dragged state immediately
        if (glossaryUI->IsDragged()) {
            glossaryUI->SetDragged(false, false);
        }
        
        // Check if window position has changed (was dragged) and re-center
        int screenWidth = Hook::GetScreenWidth();
        int screenHeight = Hook::GetScreenHeight();
        int windowWidth = glossaryUI->GetXSize();
        int windowHeight = glossaryUI->GetYSize();
        int expectedX = (screenWidth - windowWidth) / 2;
        int expectedY = (screenHeight - windowHeight) / 2;
        
        // If position doesn't match centered position, re-center it
        if (glossaryUI->GetX() != expectedX || glossaryUI->GetY() != expectedY) {
            glossaryUI->SetX(expectedX);
            glossaryUI->SetY(expectedY);
        }
        
        glossaryUI->Unlock();
        
        // Update close button position (upper right corner of window)
        if (closeButton) {
            int windowX = glossaryUI->GetX();
            int windowY = glossaryUI->GetY();
            int windowWidth = glossaryUI->GetXSize();
            POINT closeSize = Texthook::GetTextSize("[X]", 0);
            int closeX = windowX + windowWidth - closeSize.x - 4;  // 4px padding from right edge
            int closeY = windowY + 3;  // Align with title text (same as title bar text)
            
            closeButton->Lock();
            closeButton->SetBaseX(closeX);
            closeButton->SetBaseY(closeY);
            closeButton->SetActive(true);
            closeButton->Unlock();
        }
        
        // Display corruptions data if tab is active (check with lock)
        EnterCriticalSection(&dataCrit);
        bool corruptionsReady = corruptionsDataLoaded && corruptionsData != nullptr;
        bool aurasReady = aurasDataLoaded && aurasData != nullptr;
        bool effectsReady = effectsDataLoaded && effectsData != nullptr;
        LeaveCriticalSection(&dataCrit);
        
        if (corruptionsReady && corruptionsTab && corruptionsTab->IsActive()) {
            DisplayCorruptions();
        }
        
        // Display auras data if tab is active
        if (aurasReady && aurasTab && aurasTab->IsActive()) {
            DisplayAuras();
        }
        
        // Display effects data if tab is active
        if (effectsReady && gainEffectsTab && gainEffectsTab->IsActive()) {
            DisplayEffects();
        }
        
        // Display charms information if tab is active
        if (charmsTab && charmsTab->IsActive()) {
            DisplayCharms();
        }
    } else if (closeButton) {
        // Hide close button when window is not visible
        closeButton->Lock();
        closeButton->SetActive(false);
        closeButton->Unlock();
    }
    
    // Hide the minimized bar - when minimized, make it invisible so it doesn't draw
    // (This is handled in OnRightClick, but keep this as a safety check)
    if (glossaryUI && glossaryUI->IsMinimized() && glossaryUI->IsVisible()) {
        glossaryUI->Lock();
        glossaryUI->SetVisible(false);  // Hide when minimized to prevent drawing minimized bar
        glossaryUI->Unlock();
    }
}

void Glossary::OnLeftClick(bool up, unsigned int x, unsigned int y, bool* block) {
    // Handle close button click
    if (closeButton && glossaryUI && glossaryUI->IsVisible() && !glossaryUI->IsMinimized() && up) {
        // Check if click is within close button bounds
        unsigned int closeX = closeButton->GetX();
        unsigned int closeY = closeButton->GetY();
        POINT closeSize = Texthook::GetTextSize("[X]", 0);
        if (x >= closeX && x <= closeX + (unsigned int)closeSize.x && 
            y >= closeY && y <= closeY + (unsigned int)closeSize.y) {
            // Close button was clicked - minimize and hide the window
            glossaryUI->Lock();
            glossaryUI->SetMinimized(true);
            glossaryUI->SetVisible(false);
            glossaryUI->SetActive(false);
            glossaryUI->Unlock();
            *block = true;
            return;
        }
    }
    
    // Handle Glossary window - prevent dragging by clearing dragged state
    // Note: UI system processes clicks first, so we can't prevent the drag from starting
    // but we can clear it immediately and re-center
    if (glossaryUI && glossaryUI->IsVisible() && !glossaryUI->IsMinimized()) {
        // Clear any dragging state and re-center
        if (glossaryUI->IsDragged()) {
            glossaryUI->Lock();
            glossaryUI->SetDragged(false, false);
            CenterGlossaryWindow();
            glossaryUI->Unlock();
        }
    }
    
    // Handle escape menu Glossary option click
    if (!up || !IsEscMenuOpen()) {
        return;
    }

    // Calculate centered text position and width for click detection
    const int MENU_FONT = 3;
    int screenCenterX = *p_D2CLIENT_ScreenSizeX / 2;
    int textWidth = GetTextWidth(L"GLOSSARY", MENU_FONT);
    int clickX = screenCenterX - (textWidth / 2);
    int clickW = textWidth;

    // Check if the click is within the Glossary option rectangle
    if (IsMouseInRect(clickX, MENU_Y, clickW, MENU_HEIGHT)) {
        OnGlossaryClick();
        *block = true;  // Block the click from being processed by the game
    }
}

void Glossary::OnRightClick(bool up, unsigned int x, unsigned int y, bool* block) {
    // Handle right-click anywhere in the Glossary window to minimize it
    // Note: UI system processes clicks first and minimizes on title bar, but we want it to work anywhere
    if (glossaryUI && up && !glossaryUI->IsMinimized()) {
        // Check if right-click was anywhere within the window bounds
        if (glossaryUI->InWindow(x, y)) {
            // Minimize the window and hide it immediately to prevent drawing minimized bar
            glossaryUI->Lock();
            glossaryUI->SetMinimized(true);
            glossaryUI->SetVisible(false);  // Hide to prevent drawing minimized bar
            glossaryUI->Unlock();
            *block = true;
            return;
        }
    }
    // Also handle if it was already minimized by UI system (title bar click)
    if (glossaryUI && up && glossaryUI->IsMinimized()) {
        // Hide it to prevent drawing minimized bar
        glossaryUI->Lock();
        glossaryUI->SetVisible(false);
        glossaryUI->Unlock();
        *block = true;
        return;
    }
}

// Download JSON from URL using WinHTTP
std::string Glossary::DownloadJSON(const std::string& url) {
    HINTERNET hSession = NULL;
    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;
    std::string result;
    
    try {
        // Parse URL
        std::string urlStr = url;
        size_t protocolEnd = urlStr.find("://");
        if (protocolEnd == string::npos) {
            return "";
        }
        
        bool isHttps = (urlStr.substr(0, protocolEnd) == "https");
        size_t hostStart = protocolEnd + 3;
        size_t pathStart = urlStr.find('/', hostStart);
        if (pathStart == string::npos) {
            pathStart = urlStr.length();
        }
        
        std::string host = urlStr.substr(hostStart, pathStart - hostStart);
        std::string path = (pathStart < urlStr.length()) ? urlStr.substr(pathStart) : "/";
        
        if (host.empty()) {
            return "";
        }
        
        // Initialize WinHTTP session
        hSession = WinHttpOpen(L"BH Glossary/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) {
            return "";
        }
        
        // Set timeouts
        DWORD timeout = 10000;
        WinHttpSetTimeouts(hSession, timeout, timeout, timeout, timeout);
        
        // Convert host to wide string
        wchar_t* wHost = AnsiToUnicode(host.c_str());
        if (!wHost) {
            WinHttpCloseHandle(hSession);
            return "";
        }
        
        // Connect to server
        INTERNET_PORT port = isHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
        hConnect = WinHttpConnect(hSession, wHost, port, 0);
        delete[] wHost;
        
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            return "";
        }
        
        // Convert path to wide string
        wchar_t* wPath = AnsiToUnicode(path.c_str());
        if (!wPath) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "";
        }
        
        // Open request
        DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
        hRequest = WinHttpOpenRequest(hConnect, L"GET", wPath, NULL,
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        delete[] wPath;
        
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "";
        }
        
        // For HTTPS, configure security flags
        if (isHttps) {
            DWORD dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                           SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                           SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                           SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
            WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwFlags, sizeof(dwFlags));
        }
        
        // Send request
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "";
        }
        
        // Receive response
        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "";
        }
        
        // Check status code
        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                NULL, &statusCode, &statusCodeSize, NULL)) {
            if (statusCode != 200) {
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                return "";
            }
        }
        
        // Read data
        DWORD bytesAvailable = 0;
        DWORD bytesRead = 0;
        char buffer[4096];
        const DWORD maxSize = 1024 * 1024; // 1MB max
        
        while (result.length() < maxSize && WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
            ZeroMemory(buffer, sizeof(buffer));
            DWORD toRead = min(sizeof(buffer) - 1, bytesAvailable);
            if (WinHttpReadData(hRequest, buffer, toRead, &bytesRead) && bytesRead > 0) {
                result.append(buffer, bytesRead);
            } else {
                break;
            }
        }
        
        // Cleanup
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        
    } catch (...) {
        if (hRequest) WinHttpCloseHandle(hRequest);
        if (hConnect) WinHttpCloseHandle(hConnect);
        if (hSession) WinHttpCloseHandle(hSession);
        return "";
    }
    
    return result;
}

// Simple JSON parser - parse array of objects
JSONArray* Glossary::ParseJSON(const std::string& jsonString) {
    if (jsonString.empty()) {
        return new JSONArray();
    }
    
    JSONArray* array = new JSONArray();
    if (!array) {
        return nullptr;
    }
    
    // Find array start
    size_t start = jsonString.find('[');
    if (start == string::npos) {
        return array;
    }
    
    // Simple state machine to parse objects
    size_t pos = start + 1;
    bool inString = false;
    bool escape = false;
    int braceDepth = 0;
    size_t objStart = 0;
    
    while (pos < jsonString.length()) {
        char c = jsonString[pos];
        
        if (escape) {
            escape = false;
            pos++;
            continue;
        }
        
        if (c == '\\') {
            escape = true;
            pos++;
            continue;
        }
        
        if (c == '"') {
            inString = !inString;
            pos++;
            continue;
        }
        
        if (inString) {
            pos++;
            continue;
        }
        
        if (c == '{') {
            if (braceDepth == 0) {
                objStart = pos;
            }
            braceDepth++;
        } else if (c == '}') {
            braceDepth--;
            if (braceDepth == 0) {
                // Extract object string and parse it
                std::string objStr = jsonString.substr(objStart, pos - objStart + 1);
                JSONObject* obj = ParseJSONObject(objStr);
                if (obj) {
                    array->add(obj);
                }
            }
        } else if (c == ']' && braceDepth == 0) {
            break;
        }
        
        pos++;
    }
    
    return array;
}

// Parse a single JSON object
JSONObject* Glossary::ParseJSONObject(const std::string& objStr) {
    if (objStr.empty() || objStr.length() < 2) {
        return new JSONObject();
    }
    
    JSONObject* obj = new JSONObject();
    if (!obj) {
        return nullptr;
    }
    
    // Simple key-value extraction
    size_t pos = 1; // Skip opening brace
    while (pos < objStr.length() - 1) {
        // Find key
        size_t keyStart = objStr.find('"', pos);
        if (keyStart == string::npos || keyStart >= objStr.length() - 1) break;
        size_t keyEnd = objStr.find('"', keyStart + 1);
        if (keyEnd == string::npos) break;
        
        std::string key = objStr.substr(keyStart + 1, keyEnd - keyStart - 1);
        
        // Find colon
        size_t colonPos = objStr.find(':', keyEnd);
        if (colonPos == string::npos) break;
        
        // Find value
        size_t valStart = objStr.find_first_not_of(" \t\r\n", colonPos + 1);
        if (valStart == string::npos) break;
        
        if (objStr[valStart] == '"') {
            // String value
            size_t valEnd = objStr.find('"', valStart + 1);
            if (valEnd == string::npos) break;
            std::string value = objStr.substr(valStart + 1, valEnd - valStart - 1);
            obj->set(key, value);
            pos = valEnd + 1;
        } else if (objStr[valStart] == '[') {
            // Array value - find matching bracket
            int depth = 1;
            size_t valEnd = valStart + 1;
            while (valEnd < objStr.length() && depth > 0) {
                if (objStr[valEnd] == '[') depth++;
                else if (objStr[valEnd] == ']') depth--;
                valEnd++;
            }
            if (depth == 0) {
                std::string arrStr = objStr.substr(valStart, valEnd - valStart);
                JSONArray* arr = ParseJSONArray(arrStr);
                if (arr) {
                    obj->set(key, arr);
                }
                pos = valEnd;
            } else {
                break;
            }
        } else {
            // Number or other - find comma or closing brace
            size_t valEnd = objStr.find_first_of(",}", valStart);
            if (valEnd == string::npos) break;
            std::string value = objStr.substr(valStart, valEnd - valStart);
            // Trim whitespace
            while (!value.empty() && (value[0] == ' ' || value[0] == '\t')) {
                value.erase(0, 1);
            }
            while (!value.empty() && (value[value.length()-1] == ' ' || value[value.length()-1] == '\t')) {
                value.erase(value.length()-1, 1);
            }
            
            if (!value.empty()) {
                obj->set(key, value);
            }
            pos = valEnd;
        }
        
        // Skip comma
        if (pos < objStr.length() && objStr[pos] == ',') {
            pos++;
        }
    }
    
    return obj;
}

// Parse JSON array (for props array - handles nested arrays)
JSONArray* Glossary::ParseJSONArray(const std::string& arrStr) {
    if (arrStr.empty() || arrStr.length() < 2) {
        return new JSONArray();
    }
    
    JSONArray* arr = new JSONArray();
    if (!arr) {
        return nullptr;
    }
    
    size_t pos = 1; // Skip opening bracket
    bool inString = false;
    bool escape = false;
    
    while (pos < arrStr.length() - 1) {
        // Skip whitespace
        while (pos < arrStr.length() && (arrStr[pos] == ' ' || arrStr[pos] == '\t' || arrStr[pos] == '\r' || arrStr[pos] == '\n')) {
            pos++;
        }
        if (pos >= arrStr.length() - 1) break;
        
        char c = arrStr[pos];
        
        if (escape) {
            escape = false;
            pos++;
            continue;
        }
        
        if (c == '\\') {
            escape = true;
            pos++;
            continue;
        }
        
        if (c == '"') {
            if (!inString) {
                // Start of string
                inString = true;
                size_t strStart = pos + 1;
                size_t strEnd = strStart;
                while (strEnd < arrStr.length()) {
                    if (arrStr[strEnd] == '\\') {
                        strEnd += 2; // Skip escaped character
                        continue;
                    }
                    if (arrStr[strEnd] == '"') {
                        break;
                    }
                    strEnd++;
                }
                if (strEnd < arrStr.length()) {
                    std::string value = arrStr.substr(strStart, strEnd - strStart);
                    arr->add(value);
                    pos = strEnd + 1;
                    inString = false;
                } else {
                    break;
                }
            } else {
                inString = false;
                pos++;
            }
        } else if (c == '[' && !inString) {
            // Nested array - find matching bracket
            int depth = 1;
            size_t nestedStart = pos;
            size_t nestedEnd = pos + 1;
            while (nestedEnd < arrStr.length() && depth > 0) {
                if (arrStr[nestedEnd] == '[') depth++;
                else if (arrStr[nestedEnd] == ']') depth--;
                nestedEnd++;
            }
            if (depth == 0) {
                std::string nestedStr = arrStr.substr(nestedStart, nestedEnd - nestedStart);
                JSONArray* nested = ParseJSONArray(nestedStr);
                if (nested) {
                    arr->add(nested);
                }
                pos = nestedEnd;
            } else {
                break;
            }
        } else {
            pos++;
        }
        
        // Skip comma and whitespace
        while (pos < arrStr.length() && (arrStr[pos] == ',' || arrStr[pos] == ' ' || arrStr[pos] == '\t' || arrStr[pos] == '\r' || arrStr[pos] == '\n')) {
            pos++;
        }
    }
    
    return arr;
}

// Load corruptions data from URL (called once during initialization)
void Glossary::LoadCorruptionsData() {
    EnterCriticalSection(&dataCrit);
    bool alreadyLoaded = corruptionsDataLoaded;
    LeaveCriticalSection(&dataCrit);
    
    if (alreadyLoaded) {
        return; // Already loaded
    }
    
    // Download JSON asynchronously using Task system
    Task::Enqueue([this]() -> void {
        std::string jsonData = DownloadJSON("https://rustyshackleford1888.github.io/js/Corruptions.json");
        if (!jsonData.empty() && jsonData.length() > 10) {
            // Parse JSON
            JSONArray* parsed = ParseJSON(jsonData);
            if (parsed && parsed->length() > 0) {
                EnterCriticalSection(&dataCrit);
                corruptionsData = parsed;
                corruptionsDataLoaded = true;
                LeaveCriticalSection(&dataCrit);
            } else {
                if (parsed) delete parsed;
                EnterCriticalSection(&dataCrit);
                corruptionsDataLoaded = true;
                LeaveCriticalSection(&dataCrit);
            }
        } else {
            EnterCriticalSection(&dataCrit);
            corruptionsDataLoaded = true;
            LeaveCriticalSection(&dataCrit);
        }
    });
}

// Load auras data from URL (called once during initialization)
void Glossary::LoadAurasData() {
    EnterCriticalSection(&dataCrit);
    bool alreadyLoaded = aurasDataLoaded;
    LeaveCriticalSection(&dataCrit);
    
    if (alreadyLoaded) {
        return; // Already loaded
    }
    
    // Download JSON asynchronously using Task system
    Task::Enqueue([this]() -> void {
        std::string jsonData = DownloadJSON("https://rustyshackleford1888.github.io/js/auras.json");
        if (!jsonData.empty() && jsonData.length() > 10) {
            // Parse JSON
            JSONArray* parsed = ParseJSON(jsonData);
            if (parsed && parsed->length() > 0) {
                EnterCriticalSection(&dataCrit);
                aurasData = parsed;
                aurasDataLoaded = true;
                LeaveCriticalSection(&dataCrit);
            } else {
                if (parsed) delete parsed;
                EnterCriticalSection(&dataCrit);
                aurasDataLoaded = true;
                LeaveCriticalSection(&dataCrit);
            }
        } else {
            EnterCriticalSection(&dataCrit);
            aurasDataLoaded = true;
            LeaveCriticalSection(&dataCrit);
        }
    });
}

// Helper function to wrap text to fit within a column width
// Returns wrapped lines with proper indentation for continuation lines
std::vector<std::string> Glossary::WrapText(const std::string& text, unsigned int maxWidth, unsigned int font) {
    std::vector<std::string> lines;
    if (text.empty()) {
        return lines;
    }
    
    // Extract leading spaces/indentation from original text
    size_t leadingSpaces = 0;
    while (leadingSpaces < text.length() && text[leadingSpaces] == ' ') {
        leadingSpaces++;
    }
    std::string indent = text.substr(0, leadingSpaces);
    std::string content = text.substr(leadingSpaces);
    
    // Get text width using Texthook::GetTextSize
    POINT textSize = Texthook::GetTextSize(text, font);
    if (textSize.x <= (int)maxWidth) {
        // Text fits on one line
        lines.push_back(text);
        return lines;
    }
    
    // Calculate indentation for continuation lines (preserve original indent + add 2 more spaces)
    std::string continuationIndent = indent + "  ";
    POINT indentSize = Texthook::GetTextSize(continuationIndent, font);
    unsigned int availableWidth = maxWidth;
    if (indentSize.x < (int)maxWidth) {
        availableWidth = maxWidth - indentSize.x;
    } else {
        // If indent is too large, just use 4 spaces
        continuationIndent = "    ";
        POINT simpleIndentSize = Texthook::GetTextSize(continuationIndent, font);
        if (simpleIndentSize.x < (int)maxWidth) {
            availableWidth = maxWidth - simpleIndentSize.x;
        }
    }
    
    // Text needs wrapping - split by words, preserving spaces
    std::istringstream iss(content);
    std::string word;
    std::string currentLine = indent; // Start with original indent
    bool isFirstLine = true;
    
    while (iss >> word) {
        // Check if adding this word would exceed the line width
        std::string testLine = (currentLine.empty() || currentLine == indent || currentLine == continuationIndent)
            ? currentLine + word 
            : currentLine + " " + word;
        POINT testSize = Texthook::GetTextSize(testLine, font);
        
        if (testSize.x <= (int)maxWidth) {
            // Word fits on current line
            currentLine = testLine;
        } else {
            // Word doesn't fit - start new line with continuation indent
            if (!currentLine.empty() && currentLine != indent && currentLine != continuationIndent) {
                lines.push_back(currentLine);
                isFirstLine = false;
            }
            
            // Handle very long words that exceed available width
            POINT wordSize = Texthook::GetTextSize(word, font);
            if (wordSize.x > (int)availableWidth && availableWidth > 20) {
                // Word is too long - try to break it intelligently at hyphens or underscores
                size_t breakPos = word.find_last_of("-_");
                if (breakPos != std::string::npos && breakPos > 0 && breakPos < word.length() - 1) {
                    // Break at hyphen/underscore
                    std::string firstPart = word.substr(0, breakPos + 1);
                    std::string secondPart = word.substr(breakPos + 1);
                    currentLine = continuationIndent + firstPart;
                    if (!secondPart.empty()) {
                        word = secondPart; // Process second part in next iteration
                        continue;
                    }
                } else {
                    // No good break point - just put it on its own line
                    currentLine = continuationIndent + word;
                }
            } else {
                // Word fits on continuation line
                currentLine = continuationIndent + word;
            }
        }
    }
    
    if (!currentLine.empty() && currentLine != indent && currentLine != continuationIndent) {
        lines.push_back(currentLine);
    }
    
    return lines;
}

// Calculate the height needed for a slot group
int Glossary::CalculateSlotHeight(JSONObject* item, unsigned int columnWidth, unsigned int font, unsigned int lineHeight) {
    if (!item) return 0;
    
    int height = 0;
    
    // Get slot name
    std::string slot = item->getString("slot");
    if (slot.empty()) {
        slot = item->getString("name");
    }
    std::string headerText = slot + ":";
    
    // Calculate header height (with wrapping)
    std::vector<std::string> headerLines = WrapText(headerText, columnWidth, font);
    height += headerLines.size() * lineHeight;
    
    // Get props array
    JSONArray* props = item->getArray("props");
    if (props) {
        for (int j = 0; j < props->length(); j++) {
            JSONArray* prop = props->getArray(j);
            if (prop && prop->length() >= 2) {
                std::string propText = "  " + prop->getString(1);
                std::vector<std::string> propLines = WrapText(propText, columnWidth, font);
                height += propLines.size() * lineHeight;
            }
        }
    }
    
    height += 5; // Spacing between items
    
    return height;
}

// Display corruptions data in the Corruptions tab
void Glossary::DisplayCorruptions() {
    // Check data availability with lock
    EnterCriticalSection(&dataCrit);
    JSONArray* data = corruptionsData;
    bool loaded = corruptionsDataLoaded;
    LeaveCriticalSection(&dataCrit);
    
    if (!corruptionsTab || !glossaryUI || !data || !loaded) {
        return;
    }
    
    // Only display if in game
    if (!D2CLIENT_GetPlayerUnit()) {
        return;
    }
    
    if (!glossaryUI->IsActive() || glossaryUI->IsMinimized() || !glossaryUI->IsVisible()) {
        // Clear hooks if window is not visible
        if (corruptionsDisplayed) {
            for (auto hook : corruptionsTextHooks) {
                if (hook) delete hook;
            }
            corruptionsTextHooks.clear();
            corruptionsDisplayed = false;
        }
        return;
    }
    
    if (!corruptionsTab->IsActive()) {
        // Clear hooks if tab is not active
        if (corruptionsDisplayed) {
            for (auto hook : corruptionsTextHooks) {
                if (hook) delete hook;
            }
            corruptionsTextHooks.clear();
            corruptionsDisplayed = false;
        }
        return;
    }
    
    // Only create hooks once
    if (corruptionsDisplayed) {
        return;
    }
    
    glossaryUI->Lock();
    
    try {
        const unsigned int font = 0;
        const int lineHeight = 12;
        const int padding = 10;
        const int columnSpacing = 10;
        
        int tabWidth = corruptionsTab->GetXSize();
        int tabHeight = corruptionsTab->GetYSize();
        
        // Calculate column widths (50% each)
        int columnWidth = (tabWidth - padding * 2 - columnSpacing) / 2;
        
        // Track Y positions for each column
        int yPosCol1 = padding;
        int yPosCol2 = padding;
        
        // Display data (use local copy to avoid accessing shared data while locked)
        int dataLength = data->length();
        for (int i = 0; i < dataLength; i++) {
            JSONObject* item = data->getObject(i);
            if (!item) continue;
            
            // Get slot name
            std::string slot = item->getString("slot");
            if (slot.empty()) {
                slot = item->getString("name");
            }
            
            // Check if entire slot group fits in current column
            int slotHeight = CalculateSlotHeight(item, columnWidth, font, lineHeight);
            bool useColumn1 = true;
            
            // Determine which column to use
            if (yPosCol1 + slotHeight > tabHeight - padding) {
                // First column doesn't have space, use second column
                useColumn1 = false;
            } else if (yPosCol2 + slotHeight > tabHeight - padding && yPosCol1 + slotHeight <= tabHeight - padding) {
                // Second column doesn't have space but first does, use first
                useColumn1 = true;
            } else {
                // Both columns have space, use the one with less content
                useColumn1 = (yPosCol1 <= yPosCol2);
            }
            
            int currentX = useColumn1 ? padding : (padding + columnWidth + columnSpacing);
            int& currentY = useColumn1 ? yPosCol1 : yPosCol2;
            
            // Add slot header (Gold color) - wrap if needed
            std::string headerText = slot + ":";
            std::vector<std::string> headerLines = WrapText(headerText, columnWidth, font);
            for (const auto& line : headerLines) {
                Texthook* headerHook = new Texthook(corruptionsTab, currentX, currentY, "%s", line.c_str());
                if (headerHook) {
                    headerHook->SetColor(Gold);
                    headerHook->SetFont(font);
                    corruptionsTextHooks.push_back(headerHook);
                }
                currentY += lineHeight;
            }
            
            // Get props array
            JSONArray* props = item->getArray("props");
            if (props) {
                for (int j = 0; j < props->length(); j++) {
                    JSONArray* prop = props->getArray(j);
                    if (prop && prop->length() >= 2) {
                        std::string propText = "  " + prop->getString(1);
                        
                        // Wrap prop text if needed
                        std::vector<std::string> propLines = WrapText(propText, columnWidth, font);
                        for (const auto& line : propLines) {
                            Texthook* propHook = new Texthook(corruptionsTab, currentX, currentY, "%s", line.c_str());
                            if (propHook) {
                                propHook->SetColor(White);
                                propHook->SetFont(font);
                                corruptionsTextHooks.push_back(propHook);
                            }
                            currentY += lineHeight;
                        }
                    }
                }
            }
            
            currentY += 5; // Spacing between items
        }
        
        corruptionsDisplayed = true;
        
    } catch (...) {
        // If display fails, continue anyway
    }
    
    glossaryUI->Unlock();
}

// Calculate the height needed for an aura group
int Glossary::CalculateAuraHeight(JSONObject* item, unsigned int columnWidth, unsigned int font, unsigned int lineHeight) {
    if (!item) return 0;
    
    int height = 0;
    
    // Get aura name
    std::string name = item->getString("name");
    if (name.empty()) {
        name = item->getString("index");
    }
    std::string headerText = name + ":";
    
    // Calculate header height (with wrapping)
    std::vector<std::string> headerLines = WrapText(headerText, columnWidth, font);
    height += headerLines.size() * lineHeight;
    
    // Get Props array (capital P for auras)
    JSONArray* props = item->getArray("Props");
    if (props) {
        for (int j = 0; j < props->length(); j++) {
            JSONArray* prop = props->getArray(j);
            if (prop && prop->length() >= 1) {
                std::string propText = "  " + prop->getString(0);
                std::vector<std::string> propLines = WrapText(propText, columnWidth, font);
                height += propLines.size() * lineHeight;
            }
        }
    }
    
    height += 5; // Spacing between items
    
    return height;
}

// Display auras data in the Auras tab
void Glossary::DisplayAuras() {
    // Check data availability with lock
    EnterCriticalSection(&dataCrit);
    JSONArray* data = aurasData;
    bool loaded = aurasDataLoaded;
    LeaveCriticalSection(&dataCrit);
    
    if (!aurasTab || !glossaryUI || !data || !loaded) {
        return;
    }
    
    // Only display if in game
    if (!D2CLIENT_GetPlayerUnit()) {
        return;
    }
    
    if (!glossaryUI->IsActive() || glossaryUI->IsMinimized() || !glossaryUI->IsVisible()) {
        // Clear hooks if window is not visible
        if (aurasDisplayed) {
            for (auto hook : aurasTextHooks) {
                if (hook) delete hook;
            }
            aurasTextHooks.clear();
            aurasDisplayed = false;
        }
        return;
    }
    
    if (!aurasTab->IsActive()) {
        // Clear hooks if tab is not active
        if (aurasDisplayed) {
            for (auto hook : aurasTextHooks) {
                if (hook) delete hook;
            }
            aurasTextHooks.clear();
            aurasDisplayed = false;
        }
        return;
    }
    
    // Only create hooks once
    if (aurasDisplayed) {
        return;
    }
    
    glossaryUI->Lock();
    
    try {
        const unsigned int font = 0;
        const int lineHeight = 12;
        const int padding = 10;
        const int columnSpacing = 10;
        
        int tabWidth = aurasTab->GetXSize();
        int tabHeight = aurasTab->GetYSize();
        
        // Calculate column widths (50% each)
        int columnWidth = (tabWidth - padding * 2 - columnSpacing) / 2;
        
        // Track Y positions for each column
        int yPosCol1 = padding;
        int yPosCol2 = padding;
        
        // Display data (use local copy to avoid accessing shared data while locked)
        int dataLength = data->length();
        for (int i = 0; i < dataLength; i++) {
            JSONObject* item = data->getObject(i);
            if (!item) continue;
            
            // Get aura name
            std::string name = item->getString("name");
            if (name.empty()) {
                name = item->getString("index");
            }
            
            // Check if entire aura group fits in current column
            int auraHeight = CalculateAuraHeight(item, columnWidth, font, lineHeight);
            bool useColumn1 = true;
            
            // Determine which column to use
            if (yPosCol1 + auraHeight > tabHeight - padding) {
                // First column doesn't have space, use second column
                useColumn1 = false;
            } else if (yPosCol2 + auraHeight > tabHeight - padding && yPosCol1 + auraHeight <= tabHeight - padding) {
                // Second column doesn't have space but first does, use first
                useColumn1 = true;
            } else {
                // Both columns have space, use the one with less content
                useColumn1 = (yPosCol1 <= yPosCol2);
            }
            
            int currentX = useColumn1 ? padding : (padding + columnWidth + columnSpacing);
            int& currentY = useColumn1 ? yPosCol1 : yPosCol2;
            
            // Add aura header (Gold color) - wrap if needed
            std::string headerText = name + ":";
            std::vector<std::string> headerLines = WrapText(headerText, columnWidth, font);
            for (const auto& line : headerLines) {
                Texthook* headerHook = new Texthook(aurasTab, currentX, currentY, "%s", line.c_str());
                if (headerHook) {
                    headerHook->SetColor(Gold);
                    headerHook->SetFont(font);
                    aurasTextHooks.push_back(headerHook);
                }
                currentY += lineHeight;
            }
            
            // Get Props array (capital P for auras)
            JSONArray* props = item->getArray("Props");
            if (props) {
                for (int j = 0; j < props->length(); j++) {
                    JSONArray* prop = props->getArray(j);
                    if (prop && prop->length() >= 1) {
                        std::string propText = "  " + prop->getString(0);
                        
                        // Wrap prop text if needed
                        std::vector<std::string> propLines = WrapText(propText, columnWidth, font);
                        for (const auto& line : propLines) {
                            Texthook* propHook = new Texthook(aurasTab, currentX, currentY, "%s", line.c_str());
                            if (propHook) {
                                propHook->SetColor(White);
                                propHook->SetFont(font);
                                aurasTextHooks.push_back(propHook);
                            }
                            currentY += lineHeight;
                        }
                    }
                }
            }
            
            currentY += 5; // Spacing between items
        }
        
        aurasDisplayed = true;
        
    } catch (...) {
        // If display fails, continue anyway
    }
    
    glossaryUI->Unlock();
}

void Glossary::LoadEffectsData() {
    EnterCriticalSection(&dataCrit);
    bool alreadyLoaded = effectsDataLoaded;
    LeaveCriticalSection(&dataCrit);
    
    if (alreadyLoaded) {
        return; // Already loaded
    }
    
    // Download JSON asynchronously using Task system
    Task::Enqueue([this]() -> void {
        std::string jsonData = DownloadJSON("https://rustyshackleford1888.github.io/js/effects.json");
        if (!jsonData.empty() && jsonData.length() > 10) {
            // Parse JSON
            JSONArray* parsed = ParseJSON(jsonData);
            if (parsed && parsed->length() > 0) {
                EnterCriticalSection(&dataCrit);
                effectsData = parsed;
                effectsDataLoaded = true;
                LeaveCriticalSection(&dataCrit);
            } else {
                if (parsed) delete parsed;
                EnterCriticalSection(&dataCrit);
                effectsDataLoaded = true;
                LeaveCriticalSection(&dataCrit);
            }
        } else {
            EnterCriticalSection(&dataCrit);
            effectsDataLoaded = true;
            LeaveCriticalSection(&dataCrit);
        }
    });
}

int Glossary::CalculateEffectsHeight(JSONObject* item, unsigned int columnWidth, unsigned int font, unsigned int lineHeight) {
    if (!item) return 0;
    
    int height = 0;
    
    // Get effect name
    std::string name = item->getString("name");
    if (name.empty()) {
        name = item->getString("index");
    }
    std::string headerText = name + ":";
    
    // Calculate header height (with wrapping)
    std::vector<std::string> headerLines = WrapText(headerText, columnWidth, font);
    height += headerLines.size() * lineHeight;
    
    // Get Props array (capital P for effects)
    JSONArray* props = item->getArray("Props");
    if (props) {
        for (int j = 0; j < props->length(); j++) {
            JSONArray* prop = props->getArray(j);
            if (prop && prop->length() >= 1) {
                std::string propText = "  " + prop->getString(0);
                std::vector<std::string> propLines = WrapText(propText, columnWidth, font);
                height += propLines.size() * lineHeight;
            }
        }
    }
    
    height += 5; // Spacing between items
    
    return height;
}

// Display effects data in the Effects tab
void Glossary::DisplayEffects() {
    // Check data availability with lock
    EnterCriticalSection(&dataCrit);
    JSONArray* data = effectsData;
    bool loaded = effectsDataLoaded;
    LeaveCriticalSection(&dataCrit);
    
    if (!gainEffectsTab || !glossaryUI || !data || !loaded) {
        return;
    }
    
    // Only display if in game
    if (!D2CLIENT_GetPlayerUnit()) {
        return;
    }
    
    if (!glossaryUI->IsActive() || glossaryUI->IsMinimized() || !glossaryUI->IsVisible()) {
        // Clear hooks if window is not visible
        if (effectsDisplayed) {
            for (auto hook : effectsTextHooks) {
                if (hook) delete hook;
            }
            effectsTextHooks.clear();
            effectsDisplayed = false;
        }
        return;
    }
    
    if (!gainEffectsTab->IsActive()) {
        // Clear hooks if tab is not active
        if (effectsDisplayed) {
            for (auto hook : effectsTextHooks) {
                if (hook) delete hook;
            }
            effectsTextHooks.clear();
            effectsDisplayed = false;
        }
        return;
    }
    
    // Only create hooks once
    if (effectsDisplayed) {
        return;
    }
    
    glossaryUI->Lock();
    
    try {
        const unsigned int font = 0;
        const int lineHeight = 12;
        const int padding = 10;
        const int columnSpacing = 10;
        
        int tabWidth = gainEffectsTab->GetXSize();
        int tabHeight = gainEffectsTab->GetYSize();
        
        // Calculate column widths (50% each)
        int columnWidth = (tabWidth - padding * 2 - columnSpacing) / 2;
        
        // Track Y positions for each column
        int yPosCol1 = padding;
        int yPosCol2 = padding;
        
        // Display data (use local copy to avoid accessing shared data while locked)
        int dataLength = data->length();
        for (int i = 0; i < dataLength; i++) {
            JSONObject* item = data->getObject(i);
            if (!item) continue;
            
            // Get effect name
            std::string name = item->getString("name");
            if (name.empty()) {
                name = item->getString("index");
            }
            
            // Check if entire effect group fits in current column
            int effectHeight = CalculateEffectsHeight(item, columnWidth, font, lineHeight);
            bool useColumn1 = true;
            
            // Determine which column to use
            if (yPosCol1 + effectHeight > tabHeight - padding) {
                // First column doesn't have space, use second column
                useColumn1 = false;
            } else if (yPosCol2 + effectHeight > tabHeight - padding && yPosCol1 + effectHeight <= tabHeight - padding) {
                // Second column doesn't have space but first does, use first
                useColumn1 = true;
            } else {
                // Both columns have space, use the one with less content
                useColumn1 = (yPosCol1 <= yPosCol2);
            }
            
            int currentX = useColumn1 ? padding : (padding + columnWidth + columnSpacing);
            int& currentY = useColumn1 ? yPosCol1 : yPosCol2;
            
            // Add effect header (Gold color) - wrap if needed
            std::string headerText = name + ":";
            std::vector<std::string> headerLines = WrapText(headerText, columnWidth, font);
            for (const auto& line : headerLines) {
                Texthook* headerHook = new Texthook(gainEffectsTab, currentX, currentY, "%s", line.c_str());
                if (headerHook) {
                    headerHook->SetColor(Gold);
                    headerHook->SetFont(font);
                    effectsTextHooks.push_back(headerHook);
                }
                currentY += lineHeight;
            }
            
            // Get Props array (capital P for effects)
            JSONArray* props = item->getArray("Props");
            if (props) {
                for (int j = 0; j < props->length(); j++) {
                    JSONArray* prop = props->getArray(j);
                    if (prop && prop->length() >= 1) {
                        std::string propText = "  " + prop->getString(0);
                        
                        // Wrap prop text if needed
                        std::vector<std::string> propLines = WrapText(propText, columnWidth, font);
                        for (const auto& line : propLines) {
                            Texthook* propHook = new Texthook(gainEffectsTab, currentX, currentY, "%s", line.c_str());
                            if (propHook) {
                                propHook->SetColor(White);
                                propHook->SetFont(font);
                                effectsTextHooks.push_back(propHook);
                            }
                            currentY += lineHeight;
                        }
                    }
                }
            }
            
            currentY += 5; // Spacing between items
        }
        
        effectsDisplayed = true;
        
    } catch (...) {
        // If display fails, continue anyway
    }
    
    glossaryUI->Unlock();
}

// Display charms information in the Charms tab (static data)
void Glossary::DisplayCharms() {
    if (!charmsTab || !glossaryUI) {
        return;
    }
    
    // Only display if in game
    if (!D2CLIENT_GetPlayerUnit()) {
        return;
    }
    
    if (!glossaryUI->IsActive() || glossaryUI->IsMinimized() || !glossaryUI->IsVisible()) {
        // Clear hooks if window is not visible
        if (charmsDisplayed) {
            for (auto hook : charmsTextHooks) {
                if (hook) delete hook;
            }
            charmsTextHooks.clear();
            charmsDisplayed = false;
        }
        return;
    }
    
    if (!charmsTab->IsActive()) {
        // Clear hooks if tab is not active
        if (charmsDisplayed) {
            for (auto hook : charmsTextHooks) {
                if (hook) delete hook;
            }
            charmsTextHooks.clear();
            charmsDisplayed = false;
        }
        return;
    }
    
    // Only create text hooks once
    if (charmsDisplayed) {
        return;
    }
    
    glossaryUI->Lock();
    
    try {
        const unsigned int font = 6;  // Use font 6 (7px) for content
        const unsigned int headerFont = 0;  // Use font 0 (10px) for headers
        const int lineHeight = 8;  // Adjusted for smaller font
        const int headerLineHeight = 12;  // For header font
        const int padding = 5;
        const int sectionSpacing = 12;  // Spacing between sections
        
        int tabWidth = charmsTab->GetXSize();
        int tabHeight = charmsTab->GetYSize();
        
        // Starting positions
        int startX = padding;
        int currentY = padding;
        
        // Helper function to add text at position
        auto addText = [&](int x, int y, const std::string& text, TextColor color, unsigned int textFont) {
            Texthook* hook = new Texthook(charmsTab, x, y, "%s", text.c_str());
            if (hook) {
                hook->SetColor(color);
                hook->SetFont(textFont);
                charmsTextHooks.push_back(hook);
            }
        };
        
        // Helper function to wrap and add text
        auto addWrappedText = [&](int x, int& y, const std::string& text, TextColor color, int maxWidth) {
            std::vector<std::string> wrapped = WrapText(text, maxWidth, font);
            for (const auto& line : wrapped) {
                addText(x, y, line, color, font);
                y += lineHeight;
            }
        };
        
        // "Charm Drops and Inventory Management" section
        addText(startX, currentY, "Charm Drops and Inventory Management", Gold, headerFont);
        currentY += headerLineHeight + 2;
        
        // Introduction paragraph
        std::string introText = "Charms no longer drop from monsters as magic items with random properties. Instead, Charm bases now drop from the following specific Super Unique monsters in the following difficulties. These bases are treated as unique items and cannot be held more than once in the same character inventory. Annihilus Charm and Hellfire Torch still exist, but with revised stats and new augment slots.";
        addWrappedText(startX, currentY, introText, White, tabWidth - padding * 2);
        currentY += sectionSpacing;
        
        // "How to Augment Charms" section
        addText(startX, currentY, "How to Augment Charms", Gold, headerFont);
        currentY += headerLineHeight + 2;
        
        std::string augmentText = "Charms drop with certain \"property slots\" open, which can then be filled in through the use of Charm Components. Charm Components (apart from unique ones which are monster drops), can be purchased from Drognan (higher tier components are sold in Hell only). Charm components cannot be over written, if one wishes to change the properties on a charm one either has to stash the current Charm and obtain an new one or by resetting the Charm properties by transmuting the charm together with a Shaman's Stone (bought from Drognan).";
        addWrappedText(startX, currentY, augmentText, White, tabWidth - padding * 2);
        currentY += 2;
        
        std::string augmentText2 = "Drognan's components cost gem essence to use, but it costs nothing to apply Unique Charm Components you find as drops throughout the world.";
        addWrappedText(startX, currentY, augmentText2, White, tabWidth - padding * 2);
        currentY += sectionSpacing;
        
        // "Charm Locations" section
        addText(startX, currentY, "Charm Locations:", Gold, headerFont);
        currentY += headerLineHeight + 2;
        
        // Table header
        addText(startX, currentY, "Charm:", Gold, font);
        int locationColX = startX + 150;
        addText(locationColX, currentY, "Location, Monster", Gold, font);
        currentY += lineHeight + 2;
        
        // Gheed's Fortune
        addText(startX, currentY, "Gheed's Fortune:", White, font);
        addWrappedText(locationColX, currentY, "Stony Tomb (Normal, Act 2, Right outside town), 'Creeping Feature'", White, tabWidth - locationColX - padding);
        currentY += lineHeight;
        
        // Panacea Thread
        addText(startX, currentY, "Panacea Thread:", White, font);
        addWrappedText(locationColX, currentY, "Tal Rasha's Tomb (Nightmare, Act 2, The biggest of the fake ones), 'Ancient Kaa the Soulless'", White, tabWidth - locationColX - padding);
        currentY += lineHeight;
        
        // Dream Catcher
        addText(startX, currentY, "Dream Catcher:", White, font);
        addWrappedText(locationColX, currentY, "Cave (Hell, Act 1, Located in Cold Plains), 'Coldcrow'", White, tabWidth - locationColX - padding);
        currentY += sectionSpacing;
        
        // "Unique Charm Component Locations" section
        addText(startX, currentY, "Unique Charm Component Locations", Gold, headerFont);
        currentY += headerLineHeight + 2;
        
        // Table header
        int componentColX = startX;
        int monsterColX = startX + 400;  // Double the width of first column (was 200)
        int locationColX2 = startX + 600;  // Adjusted to maintain spacing
        addText(componentColX, currentY, "Component", Gold, font);
        addText(monsterColX, currentY, "Monster", Gold, font);
        addText(locationColX2, currentY, "Location", Gold, font);
        currentY += lineHeight + 2;
        
        // Table rows
        struct ComponentRow {
            std::string component;
            std::string property;
            std::string monster;
            std::string location;
        };
        
        std::vector<ComponentRow> components = {
            {"Heart of the Beast", "5% Increased Maximum Life", "Minion of Destruction", "Throne of Destruction"},
            {"Soul of the Beauty", "5% Increased Maximum Mana", "Hell Temptress", "Frozen River"},
            {"Obsidian", "50% Enhanced Damage", "Hephasto the Armorer", "River of Flame"},
            {"Gargantua Skin", "50% Enhanced Defense", "Gargantua", "Rigid Highlands"},
            {"Adamantine", "6% Physical Resistance", "The Smith", "Barracks"},
            {"Infernal Eye", "+400-600 Fire Damage", "Molten Brute", "Crystalline Passage"},
            {"Evigfryse Ore", "+300-700 Cold Damage", "Icehawk Riftwing", "Sewers Level 1"},
            {"Fragment of Thunder", "+1-900 Lightning Damage", "Shock Fiend", "Glacial Trail"},
            {"Pulsating Ooze", "+2400 Poison Damage over 4 seconds", "Unraveler", "Tal Rasha's Tomb"},
            {"Gheed's Fortune", "50% Better Chance of Finding Magical Items", "Griswold", "Tristram"},
            {"Heart of Ankara", "2% Life Regenerated per second", "AREA DROP", "Cathedral"},
            {"Living Lava", "+2% Maximum Fire Resistance", "AREA DROP", "River of Flame"},
            {"Core of Winter", "+2% Maximum Cold Resistance", "AREA DROP", "Frozen Tundra"},
            {"Static Essence", "+2% Maximum Lightning Resistance", "Rakanishu", "Stony Field"},
            {"Corrupted Blood", "+2% Maximum Poison Resistance", "Slime Prince", "Flayer Jungle"},
            {"Void Innoculation", "3% Chance to gain 5 seconds of Empower on Kill", "AREA DROP", "Arcane Sanctuary"},
            {"Touch of Rime", "Freezes Target +7", "Snapchip Shatter", "Icy Cellar"}
        };
        
        for (const auto& row : components) {
            int rowStartY = currentY;
            int maxHeight = lineHeight;
            
            // Combine component name and property in the same column
            std::string componentWithProperty = row.component + " - " + row.property;
            std::vector<std::string> componentWrapped = WrapText(componentWithProperty, 390, font);  // Double width (was 190)
            int componentY = rowStartY;
            for (const auto& line : componentWrapped) {
                addText(componentColX, componentY, line, White, font);
                componentY += lineHeight;
            }
            if ((int)componentWrapped.size() * lineHeight > maxHeight) {
                maxHeight = (int)componentWrapped.size() * lineHeight;
            }
            
            // Wrap monster name if needed
            std::vector<std::string> monsterWrapped = WrapText(row.monster, 190, font);
            int monsterY = rowStartY;
            for (const auto& line : monsterWrapped) {
                addText(monsterColX, monsterY, line, White, font);
                monsterY += lineHeight;
            }
            if ((int)monsterWrapped.size() * lineHeight > maxHeight) {
                maxHeight = (int)monsterWrapped.size() * lineHeight;
            }
            
            // Wrap location if needed
            std::vector<std::string> locationWrapped = WrapText(row.location, tabWidth - locationColX2 - padding, font);
            int locationY = rowStartY;
            for (const auto& line : locationWrapped) {
                addText(locationColX2, locationY, line, White, font);
                locationY += lineHeight;
            }
            if ((int)locationWrapped.size() * lineHeight > maxHeight) {
                maxHeight = (int)locationWrapped.size() * lineHeight;
            }
            
            currentY += maxHeight + lineHeight;
            
            // Check if we've run out of vertical space
            if (currentY > tabHeight - padding - lineHeight) {
                break;
            }
        }
        
        charmsDisplayed = true;
        
    } catch (...) {
        // If display fails, continue anyway
    }
    
    glossaryUI->Unlock();
}

