#include "MOTD.h"
#include "../../BH.h"
#include "../../D2Ptrs.h"
#include "../../D2Stubs.h"
#include "../ScreenInfo/ScreenInfo.h"
#include <string>

using namespace Drawing;

void MOTD::OnLoad() {
    LoadConfig();
    InitializeServerMessages();
}

void MOTD::LoadConfig() {
    // Load any configuration if needed in the future
}

void MOTD::InitializeServerMessages() {
    // Initialize the server messages based on the provided server list
    serverMessages["68.232.175.54"] = "-- Gameserver#1 | US | New York --";
    serverMessages["45.77.126.25"] = "-- Gameserver#2 | US | Los Angeles --";
    serverMessages["78.141.219.90"] = "-- Gameserver#3 | EMEA | Amsterdam --";
    serverMessages["45.32.119.179"] = "-- Gameserver#4 | APAC | Singapore --";
}

void MOTD::OnGameJoin() {
    // Get server IP from GameInfo (which we know works)
    GameStructInfo* pGameInfo = (*p_D2CLIENT_GameInfo);
    if (!pGameInfo || !pGameInfo->szGameServerIp) {
        return;
    }

    std::string serverIP = std::string(pGameInfo->szGameServerIp);
    std::string message = GetServerMessage(serverIP);
    
    if (!message.empty()) {
        // Store message and set timer for delayed sending
        pendingMessage = message;
        motdSendTime = GetTickCount();
        motdShown = true;
        motdDisplayTime = GetTickCount();
    }
}

void MOTD::OnGameExit() {
    motdShown = false;
    motdDisplayTime = 0;
    motdSendTime = 0;
    pendingMessage.clear();
}

void MOTD::OnDraw() {
    if (motdShown && motdSendTime > 0) {
        DWORD currentTime = GetTickCount();
        
        // Send the MOTD message after the delay
        if (currentTime - motdSendTime >= MOTD_SEND_DELAY && !pendingMessage.empty()) {
            // Convert string to wide characters
            std::wstring wideMessage(pendingMessage.begin(), pendingMessage.end());
            D2CLIENT_PrintGameString(const_cast<wchar_t*>(wideMessage.c_str()), Red);
            pendingMessage.clear(); // Clear so we don't send it again
            motdShown = false; // Hide after sending
        }
    }
}

std::string MOTD::GetServerMessage(const std::string& serverIP) {
    auto it = serverMessages.find(serverIP);
    if (it != serverMessages.end()) {
        return it->second;
    }
    return ""; // Return empty string if server not found
}
