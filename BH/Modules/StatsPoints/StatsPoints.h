#pragma once
#include <Windows.h>
#include "../Module.h"

// Maximum number of stat points a single Shift+Click will assign on the
// character screen. 0x7FFFFFFF means "no limit" (vanilla behaviour: assign all
// remaining points). Overwritten from "Shift Click Stat Points Limit" in
// BH_settings.cfg.
extern DWORD StatsPoints_ShiftClickLimit;

// Clamps EDI for Shift+click; Ctrl+Shift+click skips the clamp. Tail-calls GetKeyState.
void StatsPoints_LimitShiftClickInterception();
void StatsPoints_FlushSettingsInput();

class StatsPoints : public Module {
public:
	StatsPoints();

	void OnLoad() override;
	void LoadConfig() override;
	void BuildSettingsUI();
};
