#pragma once
#include "../Module.h"
#include "../AutoTele/AutoTele.h"
#include <regex>

struct Control;

/** D2MULTI lobby sub-panel id for D2MULTI_SetScreen (Battle.net multiplayer UI). */
enum D2LobbyPanels {
	LOBBY_PANEL_CHAT        = 0x00, // main chat lobby
	LOBBY_PANEL_CREATE      = 0x01, // Create Game screen
	LOBBY_PANEL_JOIN        = 0x02, // Join Game list
	LOBBY_PANEL_LADDER      = 0x03, // Ladder rankings
	LOBBY_PANEL_CHANNEL     = 0x04, // channel list / change channel
	LOBBY_PANEL_LOGINMSG    = 0x05, // login MOTD / news
};

class Bnet : public Module {
	private:
		std::map<string, bool> bools;
		std::map<string, unsigned int> ints;
		bool* showLastGame;
		bool* showLastPass;
		bool* nextInstead;
		bool* keepDesc;
		static string DefaultGame;
		static string DefaultPassword;
		static unsigned int failToJoin;
		static std::string lastName;
		static std::string lastPass;
		static std::string lastDesc;
		static std::regex reg;
		unsigned int* defaultGsIndex;
		static string defaultGsString;

	public:
		bool* followLeader;
		/** Set from the Misc settings tab input fields; same rules as [Leader] / [Leader Character] in BH_settings.cfg. */
		void ApplyFollowLeaderFromUI(const std::string& accRaw, const std::string& charRaw);
		/** Set from the Misc settings tab; cfg keys "Default Game Name" / "Default Password". */
		static void ApplyDefaultGameSettingsFromUI(const std::string& gameRaw, const std::string& passRaw);
		/** Battle.net account name (cfg "Leader" / "Leader Account"), e.g. Rusty. In-game lines look like Char(Acc): MrWatsit(Rusty). */
		string leaderAccount;
		/** In-game character name (cfg "Leader Character"), e.g. MrWatsit — the part before '(' in Char(Acc) messages. */
		string leaderCharacter;

		Bnet() : Module("Bnet") {};

		void OnLoad();
		void OnUnload();
		void LoadConfig();

		void OnGameJoin();
		void OnGameExit();
		void OnLoop() override;
		void OnOOGDraw() override;
		void OnChatPacketRecv(BYTE* packet, bool* block) override;
		void OnChatMsg(const char* user, const char* msg, bool fromGame, bool* block) override;
		void OnGamePacketRecv(BYTE* packet, bool* block) override;
		void OnKey(bool up, BYTE key, LPARAM lParam, bool* block) override;

		void InstallPatches();
		void RemovePatches();

		std::map<string, bool>* GetBools() { return &bools; }
		static VOID __fastcall FOG10251Patch(DWORD lpCriticalSection, char nLine);
		static DWORD __stdcall BnetLobbyAdBlockPatch(DWORD a1);
		static VOID __fastcall NextGamePatch(Control* box, BOOL (__stdcall *FunCallBack)(Control*, DWORD, DWORD));
		static VOID __fastcall NextPassPatch(Control* box, BOOL(__stdcall *FunCallBack)(Control*, DWORD, DWORD));
		static VOID __fastcall GameDescPatch(Control* box, BOOL(__stdcall *FunCallBack)(Control*, DWORD, DWORD));
		static void RemovePassPatch();

		std::map<string, unsigned int>* GetInts() { return &ints; }
		static std::string GetDefaultGamename() { return DefaultGame; }
		static std::string GetDefaultPassword() { return DefaultPassword; }
		/** Password from the last game joined (BnData), for leader-follow join attempts. */
		static std::string GetLastJoinedGamePassword() { return lastPass; }
		/** Programmatic follow-join: set lastName so it matches the join name box and NextGamePatch behavior. */
		static void SetLastGameNameForFollow(const std::string& s) { lastName = s; }
};

void FailToJoin_Interception();
void RemovePass_Interception();
