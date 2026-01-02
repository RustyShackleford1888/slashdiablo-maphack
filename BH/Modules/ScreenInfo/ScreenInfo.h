#pragma once
#include "../../D2Structs.h"
#include "../Module.h"
#include "../../Config.h"
#include "../../Drawing.h"
#include <deque>

struct StateCode {
    std::string name;
    unsigned int value;
};

struct StateWarning {
    std::string name;
    ULONGLONG startTicks;
    StateWarning(std::string n, ULONGLONG ticks) : name(n), startTicks(ticks) {}
};

struct Buff {
    BYTE state;
    int index;
    BOOL isBuff;
    ULONGLONG addedTicks;  // Timestamp when buff was added
    ULONGLONG lastCountdownMsg;  // Timestamp of last countdown message (for debugging)
};

class ScreenInfo : public Module {
private:
    map<string, string> SkillWarnings;
    std::vector<std::string> automapInfo;
    std::map<DWORD, string> SkillWarningMap;
    std::deque<StateWarning*> CurrentWarnings;
    Drawing::Texthook* bhText;
    Drawing::Texthook* mpqVersionText;
    Drawing::Texthook* d2VersionText;
    DWORD gameTimer;
    DWORD endTimer;

    int packetRequests;
    ULONGLONG warningTicks;
    ULONGLONG packetTicks;
    bool MephistoBlocked;
    bool DiabloBlocked;
    bool BaalBlocked;
    bool ReceivedQuestPacket;
    DWORD startExperience;
    int startLevel;
    string currentPlayer;
    DWORD currentExperience;
    int currentLevel;
    double currentExpGainPct;
    double currentExpPerSecond;
    double killsPerMinute;
    char* currentExpPerSecondUnit;

    // Config* cRunData;
    bool bFailedToWrite = false;
    int nTotalGames;
    string szGamesToLevel;
    string szTimeToLevel;
    string szLastXpGainPer;
    string szLastXpPerSec;
    string szLastGameTime;
    int aPlayerCountAverage[8];

    string szSavePath;
    string szColumnHeader;
    string szColumnData;

    map<string, string> automap;
    map<string, int> runcounter;
    map<string, int> killscounter;
    map<DWORD, int> UnitsOverall;
    map<DWORD, int> UnitsDead;
    vector<pair<string, string>> runDetailsColumns;
    map<string, unsigned int> runs;

    string SimpleGameName(const string& gameName);
    int GetPlayerCount();
    void FormattedXPPerSec(char* buffer, double xpPerSec);
    string FormatTime(time_t t, const char* format);
    CellFile* cf;
    void* mpqH;
    BOOL manageBuffs;
    BOOL manageConv;
    int resTracker;
    BOOL cellLoaded;
    vector<Buff> activeBuffs;
    vector<BYTE> buffs;
    vector<wchar_t*> buffNames;

public:
    static map<std::string, Toggle> Toggles;

    ScreenInfo() :
        Module("Screen Info"), warningTicks(BHGetTickCount()), packetRequests(0),
        MephistoBlocked(false), DiabloBlocked(false), BaalBlocked(false), ReceivedQuestPacket(false),
        startExperience(0), startLevel(0), mpqH(NULL), cf(NULL), cellLoaded(false) {}

    void OnLoad();
    void LoadConfig();
    void MpqLoaded();
    void OnKey(bool up, BYTE key, LPARAM lParam, bool* block);
    void OnGameJoin();
    void OnGameExit();

    void OnRightClick(bool up, int x, int y, bool* block);
    void OnDraw();
    void OnOOGDraw();
    void OnAutomapDraw();
    void OnGamePacketRecv(BYTE* packet, bool* block);

    std::string ReplaceAutomapTokens(std::string& v);
    void WriteRunTrackerData();
    void DrawPopup(wchar_t* buffName, int x, int y);
    vector<wstring> strBreakApart(wstring str, wchar_t delimiter);

    static void AddDrop(UnitAny* item);
    static void AddDrop(const string& name, unsigned int x, unsigned int y);
};

// ... existing code ...
StateCode GetStateCode(unsigned int nKey);
StateCode GetStateCode(const char* name);
long long ExpByLevel[];
