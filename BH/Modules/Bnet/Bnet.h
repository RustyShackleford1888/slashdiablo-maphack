#pragma once
#include "../Module.h"
#include "../AutoTele/AutoTele.h"
#include <regex>

struct Control;

class Bnet : public Module {
	private:
		std::map<string, bool> bools;
		bool* showLastGame;
		bool* showLastPass;
		bool* nextInstead;
		bool* keepDesc;
		static unsigned int failToJoin;
		static std::string lastName;
		static std::string lastPass;
		static std::string lastDesc;
		static std::regex reg;
		static unsigned int defaultGsIndex;
		static string defaultGsString;
		static string DefaultGame;
		static string DefaultPassword;

	public:

		Bnet() : Module("Bnet") {};

		void OnLoad();
		void OnUnload();
		void LoadConfig();

		void OnGameJoin();
		void OnGameExit();

		void InstallPatches();
		void RemovePatches();

		std::map<string, bool>* GetBools() { return &bools; }
		static VOID __fastcall FOG10251Patch(DWORD lpCriticalSection, char nLine);
		static DWORD __stdcall BnetLobbyAdBlockPatch(DWORD a1);
		static VOID __fastcall NextGamePatch(Control* box, BOOL (__stdcall *FunCallBack)(Control*, DWORD, DWORD));
		static VOID __fastcall NextPassPatch(Control* box, BOOL(__stdcall *FunCallBack)(Control*, DWORD, DWORD));
		static VOID __fastcall GameDescPatch(Control* box, BOOL(__stdcall *FunCallBack)(Control*, DWORD, DWORD));
		static void RemovePassPatch();

		static std::string GetDefaultGsString() { return "gs" + std::to_string(defaultGsIndex + 1); }
		static std::string GetDefaultGamename() { return DefaultGame; }
		static std::string GetDefaultPassword() { return DefaultPassword; }
};

void FailToJoin_Interception();
void RemovePass_Interception();
