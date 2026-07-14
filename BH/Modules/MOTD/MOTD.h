#pragma once
#include "../Module.h"
#include "../../Config.h"
#include "../../Drawing.h"
#include <map>

class MOTD : public Module {
private:
    std::map<std::string, std::string> serverMessages;
    bool motdShown;
    DWORD motdDisplayTime;
    DWORD motdSendTime;
    std::string pendingMessage;
    static const DWORD MOTD_DISPLAY_DURATION = 5000; // 5 seconds
    static const DWORD MOTD_SEND_DELAY = 0; // Instant

public:
    MOTD() : Module("MOTD"), motdShown(false), motdDisplayTime(0), motdSendTime(0) {}

    void OnLoad();
    void LoadConfig();
    void OnGameJoin();
    void OnGameExit();
    void OnDraw();
    
    std::string GetServerMessage(const std::string& serverIP);
    void InitializeServerMessages();
};
