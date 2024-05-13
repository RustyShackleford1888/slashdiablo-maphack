#include "StatsDisplay.h"
#include "../Basic/Texthook/Texthook.h"
#include "../Basic/Framehook/Framehook.h"
#include "../Basic/Boxhook/Boxhook.h"
#include "../../D2Ptrs.h"
#include "../../BH.h"

using namespace Drawing;


StatsDisplay *StatsDisplay::display;


StatsDisplay::StatsDisplay(std::string name) {
	int yPos = 10;
	int width = 240;

	InitializeCriticalSection(&crit);
	SetY(yPos);
	SetXSize(width);

	LoadConfig();

	SetName(name);
	SetActive(true);
	SetMinimized(true);

	BH::config->ReadKey("Character Stats", "VK_8", statsKey);
	display = this;
}

StatsDisplay::~StatsDisplay() {
	Lock();
	// Remove all hooks associated with the display
	while (Hooks.size() > 0) {
		delete (*Hooks.begin());
	}
	Unlock();
	DeleteCriticalSection(&crit);
}

void StatsDisplay::LoadConfig(){
	int height = 342 + 8 * 3 + 16 * 6;
	customStats.clear();

	BH::config->ReadToggle("Stats on Right", "None", false, Toggles["Stats on Right"]);

	vector<pair<string, string>> stats;
	BH::config->ReadMapList("Stat Screen", stats);
	for (unsigned int i = 0; i < stats.size(); i++) {
		std::transform(stats[i].first.begin(), stats[i].first.end(), stats[i].first.begin(), ::tolower);
		if (StatMap.count(stats[i].first) > 0) {
			StatProperties *sp = StatMap[stats[i].first];
			DisplayedStat *customStat = new DisplayedStat();
			customStat->name = stats[i].first;
			customStat->useValue = false;
			std::transform(customStat->name.begin(), customStat->name.end(), customStat->name.begin(), ::tolower);
			// Getting rid of the check for sp->saveParamBits > 0 to display weapon mastery values
			// if a param is supplied it will be used
			int num = -1;
			stringstream ss(Trim(stats[i].second));
			if (!(ss >> num).fail() && num > 0) {
				customStat->useValue = true;
				customStat->value = num;
			}
			customStats.push_back(customStat);
		}
	}
	if (customStats.size() > 0) {
		height += (customStats.size() * 16) + 8;
	}

	int xPos = Toggles["Stats on Right"].state ?
		*p_D2CLIENT_ScreenSizeX - 10 - GetXSize() : 10;
	SetX(xPos);
	SetYSize(height);
}

void StatsDisplay::SetX(unsigned int newX) {
	if (newX >= 0 && newX <= Hook::GetScreenWidth()) {
		Lock();
		x = newX;
		Unlock();
	}
}

void StatsDisplay::SetY(unsigned int newY) {
	if (newY >= 0 && newY <= Hook::GetScreenHeight()) {
		Lock();
		y = newY;
		Unlock();
	}
}

void StatsDisplay::SetXSize(unsigned int newXSize) {
	if (newXSize >= 0 && newXSize <= (Hook::GetScreenWidth() - GetX())) {
		Lock();
		xSize = newXSize;
		Unlock();
	}
}

void StatsDisplay::SetYSize(unsigned int newYSize) {
	if (newYSize >= 0 && newYSize <= (Hook::GetScreenHeight() - GetY())) {
		Lock();
		ySize = newYSize;
		Unlock();
	}
}

bool StatsDisplay::InRange(unsigned int x, unsigned int y) {
	return IsActive() &&
		x >= GetX() && y >= GetY() &&
		x <= GetX() + GetXSize() && y <= GetY() + GetYSize();
}

void StatsDisplay::Draw() {
	display->Lock();
	display->OnDraw();
	display->Unlock();
}

void StatsDisplay::OnDraw() {
	UnitAny *unit = D2CLIENT_GetPlayerUnit();
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
		for(std::list<Hook*>::iterator it = Hooks.begin(); it != Hooks.end(); it++)
			(*it)->OnDraw();

		int y = GetY();
		RECT pRect;
		pRect.left = GetX();
		pRect.top = y;
		pRect.right = x + GetXSize();
		pRect.bottom = y + GetYSize();

		Drawing::Boxhook::Draw(GetX(),GetY(), GetXSize(), GetYSize(), White, Drawing::BTBlack);
		Drawing::Framehook::DrawRectStub(&pRect);
	
		Texthook::Draw(column1, (y += 8), None, 6, Gold,
				"Name:\377c0 %s",
				isMerc ? "\377c;Mercenary" : unit->pPlayerData->szName);
		Texthook::Draw(pRect.right - 5, y, Right, 6, Gold,
				L"Level:\377c0 %d",
				(int)D2COMMON_GetUnitStat(unit, STAT_LEVEL, 0));
		Texthook::Draw(pRect.right - 5, y + 12, Right, 6, Gold,
				L"Additional XP:\377c: %d%%",
				(int)D2COMMON_GetUnitStat(unit, STAT_ADDEXPERIENCE, 0));

		y += 8;

		BnetData* pData = (*p_D2LAUNCH_BnData);
		int xPacMultiplier = pData->nCharFlags & PLAYER_TYPE_EXPANSION ? 2 : 1;
		int resPenalty[3] = { RES_PENALTY_CLS_NORM, RES_PENALTY_CLS_NM, RES_PENALTY_CLS_HELL };
		int penalty = resPenalty[D2CLIENT_GetDifficulty()] * xPacMultiplier;
		int fMax = (int)D2COMMON_GetUnitStat(unit, STAT_MAXFIRERESIST, 0) + 75;
		int cMax = (int)D2COMMON_GetUnitStat(unit, STAT_MAXCOLDRESIST, 0) + 75;
		int lMax = (int)D2COMMON_GetUnitStat(unit, STAT_MAXLIGHTNINGRESIST, 0) + 75;
		int pMax = (int)D2COMMON_GetUnitStat(unit, STAT_MAXPOISONRESIST, 0) + 75;
		int pLengthReduce = (int)D2COMMON_GetUnitStat(unit, STAT_POISONLENGTHREDUCTION, 0);

		Texthook::Draw(column1, (y += 16), None, 6, Red, L"\377c4Fire Resist:\377c1 %d \377c0/ %d", (int)D2COMMON_GetUnitStat(unit, STAT_FIRERESIST, 0) + penalty, fMax);
		Texthook::Draw(column1, (y += 16), None, 6, Blue, L"\377c4Cold Resist:\377c3 %d \377c0/ %d", (int)D2COMMON_GetUnitStat(unit, STAT_COLDRESIST, 0) + penalty, cMax);
		Texthook::Draw(column1, (y += 16), None, 6, Yellow, L"\377c4Lightning Resist:\377c9 %d \377c0/ %d", (int)D2COMMON_GetUnitStat(unit, STAT_LIGHTNINGRESIST, 0) + penalty, lMax);
		Texthook::Draw(column1, (y += 16), None, 6, Gold,
				L"Poison Resist:\377c2 %d \377c0/ %d  \377c4Length:\377c: %d%%",
				(int)D2COMMON_GetUnitStat(unit, STAT_POISONRESIST, 0) + penalty,
				pMax,
				(100 - penalty - pLengthReduce)
				);
		y += 8;

		int fAbsorb = (int)D2COMMON_GetUnitStat(unit, STAT_FIREABSORB, 0);
		int fAbsorbPct = (int)D2COMMON_GetUnitStat(unit, STAT_FIREABSORBPERCENT, 0);
		int cAbsorb = (int)D2COMMON_GetUnitStat(unit, STAT_COLDABSORB, 0);
		int cAbsorbPct = (int)D2COMMON_GetUnitStat(unit, STAT_COLDABSORBPERCENT, 0);
		int lAbsorb = (int)D2COMMON_GetUnitStat(unit, STAT_LIGHTNINGABSORB, 0);
		int lAbsorbPct = (int)D2COMMON_GetUnitStat(unit, STAT_LIGHTNINGABSORBPERCENT, 0);
		int mAbsorb = (int)D2COMMON_GetUnitStat(unit, STAT_MAGICABSORB, 0);
		int mAbsorbPct = (int)D2COMMON_GetUnitStat(unit, STAT_MAGICABSORBPERCENT, 0);
		Texthook::Draw(column1, (y += 16), None, 6, Red, L"\377c4Absorption: \377c1%d\377c0/\377c1%d%c \377c3%d\377c0/\377c3%d%c \377c9%d\377c0/\377c9%d%c \377c8%d\377c0/\377c8%d%c", fAbsorb, fAbsorbPct, '%', cAbsorb, cAbsorbPct, '%', lAbsorb, lAbsorbPct, '%', mAbsorb, mAbsorbPct, '%');

		int dmgReduction = (int)D2COMMON_GetUnitStat(unit, STAT_DMGREDUCTION, 0);
		int dmgReductionPct = (int)D2COMMON_GetUnitStat(unit, STAT_DMGREDUCTIONPCT, 0);
		int magReduction = (int)D2COMMON_GetUnitStat(unit, STAT_MAGICDMGREDUCTION, 0);
		int magReductionPct = (int)D2COMMON_GetUnitStat(unit, STAT_MAGICDMGREDUCTIONPCT, 0);
		Texthook::Draw(column1, (y += 16), None, 6, Tan, L"\377c4Damage Reduction: \377c7%d\377c0/\377c7%d%c \377c8%d\377c0/\377c8%d%c", dmgReduction, dmgReductionPct, '%', magReduction, magReductionPct, '%');
		Texthook::Draw(column1, (y += 16), None, 6, Gold, L"Curse Reduction:\377c0 %d%%", (int)D2COMMON_GetUnitStat(unit, STAT_CURSERESISTANCE, 0));
		y += 8;

		int fMastery = (int)D2COMMON_GetUnitStat(unit, STAT_FIREMASTERY, 0);
		int cMastery = (int)D2COMMON_GetUnitStat(unit, STAT_COLDMASTERY, 0);
		int lMastery = (int)D2COMMON_GetUnitStat(unit, STAT_LIGHTNINGMASTERY, 0);
		int pMastery = (int)D2COMMON_GetUnitStat(unit, STAT_POISONMASTERY, 0);
		int mMastery = (int)D2COMMON_GetUnitStat(unit, STAT_PASSIVEMAGICDMGMASTERY, 0);

		int fPierce = (int)D2COMMON_GetUnitStat(unit, STAT_PSENEMYFIRERESREDUC, 0);
		int cPierce = (int)D2COMMON_GetUnitStat(unit, STAT_PSENEMYCOLDRESREDUC, 0);
		int lPierce = (int)D2COMMON_GetUnitStat(unit, STAT_PSENEMYLIGHTNRESREDUC, 0);
		int pPierce = (int)D2COMMON_GetUnitStat(unit, STAT_PSENEMYPSNRESREDUC, 0);    
		int mPierce = (int)D2COMMON_GetUnitStat(unit, STAT_PASSIVEMAGICRESREDUC, 0);
		int physpierce = (int)D2COMMON_GetUnitStat(unit, STAT_PASSIVEDMGPIERCE, 0);

		Texthook::Draw(column1, (y += 16), None, 6, Gold,
				L"Elemental Mastery:\377c1 %d%%\377c3 %d%%\377c9 %d%%\377c2 %d%%\377c8 %d%%",
				fMastery, cMastery, lMastery, pMastery, mMastery);
		Texthook::Draw(column1, (y += 16), None, 6, Gold,
				L"Pierce:\377c1 %d%%\377c3 %d%%\377c9 %d%%\377c2 %d%%\377c8 %d%%\377c0 %d%%",
				fPierce, cPierce, lPierce, pPierce, mPierce, physpierce);
		int classNum = pData->nCharClass;
		auto classArMod = CharList[classNum]->toHitFactor - 35;
		int dexAR = (int)D2COMMON_GetUnitStat(unit, STAT_DEXTERITY, 0) * 5 + classArMod;
		int gearAR = (int)D2COMMON_GetUnitStat(unit, STAT_ATTACKRATING, 0);

		Texthook::Draw(column1, (y += 16), None, 6, Gold,
				L"Base AR:\377c5 dex:\377c0 %d\377c5 equip:\377c0% d\377c5 total:\377c0 %d",
				dexAR, gearAR, dexAR + gearAR);

		int gearDef = (int)D2COMMON_GetUnitStat(unit, STAT_DEFENSE, 0);
		int dexDef = (int)D2COMMON_GetUnitStat(unit, STAT_DEXTERITY, 0) / 4;
		
		Texthook::Draw(column1, (y += 16), None, 6, Gold,
				L"Base Def:\377c5 dex:\377c0 %d\377c5 equip:\377c0 %d\377c5 total:\377c0 %d",
				dexDef, gearDef, dexDef + gearDef);

		Texthook::Draw(column1, (y += 16), None, 6, Gold,
				L"Base Damage:\377c5 1h:\377c0 %d-%d\377c5 2h:\377c0 %d-%d",
				(int)D2COMMON_GetUnitStat(unit, STAT_MINIMUMDAMAGE, 0),
				(int)D2COMMON_GetUnitStat(unit, STAT_MAXIMUMDAMAGE, 0),
				(int)D2COMMON_GetUnitStat(unit, STAT_SECONDARYMINIMUMDAMAGE, 0),
				(int)D2COMMON_GetUnitStat(unit, STAT_SECONDARYMAXIMUMDAMAGE, 0));

		y += 8;

		Texthook::Draw(column1, (y += 16), None, 6, Gold,
				L"Cast Rate:\377c0 %d",
				(int)D2COMMON_GetUnitStat(unit, STAT_FASTERCAST, 0)
				);
		Texthook::Draw(column2, y, None, 6, Gold,
				L"Block Rate:\377c0 %d",
				(int)D2COMMON_GetUnitStat(unit, STAT_FASTERBLOCK, 0)
				);
		Texthook::Draw(column1, (y += 16), None, 6, Gold,
				L"Hit Recovery:\377c0 %d",
				(int)D2COMMON_GetUnitStat(unit, STAT_FASTERHITRECOVERY, 0)
				);
		Texthook::Draw(column2, y, None, 6, Gold,
				L"Run/Walk:\377c0 %d",
				(int)D2COMMON_GetUnitStat(unit, STAT_FASTERRUNWALK, 0)
				);
		Texthook::Draw(column1, (y += 16), None, 6, Gold,
				L"Attack Rate:\377c0 %d",
				(int)D2COMMON_GetUnitStat(unit, STAT_ATTACKRATE, 0));
		Texthook::Draw(column2, y, None, 6, Gold,
				L"IAS:\377c0 %d",
				(int)D2COMMON_GetUnitStat(unit, STAT_IAS, 0));

		y += 8;

		Texthook::Draw(column1, (y += 16), None, 6, Gold,
				L"Crushing Blow:\377c0 %d",
				(int)D2COMMON_GetUnitStat(unit, STAT_CRUSHINGBLOW, 0));
		Texthook::Draw(column2, y, None, 6, Gold,
				L"Open Wounds: \377c0%d",
				(int)D2COMMON_GetUnitStat(unit, STAT_OPENWOUNDS, 0));
		Texthook::Draw(column1, (y += 16), None, 6, Gold,
				L"Deadly Strike:\377c0 %d",
				(int)D2COMMON_GetUnitStat(unit, STAT_DEADLYSTRIKE, 0));
		Texthook::Draw(column2, y, None, 6, Gold,
				L"Critical Strike: \377c0%d",
				(int)D2COMMON_GetUnitStat(unit, STAT_CRITICALSTRIKE, 0));
		Texthook::Draw(column1, (y += 16), None, 6, Gold,
				L"Life Leech:\377c1 %d",
				(int)D2COMMON_GetUnitStat(unit, STAT_LIFELEECH, 0));
		Texthook::Draw(column2, y, None, 6, Gold,
				L"Mana Leech:\377c3 %d",
				(int)D2COMMON_GetUnitStat(unit, STAT_MANALEECH, 0));
		Texthook::Draw(column1, (y += 16), None, 6, Gold,
				L"Projectile Pierce:\377c0 %d",
				(int)D2COMMON_GetUnitStat(unit, STAT_PIERCINGATTACK, 0) +
				(int)D2COMMON_GetUnitStat(unit, STAT_PIERCE, 0)
				);

		y += 8;

		int minFire = (int)D2COMMON_GetUnitStat(unit, STAT_MINIMUMFIREDAMAGE, 0);
		int maxFire = (int)D2COMMON_GetUnitStat(unit, STAT_MAXIMUMFIREDAMAGE, 0);
		int minLight = (int)D2COMMON_GetUnitStat(unit, STAT_MINIMUMLIGHTNINGDAMAGE, 0);
		int maxLight = (int)D2COMMON_GetUnitStat(unit, STAT_MAXIMUMLIGHTNINGDAMAGE, 0);
		int minCold = (int)D2COMMON_GetUnitStat(unit, STAT_MINIMUMCOLDDAMAGE, 0);
		int maxCold = (int)D2COMMON_GetUnitStat(unit, STAT_MAXIMUMCOLDDAMAGE, 0);
		int minPoison = (int)D2COMMON_GetUnitStat(unit, STAT_MINIMUMPOISONDAMAGE, 0);
		int maxPoison = (int)D2COMMON_GetUnitStat(unit, STAT_MAXIMUMPOISONDAMAGE, 0);
		int poisonLength = (int)D2COMMON_GetUnitStat(unit, STAT_POISONDAMAGELENGTH, 0);
		int poisonLengthOverride = (int)D2COMMON_GetUnitStat(unit, STAT_SKILLPOISONOVERRIDELEN, 0);
		if (poisonLengthOverride > 0) {
			poisonLength = poisonLengthOverride;
		}
		int minMagic = (int)D2COMMON_GetUnitStat(unit, STAT_MINIMUMMAGICALDAMAGE, 0);
		int maxMagic = (int)D2COMMON_GetUnitStat(unit, STAT_MAXIMUMMAGICALDAMAGE, 0);
		int addedPhys = (int)D2COMMON_GetUnitStat(unit, STAT_ADDSDAMAGE, 0);

		//TODO CLEAN THIS UP TO MAKE ROOM FOR Breakpoints
		Texthook::Draw(column1, (y += 16), None, 6, Gold,
				L"Added Damage:\377c0 %d",
				addedPhys);
		Texthook::Draw(column2, y, None, 6, Orange,
				"%d-%d",
				minMagic, maxMagic);
		Texthook::Draw(column1, (y += 16), None, 6, Red,
				"%d-%d",
				minFire, maxFire);
		Texthook::Draw(column2, y, None, 6, Blue,
				"%d-%d",
				minCold, maxCold);
		Texthook::Draw(column1, (y += 16), None, 6, Yellow,
				"%d-%d",
				minLight, maxLight);
		Texthook::Draw(column2, y, None, 6, Green,
				"%d-%d over %.1fs",
				(int)(minPoison / 256.0 * poisonLength),
				(int)(maxPoison / 256.0 * poisonLength),
				poisonLength / 25.0);

		y += 8;

		Texthook::Draw(column1, (y += 16), None, 6, Gold,
				L"Magic Find:\377c3 %d",
				(int)D2COMMON_GetUnitStat(unit, STAT_MAGICFIND, 0)
				);
		Texthook::Draw(column2, y, None, 6, Gold,
				L"Gold Find:\377c9 %d",
				(int)D2COMMON_GetUnitStat(unit, STAT_GOLDFIND, 0));

		Texthook::Draw(column1, (y += 16), None, 6, Gold,
				L"Stash Gold:\377c9 %d",
				(int)D2COMMON_GetUnitStat(unit, STAT_GOLDBANK, 0));

		if (customStats.size() > 0) {
			y += 8;
			for (unsigned int i = 0; i < customStats.size(); i++) {
				int secondary = customStats[i]->useValue ? customStats[i]->value : 0;
				int stat = (int)D2COMMON_GetUnitStat(unit, STAT_NUMBER(customStats[i]->name), secondary);
				if (secondary > 0) {
					Texthook::Draw(column1, (y += 16), None, 6, Gold, "%s[%d]:\377c0 %d",
							customStats[i]->name.c_str(), secondary, stat);
				} else {
					Texthook::Draw(column1, (y += 16), None, 6, Gold, "%s:\377c0 %d",
							customStats[i]->name.c_str(), stat);
				}
			}
		}
	}
}

bool StatsDisplay::KeyClick(bool bUp, BYTE bKey, LPARAM lParam) {
	display->Lock();
	bool block = display->OnKey(bUp, bKey, lParam);
	display->Unlock();
	return block;
}

bool StatsDisplay::OnKey(bool up, BYTE kkey, LPARAM lParam) {
	UnitAny *unit = D2CLIENT_GetPlayerUnit();
	if (!unit)
		return false;

	if (IsMinimized()) {
		if (!up && kkey == statsKey) {
			LoadConfig();
			SetMinimized(false);
			return true;
		}
	} else {
		if (!up && (kkey == statsKey || kkey == VK_ESCAPE)) {
			SetMinimized(true);
			return true;
		}
	}
	return false;
}

bool StatsDisplay::Click(bool up, unsigned int mouseX, unsigned int mouseY) {
	display->Lock();
	bool block = display->OnClick(up, mouseX, mouseY);
	display->Unlock();
	return block;
}

bool StatsDisplay::OnClick(bool up, unsigned int x, unsigned int y) {
	UnitAny *unit = D2CLIENT_GetPlayerUnit();
	if (!unit)
		return false;

	if (!IsMinimized() && InRange(x, y)) {
		SetMinimized(true);
		return true;
	}
	return false;
}

BpDisplay *BpDisplay::display;

BpDisplay::BpDisplay(std::string name2) {
	int yPos = 10;
	int width = 240;

	InitializeCriticalSection(&crit);
	SetY(yPos);
	SetXSize(width);

	LoadConfig();

	SetName(name2);
	SetActive(true);
	SetMinimized(true);

	BH::config->ReadKey("Breakpoints", "VK_9", bpKey);
	display = this;
}

BpDisplay::~BpDisplay() {
	Lock();
	// Remove all hooks associated with the display
	while (Hooks.size() > 0) {
		delete (*Hooks.begin());
	}
	Unlock();
	DeleteCriticalSection(&crit);
}

void BpDisplay::LoadConfig(){
	int height = 342 + 8 * 3 + 16 * 6;

	BH::config->ReadToggle("Stats on Right", "None", false, Toggles["Stats on Right"]);

	int xPos = Toggles["Stats on Right"].state ?
		*p_D2CLIENT_ScreenSizeX - 10 - GetXSize() : 10;
	SetX(xPos);
	SetYSize(height);
}

void BpDisplay::SetX(unsigned int newX) {
	if (newX >= 0 && newX <= Hook::GetScreenWidth()) {
		Lock();
		x = newX;
		Unlock();
	}
}

void BpDisplay::SetY(unsigned int newY) {
	if (newY >= 0 && newY <= Hook::GetScreenHeight()) {
		Lock();
		y = newY;
		Unlock();
	}
}

void BpDisplay::SetXSize(unsigned int newXSize) {
	if (newXSize >= 0 && newXSize <= (Hook::GetScreenWidth() - GetX())) {
		Lock();
		xSize = newXSize;
		Unlock();
	}
}

void BpDisplay::SetYSize(unsigned int newYSize) {
	if (newYSize >= 0 && newYSize <= (Hook::GetScreenHeight() - GetY())) {
		Lock();
		ySize = newYSize;
		Unlock();
	}
}

bool BpDisplay::InRange(unsigned int x, unsigned int y) {
	return IsActive() &&
		x >= GetX() && y >= GetY() &&
		x <= GetX() + GetXSize() && y <= GetY() + GetYSize();
}

void BpDisplay::Draw() {
	display->Lock();
	display->OnDraw();
	display->Unlock();
}

void BpDisplay::OnDraw() {
	UnitAny *unit = D2CLIENT_GetPlayerUnit();
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
		for(std::list<Hook*>::iterator it = Hooks.begin(); it != Hooks.end(); it++)
			(*it)->OnDraw();

		int y = GetY();
		RECT pRect;
		pRect.left = GetX();
		pRect.top = y;
		pRect.right = x + GetXSize();
		pRect.bottom = y + GetYSize();

		Drawing::Boxhook::Draw(GetX(),GetY(), GetXSize(), GetYSize(), White, Drawing::BTBlack);
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
				} else if (currentFCR >= 99) {
					currentFrames = 12;
					nextBreakpointFCR = 152 - currentFCR;
				} else if (currentFCR >= 68) {
					currentFrames = 13;
					nextBreakpointFCR = 99 - currentFCR;
				} else if (currentFCR >= 48) {
					currentFrames = 14;
					nextBreakpointFCR = 68 - currentFCR;
				} else if (currentFCR >= 32) {
					currentFrames = 15;
					nextBreakpointFCR = 48 - currentFCR;
				} else if (currentFCR >= 22) {
					currentFrames = 16;
					nextBreakpointFCR = 32 - currentFCR;
				} else if (currentFCR >= 14) {
					currentFrames = 17;
					nextBreakpointFCR = 22 - currentFCR;
				} else if (currentFCR >= 7) {
					currentFrames = 18;
					nextBreakpointFCR = 14 - currentFCR;
				} else {
					currentFrames = 19;
					nextBreakpointFCR = 7 - currentFCR;
				}
				break;
			case 1: // Sorceress
				if (currentFCR >= 200) {
					currentFrames = 7;
				} else if (currentFCR >= 105) {
					currentFrames = 8;
					nextBreakpointFCR = 200 - currentFCR;
				} else if (currentFCR >= 63) {
					currentFrames = 9;
					nextBreakpointFCR = 105 - currentFCR;
				} else if (currentFCR >= 37) {
					currentFrames = 10;
					nextBreakpointFCR = 63 - currentFCR;
				} else if (currentFCR >= 20) {
					currentFrames = 11;
					nextBreakpointFCR = 37 - currentFCR;
				} else if (currentFCR >= 9) {
					currentFrames = 12;
					nextBreakpointFCR = 20 - currentFCR;
				} else {
					currentFrames = 13;
					nextBreakpointFCR = 9 - currentFCR;
				}
				break;
			case 2: // Necromancer
				if (currentFCR >= 180) {
					currentFrames = 13;
				} else if (currentFCR >= 120) {
					currentFrames = 14;
				} else if (currentFCR >= 86) {
					currentFrames = 15;
				} else if (currentFCR >= 65) {
					currentFrames = 16;
				} else if (currentFCR >= 48) {
					currentFrames = 17;
				} else if (currentFCR >= 35) {
					currentFrames = 18;
				} else if (currentFCR >= 24) {
					currentFrames = 19;
				} else if (currentFCR >= 18) {
					currentFrames = 20;
				} else if (currentFCR >= 11) {
					currentFrames = 21;
				} else if (currentFCR >= 6) {
					currentFrames = 22;
				} else {
					currentFrames = 23;
				}
				break;
			case 3: // Paladin
				if (currentFCR >= 125) {
					currentFrames = 9;
				} else if (currentFCR >= 75) {
					currentFrames = 10;
				} else if (currentFCR >= 48) {
					currentFrames = 11;
				} else if (currentFCR >= 30) {
					currentFrames = 12;
				} else if (currentFCR >= 18) {
					currentFrames = 13;
				} else if (currentFCR >= 9) {
					currentFrames = 14;
				} else {
					currentFrames = 15;
				}
				break;
			case 4: // Barbarian
				if (currentFCR >= 200) {
					currentFrames = 7;
				} else if (currentFCR >= 105) {
					currentFrames = 8;
				} else if (currentFCR >= 63) {
					currentFrames = 9;
				} else if (currentFCR >= 37) {
					currentFrames = 10;
				} else if (currentFCR >= 20) {
					currentFrames = 11;
				} else if (currentFCR >= 9) {
					currentFrames = 12;
				} else {
					currentFrames = 13;
				}
				break;
			case 5: // Druid
				if (currentFCR >= 163) {
					currentFrames = 10;
				} else if (currentFCR >= 99) {
					currentFrames = 11;
				} else if (currentFCR >= 68) {
					currentFrames = 12;
				} else if (currentFCR >= 46) {
					currentFrames = 13;
				} else if (currentFCR >= 30) {
					currentFrames = 14;
				} else if (currentFCR >= 19) {
					currentFrames = 15;
				} else if (currentFCR >= 4) {
					currentFrames = 16;
				} else {
					currentFrames = 17;
				}
				break;
			case 6: // Assassin
				if (currentFCR >= 102) {
					currentFrames = 10;
				} else if (currentFCR >= 65) {
					currentFrames = 11;
				} else if (currentFCR >= 42) {
					currentFrames = 12;
				} else if (currentFCR >= 27) {
					currentFrames = 13;
				} else if (currentFCR >= 16) {
					currentFrames = 14;
				} else if (currentFCR >= 8) {
					currentFrames = 15;
				} else {
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

bool BpDisplay::KeyClick(bool bUp, BYTE bKey, LPARAM lParam) {
	display->Lock();
	bool block = display->OnKey(bUp, bKey, lParam);
	display->Unlock();
	return block;
}

bool BpDisplay::OnKey(bool up, BYTE kkey, LPARAM lParam) {
	UnitAny *unit = D2CLIENT_GetPlayerUnit();
	if (!unit)
		return false;

	if (IsMinimized()) {
		if (!up && kkey == bpKey) {
			LoadConfig();
			SetMinimized(false);
			return true;
		}
	} else {
		if (!up && (kkey == bpKey || kkey == VK_ESCAPE)) {
			SetMinimized(true);
			return true;
		}
	}
	return false;
}

bool BpDisplay::Click(bool up, unsigned int mouseX, unsigned int mouseY) {
	display->Lock();
	bool block = display->OnClick(up, mouseX, mouseY);
	display->Unlock();
	return block;
}

bool BpDisplay::OnClick(bool up, unsigned int x, unsigned int y) {
	UnitAny *unit = D2CLIENT_GetPlayerUnit();
	if (!unit)
		return false;

	if (!IsMinimized() && InRange(x, y)) {
		SetMinimized(true);
		return true;
	}
	return false;
}

