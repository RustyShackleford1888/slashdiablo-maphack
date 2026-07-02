#pragma once
#include <Windows.h>
#include "../Module.h"

extern DWORD SkillsPoints_ShiftClickLimit;

void __stdcall SkillsPoints_SendPacketHook(size_t aLen, DWORD arg1, BYTE* aPacket);
void SkillsPoints_FlushSettingsInput();

class SkillsPoints : public Module {
private:
	bool InstallSendPacketHook();
	void RemoveSendPacketHook();

public:
	SkillsPoints();

	void OnLoad() override;
	void BuildSettingsUI();
	void OnUnload() override;
	void OnGameJoin() override;
	void LoadConfig() override;
	void OnLoop() override;
};
