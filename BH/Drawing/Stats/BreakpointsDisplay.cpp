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
		int currentFCRFrames = 0; 
		std::string nextBreakpointFCR;
		int currentFHR = (int)D2COMMON_GetUnitStat(unit, STAT_FASTERHITRECOVERY, 0);
		int currentFHRFrames = 0;
		std::string nextBreakpointFHR;

		// Calculate current frames for each character class
		switch (pData->nCharClass) {
		case 0: // Amazon
			if (currentFCR >= 152) {
				currentFCRFrames = 11;
				nextBreakpointFCR = "MAX";
			}
			else if (currentFCR >= 99) {
				currentFCRFrames = 12;
				nextBreakpointFCR = std::to_string(152 - currentFCR);
			}
			else if (currentFCR >= 68) {
				currentFCRFrames = 13;
				nextBreakpointFCR = std::to_string(99 - currentFCR);
			}
			else if (currentFCR >= 48) {
				currentFCRFrames = 14;
				nextBreakpointFCR = std::to_string(68 - currentFCR);
			}
			else if (currentFCR >= 32) {
				currentFCRFrames = 15;
				nextBreakpointFCR = std::to_string(48 - currentFCR);
			}
			else if (currentFCR >= 22) {
				currentFCRFrames = 16;
				nextBreakpointFCR = std::to_string(32 - currentFCR);
			}
			else if (currentFCR >= 14) {
				currentFCRFrames = 17;
				nextBreakpointFCR = std::to_string(22 - currentFCR);
			}
			else if (currentFCR >= 7) {
				currentFCRFrames = 18;
				nextBreakpointFCR = std::to_string(14 - currentFCR);
			}
			else {
				currentFCRFrames = 19;
				nextBreakpointFCR = std::to_string(7 - currentFCR);
			}

			if (currentFHR >= 600) {
				currentFHRFrames = 3;
				nextBreakpointFHR = "MAX";
			}
			else if (currentFHR >= 174) {
				currentFHRFrames = 4;
				nextBreakpointFHR = std::to_string(600 - currentFHR);
			}
			else if (currentFHR >= 86) {
				currentFHRFrames = 5;
				nextBreakpointFHR = std::to_string(174 - currentFHR);
			}
			else if (currentFHR >= 52) {
				currentFHRFrames = 6;
				nextBreakpointFHR = std::to_string(86 - currentFHR);
			}
			else if (currentFHR >= 32) {
				currentFHRFrames = 7;
				nextBreakpointFHR = std::to_string(52 - currentFHR);
			}
			else if (currentFHR >= 20) {
				currentFHRFrames = 8;
				nextBreakpointFHR = std::to_string(32 - currentFHR);
			}
			else if (currentFHR >= 13) {
				currentFHRFrames = 9;
				nextBreakpointFHR = std::to_string(20 - currentFHR);
			}
			else if (currentFHR >= 6) {
				currentFHRFrames = 10;
				nextBreakpointFHR = std::to_string(13 - currentFHR);
			}
			else {
				currentFHRFrames = 11;
				nextBreakpointFHR = std::to_string(6 - currentFHR);
			}
			break;
		case 1: // Sorceress
			if (D2CLIENT_GetPlayerUnit()->pInfo->pRightSkill->pSkillInfo->wSkillId == 0x35 ||
				D2CLIENT_GetPlayerUnit()->pInfo->pLeftSkill->pSkillInfo->wSkillId == 0x35 ||
				D2CLIENT_GetPlayerUnit()->pInfo->pRightSkill->pSkillInfo->wSkillId == 0x31 ||
				D2CLIENT_GetPlayerUnit()->pInfo->pLeftSkill->pSkillInfo->wSkillId == 0x31) {
				if (currentFCR >= 194) {
					currentFCRFrames = 11;
					nextBreakpointFCR = "MAX";
				}
				else if (currentFCR >= 117) {
					currentFCRFrames = 12;
					nextBreakpointFCR = std::to_string(194 - currentFCR);
				}
				else if (currentFCR >= 78) {
					currentFCRFrames = 13;
					nextBreakpointFCR = std::to_string(117 - currentFCR);
				}
				else if (currentFCR >= 52) {
					currentFCRFrames = 14;
					nextBreakpointFCR = std::to_string(78 - currentFCR);
				}
				else if (currentFCR >= 35) {
					currentFCRFrames = 15;
					nextBreakpointFCR = std::to_string(52 - currentFCR);
				}
				else if (currentFCR >= 23) {
					currentFCRFrames = 16;
					nextBreakpointFCR = std::to_string(35 - currentFCR);
				}
				else if (currentFCR >= 15) {
					currentFCRFrames = 17;
					nextBreakpointFCR = std::to_string(23 - currentFCR);
				}
				else if (currentFCR >= 7) {
					currentFCRFrames = 18;
					nextBreakpointFCR = std::to_string(15 - currentFCR);
				}
				else {
					currentFCRFrames = 19;
					nextBreakpointFCR = std::to_string(9 - currentFCR);
				}
			}
			else {
				if (currentFCR >= 200) {
					currentFCRFrames = 7;
					nextBreakpointFCR = "MAX";
				}
				else if (currentFCR >= 105) {
					currentFCRFrames = 8;
					nextBreakpointFCR = std::to_string(200 - currentFCR);
				}
				else if (currentFCR >= 63) {
					currentFCRFrames = 9;
					nextBreakpointFCR = std::to_string(105 - currentFCR);
				}
				else if (currentFCR >= 37) {
					currentFCRFrames = 10;
					nextBreakpointFCR = std::to_string(63 - currentFCR);
				}
				else if (currentFCR >= 20) {
					currentFCRFrames = 11;
					nextBreakpointFCR = std::to_string(37 - currentFCR);
				}
				else if (currentFCR >= 9) {
					currentFCRFrames = 12;
					nextBreakpointFCR = std::to_string(20 - currentFCR);
				}
				else {
					currentFCRFrames = 13;
					nextBreakpointFCR = std::to_string(9 - currentFCR);
				}
			}
			if (currentFHR >= 1480) {
				currentFHRFrames = 4;
				nextBreakpointFHR = "MAX";
			}
			else if (currentFHR >= 280) {
				currentFHRFrames = 5;
				nextBreakpointFHR = std::to_string(1480 - currentFHR);
			}
			else if (currentFHR >= 142) {
				currentFHRFrames = 6;
				nextBreakpointFHR = std::to_string(280 - currentFHR);
			}
			else if (currentFHR >= 86) {
				currentFHRFrames = 7;
				nextBreakpointFHR = std::to_string(142 - currentFHR);
			}
			else if (currentFHR >= 60) {
				currentFHRFrames = 8;
				nextBreakpointFHR = std::to_string(86 - currentFHR);
			}
			else if (currentFHR >= 42) {
				currentFHRFrames = 9;
				nextBreakpointFHR = std::to_string(60 - currentFHR);
			}
			else if (currentFHR >= 30) {
				currentFHRFrames = 10;
				nextBreakpointFHR = std::to_string(42 - currentFHR);
			}
			else if (currentFHR >= 20) {
				currentFHRFrames = 11;
				nextBreakpointFHR = std::to_string(30 - currentFHR);
			}
			else if (currentFHR >= 14) {
				currentFHRFrames = 12;
				nextBreakpointFHR = std::to_string(20 - currentFHR);
			}
			else if (currentFHR >= 9) {
				currentFHRFrames = 13;
				nextBreakpointFHR = std::to_string(14 - currentFHR);
			}
			else if (currentFHR >= 5) {
				currentFHRFrames = 14;
				nextBreakpointFHR = std::to_string(9 - currentFHR);
			}
			else {
				currentFHRFrames = 15;
				nextBreakpointFHR = std::to_string(5 - currentFHR);
			}

			break;

		case 2: // Necromancer
			if (D2COMMON_GetUnitState(unit, 176)) { // Vampire form
				if (currentFCR >= 180) {
					currentFCRFrames = 13;
					nextBreakpointFCR = "MAX";
				}
				else if (currentFCR >= 120) {
					currentFCRFrames = 14;
					nextBreakpointFCR = std::to_string(180 - currentFCR);
				}
				else if (currentFCR >= 86) {
					currentFCRFrames = 15;
					nextBreakpointFCR = std::to_string(120 - currentFCR);
				}
				else if (currentFCR >= 65) {
					currentFCRFrames = 16;
					nextBreakpointFCR = std::to_string(86 - currentFCR);
				}
				else if (currentFCR >= 48) {
					currentFCRFrames = 17;
					nextBreakpointFCR = std::to_string(65 - currentFCR);
				}
				else if (currentFCR >= 35) {
					currentFCRFrames = 18;
					nextBreakpointFCR = std::to_string(48 - currentFCR);
				}
				else if (currentFCR >= 24) {
					currentFCRFrames = 19;
					nextBreakpointFCR = std::to_string(35 - currentFCR);
				}
				else if (currentFCR >= 18) {
					currentFCRFrames = 20;
					nextBreakpointFCR = std::to_string(24 - currentFCR);
				}
				else if (currentFCR >= 11) {
					currentFCRFrames = 21;
					nextBreakpointFCR = std::to_string(18 - currentFCR);
				}
				else if (currentFCR >= 6) {
					currentFCRFrames = 22;
					nextBreakpointFCR = std::to_string(11 - currentFCR);
				}
				else {
					currentFCRFrames = 23;
					nextBreakpointFCR = std::to_string(6 - currentFCR);
				}
			}
			else { // Human form (default)
				if (currentFCR >= 125) {
					currentFCRFrames = 9;
					nextBreakpointFCR = "MAX";
				}
				else if (currentFCR >= 75) {
					currentFCRFrames = 10;
					nextBreakpointFCR = std::to_string(125 - currentFCR);
				}
				else if (currentFCR >= 48) {
					currentFCRFrames = 11;
					nextBreakpointFCR = std::to_string(75 - currentFCR);
				}
				else if (currentFCR >= 30) {
					currentFCRFrames = 12;
					nextBreakpointFCR = std::to_string(48 - currentFCR);
				}
				else if (currentFCR >= 18) {
					currentFCRFrames = 13;
					nextBreakpointFCR = std::to_string(30 - currentFCR);
				}
				else if (currentFCR >= 9) {
					currentFCRFrames = 14;
					nextBreakpointFCR = std::to_string(18 - currentFCR);
				}
				else {
					currentFCRFrames = 15;
					nextBreakpointFCR = std::to_string(9 - currentFCR);
				}
			}
			if (currentFHR >= 377) {
				currentFHRFrames = 4;
				nextBreakpointFHR = "MAX";
			}
			else if (currentFHR >= 152) {
				currentFHRFrames = 5;
				nextBreakpointFHR = std::to_string(377 - currentFHR);
			}
			else if (currentFHR >= 86) {
				currentFHRFrames = 6;
				nextBreakpointFHR = std::to_string(152 - currentFHR);
			}
			else if (currentFHR >= 56) {
				currentFHRFrames = 7;
				nextBreakpointFHR = std::to_string(86 - currentFHR);
			}
			else if (currentFHR >= 39) {
				currentFHRFrames = 8;
				nextBreakpointFHR = std::to_string(56 - currentFHR);
			}
			else if (currentFHR >= 26) {
				currentFHRFrames = 9;
				nextBreakpointFHR = std::to_string(39 - currentFHR);
			}
			else if (currentFHR >= 16) {
				currentFHRFrames = 10;
				nextBreakpointFHR = std::to_string(26 - currentFHR);
			}
			else if (currentFHR >= 10) {
				currentFHRFrames = 11;
				nextBreakpointFHR = std::to_string(16 - currentFHR);
			}
			else if (currentFHR >= 5) {
				currentFHRFrames = 12;
				nextBreakpointFHR = std::to_string(10 - currentFHR);
			}
			else {
				currentFHRFrames = 13;
				nextBreakpointFHR = std::to_string(5 - currentFHR);
			}

			break;
		case 3: // Paladin
			if (currentFCR >= 125) {
				currentFCRFrames = 9;
				nextBreakpointFCR = "MAX";
			}
			else if (currentFCR >= 75) {
				currentFCRFrames = 10;
				nextBreakpointFCR = std::to_string(125 - currentFCR);
			}
			else if (currentFCR >= 48) {
				currentFCRFrames = 11;
				nextBreakpointFCR = std::to_string(75 - currentFCR);
			}
			else if (currentFCR >= 30) {
				currentFCRFrames = 12;
				nextBreakpointFCR = std::to_string(48 - currentFCR);
			}
			else if (currentFCR >= 18) {
				currentFCRFrames = 13;
				nextBreakpointFCR = std::to_string(30 - currentFCR);
			}
			else if (currentFCR >= 9) {
				currentFCRFrames = 14;
				nextBreakpointFCR = std::to_string(18 - currentFCR);
			}
			else {
				currentFCRFrames = 15;
				nextBreakpointFCR = std::to_string(9 - currentFCR);
			}
			if (currentFHR >= 4680) {
				currentFHRFrames = 2;
				nextBreakpointFHR = "MAX";
			}
			else if (currentFHR >= 200) {
				currentFHRFrames = 3;
				nextBreakpointFHR = std::to_string(4680 - currentFHR);
			}
			else if (currentFHR >= 86) {
				currentFHRFrames = 4;
				nextBreakpointFHR = std::to_string(200 - currentFHR);
			}
			else if (currentFHR >= 48) {
				currentFHRFrames = 5;
				nextBreakpointFHR = std::to_string(86 - currentFHR);
			}
			else if (currentFHR >= 27) {
				currentFHRFrames = 6;
				nextBreakpointFHR = std::to_string(48 - currentFHR);
			}
			else if (currentFHR >= 15) {
				currentFHRFrames = 7;
				nextBreakpointFHR = std::to_string(27 - currentFHR);
			}
			else if (currentFHR >= 7) {
				currentFHRFrames = 8;
				nextBreakpointFHR = std::to_string(15 - currentFHR);
			}
			else {
				currentFHRFrames = 9;
				nextBreakpointFHR = std::to_string(7 - currentFHR);
			}

			break;
		case 4: // Barbarian
			if (currentFCR >= 200) {
				currentFCRFrames = 7;
				nextBreakpointFCR = "MAX";
			}
			else if (currentFCR >= 105) {
				currentFCRFrames = 8;
				nextBreakpointFCR = std::to_string(200 - currentFCR);
			}
			else if (currentFCR >= 63) {
				currentFCRFrames = 9;
				nextBreakpointFCR = std::to_string(105 - currentFCR);
			}
			else if (currentFCR >= 37) {
				currentFCRFrames = 10;
				nextBreakpointFCR = std::to_string(63 - currentFCR);
			}
			else if (currentFCR >= 20) {
				currentFCRFrames = 11;
				nextBreakpointFCR = std::to_string(37 - currentFCR);
			}
			else if (currentFCR >= 9) {
				currentFCRFrames = 12;
				nextBreakpointFCR = std::to_string(20 - currentFCR);
			}
			else {
				currentFCRFrames = 13;
				nextBreakpointFCR = std::to_string(9 - currentFCR);
			}
			if (currentFHR >= 4680) {
				currentFHRFrames = 2;
				nextBreakpointFHR = "MAX";
			}
			else if (currentFHR >= 200) {
				currentFHRFrames = 3;
				nextBreakpointFHR = std::to_string(4680 - currentFHR);
			}
			else if (currentFHR >= 86) {
				currentFHRFrames = 4;
				nextBreakpointFHR = std::to_string(200 - currentFHR);
			}
			else if (currentFHR >= 48) {
				currentFHRFrames = 5;
				nextBreakpointFHR = std::to_string(86 - currentFHR);
			}
			else if (currentFHR >= 27) {
				currentFHRFrames = 6;
				nextBreakpointFHR = std::to_string(48 - currentFHR);
			}
			else if (currentFHR >= 15) {
				currentFHRFrames = 7;
				nextBreakpointFHR = std::to_string(27 - currentFHR);
			}
			else if (currentFHR >= 7) {
				currentFHRFrames = 8;
				nextBreakpointFHR = std::to_string(15 - currentFHR);
			}
			else {
				currentFHRFrames = 9;
				nextBreakpointFHR = std::to_string(7 - currentFHR);
			}
			break;
		case 5: // Druid
			if (D2COMMON_GetUnitState(unit, 139)) { // Werewolf form
				if (currentFCR >= 157) {
					currentFCRFrames = 9;
					nextBreakpointFCR = "MAX";
				}
				else if (currentFCR >= 95) {
					currentFCRFrames = 10;
					nextBreakpointFCR = std::to_string(157 - currentFCR);
				}
				else if (currentFCR >= 60) {
					currentFCRFrames = 11;
					nextBreakpointFCR = std::to_string(95 - currentFCR);
				}
				else if (currentFCR >= 40) {
					currentFCRFrames = 12;
					nextBreakpointFCR = std::to_string(60 - currentFCR);
				}
				else if (currentFCR >= 26) {
					currentFCRFrames = 13;
					nextBreakpointFCR = std::to_string(40 - currentFCR);
				}
				else if (currentFCR >= 14) {
					currentFCRFrames = 14;
					nextBreakpointFCR = std::to_string(26 - currentFCR);
				}
				else if (currentFCR >= 6) {
					currentFCRFrames = 15;
					nextBreakpointFCR = std::to_string(14 - currentFCR);
				}
				else {
					currentFCRFrames = 16;
					nextBreakpointFCR = std::to_string(6 - currentFCR);
				}
				if (currentFHR >= 280) {
					currentFHRFrames = 2;
					nextBreakpointFHR = "MAX";
				}
				else if (currentFHR >= 86) {
					currentFHRFrames = 3;
					nextBreakpointFHR = std::to_string(280 - currentFHR);
				}
				else if (currentFHR >= 42) {
					currentFHRFrames = 4;
					nextBreakpointFHR = std::to_string(86 - currentFHR);
				}
				else if (currentFHR >= 20) {
					currentFHRFrames = 5;
					nextBreakpointFHR = std::to_string(42 - currentFHR);
				}
				else if (currentFHR >= 9) {
					currentFHRFrames = 6;
					nextBreakpointFHR = std::to_string(20 - currentFHR);
				}
				else {
					currentFHRFrames = 7;
					nextBreakpointFHR = std::to_string(9 - currentFHR);
				}
			}
			else if (D2COMMON_GetUnitState(unit, 140)) { // Werebear form
				if (currentFCR >= 163) {
					currentFCRFrames = 9;
					nextBreakpointFCR = "MAX";
				}
				else if (currentFCR >= 99) {
					currentFCRFrames = 10;
					nextBreakpointFCR = std::to_string(163 - currentFCR);
				}
				else if (currentFCR >= 63) {
					currentFCRFrames = 11;
					nextBreakpointFCR = std::to_string(99 - currentFCR);
				}
				else if (currentFCR >= 40) {
					currentFCRFrames = 12;
					nextBreakpointFCR = std::to_string(63 - currentFCR);
				}
				else if (currentFCR >= 26) {
					currentFCRFrames = 13;
					nextBreakpointFCR = std::to_string(40 - currentFCR);
				}
				else if (currentFCR >= 15) {
					currentFCRFrames = 14;
					nextBreakpointFCR = std::to_string(26 - currentFCR);
				}
				else if (currentFCR >= 7) {
					currentFCRFrames = 15;
					nextBreakpointFCR = std::to_string(15 - currentFCR);
				}
				else {
					currentFCRFrames = 16;
					nextBreakpointFCR = std::to_string(7 - currentFCR);
				}
				if (currentFHR >= 360) {
					currentFHRFrames = 4;
					nextBreakpointFHR = "MAX";
				}
				else if (currentFHR >= 152) {
					currentFHRFrames = 5;
					nextBreakpointFHR = std::to_string(360 - currentFHR);
				}
				else if (currentFHR >= 86) {
					currentFHRFrames = 6;
					nextBreakpointFHR = std::to_string(152 - currentFHR);
				}
				else if (currentFHR >= 54) {
					currentFHRFrames = 7;
					nextBreakpointFHR = std::to_string(86 - currentFHR);
				}
				else if (currentFHR >= 37) {
					currentFHRFrames = 8;
					nextBreakpointFHR = std::to_string(54 - currentFHR);
				}
				else if (currentFHR >= 24) {
					currentFHRFrames = 9;
					nextBreakpointFHR = std::to_string(37 - currentFHR);
				}
				else if (currentFHR >= 16) {
					currentFHRFrames = 10;
					nextBreakpointFHR = std::to_string(24 - currentFHR);
				}
				else if (currentFHR >= 10) {
					currentFHRFrames = 11;
					nextBreakpointFHR = std::to_string(16 - currentFHR);
				}
				else if (currentFHR >= 5) {
					currentFHRFrames = 12;
					nextBreakpointFHR = std::to_string(10 - currentFHR);
				}
				else {
					currentFHRFrames = 13;
					nextBreakpointFHR = std::to_string(5 - currentFHR);
				}
			}
			else { // Human form (default)
				if (currentFCR >= 163) {
					currentFCRFrames = 10;
					nextBreakpointFCR = "MAX";
				}
				else if (currentFCR >= 99) {
					currentFCRFrames = 11;
					nextBreakpointFCR = std::to_string(163 - currentFCR);
				}
				else if (currentFCR >= 68) {
					currentFCRFrames = 12;
					nextBreakpointFCR = std::to_string(99 - currentFCR);
				}
				else if (currentFCR >= 46) {
					currentFCRFrames = 13;
					nextBreakpointFCR = std::to_string(68 - currentFCR);
				}
				else if (currentFCR >= 30) {
					currentFCRFrames = 14;
					nextBreakpointFCR = std::to_string(46 - currentFCR);
				}
				else if (currentFCR >= 19) {
					currentFCRFrames = 15;
					nextBreakpointFCR = std::to_string(30 - currentFCR);
				}
				else if (currentFCR >= 10) {
					currentFCRFrames = 16;
					nextBreakpointFCR = std::to_string(19 - currentFCR);
				}
				else if (currentFCR >= 4) {
					currentFCRFrames = 17;
					nextBreakpointFCR = std::to_string(10 - currentFCR);
				}
				else {
					currentFCRFrames = 18;
					nextBreakpointFCR = std::to_string(4 - currentFCR);
				}
				if (currentFHR >= 456) {
					currentFHRFrames = 4;
					nextBreakpointFHR = "MAX";
				}
				else if (currentFHR >= 174) {
					currentFHRFrames = 5;
					nextBreakpointFHR = std::to_string(456 - currentFHR);
				}
				else if (currentFHR >= 99) {
					currentFHRFrames = 6;
					nextBreakpointFHR = std::to_string(174 - currentFHR);
				}
				else if (currentFHR >= 63) {
					currentFHRFrames = 7;
					nextBreakpointFHR = std::to_string(99 - currentFHR);
				}
				else if (currentFHR >= 42) {
					currentFHRFrames = 8;
					nextBreakpointFHR = std::to_string(63 - currentFHR);
				}
				else if (currentFHR >= 29) {
					currentFHRFrames = 9;
					nextBreakpointFHR = std::to_string(42 - currentFHR);
				}
				else if (currentFHR >= 19) {
					currentFHRFrames = 10;
					nextBreakpointFHR = std::to_string(29 - currentFHR);
				}
				else if (currentFHR >= 13) {
					currentFHRFrames = 11;
					nextBreakpointFHR = std::to_string(19 - currentFHR);
				}
				else if (currentFHR >= 7) {
					currentFHRFrames = 12;
					nextBreakpointFHR = std::to_string(13 - currentFHR);
				}
				else if (currentFHR >= 3) {
					currentFHRFrames = 13;
					nextBreakpointFHR = std::to_string(7 - currentFHR);
				}
				else {
					currentFHRFrames = 14;
					nextBreakpointFHR = std::to_string(3 - currentFHR);
				}

			}
			break;

		case 6: // Assassin
			if (currentFCR >= 174) {
				currentFCRFrames = 9;
				nextBreakpointFCR = "MAX";
			}
			else if (currentFCR >= 102) {
				currentFCRFrames = 10;
				nextBreakpointFCR = std::to_string(174 - currentFCR);
			}
			else if (currentFCR >= 65) {
				currentFCRFrames = 11;
				nextBreakpointFCR = std::to_string(102 - currentFCR);
			}
			else if (currentFCR >= 42) {
				currentFCRFrames = 12;
				nextBreakpointFCR = std::to_string(65 - currentFCR);
			}
			else if (currentFCR >= 27) {
				currentFCRFrames = 13;
				nextBreakpointFCR = std::to_string(42 - currentFCR);
			}
			else if (currentFCR >= 16) {
				currentFCRFrames = 14;
				nextBreakpointFCR = std::to_string(27 - currentFCR);
			}
			else if (currentFCR >= 8) {
				currentFCRFrames = 15;
				nextBreakpointFCR = std::to_string(16 - currentFCR);
			}
			else {
				currentFCRFrames = 16;
				nextBreakpointFCR = std::to_string(8 - currentFCR);
			}
			if (currentFHR >= 4680) {
				currentFHRFrames = 2;
				nextBreakpointFHR = "MAX";
			}
			else if (currentFHR >= 200) {
				currentFHRFrames = 3;
				nextBreakpointFHR = std::to_string(4680 - currentFHR);
			}
			else if (currentFHR >= 86) {
				currentFHRFrames = 4;
				nextBreakpointFHR = std::to_string(200 - currentFHR);
			}
			else if (currentFHR >= 48) {
				currentFHRFrames = 5;
				nextBreakpointFHR = std::to_string(86 - currentFHR);
			}
			else if (currentFHR >= 27) {
				currentFHRFrames = 6;
				nextBreakpointFHR = std::to_string(48 - currentFHR);
			}
			else if (currentFHR >= 15) {
				currentFHRFrames = 7;
				nextBreakpointFHR = std::to_string(27 - currentFHR);
			}
			else if (currentFHR >= 7) {
				currentFHRFrames = 8;
				nextBreakpointFHR = std::to_string(15 - currentFHR);
			}
			else {
				currentFHRFrames = 9;
				nextBreakpointFHR = std::to_string(7 - currentFHR);
			}
			break;
		}

		// Now you can use currentFCRFrames in the subsequent code block

		// Now you can use currentFCR and nextBreakpointFCR in your text drawing function
		Texthook::Draw(column1, (y += 8), None, 6, Gold,
			L"Breakpoints");
		Texthook::Draw(column1, (y += 16), None, 6, Gold,
			L"FCR:\377c0 %d%% (Frames:%d Next BP:%s%S%s)",
			currentFCR, currentFCRFrames,
			(nextBreakpointFCR == "MAX" ? L"" : L"+"),
			nextBreakpointFCR.c_str(),
			(nextBreakpointFCR == "MAX" ? L"" : L"%"));
		Texthook::Draw(column1, y += 16, None, 6, Gold,
			L"Block Rate:\377c0 %d",
			(int)D2COMMON_GetUnitStat(unit, STAT_FASTERBLOCK, 0)
		);
		Texthook::Draw(column1, (y += 16), None, 6, Gold,
			L"FHR:\377c0 %d%% (Frames:%d Next BP:%s%S%s)",
			currentFHR, currentFHRFrames,
			(nextBreakpointFHR == "MAX" ? L"" : L"+"),
			nextBreakpointFHR.c_str(),
			(nextBreakpointFHR == "MAX" ? L"" : L"%"));
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

