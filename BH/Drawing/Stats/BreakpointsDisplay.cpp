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
	int height = 200;

	BH::config->ReadToggle(" on Right", "None", false, Toggles["Stats on Right"]);

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
		std::string nextBreakpointFCR; // Variable to store the FCR percentage needed for the next breakpoint

		// Calculate current frames for each character class
		switch (pData->nCharClass) {
		case 0: // Amazon
			if (currentFCR >= 152) {
				currentFrames = 11;
				nextBreakpointFCR = "MAX";
			}
			else if (currentFCR >= 99) {
				currentFrames = 12;
				nextBreakpointFCR = std::to_string(152 - currentFCR);
			}
			else if (currentFCR >= 68) {
				currentFrames = 13;
				nextBreakpointFCR = std::to_string(99 - currentFCR);
			}
			else if (currentFCR >= 48) {
				currentFrames = 14;
				nextBreakpointFCR = std::to_string(68 - currentFCR);
			}
			else if (currentFCR >= 32) {
				currentFrames = 15;
				nextBreakpointFCR = std::to_string(48 - currentFCR);
			}
			else if (currentFCR >= 22) {
				currentFrames = 16;
				nextBreakpointFCR = std::to_string(32 - currentFCR);
			}
			else if (currentFCR >= 14) {
				currentFrames = 17;
				nextBreakpointFCR = std::to_string(22 - currentFCR);
			}
			else if (currentFCR >= 7) {
				currentFrames = 18;
				nextBreakpointFCR = std::to_string(14 - currentFCR);
			}
			else {
				currentFrames = 19;
				nextBreakpointFCR = std::to_string(7 - currentFCR);
			}
			break;
		case 1: // Sorceress
			if (D2CLIENT_GetPlayerUnit()->pInfo->pRightSkill->pSkillInfo->wSkillId == 0x35 ||
				D2CLIENT_GetPlayerUnit()->pInfo->pLeftSkill->pSkillInfo->wSkillId == 0x35 ||
				D2CLIENT_GetPlayerUnit()->pInfo->pRightSkill->pSkillInfo->wSkillId == 0x31 ||
				D2CLIENT_GetPlayerUnit()->pInfo->pLeftSkill->pSkillInfo->wSkillId == 0x31) {
				if (currentFCR >= 194) {
					currentFrames = 11;
					nextBreakpointFCR = "MAX";
				}
				else if (currentFCR >= 117) {
					currentFrames = 12;
					nextBreakpointFCR = std::to_string(194 - currentFCR);
				}
				else if (currentFCR >= 78) {
					currentFrames = 13;
					nextBreakpointFCR = std::to_string(117 - currentFCR);
				}
				else if (currentFCR >= 52) {
					currentFrames = 14;
					nextBreakpointFCR = std::to_string(78 - currentFCR);
				}
				else if (currentFCR >= 35) {
					currentFrames = 15;
					nextBreakpointFCR = std::to_string(52 - currentFCR);
				}
				else if (currentFCR >= 23) {
					currentFrames = 16;
					nextBreakpointFCR = std::to_string(35 - currentFCR);
				}
				else if (currentFCR >= 15) {
					currentFrames = 17;
					nextBreakpointFCR = std::to_string(23 - currentFCR);
				}
				else if (currentFCR >= 7) {
					currentFrames = 18;
					nextBreakpointFCR = std::to_string(15 - currentFCR);
				}
				else {
					currentFrames = 19;
					nextBreakpointFCR = std::to_string(9 - currentFCR);
				}
			}
			else {
				if (currentFCR >= 200) {
					currentFrames = 7;
					nextBreakpointFCR = "MAX";
				}
				else if (currentFCR >= 105) {
					currentFrames = 8;
					nextBreakpointFCR = std::to_string(200 - currentFCR);
				}
				else if (currentFCR >= 63) {
					currentFrames = 9;
					nextBreakpointFCR = std::to_string(105 - currentFCR);
				}
				else if (currentFCR >= 37) {
					currentFrames = 10;
					nextBreakpointFCR = std::to_string(63 - currentFCR);
				}
				else if (currentFCR >= 20) {
					currentFrames = 11;
					nextBreakpointFCR = std::to_string(37 - currentFCR);
				}
				else if (currentFCR >= 9) {
					currentFrames = 12;
					nextBreakpointFCR = std::to_string(20 - currentFCR);
				}
				else {
					currentFrames = 13;
					nextBreakpointFCR = std::to_string(9 - currentFCR);
				}
			}
			break;

		case 2: // Necromancer
			if (D2COMMON_GetUnitState(unit, 176)) { // Vampire form
				if (currentFCR >= 180) {
					currentFrames = 13;
					nextBreakpointFCR = "MAX";
				}
				else if (currentFCR >= 120) {
					currentFrames = 14;
					nextBreakpointFCR = std::to_string(180 - currentFCR);
				}
				else if (currentFCR >= 86) {
					currentFrames = 15;
					nextBreakpointFCR = std::to_string(120 - currentFCR);
				}
				else if (currentFCR >= 65) {
					currentFrames = 16;
					nextBreakpointFCR = std::to_string(86 - currentFCR);
				}
				else if (currentFCR >= 48) {
					currentFrames = 17;
					nextBreakpointFCR = std::to_string(65 - currentFCR);
				}
				else if (currentFCR >= 35) {
					currentFrames = 18;
					nextBreakpointFCR = std::to_string(48 - currentFCR);
				}
				else if (currentFCR >= 24) {
					currentFrames = 19;
					nextBreakpointFCR = std::to_string(35 - currentFCR);
				}
				else if (currentFCR >= 18) {
					currentFrames = 20;
					nextBreakpointFCR = std::to_string(24 - currentFCR);
				}
				else if (currentFCR >= 11) {
					currentFrames = 21;
					nextBreakpointFCR = std::to_string(18 - currentFCR);
				}
				else if (currentFCR >= 6) {
					currentFrames = 22;
					nextBreakpointFCR = std::to_string(11 - currentFCR);
				}
				else {
					currentFrames = 23;
					nextBreakpointFCR = std::to_string(6 - currentFCR);
				}
			}
			else { // Human form (default)
				if (currentFCR >= 125) {
					currentFrames = 9;
					nextBreakpointFCR = "MAX";
				}
				else if (currentFCR >= 75) {
					currentFrames = 10;
					nextBreakpointFCR = std::to_string(125 - currentFCR);
				}
				else if (currentFCR >= 48) {
					currentFrames = 11;
					nextBreakpointFCR = std::to_string(75 - currentFCR);
				}
				else if (currentFCR >= 30) {
					currentFrames = 12;
					nextBreakpointFCR = std::to_string(48 - currentFCR);
				}
				else if (currentFCR >= 18) {
					currentFrames = 13;
					nextBreakpointFCR = std::to_string(30 - currentFCR);
				}
				else if (currentFCR >= 9) {
					currentFrames = 14;
					nextBreakpointFCR = std::to_string(18 - currentFCR);
				}
				else {
					currentFrames = 15;
					nextBreakpointFCR = std::to_string(9 - currentFCR);
				}
			}
			break;
		case 3: // Paladin
			if (currentFCR >= 125) {
				currentFrames = 9;
				nextBreakpointFCR = "MAX";
			}
			else if (currentFCR >= 75) {
				currentFrames = 10;
				nextBreakpointFCR = std::to_string(125 - currentFCR);
			}
			else if (currentFCR >= 48) {
				currentFrames = 11;
				nextBreakpointFCR = std::to_string(75 - currentFCR);
			}
			else if (currentFCR >= 30) {
				currentFrames = 12;
				nextBreakpointFCR = std::to_string(48 - currentFCR);
			}
			else if (currentFCR >= 18) {
				currentFrames = 13;
				nextBreakpointFCR = std::to_string(30 - currentFCR);
			}
			else if (currentFCR >= 9) {
				currentFrames = 14;
				nextBreakpointFCR = std::to_string(18 - currentFCR);
			}
			else {
				currentFrames = 15;
				nextBreakpointFCR = std::to_string(9 - currentFCR);
			}
			break;
		case 4: // Barbarian
			if (currentFCR >= 200) {
				currentFrames = 7;
				nextBreakpointFCR = "MAX";
			}
			else if (currentFCR >= 105) {
				currentFrames = 8;
				nextBreakpointFCR = std::to_string(200 - currentFCR);
			}
			else if (currentFCR >= 63) {
				currentFrames = 9;
				nextBreakpointFCR = std::to_string(105 - currentFCR);
			}
			else if (currentFCR >= 37) {
				currentFrames = 10;
				nextBreakpointFCR = std::to_string(63 - currentFCR);
			}
			else if (currentFCR >= 20) {
				currentFrames = 11;
				nextBreakpointFCR = std::to_string(37 - currentFCR);
			}
			else if (currentFCR >= 9) {
				currentFrames = 12;
				nextBreakpointFCR = std::to_string(20 - currentFCR);
			}
			else {
				currentFrames = 13;
				nextBreakpointFCR = std::to_string(9 - currentFCR);
			}
			break;
		case 5: // Druid
			if (D2COMMON_GetUnitState(unit, 139)) { // Werewolf form
				if (currentFCR >= 157) {
					currentFrames = 9;
					nextBreakpointFCR = "MAX";
				}
				else if (currentFCR >= 95) {
					currentFrames = 10;
					nextBreakpointFCR = std::to_string(157 - currentFCR);
				}
				else if (currentFCR >= 60) {
					currentFrames = 11;
					nextBreakpointFCR = std::to_string(95 - currentFCR);
				}
				else if (currentFCR >= 40) {
					currentFrames = 12;
					nextBreakpointFCR = std::to_string(60 - currentFCR);
				}
				else if (currentFCR >= 26) {
					currentFrames = 13;
					nextBreakpointFCR = std::to_string(40 - currentFCR);
				}
				else if (currentFCR >= 14) {
					currentFrames = 14;
					nextBreakpointFCR = std::to_string(26 - currentFCR);
				}
				else if (currentFCR >= 6) {
					currentFrames = 15;
					nextBreakpointFCR = std::to_string(14 - currentFCR);
				}
				else {
					currentFrames = 16;
					nextBreakpointFCR = std::to_string(6 - currentFCR);
				}
			}
			else if (D2COMMON_GetUnitState(unit, 140)) { // Werebear form
				if (currentFCR >= 163) {
					currentFrames = 9;
					nextBreakpointFCR = "MAX";
				}
				else if (currentFCR >= 99) {
					currentFrames = 10;
					nextBreakpointFCR = std::to_string(163 - currentFCR);
				}
				else if (currentFCR >= 63) {
					currentFrames = 11;
					nextBreakpointFCR = std::to_string(99 - currentFCR);
				}
				else if (currentFCR >= 40) {
					currentFrames = 12;
					nextBreakpointFCR = std::to_string(63 - currentFCR);
				}
				else if (currentFCR >= 26) {
					currentFrames = 13;
					nextBreakpointFCR = std::to_string(40 - currentFCR);
				}
				else if (currentFCR >= 15) {
					currentFrames = 14;
					nextBreakpointFCR = std::to_string(26 - currentFCR);
				}
				else if (currentFCR >= 7) {
					currentFrames = 15;
					nextBreakpointFCR = std::to_string(15 - currentFCR);
				}
				else {
					currentFrames = 16;
					nextBreakpointFCR = std::to_string(7 - currentFCR);
				}
			}
			else { // Human form (default)
				if (currentFCR >= 163) {
					currentFrames = 10;
					nextBreakpointFCR = "MAX";
				}
				else if (currentFCR >= 99) {
					currentFrames = 11;
					nextBreakpointFCR = std::to_string(163 - currentFCR);
				}
				else if (currentFCR >= 68) {
					currentFrames = 12;
					nextBreakpointFCR = std::to_string(99 - currentFCR);
				}
				else if (currentFCR >= 46) {
					currentFrames = 13;
					nextBreakpointFCR = std::to_string(68 - currentFCR);
				}
				else if (currentFCR >= 30) {
					currentFrames = 14;
					nextBreakpointFCR = std::to_string(46 - currentFCR);
				}
				else if (currentFCR >= 19) {
					currentFrames = 15;
					nextBreakpointFCR = std::to_string(30 - currentFCR);
				}
				else if (currentFCR >= 10) {
					currentFrames = 16;
					nextBreakpointFCR = std::to_string(19 - currentFCR);
				}
				else if (currentFCR >= 4) {
					currentFrames = 17;
					nextBreakpointFCR = std::to_string(10 - currentFCR);
				}
				else {
					currentFrames = 18;
					nextBreakpointFCR = std::to_string(4 - currentFCR);
				}
			}
			break;

		case 6: // Assassin
			if (currentFCR >= 174) {
				currentFrames = 9;
				nextBreakpointFCR = "MAX";
			}
			else if (currentFCR >= 102) {
				currentFrames = 10;
				nextBreakpointFCR = std::to_string(174 - currentFCR);
			}
			else if (currentFCR >= 65) {
				currentFrames = 11;
				nextBreakpointFCR = std::to_string(102 - currentFCR);
			}
			else if (currentFCR >= 42) {
				currentFrames = 12;
				nextBreakpointFCR = std::to_string(65 - currentFCR);
			}
			else if (currentFCR >= 27) {
				currentFrames = 13;
				nextBreakpointFCR = std::to_string(42 - currentFCR);
			}
			else if (currentFCR >= 16) {
				currentFrames = 14;
				nextBreakpointFCR = std::to_string(27 - currentFCR);
			}
			else if (currentFCR >= 8) {
				currentFrames = 15;
				nextBreakpointFCR = std::to_string(16 - currentFCR);
			}
			else {
				currentFrames = 16;
				nextBreakpointFCR = std::to_string(8 - currentFCR);
			}
			break;
		}

		// Now you can use currentFrames in the subsequent code block

		// Now you can use currentFCR and nextBreakpointFCR in your text drawing function
		Texthook::Draw(column1, (y += 8), None, 6, Gold,
			L"Breakpoints");
		Texthook::Draw(column1, (y += 16), None, 6, Gold,
			L"FCR:\377c0 %d%% (Frames:%d Next BP:+%S%%)",
			currentFCR, currentFrames, nextBreakpointFCR.c_str());
		Texthook::Draw(column1, y += 16, None, 6, Gold,
			L"Block Rate:\377c0 %d",
			(int)D2COMMON_GetUnitStat(unit, STAT_FASTERBLOCK, 0)
		);
		Texthook::Draw(column1, (y += 16), None, 6, Gold,
			L"Hit Recovery:\377c0 %d",
			(int)D2COMMON_GetUnitStat(unit, STAT_FASTERHITRECOVERY, 0)
		);
		Texthook::Draw(column1, y += 16, None, 6, Gold,
			L"Run/Walk:\377c0 %d",
			(int)D2COMMON_GetUnitStat(unit, STAT_FASTERRUNWALK, 0)
		);
		Texthook::Draw(column1, (y += 16), None, 6, Gold,
			L"Attack Rate:\377c0 %d",
			(int)D2COMMON_GetUnitStat(unit, STAT_ATTACKRATE, 0));
		Texthook::Draw(column1, y += 16, None, 6, Gold,
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

