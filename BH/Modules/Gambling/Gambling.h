#pragma once
#include "../Module.h"
#include "../../Config.h"
#include "../../Drawing.h"

class Gambling : public Module {
	private:
		map<std::string, Toggle> Toggles;
		unsigned int refreshKey;
		DWORD lastRefreshTime;
		static const DWORD REFRESH_COOLDOWN = 500; // 500ms cooldown
		bool pendingRefresh;
		DWORD pendingGambleTime; // Timestamp when waiting for menu to open
		bool inGamblingScreen; // Flag to track if we're in a gambling screen
		
	public:
		Gambling() : Module("Gambling") {};
		void OnLoad();
		void OnUnload();
		void OnKey(bool up, BYTE key, LPARAM lParam, bool* block);
		void OnDraw();
		bool IsGambling();
		void LoadConfig();
		unsigned int* GetRefreshKeyPtr() { return &refreshKey; }
};

