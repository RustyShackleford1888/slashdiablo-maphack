#include "BreakpointsDisplay.h"
#include "../Basic/Texthook/Texthook.h"
#include "../Basic/Framehook/Framehook.h"
#include "../Basic/Boxhook/Boxhook.h"
#include "../../D2Ptrs.h"
#include "../../BH.h"

using namespace Drawing;


BreakpointsDisplay* BreakpointsDisplay::display;

BreakpointsDisplay::BreakpointsDisplay(std::string name2) {
	int yPos = 10;
	int width = 240;

	InitializeCriticalSection(&crit);
	SetY(yPos);
	SetXSize(width);

	LoadConfig();

	SetName(name2);
	SetActive(true);
	SetMinimized(true);

	BH::config->ReadKey("Breakpoints Summary", "VK_9", breakpointKey);
	display = this;
}

BreakpointsDisplay::~BreakpointsDisplay() {
	Lock();
	// Remove all hooks associated with the display
	while (Hooks.size() > 0) {
		delete (*Hooks.begin());
	}
	Unlock();
	DeleteCriticalSection(&crit);
}

void BreakpointsDisplay::LoadConfig() {
	int height = 342 + 8 * 3 + 16 * 6;

	BH::config->ReadToggle("Stats on Right", "None", false, Toggles["Stats on Right"]);

	int xPos = Toggles["Stats on Right"].state ?
		*p_D2CLIENT_ScreenSizeX - 10 - GetXSize() : 10;
	SetX(xPos);
	SetYSize(height);
}

void BreakpointsDisplay::SetX(unsigned int newX) {
	if (newX >= 0 && newX <= Hook::GetScreenWidth()) {
		Lock();
		x = newX;
		Unlock();
	}
}

void BreakpointsDisplay::SetY(unsigned int newY) {
	if (newY >= 0 && newY <= Hook::GetScreenHeight()) {
		Lock();
		y = newY;
		Unlock();
	}
}

void BreakpointsDisplay::SetXSize(unsigned int newXSize) {
	if (newXSize >= 0 && newXSize <= (Hook::GetScreenWidth() - GetX())) {
		Lock();
		xSize = newXSize;
		Unlock();
	}
}

void BreakpointsDisplay::SetYSize(unsigned int newYSize) {
	if (newYSize >= 0 && newYSize <= (Hook::GetScreenHeight() - GetY())) {
		Lock();
		ySize = newYSize;
		Unlock();
	}
}

bool BreakpointsDisplay::InRange(unsigned int x, unsigned int y) {
	return IsActive() &&
		x >= GetX() && y >= GetY() &&
		x <= GetX() + GetXSize() && y <= GetY() + GetYSize();
}

void BreakpointsDisplay::Draw() {
	display->Lock();
	display->OnDraw();
	display->Unlock();
}

void BreakpointsDisplay::OnDraw() {
	UnitAny* unit = D2CLIENT_GetPlayerUnit();
	bool isMerc = false;
	if (!unit)
		return;
	int column1 = GetX() + 5;
	int column2 = column1 + GetXSize() / 2;

	if (!IsMinimized()) {
		if (D2CLIENT_GetUIState(UI_MERC)) {
			unit = D2CLIENT_GetMercUnit();
			isMerc = true;
		}
		for (std::list<Hook*>::iterator it = Hooks.begin(); it != Hooks.end(); it++)
			(*it)->OnDraw();

		int y = GetY();
		RECT pRect;
		pRect.left = GetX();
		pRect.top = y;
		pRect.right = x + GetXSize();
		pRect.bottom = y + GetYSize();

		Drawing::Boxhook::Draw(GetX(), GetY(), GetXSize(), GetYSize(), White, Drawing::BTBlack);
		Drawing::Framehook::DrawRectStub(&pRect);

		BnetData* pData = (*p_D2LAUNCH_BnData);

		int currentFCR = (int)D2COMMON_GetUnitStat(unit, STAT_FASTERCAST, 0);
		int currentFrames = 0; // Variable to store the current frames
		int nextBreakpointFCR = 0; // Variable to store the FCR percentage needed for the next breakpoint

		// Calculate current frames for each character class
		switch (pData->nCharClass) {
		case 0: // Amazon
			if (currentFCR >= 152) {
				currentFrames = 11;
			}
			else if (currentFCR >= 99) {
				currentFrames = 12;
				nextBreakpointFCR = 152 - currentFCR;
			}
			else if (currentFCR >= 68) {
				currentFrames = 13;
				nextBreakpointFCR = 99 - currentFCR;
			}
			else if (currentFCR >= 48) {
				currentFrames = 14;
				nextBreakpointFCR = 68 - currentFCR;
			}
			else if (currentFCR >= 32) {
				currentFrames = 15;
				nextBreakpointFCR = 48 - currentFCR;
			}
			else if (currentFCR >= 22) {
				currentFrames = 16;
				nextBreakpointFCR = 32 - currentFCR;
			}
			else if (currentFCR >= 14) {
				currentFrames = 17;
				nextBreakpointFCR = 22 - currentFCR;
			}
			else if (currentFCR >= 7) {
				currentFrames = 18;
				nextBreakpointFCR = 14 - currentFCR;
			}
			else {
				currentFrames = 19;
				nextBreakpointFCR = 7 - currentFCR;
			}
			break;
		case 1: // Sorceress
			if (currentFCR >= 200) {
				currentFrames = 7;
			}
			else if (currentFCR >= 105) {
				currentFrames = 8;
				nextBreakpointFCR = 200 - currentFCR;
			}
			else if (currentFCR >= 63) {
				currentFrames = 9;
				nextBreakpointFCR = 105 - currentFCR;
			}
			else if (currentFCR >= 37) {
				currentFrames = 10;
				nextBreakpointFCR = 63 - currentFCR;
			}
			else if (currentFCR >= 20) {
				currentFrames = 11;
				nextBreakpointFCR = 37 - currentFCR;
			}
			else if (currentFCR >= 9) {
				currentFrames = 12;
				nextBreakpointFCR = 20 - currentFCR;
			}
			else {
				currentFrames = 13;
				nextBreakpointFCR = 9 - currentFCR;
			}
			break;
		case 2: // Necromancer
			if (currentFCR >= 180) {
				currentFrames = 13;
			}
			else if (currentFCR >= 120) {
				currentFrames = 14;
			}
			else if (currentFCR >= 86) {
				currentFrames = 15;
			}
			else if (currentFCR >= 65) {
				currentFrames = 16;
			}
			else if (currentFCR >= 48) {
				currentFrames = 17;
			}
			else if (currentFCR >= 35) {
				currentFrames = 18;
			}
			else if (currentFCR >= 24) {
				currentFrames = 19;
			}
			else if (currentFCR >= 18) {
				currentFrames = 20;
			}
			else if (currentFCR >= 11) {
				currentFrames = 21;
			}
			else if (currentFCR >= 6) {
				currentFrames = 22;
			}
			else {
				currentFrames = 23;
			}
			break;
		case 3: // Paladin
			if (currentFCR >= 125) {
				currentFrames = 9;
			}
			else if (currentFCR >= 75) {
				currentFrames = 10;
			}
			else if (currentFCR >= 48) {
				currentFrames = 11;
			}
			else if (currentFCR >= 30) {
				currentFrames = 12;
			}
			else if (currentFCR >= 18) {
				currentFrames = 13;
			}
			else if (currentFCR >= 9) {
				currentFrames = 14;
			}
			else {
				currentFrames = 15;
			}
			break;
		case 4: // Barbarian
			if (currentFCR >= 200) {
				currentFrames = 7;
			}
			else if (currentFCR >= 105) {
				currentFrames = 8;
			}
			else if (currentFCR >= 63) {
				currentFrames = 9;
			}
			else if (currentFCR >= 37) {
				currentFrames = 10;
			}
			else if (currentFCR >= 20) {
				currentFrames = 11;
			}
			else if (currentFCR >= 9) {
				currentFrames = 12;
			}
			else {
				currentFrames = 13;
			}
			break;
		case 5: // Druid
			if (currentFCR >= 163) {
				currentFrames = 10;
			}
			else if (currentFCR >= 99) {
				currentFrames = 11;
			}
			else if (currentFCR >= 68) {
				currentFrames = 12;
			}
			else if (currentFCR >= 46) {
				currentFrames = 13;
			}
			else if (currentFCR >= 30) {
				currentFrames = 14;
			}
			else if (currentFCR >= 19) {
				currentFrames = 15;
			}
			else if (currentFCR >= 4) {
				currentFrames = 16;
			}
			else {
				currentFrames = 17;
			}
			break;
		case 6: // Assassin
			if (currentFCR >= 102) {
				currentFrames = 10;
			}
			else if (currentFCR >= 65) {
				currentFrames = 11;
			}
			else if (currentFCR >= 42) {
				currentFrames = 12;
			}
			else if (currentFCR >= 27) {
				currentFrames = 13;
			}
			else if (currentFCR >= 16) {
				currentFrames = 14;
			}
			else if (currentFCR >= 8) {
				currentFrames = 15;
			}
			else {
				currentFrames = 16;
			}
			break;
		}
		// Now you can use currentFrames in the subsequent code block

		// Calculate next breakpoint FCR percentage based on currentFrames and pData->nCharClass

		// Now you can use currentFCR and nextBreakpointFCR in your text drawing function
		Texthook::Draw(column1, (y += 16), None, 6, Gold,
			L"FCR:\377c0 %d (Frames:%d Next BP:+%d%%)",
			currentFCR, currentFrames, nextBreakpointFCR);
		Texthook::Draw(column1, y, None, 6, Gold,
			L"Block Rate:\377c0 %d",
			(int)D2COMMON_GetUnitStat(unit, STAT_FASTERBLOCK, 0)
		);
		Texthook::Draw(column1, (y += 16), None, 6, Gold,
			L"Hit Recovery:\377c0 %d",
			(int)D2COMMON_GetUnitStat(unit, STAT_FASTERHITRECOVERY, 0)
		);
		Texthook::Draw(column1, y, None, 6, Gold,
			L"Run/Walk:\377c0 %d",
			(int)D2COMMON_GetUnitStat(unit, STAT_FASTERRUNWALK, 0)
		);
		Texthook::Draw(column1, (y += 16), None, 6, Gold,
			L"Attack Rate:\377c0 %d",
			(int)D2COMMON_GetUnitStat(unit, STAT_ATTACKRATE, 0));
		Texthook::Draw(column1, y, None, 6, Gold,
			L"IAS:\377c0 %d",
			(int)D2COMMON_GetUnitStat(unit, STAT_IAS, 0));

		y += 8;
	}
}

bool BreakpointsDisplay::KeyClick(bool bUp, BYTE bKey, LPARAM lParam) {
	display->Lock();
	bool block = display->OnKey(bUp, bKey, lParam);
	display->Unlock();
	return block;
}

bool BreakpointsDisplay::OnKey(bool up, BYTE kkey, LPARAM lParam) {
	UnitAny* unit = D2CLIENT_GetPlayerUnit();
	if (!unit)
		return false;

	if (IsMinimized()) {
		if (!up && kkey == breakpointKey) {
			LoadConfig();
			SetMinimized(false);
			return true;
		}
	}
	else {
		if (!up && (kkey == breakpointKey || kkey == VK_ESCAPE)) {
			SetMinimized(true);
			return true;
		}
	}
	return false;
}

bool BreakpointsDisplay::Click(bool up, unsigned int mouseX, unsigned int mouseY) {
	display->Lock();
	bool block = display->OnClick(up, mouseX, mouseY);
	display->Unlock();
	return block;
}

bool BreakpointsDisplay::OnClick(bool up, unsigned int x, unsigned int y) {
	UnitAny* unit = D2CLIENT_GetPlayerUnit();
	if (!unit)
		return false;

	if (!IsMinimized() && InRange(x, y)) {
		SetMinimized(true);
		return true;
	}
	return false;
}

