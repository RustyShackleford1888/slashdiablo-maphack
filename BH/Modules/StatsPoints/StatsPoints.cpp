#include "StatsPoints.h"

#include "../../BH.h"

#include "../../Config.h"

#include "../../Drawing.h"

#include "../ItemMover/ItemMover.h"

#include <cstdlib>



#pragma optimize("", off)



DWORD StatsPoints_ShiftClickLimit = 0x7FFFFFFF;



// Must outlive LoadConfig; Config::ReadInt keeps a pointer for config->Write().

static int configShiftClickStatLimit = 0;

static Drawing::Inputhook* statShiftLimitInput = nullptr;



static SHORT(WINAPI *RealGetKeyState)(int) = GetKeyState;



static void ApplyShiftClickStatLimit() {

	StatsPoints_ShiftClickLimit = (configShiftClickStatLimit > 0)

		? (DWORD)configShiftClickStatLimit

		: 0x7FFFFFFF;

}



static void SyncShiftClickStatLimitFromInput() {

	if (!statShiftLimitInput)

		return;



	int parsed = atoi(statShiftLimitInput->GetText().c_str());

	if (parsed < 0)

		parsed = 0;



	configShiftClickStatLimit = parsed;

	ApplyShiftClickStatLimit();

}



static void RefreshShiftClickStatLimitInput() {

	if (!statShiftLimitInput)

		return;



	char buf[16];

	sprintf_s(buf, "%d", configShiftClickStatLimit);

	statShiftLimitInput->SetText("%s", buf);

}



// D2Client+0xBDEF5: CALL GetKeyState(VK_SHIFT) in the stat "+" handler. EDI is the
// batch size. Shift+click is clamped to the configured limit; Ctrl+Shift+click
// leaves EDI alone so all remaining stat points can be assigned.

void __declspec(naked) StatsPoints_LimitShiftClickInterception() {

	__asm {

		push 0x11

		call dword ptr [RealGetKeyState]

		test ax, 0x8000

		jnz passthrough

		cmp StatsPoints_ShiftClickLimit, 0x7FFFFFFF

		je  passthrough

		cmp edi, StatsPoints_ShiftClickLimit

		jl  passthrough

		mov edi, StatsPoints_ShiftClickLimit

	passthrough:

		jmp dword ptr [RealGetKeyState]

	}

}



StatsPoints::StatsPoints() : Module("StatsPoints") {}



void StatsPoints::BuildSettingsUI() {

	ItemMover* mover = (ItemMover*)BH::moduleManager->Get("item-mover");

	if (!mover || !mover->GetInteractionTab())

		return;



	Drawing::UITab* tab = mover->GetInteractionTab();

	unsigned int x = mover->GetInteractionRightColumnX();

	unsigned int& y = mover->GetInteractionRightColumnY();



	new Drawing::Texthook(tab, x, (y += 10), "Stats per Shift+Click:");

	statShiftLimitInput = new Drawing::Inputhook(tab, x + 140, y - 4, 36, "%s", "");

	statShiftLimitInput->SetFont(0);

	RefreshShiftClickStatLimitInput();

}



void StatsPoints::OnLoad() {
	LoadConfig();
}



void StatsPoints::LoadConfig() {

	BH::config->ReadInt("Shift Click Stat Points Limit", configShiftClickStatLimit);

	ApplyShiftClickStatLimit();

	RefreshShiftClickStatLimitInput();

}



void StatsPoints_FlushSettingsInput() {

	SyncShiftClickStatLimitFromInput();

}


