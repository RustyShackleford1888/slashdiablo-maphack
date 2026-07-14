#include "Bnet.h"
#include "../../D2Ptrs.h"
#include "../../BH.h"
#include "../../Common.h"
#include <vector>
#include <utility>
#include <cstring>
#include <cctype>
#include <cmath>
#include <algorithm>
#include <string>

unsigned int Bnet::failToJoin;
std::string Bnet::DefaultGame;
std::string Bnet::DefaultPassword;


std::string Bnet::lastName;
std::string Bnet::lastPass;
std::string Bnet::lastDesc;
std::string Bnet::defaultGsString;
std::regex Bnet::reg = std::regex("^(.*?)(\\d+)$");

// Fixes Unrecoverable internal error 6FF61787
Patch* fog10251Patch = new Patch(Jump, FOG, { 0x11690, 0x11690 }, (int)Bnet::FOG10251Patch, 5);

Patch* bnetLobbyPatch = new Patch(Jump, D2MULTI, { 0xBC00, 0xF9B0 }, (int)Bnet::BnetLobbyAdBlockPatch, 5);

Patch* nextGame1 = new Patch(Call, D2MULTI, { 0x14D29, 0xADAB }, (int)Bnet::NextGamePatch, 5);
Patch* nextGame2 = new Patch(Call, D2MULTI, { 0x14A0B, 0xB5E9 }, (int)Bnet::NextGamePatch, 5);
Patch* nextPass1 = new Patch(Call, D2MULTI, { 0x14D64, 0xADE6 }, (int)Bnet::NextPassPatch, 5);
Patch* nextPass2 = new Patch(Call, D2MULTI, { 0x14A46, 0xB624 }, (int)Bnet::NextPassPatch, 5);

Patch* gameDesc = new Patch(Call, D2MULTI, { 0x14D8F, 0xB64F }, (int)Bnet::GameDescPatch, 5);

Patch* ftjPatch = new Patch(Call, D2CLIENT, { 0x4363E, 0x443FE }, (int)FailToJoin_Interception, 6);
Patch* removePass = new Patch(Call, D2MULTI, { 0x1250, 0x1AD0 }, (int)RemovePass_Interception, 5);

namespace {
static const unsigned char kSidFriendsList = 0x65;
static const unsigned char kSidFriendsUpdate = 0x66;
static const DWORD kUiEditbox = 1;
static const DWORD kUiButton = 6;
static const int kJoinTplW = 64;
static const int kJoinTplH = 10;
static const unsigned char kJoinTpl[kJoinTplW * kJoinTplH] = {
    71, 77, 80, 78, 75, 88, 88, 90, 100, 108, 103, 95, 97, 95, 91, 99,
    102, 85, 79, 96, 102, 102, 108, 103, 110, 114, 106, 112, 114, 110, 98, 82,
    114, 114, 105, 89, 93, 104, 99, 96, 100, 96, 108, 103, 102, 96, 98, 96,
    101, 102, 101, 103, 96, 99, 111, 111, 105, 114, 129, 105, 107, 104, 96, 84,
    122, 132, 127, 125, 127, 131, 130, 128, 133, 130, 126, 125, 124, 126, 119, 131,
    128, 129, 118, 120, 125, 122, 136, 136, 134, 137, 135, 139, 140, 140, 132, 81,
    128, 139, 133, 129, 131, 135, 133, 130, 133, 132, 134, 130, 125, 122, 122, 131,
    128, 127, 124, 129, 129, 132, 139, 137, 133, 136, 136, 137, 136, 135, 126, 57,
    116, 136, 135, 132, 131, 131, 131, 132, 126, 83, 100, 78, 71, 102, 54, 106,
    65, 106, 95, 76, 97, 56, 114, 133, 134, 130, 133, 134, 128, 127, 123, 75,
    115, 136, 135, 132, 131, 131, 131, 132, 127, 125, 129, 114, 92, 64, 48, 98,
    95, 79, 66, 114, 133, 130, 127, 133, 134, 130, 134, 134, 128, 127, 121, 59,
    114, 130, 132, 134, 128, 124, 129, 134, 129, 94, 95, 90, 83, 100, 74, 99,
    88, 95, 122, 105, 113, 73, 119, 128, 128, 129, 135, 134, 134, 134, 126, 70,
    112, 129, 133, 133, 127, 125, 130, 134, 130, 125, 122, 111, 92, 81, 70, 111,
    103, 97, 85, 116, 133, 130, 129, 128, 128, 129, 135, 134, 134, 134, 124, 64,
    80, 97, 100, 100, 96, 99, 101, 101, 103, 101, 97, 100, 103, 106, 108, 104,
    100, 100, 102, 105, 105, 98, 102, 102, 106, 103, 110, 108, 106, 105, 107, 62,
    80, 98, 100, 99, 97, 99, 101, 101, 103, 101, 94, 95, 97, 106, 108, 104,
    101, 102, 103, 107, 106, 101, 104, 103, 106, 103, 108, 106, 106, 107, 107, 57,
    80, 116, 110, 115, 116, 120, 119, 117, 115, 117, 114, 117, 122, 117, 117, 115,
    112, 114, 113, 114, 107, 75, 114, 109, 113, 116, 122, 119, 115, 111, 115, 110,
    112, 121, 118, 116, 117, 114, 114, 114, 114, 112, 72, 110, 110, 111, 117, 118,
    120, 118, 115, 118, 115, 114, 121, 119, 115, 116, 114, 112, 112, 111, 115, 70,
    84, 130, 128, 102, 113, 103, 94, 130, 100, 131, 93, 97, 109, 96, 111, 91,
    103, 105, 130, 131, 120, 80, 128, 130, 136, 116, 126, 128, 100, 132, 92, 108,
    100, 97, 119, 83, 114, 87, 126, 133, 132, 126, 79, 123, 130, 135, 140, 140,
    131, 95, 105, 110, 122, 103, 106, 112, 87, 104, 141, 138, 133, 132, 129, 82,
    79, 130, 120, 83, 109, 79, 69, 99, 58, 97, 68, 65, 97, 59, 90, 58,
    90, 82, 112, 133, 120, 76, 128, 129, 136, 100, 95, 90, 59, 103, 73, 89,
    85, 78, 96, 53, 104, 57, 107, 135, 134, 126, 75, 122, 128, 134, 137, 135,
    120, 59, 70, 97, 92, 89, 91, 121, 87, 126, 142, 140, 136, 134, 130, 78,
    67, 121, 122, 113, 113, 114, 114, 115, 120, 115, 115, 113, 116, 114, 118, 111,
    120, 112, 112, 123, 115, 65, 117, 122, 125, 115, 106, 113, 117, 117, 110, 118,
    113, 111, 118, 108, 119, 120, 119, 125, 123, 121, 66, 111, 121, 125, 125, 123,
    125, 101, 111, 123, 108, 118, 114, 121, 117, 125, 131, 131, 126, 123, 123, 75,
    36, 57, 64, 60, 66, 71, 76, 74, 83, 85, 89, 93, 83, 74, 77, 84,
    95, 100, 100, 97, 96, 73, 86, 90, 79, 82, 86, 90, 87, 82, 80, 88,
    95, 94, 98, 97, 89, 93, 91, 96, 95, 86, 69, 86, 95, 89, 80, 85,
    96, 97, 88, 87, 90, 97, 91, 83, 96, 112, 109, 106, 79, 85, 90, 75,
};

std::vector<std::string> g_friendAccounts;
std::vector<BYTE> g_friendLoc;
std::vector<std::string> g_friendGame;
std::string g_leaderLastGame;
std::string g_pendingJoinGame;
/** Leader's new game seen from friend packet while still in-game / UI not ready; applied in OnLoop when OOG. */
std::string g_deferredLeaderJoinGame;
int g_joinPhase = 0;
DWORD g_joinPhaseTime = 0;
bool g_joinWakeupTried = false;
int g_leaderAbsentStreak = 0;
bool g_sawLeaderInRoster = false;
DWORD g_lastJoinAttemptMs = 0;
int g_inGameRosterTick = 0;
/** 0 = none; else GetTickCount() when follow flow clicked main-lobby JOIN; second click after a random delay. */
static DWORD g_followJoinGamePanelArmedAt = 0;
/** Per-arm: random in [kFollowJoinPanelDelayMinMs, kFollowJoinPanelDelayMaxMs]. */
static DWORD g_followJoinGamePanelClickAfterMs = 0;
/** Snapshot when arming: leader game to put in the join name box (g_pendingJoinGame, else g_leaderLastGame). */
static std::string g_followJoinPanelGameName;
static const unsigned int kFollowJoinPanelDelayMinMs = 500u;
static const unsigned int kFollowJoinPanelDelayMaxMs = 5000u;
/** Ignore duplicate D2CLIENT_ExitGame() from roster+chat+0x26 in the same ~300ms window (not a latch — allows retry if exit did not go through). */
static DWORD g_lastFollowExitGameMs = 0;
static const DWORD kFollowExitGameMinGapMs = 300u;
/** Collapse simultaneous enter-whisper / friend updates (multi-follower) into one auto lobby sequence. */
static DWORD g_lastFollowBnetAutoActionMs = 0;
static const DWORD kFollowBnetMinAutoActionGapMs = 600u;

static void ClearFollowJoinPanelArm() {
	g_followJoinGamePanelArmedAt = 0;
	g_followJoinGamePanelClickAfterMs = 0;
	g_followJoinPanelGameName.clear();
}

static void ClickClientPoint(int cx, int cy);
static void DoFollowExitGame() {
	DWORD now = GetTickCount();
	if (g_lastFollowExitGameMs != 0) {
		DWORD gap = now - g_lastFollowExitGameMs;
		if (gap < kFollowExitGameMinGapMs)
			return;
	}
	g_lastFollowExitGameMs = now;
	D2CLIENT_ExitGame();
}

static bool LeaderFollowEnabled(Bnet* b) {
	return b && b->followLeader && *b->followLeader
		&& (!b->leaderAccount.empty() || !b->leaderCharacter.empty());
}

// Battle.net UI shows accounts as *Name; roster and friend packets often use the same form.
static std::string NormalizeBnetAccountTag(const char* s) {
	if (!s)
		return "";
	std::string t = Trim(std::string(s));
	while (!t.empty() && t[0] == '*')
		t.erase(0, 1);
	return Trim(t);
}

static bool LeaderTagMatches(const std::string& leaderCfg, const char* rosterOrFriendField) {
	if (leaderCfg.empty() || !rosterOrFriendField || !rosterOrFriendField[0])
		return false;
	return _stricmp(
		NormalizeBnetAccountTag(rosterOrFriendField).c_str(),
		NormalizeBnetAccountTag(leaderCfg.c_str()).c_str()) == 0;
}

static const char* FindCiSubstring(const char* hay, const char* needle) {
	if (!hay || !needle || !*needle)
		return nullptr;
	size_t nl = strlen(needle);
	for (const char* p = hay; *p; ++p) {
		if (_strnicmp(p, needle, (unsigned int)nl) == 0)
			return p;
	}
	return nullptr;
}

static const char* ReadBnCString(const BYTE* pkt, size_t& pos, size_t cap) {
	if (pos >= cap)
		return nullptr;
	const char* start = (const char*)(pkt + pos);
	while (pos < cap && pkt[pos])
		pos++;
	if (pos >= cap)
		return nullptr;
	pos++;
	return start;
}

static void ResizeGrayNearest(
	const std::vector<unsigned char>& src, int sw, int sh,
	std::vector<unsigned char>& dst, int dw, int dh) {
	dst.assign((size_t)dw * (size_t)dh, 0);
	for (int y = 0; y < dh; ++y) {
		int sy = (int)((long long)y * (long long)sh / (long long)dh);
		if (sy < 0) sy = 0;
		if (sy >= sh) sy = sh - 1;
		for (int x = 0; x < dw; ++x) {
			int sx = (int)((long long)x * (long long)sw / (long long)dw);
			if (sx < 0) sx = 0;
			if (sx >= sw) sx = sw - 1;
			dst[(size_t)y * (size_t)dw + (size_t)x] = src[(size_t)sy * (size_t)sw + (size_t)sx];
		}
	}
}

static bool CaptureClientGray(std::vector<unsigned char>& gray, int& w, int& h) {
	gray.clear();
	w = h = 0;
	HWND hwnd = D2GFX_GetHwnd();
	if (!hwnd)
		return false;
	RECT rc = { 0 };
	if (!GetClientRect(hwnd, &rc))
		return false;
	w = rc.right - rc.left;
	h = rc.bottom - rc.top;
	if (w <= 0 || h <= 0)
		return false;

	HDC hdcWin = GetDC(hwnd);
	if (!hdcWin)
		return false;
	HDC hdcMem = CreateCompatibleDC(hdcWin);
	if (!hdcMem) {
		ReleaseDC(hwnd, hdcWin);
		return false;
	}
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	HBITMAP hbmp = CreateDIBSection(hdcWin, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!hbmp || !bits) {
		if (hbmp) DeleteObject(hbmp);
		DeleteDC(hdcMem);
		ReleaseDC(hwnd, hdcWin);
		return false;
	}
	HGDIOBJ old = SelectObject(hdcMem, hbmp);
	BOOL okBlit = BitBlt(hdcMem, 0, 0, w, h, hdcWin, 0, 0, SRCCOPY);
	if (old) SelectObject(hdcMem, old);
	if (!okBlit) {
		DeleteObject(hbmp);
		DeleteDC(hdcMem);
		ReleaseDC(hwnd, hdcWin);
		return false;
	}

	gray.resize((size_t)w * (size_t)h);
	const unsigned char* p = (const unsigned char*)bits;
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			size_t i = ((size_t)y * (size_t)w + (size_t)x) * 4;
			unsigned char b = p[i + 0];
			unsigned char g = p[i + 1];
			unsigned char r = p[i + 2];
			gray[(size_t)y * (size_t)w + (size_t)x] = (unsigned char)((30 * r + 59 * g + 11 * b) / 100);
		}
	}

	DeleteObject(hbmp);
	DeleteDC(hdcMem);
	ReleaseDC(hwnd, hdcWin);
	return true;
}

static bool FindJoinByPixelTemplate(int* outX, int* outY) {
	if (!outX || !outY)
		return false;
	*outX = *outY = 0;

	std::vector<unsigned char> scr;
	int sw = 0, sh = 0;
	if (!CaptureClientGray(scr, sw, sh))
		return false;

	// Template strip spans roughly 40% of screen width (CREATE..QUIT cluster).
	int tw = (int)((long long)sw * 40 / 100);
	if (tw < 220) tw = 220;
	if (tw > sw - 10) tw = sw - 10;
	int th = (int)((long long)tw * (long long)kJoinTplH / (long long)kJoinTplW);
	if (th < 28) th = 28;
	if (th > sh / 4) th = sh / 4;

	std::vector<unsigned char> tpl;
	ResizeGrayNearest(std::vector<unsigned char>(kJoinTpl, kJoinTpl + kJoinTplW * kJoinTplH), kJoinTplW, kJoinTplH, tpl, tw, th);

	int x0 = (int)((long long)sw * 58 / 100);
	int x1 = sw - tw - 1;
	int y0 = (int)((long long)sh * 78 / 100);
	int y1 = sh - th - 1;
	if (x0 >= x1 || y0 >= y1)
		return false;

	long long bestSad = (1LL << 62);
	int bestX = -1, bestY = -1;
	for (int y = y0; y <= y1; y += 2) {
		for (int x = x0; x <= x1; x += 2) {
			long long sad = 0;
			int cnt = 0;
			for (int ty = 0; ty < th; ty += 2) {
				const unsigned char* srow = &scr[(size_t)(y + ty) * (size_t)sw + (size_t)x];
				const unsigned char* trow = &tpl[(size_t)ty * (size_t)tw];
				for (int tx = 0; tx < tw; tx += 2) {
					sad += std::abs((int)srow[tx] - (int)trow[tx]);
					++cnt;
				}
				if (cnt > 0 && sad / cnt > 32) // early prune
					break;
			}
			if (sad < bestSad) {
				bestSad = sad;
				bestX = x;
				bestY = y;
			}
		}
	}
	if (bestX < 0 || bestY < 0)
		return false;
	long long sampleCount = ((long long)(th + 1) / 2) * ((long long)(tw + 1) / 2);
	if (sampleCount <= 0)
		return false;
	long long avg = bestSad / sampleCount;
	if (avg > 22)
		return false;
	if (bestY < (int)((long long)sh * 76 / 100))
		return false;

	// JOIN center in this strip is in top row, roughly 75% across.
	*outX = bestX + (int)((long long)tw * 75 / 100);
	*outY = bestY + (int)((long long)th * 24 / 100);
	return true;
}

static bool IsControlReadable(Control* c) {
	return c && !IsBadReadPtr(c, sizeof(Control));
}

static Control* FindControlGeom(DWORD ctrlType, int x, int y, int w, int h) {
	for (Control* p = *p_D2WIN_FirstControl; p; p = p->pNext) {
		if (!IsControlReadable(p))
			break;
		if ((DWORD)p->dwType == ctrlType
			&& (int)p->dwPosX == x && (int)p->dwPosY == y
			&& (int)p->dwSizeX == w && (int)p->dwSizeY == h)
			return p;
	}
	return nullptr;
}

static DWORD LobbyScreenW() {
	DWORD w = *p_D2CLIENT_ScreenSizeX;
	return w ? w : 800;
}

static DWORD LobbyScreenH() {
	DWORD h = *p_D2CLIENT_ScreenSizeY;
	return h ? h : 600;
}

static void GetClientSizeSafe(int* outW, int* outH) {
	if (!outW || !outH)
		return;
	*outW = (int)LobbyScreenW();
	*outH = (int)LobbyScreenH();
	HWND hwnd = D2GFX_GetHwnd();
	if (!hwnd)
		return;
	RECT rc = { 0 };
	if (!GetClientRect(hwnd, &rc))
		return;
	int w = rc.right - rc.left;
	int h = rc.bottom - rc.top;
	if (w > 0 && h > 0) {
		*outW = w;
		*outH = h;
	}
}

// Join-game layout was authored for 800x600; try that and a 640x480-proportional pass (common BN UI).
static Control* FindJoinControlScaled(DWORD ctrlType, int x800, int y600, int w800, int h600) {
	DWORD sw = LobbyScreenW();
	DWORD sh = LobbyScreenH();
	const int bases[2][2] = { { 800, 600 }, { 640, 480 } };
	for (int bi = 0; bi < 2; ++bi) {
		int bw = bases[bi][0], bh = bases[bi][1];
		int nx = (int)((long long)x800 * (long long)sw / bw);
		int ny = (int)((long long)y600 * (long long)sh / bh);
		int nw = (int)((long long)w800 * (long long)sw / bw);
		int nh = (int)((long long)h600 * (long long)sh / bh);
		for (int ox = -2; ox <= 2; ox += 2) {
			for (int oy = -2; oy <= 2; oy += 2) {
				Control* c = FindControlGeom(ctrlType, nx + ox, ny + oy, nw, nh);
				if (c)
					return c;
			}
		}
	}
	return nullptr;
}

static Control* FindJoinGoButtonBelowRow(Control* nameL, Control* passR, DWORD sh) {
	if (!nameL || !passR)
		return nullptr;
	int rowY = (int)nameL->dwPosY;
	int midX = ((int)nameL->dwPosX + (int)nameL->dwSizeX / 2 + (int)passR->dwPosX + (int)passR->dwSizeX / 2) / 2;
	Control* bestBtn = nullptr;
	int bestDist = 999999;
	for (Control* q = *p_D2WIN_FirstControl; q; q = q->pNext) {
		if ((DWORD)q->dwType != kUiButton)
			continue;
		int bh = (int)q->dwSizeY;
		if (bh < 20 || bh > 52)
			continue;
		int bw = (int)q->dwSizeX;
		if (bw < 90 || bw > 260)
			continue;
		int qy = (int)q->dwPosY;
		if (qy < rowY + 35 || qy > rowY + 420)
			continue;
		int qcx = (int)q->dwPosX + bw / 2;
		int dist = (int)std::abs(qy - (rowY + (int)sh * 48 / 100)) + (int)std::abs(qcx - midX);
		if (dist < bestDist) {
			bestDist = dist;
			bestBtn = q;
		}
	}
	return bestBtn;
}

// D2MULTI exposes the join password edit; game name is the other edit on the same row to the left.
static bool ResolveJoinGameControlsFromPassPtr(Control** nameBox, Control** passBox, Control** goBtn) {
	*nameBox = nullptr;
	*goBtn = nullptr;
	Control* pass = *p_D2MULTI_PassBox;
	if (!pass)
		return false;
	if (IsBadReadPtr(pass, sizeof(Control)))
		return false;
	if ((DWORD)pass->dwType != kUiEditbox)
		return false;
	DWORD sh = LobbyScreenH();
	// Join-game password box sits in the upper half of the client; avoid treating channel/other edits as join UI.
	if (sh && (int)pass->dwPosY > (int)(sh * 52 / 100))
		return false;
	*passBox = pass;
	Control* bestName = nullptr;
	int bestDx = 999999;
	for (Control* p = *p_D2WIN_FirstControl; p; p = p->pNext) {
		if (p == pass)
			continue;
		if ((DWORD)p->dwType != kUiEditbox)
			continue;
		if ((int)std::abs((int)p->dwPosY - (int)pass->dwPosY) > 10)
			continue;
		if ((int)p->dwPosX >= (int)pass->dwPosX)
			continue;
		int dx = (int)pass->dwPosX - (int)p->dwPosX;
		if (dx < 50 || dx > 700)
			continue;
		if (dx < bestDx) {
			bestDx = dx;
			bestName = p;
		}
	}
	*nameBox = bestName;
	if (!*nameBox)
		return false;
	*goBtn = FindJoinGoButtonBelowRow(*nameBox, *passBox, sh);
	return *goBtn != nullptr;
}

static bool FindJoinGameControlsHeuristic(Control** outName, Control** outPass, Control** outGo) {
	*outName = *outPass = *outGo = nullptr;
	DWORD sh = LobbyScreenH();
	std::vector<Control*> edits;
	for (Control* p = *p_D2WIN_FirstControl; p; p = p->pNext) {
		if ((DWORD)p->dwType != kUiEditbox)
			continue;
		int w = (int)p->dwSizeX, ht = (int)p->dwSizeY;
		if (w < 95 || w > 260 || ht < 14 || ht > 34)
			continue;
		if ((int)p->dwPosY > (int)(sh * 42 / 100))
			continue;
		if ((int)p->dwPosY < 8)
			continue;
		edits.push_back(p);
	}
	for (size_t i = 0; i < edits.size(); ++i) {
		for (size_t j = i + 1; j < edits.size(); ++j) {
			Control* a = edits[i];
			Control* b = edits[j];
			int dy = (int)std::abs((int)a->dwPosY - (int)b->dwPosY);
			if (dy > 12)
				continue;
			Control* L = a;
			Control* R = b;
			if ((int)L->dwPosX > (int)R->dwPosX)
				std::swap(L, R);
			int dx = (int)R->dwPosX - (int)L->dwPosX;
			if (dx < 60 || dx > 680)
				continue;
			if ((int)std::abs((int)L->dwSizeX - (int)R->dwSizeX) > 50)
				continue;
			Control* bestBtn = FindJoinGoButtonBelowRow(L, R, sh);
			if (bestBtn) {
				*outName = L;
				*outPass = R;
				*outGo = bestBtn;
				return true;
			}
		}
	}
	return false;
}

static bool ResolveJoinGameControls(Control** nameBox, Control** passBox, Control** goBtn) {
	if (ResolveJoinGameControlsFromPassPtr(nameBox, passBox, goBtn))
		return true;
	*nameBox = FindJoinControlScaled(kUiEditbox, 432, 148, 155, 20);
	*passBox = FindJoinControlScaled(kUiEditbox, 606, 148, 155, 20);
	*goBtn = FindJoinControlScaled(kUiButton, 594, 433, 172, 32);
	if (*nameBox && *passBox && *goBtn)
		return true;
	return FindJoinGameControlsHeuristic(nameBox, passBox, goBtn);
}

/** Fills the join **game name** edit with ANSI text; uses D2WIN (same as NextGamePatch) when the join panel is open. */
static void SetJoinPanelGameNameFromFollowAnsi(const char* ansi) {
	if (!ansi || !*ansi)
		return;
	if (!p_D2WIN_FirstControl || !*p_D2WIN_FirstControl)
		return;
	Control* nameBox = nullptr, * passBox = nullptr, * goBtn = nullptr;
	if (!ResolveJoinGameControls(&nameBox, &passBox, &goBtn) || !nameBox)
		return;
	wchar_t* w = AnsiToUnicode(ansi);
	if (!w)
		return;
	D2WIN_SetControlText(nameBox, w);
	D2WIN_SelectEditBoxText(nameBox);
	delete[] w;
}

static Control* ResolveJoinGameTabButton() {
	Control* c = FindJoinControlScaled(kUiButton, 652, 469, 120, 20);
	if (c)
		return c;
	DWORD sw = LobbyScreenW();
	DWORD sh = LobbyScreenH();
	Control* best = nullptr;
	int bestScore = 999999;
	for (Control* p = *p_D2WIN_FirstControl; p; p = p->pNext) {
		if ((DWORD)p->dwType != kUiButton)
			continue;
		int w = (int)p->dwSizeX;
		int h = (int)p->dwSizeY;
		// Keep this strict to avoid selecting unrelated corner controls.
		if (w < 95 || w > 150 || h < 14 || h > 28)
			continue;
		if ((int)p->dwPosY < (int)sh * 65 / 100)
			continue;
		if ((int)p->dwPosX < (int)sw * 55 / 100)
			continue;
		int score = (int)std::abs((int)p->dwPosX + w / 2 - (int)(sw * 82 / 100))
			+ (int)std::abs((int)p->dwPosY + h / 2 - (int)(sh * 78 / 100));
		if (score < bestScore) {
			bestScore = score;
			best = p;
		}
	}
	return best;
}

static Control* ResolveCreateGameTabButton() {
	Control* c = FindJoinControlScaled(kUiButton, 533, 469, 120, 20);
	if (c)
		return c;
	DWORD sw = LobbyScreenW();
	DWORD sh = LobbyScreenH();
	Control* best = nullptr;
	int bestScore = 999999;
	for (Control* p = *p_D2WIN_FirstControl; p; p = p->pNext) {
		if ((DWORD)p->dwType != kUiButton)
			continue;
		int w = (int)p->dwSizeX;
		int h = (int)p->dwSizeY;
		if (w < 70 || w > 200 || h < 14 || h > 30)
			continue;
		if ((int)p->dwPosY < (int)sh * 55 / 100)
			continue;
		int score = (int)std::abs((int)p->dwPosX + w / 2 - (int)(sw * 67 / 100))
			+ (int)std::abs((int)p->dwPosY + h / 2 - (int)(sh * 78 / 100));
		if (score < bestScore) {
			bestScore = score;
			best = p;
		}
	}
	return best;
}

// Lobby / join-panel clicks: D2 client rect. Integer only: cx = centerX*W/refW, etc.
// Main JOIN — wide 3840x2160: (3043.5, 1660) as 30435*W/38400, 1660*H/2160. 4:3 1200x900: (1069, 687.5) as 1069*W/1200, 1375*H/1800.
static void GetLobbyClientPixels(int& outW, int& outH) {
	GetClientSizeSafe(&outW, &outH);
	if (outW <= 0 || outH <= 0) {
		outW = (int)LobbyScreenW();
		outH = (int)LobbyScreenH();
	}
}

/** 4:3/5:4 family vs 16:9 — integer aspect test: w/h < 1.45. */
static bool LobbyClientIsFourByThreeish(int w, int h) {
	if (w <= 0 || h <= 0)
		return false;
	return (long long)w * 100 < (long long)h * 145;
}

static void ClickLobbyCreateByHardcodedLocation() {
	int sw, sh;
	GetLobbyClientPixels(sw, sh);
	if (sw <= 0 || sh <= 0)
		return;
	int joinCx, joinCy;
	if (LobbyClientIsFourByThreeish(sw, sh)) {
		// 1200x900 ref, main-lobby JOIN center (1069, 687.5)
		joinCx = (int)((long long)1069 * (long long)sw / 1200);
		joinCy = (int)((long long)1375 * (long long)sh / 1800); // 687.5 at ref height
	} else {
		// 3840x2160 ref, main-lobby JOIN center (3043.5, 1660)
		joinCx = (int)((long long)30435 * (long long)sw / 38400);
		joinCy = (int)((long long)1660 * (long long)sh / 2160);
	}
	// CREATE: 119px left of JOIN at 800 ref width
	int cx = joinCx - (int)((long long)119 * (long long)sw / 800);
	ClickClientPoint(cx, joinCy);
}

static void ClickLobbyJoinByHardcodedLocation() {
	int sw, sh;
	GetLobbyClientPixels(sw, sh);
	if (sw <= 0 || sh <= 0)
		return;
	int cx, cy;
	if (LobbyClientIsFourByThreeish(sw, sh)) {
		// 1200x900 ref, main-lobby JOIN center
		cx = (int)((long long)1069 * (long long)sw / 1200);
		cy = (int)((long long)1375 * (long long)sh / 1800);
	} else {
		// 3840x2160 ref, main-lobby JOIN center
		cx = (int)((long long)30435 * (long long)sw / 38400);
		cy = (int)((long long)1660 * (long long)sh / 2160);
	}
	ClickClientPoint(cx, cy);
}

// Join Game panel: **game name** field center (for double-click), D2 **client** coords, derived from your corners.
// 1200x900: (643,193)-(885,228)  =>  center (764,211)
// 3840x2160: (2015,459)-(2608,537/541)  =>  center (2312,499)  (Y = average of four corners)
// Leave a branch at (0,0) to skip name-field double-click for that layout.
static const int kJoinNameFieldCenterX_Ref1200x900 = 764;
static const int kJoinNameFieldCenterY_Ref1200x900 = 211;
static const int kJoinNameFieldCenterX_Ref3840x2160 = 2312;
static const int kJoinNameFieldCenterY_Ref3840x2160 = 499;

static void DoubleClickClientPoint(int cx, int cy) {
	// Two full clicks back-to-back (not OS double-click semantics) — just rapid successive singles.
	ClickClientPoint(cx, cy);
	Sleep(5);
	ClickClientPoint(cx, cy);
}

static void DoubleClickJoinPanelGameNameFieldByHardcodedLocation() {
	int sw, sh;
	GetLobbyClientPixels(sw, sh);
	if (sw <= 0 || sh <= 0)
		return;
	int cx, cy;
	if (LobbyClientIsFourByThreeish(sw, sh)) {
		if (kJoinNameFieldCenterX_Ref1200x900 == 0 && kJoinNameFieldCenterY_Ref1200x900 == 0)
			return;
		cx = (int)((long long)kJoinNameFieldCenterX_Ref1200x900 * (long long)sw / 1200);
		cy = (int)((long long)kJoinNameFieldCenterY_Ref1200x900 * (long long)sh / 900);
	} else {
		if (kJoinNameFieldCenterX_Ref3840x2160 == 0 && kJoinNameFieldCenterY_Ref3840x2160 == 0)
			return;
		cx = (int)((long long)kJoinNameFieldCenterX_Ref3840x2160 * (long long)sw / 3840);
		cy = (int)((long long)kJoinNameFieldCenterY_Ref3840x2160 * (long long)sh / 2160);
	}
	DoubleClickClientPoint(cx, cy);
}

// Join Game panel button: integer scale (center*client)/ref.
// 4:3:  1200x900 ref, center 1024, 630.
// Wide: 3840x2160 ref, user (2626,1460)-(3233,1563) => center (2929.5, 1511.5) => 5859*W/7680, 3023*H/4320.
static void ClickJoinGamePanelButtonByHardcodedLocation() {
	int sw, sh;
	GetLobbyClientPixels(sw, sh);
	if (sw <= 0 || sh <= 0)
		return;
	int cx, cy;
	if (LobbyClientIsFourByThreeish(sw, sh)) {
		cx = (int)((long long)1024 * (long long)sw / 1200);
		cy = (int)((long long)630 * (long long)sh / 900);
	} else {
		cx = (int)((long long)5859 * (long long)sw / 7680);
		cy = (int)((long long)3023 * (long long)sh / 4320);
	}
	ClickClientPoint(cx, cy);
}

/** Unseeded rand() is identical per process; multiple D2 clients would pick the same delay. */
static DWORD RandomFollowJoinPanelDelayMs() {
	const unsigned int span = kFollowJoinPanelDelayMaxMs - kFollowJoinPanelDelayMinMs + 1u;
	LARGE_INTEGER qpc;
	if (!QueryPerformanceCounter(&qpc))
		qpc.QuadPart = 0;
	static unsigned s_armCount = 0;
	++s_armCount;
	DWORD u = GetTickCount();
	u ^= GetCurrentProcessId() * 0x10001u;
	u ^= GetCurrentThreadId() * 0x9E37u;
	u ^= (DWORD)(qpc.QuadPart ^ (qpc.QuadPart >> 32)) + s_armCount * 0x9E3779B9u;
	u *= 0x7FEB352Du;
	u ^= u >> 16;
	return kFollowJoinPanelDelayMinMs + (u % span);
}

static void ArmFollowJoinGamePanelClickAfterLobbyJoin() {
	if (!g_pendingJoinGame.empty())
		g_followJoinPanelGameName = g_pendingJoinGame;
	else
		g_followJoinPanelGameName = g_leaderLastGame;
	g_followJoinGamePanelArmedAt = GetTickCount();
	g_followJoinGamePanelClickAfterMs = RandomFollowJoinPanelDelayMs();
}

/** Call from Bnet::OnOOGDraw (not only OnLoop) so Bnet channel UI still ticks the join-panel delay. */
static void ProcessFollowJoinGamePanelDelayedClick(Bnet* b) {
	if (g_followJoinGamePanelArmedAt == 0)
		return;
	if (!LeaderFollowEnabled(b)) {
		ClearFollowJoinPanelArm();
		return;
	}
	if (D2CLIENT_GetPlayerUnit()) {
		ClearFollowJoinPanelArm();
		return;
	}
	const DWORD now = GetTickCount();
	if (now - g_followJoinGamePanelArmedAt < g_followJoinGamePanelClickAfterMs)
		return;
	const std::string toType = g_followJoinPanelGameName;
	ClearFollowJoinPanelArm();
	// After main-lobby JOIN: focus the name field, set leader's game name, then the panel's Join Game button
	DoubleClickJoinPanelGameNameFieldByHardcodedLocation();
	Sleep(100);
	if (!toType.empty()) {
		Bnet::SetLastGameNameForFollow(toType);
		SetJoinPanelGameNameFromFollowAnsi(toType.c_str());
		Sleep(50);
	}
	ClickJoinGamePanelButtonByHardcodedLocation();
}

static void CollectUiButtonsRecursive(Control* p, std::vector<Control*>& out) {
	for (; p; ) {
		if (!IsControlReadable(p))
			break;
		Control* next = p->pNext;
		if ((DWORD)p->dwType == kUiButton)
			out.push_back(p);
		if (IsControlReadable(p->pChildControl))
			CollectUiButtonsRecursive(p->pChildControl, out);
		p = next;
	}
}

// Main multiplayer lobby (first screen): bottom strip CREATE | JOIN | … — must click JOIN to open the join-game panel.
static Control* FindMainLobbyJoinNavButton() {
	DWORD sh = LobbyScreenH();
	if (sh < 400)
		sh = 600;
	std::vector<Control*> allBtns;
	CollectUiButtonsRecursive(*p_D2WIN_FirstControl, allBtns);
	std::vector<Control*> btns;
	for (Control* p : allBtns) {
		if (p->dwDisabled)
			continue;
		int w = (int)p->dwSizeX, h = (int)p->dwSizeY;
		if (w < 55 || w > 380 || h < 10 || h > 52)
			continue;
		int y = (int)p->dwPosY;
		if (y < (int)(sh * 55 / 100))
			continue;
		btns.push_back(p);
	}
	if (btns.size() < 2)
		return nullptr;

	int minY = 999999;
	for (Control* b : btns)
		minY = (std::min)(minY, (int)b->dwPosY);

	std::vector<Control*> topRow;
	for (Control* b : btns) {
		if (std::abs((int)b->dwPosY - minY) <= 20)
			topRow.push_back(b);
	}
	std::sort(topRow.begin(), topRow.end(), [](Control* a, Control* b) {
		return a->dwPosX < b->dwPosX;
	});
	// Some lobby layouts have two independent button groups on the same Y row:
	// [SEND|WHISPER|HELP]   [CREATE|JOIN]. Prefer JOIN from the rightmost cluster.
	if (topRow.size() >= 4) {
		std::vector<std::vector<Control*>> clusters;
		clusters.push_back(std::vector<Control*>());
		clusters.back().push_back(topRow[0]);
		for (size_t i = 1; i < topRow.size(); ++i) {
			Control* prev = topRow[i - 1];
			Control* cur = topRow[i];
			int prevRight = (int)prev->dwPosX + (int)prev->dwSizeX;
			int gap = (int)cur->dwPosX - prevRight;
			if (gap > 50)
				clusters.push_back(std::vector<Control*>());
			clusters.back().push_back(cur);
		}
		if (clusters.size() >= 2) {
			std::vector<Control*>& rightCluster = clusters.back();
			if (rightCluster.size() == 2)
				return rightCluster[1]; // CREATE | JOIN
			if (rightCluster.size() >= 5)
				return rightCluster[1]; // CREATE | JOIN | CHANNEL | LADDER | QUIT
		}
	}
	if (topRow.size() == 2)
		return topRow[1];
	if (topRow.size() >= 5)
		return topRow[1];

	if (topRow.size() == 3) {
		int maxY = -1;
		for (Control* b : btns)
			maxY = (std::max)(maxY, (int)b->dwPosY);
		std::vector<Control*> otherRow;
		for (Control* b : btns) {
			if (std::abs((int)b->dwPosY - maxY) <= 20)
				otherRow.push_back(b);
		}
		std::sort(otherRow.begin(), otherRow.end(), [](Control* a, Control* b) {
			return a->dwPosX < b->dwPosX;
		});
		if (otherRow.size() == 2)
			return otherRow[1];
	}

	std::sort(btns.begin(), btns.end(), [](Control* a, Control* b) {
		if (a->dwPosY != b->dwPosY)
			return a->dwPosY < b->dwPosY;
		return a->dwPosX < b->dwPosX;
	});
	if (btns.size() >= 4 && btns.size() <= 6)
		return btns[1];
	return nullptr;
}

static void ClickClientPoint(int cx, int cy) {
	if (!p_D2CLIENT_MouseX || !p_D2CLIENT_MouseY || !p_D2CLIENT_CursorHoverX || !p_D2CLIENT_CursorHoverY)
		return;
	HWND hwnd = D2GFX_GetHwnd();
	if (!hwnd)
		return;
	RECT cr = { 0 };
	if (!GetClientRect(hwnd, &cr))
		return;
	int w = (int)(cr.right - cr.left);
	int h = (int)(cr.bottom - cr.top);
	if (w <= 0 || h <= 0)
		return;
	if (cx < 0) cx = 0;
	if (cy < 0) cy = 0;
	if (cx >= w) cx = w - 1;
	if (cy >= h) cy = h - 1;
	DWORD oldMx = *p_D2CLIENT_MouseX;
	DWORD oldMy = *p_D2CLIENT_MouseY;
	DWORD oldHx = *p_D2CLIENT_CursorHoverX;
	DWORD oldHy = *p_D2CLIENT_CursorHoverY;
	*p_D2CLIENT_MouseX = (DWORD)cx;
	*p_D2CLIENT_MouseY = (DWORD)cy;
	*p_D2CLIENT_CursorHoverX = (DWORD)cx;
	*p_D2CLIENT_CursorHoverY = (DWORD)cy;
	LPARAM lp = MAKELPARAM((SHORT)cx, (SHORT)cy);
	// Synchronous: BH's WndProc reads MouseX/Y, not lParam, for click routing.
	SendMessage(hwnd, WM_MOUSEMOVE, 0, lp);
	SendMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp);
	Sleep(25);
	SendMessage(hwnd, WM_LBUTTONUP, 0, lp);
	*p_D2CLIENT_MouseX = oldMx;
	*p_D2CLIENT_MouseY = oldMy;
	*p_D2CLIENT_CursorHoverX = oldHx;
	*p_D2CLIENT_CursorHoverY = oldHy;
}

static void ClickControlCenter(Control* c) {
	if (!IsControlReadable(c))
		return;
	int cx = (int)c->dwPosX + (int)c->dwSizeX / 2;
	int cy = (int)c->dwPosY + (int)c->dwSizeY / 2;
	ClickClientPoint(cx, cy);
}

static void TryQueueLeaderJoinForNewGameName(Bnet* b, const std::string& game) {
	if (game.empty() || !LeaderFollowEnabled(b))
		return;
	if (_stricmp(game.c_str(), g_leaderLastGame.c_str()) != 0) {
		if (!D2CLIENT_GetPlayerUnit() && *p_D2WIN_FirstControl) {
			g_leaderLastGame = game;
			DWORD now = GetTickCount();
			if (g_lastFollowBnetAutoActionMs != 0) {
				DWORD gap = now - g_lastFollowBnetAutoActionMs;
				if (gap < kFollowBnetMinAutoActionGapMs) {
					g_deferredLeaderJoinGame = game;
					return;
				}
			}
			g_lastFollowBnetAutoActionMs = now;
			g_pendingJoinGame = game;
			g_joinPhase = 1;
			g_joinPhaseTime = now;
			g_joinWakeupTried = false;
			g_deferredLeaderJoinGame.clear();
			ClickLobbyJoinByHardcodedLocation();
			ArmFollowJoinGamePanelClickAfterLobbyJoin();
			g_joinPhase = 0;
			g_pendingJoinGame.clear();
		} else {
			g_deferredLeaderJoinGame = game;
		}
	}
}

static void ApplyLeaderFriendLocation(Bnet* b, BYTE locId, const char* locName) {
	if (!LeaderFollowEnabled(b))
		return;

	const std::string game(locName ? locName : "");

	if (locId == 0x00 || locId == 0x01 || locId == 0x02) {
		g_pendingJoinGame.clear();
		g_joinPhase = 0;
		g_leaderLastGame.clear();
		g_deferredLeaderJoinGame.clear();
		ClearFollowJoinPanelArm();
		return;
	}

	if (locId == 0x04)
		return;

	if ((locId == 0x03 || locId == 0x05) && !game.empty())
		TryQueueLeaderJoinForNewGameName(b, game);
}

static std::string BuildBnChatCombined(const char* user, const char* msg) {
	std::string combined;
	if (msg && msg[0])
		combined = msg;
	if (user && user[0]) {
		if (!combined.empty())
			combined.insert(0, " ");
		combined.insert(0, user);
	}
	return Trim(combined);
}

/** BN whisper / channel lines: "Your friend Rusty has left|entered ..." */
static bool FriendWhisperMatchesLeader(const std::string& combined, Bnet* b, std::string* friendAcctOut) {
	if (!FindCiSubstring(combined.c_str(), "your friend "))
		return false;
	const char* yf = FindCiSubstring(combined.c_str(), "your friend ");
	yf += 12; // "your friend "
	while (*yf && std::isspace(static_cast<unsigned char>(*yf)))
		yf++;
	std::string friendAcct;
	while (*yf && (unsigned char)*yf > 32 && *yf != '(' && *yf != ':')
		friendAcct.push_back(*yf++);
	friendAcct = Trim(friendAcct);
	if (friendAcctOut)
		*friendAcctOut = friendAcct;

	bool match = false;
	if (!friendAcct.empty() && !b->leaderAccount.empty()
		&& LeaderTagMatches(b->leaderAccount, friendAcct.c_str()))
		match = true;

	if (!match && !b->leaderAccount.empty()) {
		size_t lpar = combined.find_last_of('(');
		size_t rpar = combined.find_last_of(')');
		if (lpar != std::string::npos && rpar != std::string::npos && rpar > lpar) {
			std::string inner = Trim(combined.substr(lpar + 1, rpar - lpar - 1));
			if (LeaderTagMatches(b->leaderAccount, inner.c_str()))
				match = true;
		}
	}

	if (!match && !b->leaderCharacter.empty()) {
		const char* fromKw = FindCiSubstring(combined.c_str(), "from ");
		if (fromKw) {
			fromKw += 5; // "from "
			while (*fromKw && std::isspace(static_cast<unsigned char>(*fromKw)))
				fromKw++;
			const char* nameEnd = fromKw;
			while (*nameEnd && *nameEnd != '(' && !std::isspace(static_cast<unsigned char>(*nameEnd)))
				nameEnd++;
			std::string fromChar(fromKw, (size_t)(nameEnd - fromKw));
			fromChar = Trim(fromChar);
			if (!fromChar.empty() && !_stricmp(fromChar.c_str(), b->leaderCharacter.c_str()))
				match = true;
		}
	}

	// Whisper sender "Char (*Acc) whispers:" — character before '(' at start of line
	if (!match && !b->leaderCharacter.empty()) {
		const char* hay = combined.c_str();
		if (hay[0]) {
			size_t lpar = combined.find('(');
			if (lpar != std::string::npos && lpar > 0) {
				std::string head = Trim(combined.substr(0, lpar));
				if (!head.empty() && !_stricmp(head.c_str(), b->leaderCharacter.c_str()))
					match = true;
			}
		}
	}

	return match;
}

static std::string ParseEnteredGameNameFromBnCombined(const std::string& combined) {
	const char* gn = FindCiSubstring(combined.c_str(), "game named \"");
	if (gn) {
		gn += 12; // strlen "game named \""
		std::string game;
		while (*gn && *gn != '"')
			game.push_back(*gn++);
		return Trim(game);
	}
	gn = FindCiSubstring(combined.c_str(), "game named '");
	if (gn) {
		gn += 12; // strlen "game named '"
		std::string game;
		while (*gn && *gn != '\'')
			game.push_back(*gn++);
		return Trim(game);
	}
	return "";
}

// Realm sends: Your friend Rusty has entered ... game named "GameName". (often as whisper, not 0x65/0x66.)
static void MaybeQueueLeaderJoinFromBnChat(const char* user, const char* msg, Bnet* b) {
	if (!LeaderFollowEnabled(b))
		return;
	std::string combined = BuildBnChatCombined(user, msg);
	if (combined.empty())
		return;
	if (!FindCiSubstring(combined.c_str(), "has entered"))
		return;
	if (!FindCiSubstring(combined.c_str(), "diablo"))
		return;

	std::string friendAcct;
	if (!FriendWhisperMatchesLeader(combined, b, &friendAcct))
		return;

	std::string game = ParseEnteredGameNameFromBnCombined(combined);
	if (game.empty())
		return;

	TryQueueLeaderJoinForNewGameName(b, game);
}

static void ParseFriendsListPacket(const BYTE* p, size_t cap, Bnet* b) {
	if (cap < 3)
		return;
	BYTE n = p[2];
	if (n > 128)
		n = 128;
	size_t pos = 3;
	g_friendAccounts.clear();
	g_friendLoc.clear();
	g_friendGame.clear();
	g_friendAccounts.reserve(n);
	g_friendLoc.reserve(n);
	g_friendGame.reserve(n);

	for (BYTE i = 0; i < n; i++) {
		const char* acct = ReadBnCString(p, pos, cap);
		if (!acct)
			return;
		if (pos + 5 > cap)
			return;
		BYTE status = p[pos++];
		BYTE loc = p[pos++];
		DWORD product = 0;
		if (pos + 4 > cap)
			return;
		memcpy(&product, p + pos, 4);
		(void)product;
		pos += 4;
		const char* locn = ReadBnCString(p, pos, cap);
		if (!locn)
			locn = "";
		g_friendAccounts.push_back(acct);
		g_friendLoc.push_back(loc);
		g_friendGame.push_back(locn);
	}

	for (size_t i = 0; i < g_friendAccounts.size(); i++) {
		if (!LeaderTagMatches(b->leaderAccount, g_friendAccounts[i].c_str()))
			continue;
		if (i < g_friendLoc.size() && i < g_friendGame.size())
			ApplyLeaderFriendLocation(b, g_friendLoc[i], g_friendGame[i].c_str());
		return;
	}
}

static void ParseFriendsUpdatePacket(const BYTE* p, size_t cap, Bnet* b) {
	if (cap < 9)
		return;
	BYTE idx = p[2];
	if (idx >= g_friendAccounts.size())
		return;
	BYTE status = p[3];
	BYTE loc = p[4];
	(void)status;
	size_t pos = 9;
	const char* locn = ReadBnCString(p, pos, cap);
	if (!locn)
		locn = "";
	if (idx < g_friendLoc.size() && idx < g_friendGame.size()) {
		g_friendLoc[idx] = loc;
		g_friendGame[idx] = locn;
	}
	if (!LeaderTagMatches(b->leaderAccount, g_friendAccounts[idx].c_str()))
		return;
	ApplyLeaderFriendLocation(b, loc, locn);
}

static void ProcessLeaderJoinFsm(Bnet* b) {
	if (!LeaderFollowEnabled(b))
		return;
	if (g_joinPhase == 0)
		return;
	if (g_pendingJoinGame.empty()) {
		g_joinPhase = 0;
		return;
	}

	// Follow join: on queue, open the join panel and schedule the delayed in-panel click.
	if (g_joinPhase == 1) {
		g_lastFollowBnetAutoActionMs = GetTickCount();
		ClickLobbyJoinByHardcodedLocation();
		ArmFollowJoinGamePanelClickAfterLobbyJoin();
		g_joinPhase = 0;
		g_pendingJoinGame.clear();
		g_joinWakeupTried = false;
		return;
	}
	// Stale g_joinPhase 2/3 from older builds
	g_joinPhase = 0;
	g_joinWakeupTried = false;
}

// Roster szName2 is often empty on modded realms. Game text uses Char(Account), e.g. MrWatsit(Rusty) = character MrWatsit, account Rusty.
// Parse raw 0x26 (game chat) with two possible name layouts — D2Handlers uses one split; some builds differ.
// layout: "A" null-term name at +10, "B" 16-byte name + msg at +26
static bool TryExitLeaderLeftFromCombined(std::string combined, Bnet* b) {
	if (!LeaderFollowEnabled(b) || !D2CLIENT_GetPlayerUnit())
		return false;
	combined = Trim(combined);
	if (combined.empty())
		return false;

	const char* pfound = FindCiSubstring(combined.c_str(), "left our world");
	if (!pfound)
		pfound = FindCiSubstring(combined.c_str(), "has left our world");
	if (!pfound)
		pfound = FindCiSubstring(combined.c_str(), "left the game");
	if (!pfound)
		pfound = FindCiSubstring(combined.c_str(), "has left the game");
	if (!pfound)
		return false;

	size_t endPos = (size_t)(pfound - combined.c_str());
	std::string who = Trim(combined.substr(0, endPos));
	while (!who.empty() && (who.back() == '.' || who.back() == '!' || who.back() == ':'
			|| std::isspace(static_cast<unsigned char>(who.back()))))
		who.pop_back();
	while (!who.empty() && (who[0] == ':' || who[0] == '*' || who[0] == '.' || who[0] == ','
			|| std::isspace(static_cast<unsigned char>(who[0]))))
		who.erase(0, 1);
	who = Trim(who);
	if (who.empty())
		return false;

	std::string charPart, accPart;
	size_t lpar = who.find_last_of('(');
	size_t rpar = who.find_last_of(')');
	if (lpar != std::string::npos && rpar != std::string::npos && rpar > lpar && lpar < who.length()) {
		accPart = Trim(who.substr(lpar + 1, rpar - lpar - 1));
		charPart = Trim(who.substr(0, lpar));
	} else {
		charPart = who;
	}

	bool match = false;
	if (!accPart.empty() && !b->leaderAccount.empty() && LeaderTagMatches(b->leaderAccount, accPart.c_str()))
		match = true;
	if (!charPart.empty() && !b->leaderCharacter.empty()
		&& !_stricmp(charPart.c_str(), b->leaderCharacter.c_str()))
		match = true;
	if (!charPart.empty() && !b->leaderAccount.empty() && LeaderTagMatches(b->leaderAccount, charPart.c_str()))
		match = true;

	if (!match)
		return false;

	DoFollowExitGame();
	g_sawLeaderInRoster = false;
	g_leaderAbsentStreak = 0;
	return true;
}

static void MaybeExitLeaderLeftFromPacket0x26(const BYTE* pkt, Bnet* b) {
	if (!pkt || pkt[0] != 0x26)
		return;

	std::vector<std::pair<std::string, const char*>> cands;

	{
		const char* pName = (const char*)(pkt + 10);
		size_t nl = strnlen(pName, 128);
		const char* pMsg = (const char*)(pkt + 10 + nl + 1);
		size_t ml = strnlen(pMsg, 512);
		std::string comb;
		if (nl > 0)
			comb.assign(pName, nl);
		if (ml > 0) {
			if (!comb.empty())
				comb.push_back(' ');
			comb.append(pMsg, ml);
		}
		if (!comb.empty())
			cands.emplace_back(std::move(comb), "A");
	}

	{
		char nb[17];
		memcpy(nb, pkt + 10, 16);
		nb[16] = '\0';
		size_t n2 = strnlen(nb, 16);
		std::string nameB(nb, n2);
		const char* pMsgB = (const char*)(pkt + 26);
		size_t ml = strnlen(pMsgB, 512);
		std::string comb;
		if (!nameB.empty())
			comb = nameB;
		if (ml > 0) {
			if (!comb.empty())
				comb.push_back(' ');
			comb.append(pMsgB, ml);
		}
		if (!comb.empty())
			cands.emplace_back(std::move(comb), "B");
	}

	for (size_t i = 0; i < cands.size(); ++i) {
		bool dup = false;
		for (size_t j = 0; j < i; ++j) {
			if (cands[i].first == cands[j].first) {
				dup = true;
				break;
			}
		}
		if (dup)
			continue;
		if (TryExitLeaderLeftFromCombined(cands[i].first, b))
			return;
	}
}

// Slash/realm friend line (green): "From Char (*Acc): Your friend Acc has left a Diablo II ... game."
// Delivered via ChatHandler / OnChatMsg, not as game packet 0x26.
static void MaybeExitLeaderLeftFromBnChat(const char* user, const char* msg, Bnet* b) {
	if (!LeaderFollowEnabled(b) || !D2CLIENT_GetPlayerUnit())
		return;

	std::string combined = BuildBnChatCombined(user, msg);
	if (combined.empty())
		return;
	if (!FindCiSubstring(combined.c_str(), "has left"))
		return;
	if (!FindCiSubstring(combined.c_str(), "diablo"))
		return;

	std::string friendAcct;
	if (!FriendWhisperMatchesLeader(combined, b, &friendAcct)) {
		return;
	}

	DoFollowExitGame();
	g_sawLeaderInRoster = false;
	g_leaderAbsentStreak = 0;
}

} // namespace

void Bnet::OnLoad() {
	showLastGame = &bools["Autofill Last Game"];
	*showLastGame = true;
	
	showLastPass = &bools["Autofill Last Password"];
	*showLastPass = true;

	nextInstead = &bools["Autofill Next Game"];
	*nextInstead = true;

	keepDesc = &bools["Autofill Description"];
	*keepDesc = true;

	followLeader = &bools["Follow Leader"];
	*followLeader = false;

	defaultGsIndex = &ints["Default Gs"];
	*defaultGsIndex = 0;

	failToJoin = 4000;
	LoadConfig();
}

void Bnet::LoadConfig() {
	BH::config->ReadBoolean("Autofill Last Game", *showLastGame);
	BH::config->ReadBoolean("Autofill Last Password", *showLastPass);
	BH::config->ReadBoolean("Autofill Next Game", *nextInstead);
	BH::config->ReadBoolean("Autofill Description", *keepDesc);
	BH::config->ReadInt("Fail To Join", failToJoin);
	BH::config->ReadInt("Default Gs", *defaultGsIndex);
	defaultGsString = "gs" + std::to_string(*defaultGsIndex + 1);
	BH::config->ReadString("Default Game Name", DefaultGame);
	BH::config->ReadString("Default Password", DefaultPassword);

	BH::config->ReadBoolean("Follow Leader", *followLeader);
	leaderAccount.clear();
	BH::config->ReadString("Leader", leaderAccount);
	if (leaderAccount.empty())
		BH::config->ReadString("leader", leaderAccount);
	if (leaderAccount.empty())
		BH::config->ReadString("Leader Account", leaderAccount);
	if (leaderAccount.empty())
		BH::config->ReadString("leader account", leaderAccount);
	leaderAccount = Trim(leaderAccount);
	leaderAccount = NormalizeBnetAccountTag(leaderAccount.c_str());

	leaderCharacter.clear();
	BH::config->ReadString("Leader Character", leaderCharacter);
	if (leaderCharacter.empty())
		BH::config->ReadString("Leader char", leaderCharacter);
	leaderCharacter = Trim(leaderCharacter);

	InstallPatches();
}

void Bnet::ApplyFollowLeaderFromUI(const std::string& accRaw, const std::string& charRaw) {
	std::string a = Trim(accRaw);
	a = NormalizeBnetAccountTag(a.c_str());
	std::string c = Trim(charRaw);
	if (a == leaderAccount && c == leaderCharacter) return;
	leaderAccount = std::move(a);
	leaderCharacter = std::move(c);
}

void Bnet::ApplyDefaultGameSettingsFromUI(const std::string& gameRaw, const std::string& passRaw) {
	std::string g = Trim(gameRaw);
	std::string p = Trim(passRaw);
	if (g == DefaultGame && p == DefaultPassword) return;
	DefaultGame = std::move(g);
	DefaultPassword = std::move(p);
}

void Bnet::InstallPatches() {
	fog10251Patch->Install();
	bnetLobbyPatch->Install();
	if (*showLastGame || *nextInstead) {
		nextGame1->Install();
		nextGame2->Install();
	}

	if (*showLastPass) {
		nextPass1->Install();
		nextPass2->Install();
		removePass->Install();
	}

	if (*keepDesc) {
		gameDesc->Install();
	}

	if (failToJoin > 0 && !D2CLIENT_GetPlayerUnit())
		ftjPatch->Install();
}

void Bnet::RemovePatches() {
	fog10251Patch->Remove();
	bnetLobbyPatch->Remove();
	nextGame1->Remove();
	nextGame2->Remove();

	nextPass1->Remove();
	nextPass2->Remove();

	gameDesc->Remove();

	ftjPatch->Remove();
	removePass->Remove();
}

void Bnet::OnUnload() {
	ClearFollowJoinPanelArm();
	RemovePatches();
}

void Bnet::OnGameJoin() {
	if ( strlen((*p_D2LAUNCH_BnData)->szGameName) > 0)
		lastName = (*p_D2LAUNCH_BnData)->szGameName;

	if ( strlen((*p_D2LAUNCH_BnData)->szGamePass) > 0)
		lastPass = (*p_D2LAUNCH_BnData)->szGamePass;
	else
		lastPass = "";
	
	if ( strlen((*p_D2LAUNCH_BnData)->szGameDesc) > 0)
		lastDesc = (*p_D2LAUNCH_BnData)->szGameDesc;
	else
		lastDesc = "";

	RemovePatches();

	g_sawLeaderInRoster = false;
	g_leaderAbsentStreak = 0;
	g_inGameRosterTick = 0;
	g_deferredLeaderJoinGame.clear();
	ClearFollowJoinPanelArm();
	g_lastFollowExitGameMs = 0;
	g_lastFollowBnetAutoActionMs = 0;
}

void Bnet::OnGameExit() {
	g_joinPhase = 0;
	g_pendingJoinGame.clear();
	ClearFollowJoinPanelArm();
	g_lastFollowExitGameMs = 0;

	if (*nextInstead) {
		std::smatch match;
		if (std::regex_search(Bnet::lastName, match, Bnet::reg) && match.size() == 3) {
			std::string name = match.format("$1");
			if (name.length() != 0) {
				int count = atoi(match.format("$2").c_str());

				//Restart at 1 if the next number would exceed the max game name length of 15
				if (lastName.length() == 15) {
					int maxCountLength = 15 - name.length();
					int countLength = 1;
					int tempCount = count + 1;
					while (tempCount > 9) {
						countLength++;
						tempCount /= 10;
					}
					if (countLength > maxCountLength) {
						count = 1;
					} else {
						count++;
					}
				} else {
					count++;
				}
				char buffer[16];
				sprintf_s(buffer, sizeof(buffer), "%s%d", name.c_str(), count);
				lastName = std::string(buffer);
			}
		}
	}

	InstallPatches();
}

void Bnet::OnLoop() {
	if (!LeaderFollowEnabled(this)) {
		ClearFollowJoinPanelArm();
		return;
	}
	// Note: do not run ProcessFollowJoinGamePanelDelayedClick here — GameLoop can be inactive in
	// the Battle.net channel; OOG draw runs every frame in menu/Bnet (see OnOOGDraw).
	if (D2CLIENT_GetPlayerUnit()) {
		if ((g_inGameRosterTick++ % 6) != 0)
			return;
		bool leaderHere = false;
		for (RosterUnit* ru = *p_D2CLIENT_PlayerUnitList; ru; ru = ru->pNext) {
			bool byAcc = ru->szName2[0] && LeaderTagMatches(leaderAccount, ru->szName2);
			bool byCharAcc = !leaderAccount.empty() && LeaderTagMatches(leaderAccount, ru->szName);
			bool byLeaderChar = !leaderCharacter.empty() && ru->szName[0]
				&& !_stricmp(ru->szName, leaderCharacter.c_str());
			if (byAcc || byCharAcc || byLeaderChar) {
				leaderHere = true;
				break;
			}
		}
		if (leaderHere) {
			g_sawLeaderInRoster = true;
			g_leaderAbsentStreak = 0;
		} else if (g_sawLeaderInRoster) {
			g_leaderAbsentStreak++;
			if (g_leaderAbsentStreak >= 3) {
				DoFollowExitGame();
				g_sawLeaderInRoster = false;
				g_leaderAbsentStreak = 0;
			}
		}
	} else {
		g_inGameRosterTick = 0;
		if (!g_deferredLeaderJoinGame.empty() && *p_D2WIN_FirstControl
			&& g_pendingJoinGame.empty() && g_joinPhase == 0) {
			g_leaderLastGame = g_deferredLeaderJoinGame;
			g_pendingJoinGame = g_deferredLeaderJoinGame;
			g_deferredLeaderJoinGame.clear();
			g_joinPhase = 1;
			g_joinPhaseTime = GetTickCount();
		}
		ProcessLeaderJoinFsm(this);
	}
}

void Bnet::OnOOGDraw() {
	if (D2CLIENT_GetPlayerUnit())
		return;
	// Random-delayed (0.5s–5s) "Join Game" click must run on the OOG path — Bnet::OnLoop is driven by
	// D2CLIENT GameLoop, which is not ticked the same way while in the Bnet channel.
	ProcessFollowJoinGamePanelDelayedClick(this);
}

void Bnet::OnChatPacketRecv(BYTE* packet, bool* block) {
	(void)block;
	if (!packet || packet[0] != 0xFF)
		return;
	if (!LeaderFollowEnabled(this))
		return;
	const size_t cap = 2048;
	BYTE cmd = packet[1];
	if (cmd == kSidFriendsList)
		ParseFriendsListPacket(packet, cap, this);
	else if (cmd == kSidFriendsUpdate)
		ParseFriendsUpdatePacket(packet, cap, this);
}

void Bnet::OnChatMsg(const char* user, const char* msg, bool fromGame, bool* block) {
	(void)fromGame;
	(void)block;
	MaybeQueueLeaderJoinFromBnChat(user, msg, this);
	MaybeExitLeaderLeftFromBnChat(user, msg, this);
}

void Bnet::OnGamePacketRecv(BYTE* packet, bool* block) {
	(void)block;
	if (!packet || packet[0] != 0x26)
		return;
	MaybeExitLeaderLeftFromPacket0x26(packet, this);
}

void Bnet::OnKey(bool up, BYTE key, LPARAM lParam, bool* block) {
	(void)up;
	(void)key;
	(void)lParam;
	(void)block;
}

VOID __fastcall Bnet::FOG10251Patch(DWORD lpCriticalSection, char nLine) {
	return;
}

DWORD __stdcall Bnet::BnetLobbyAdBlockPatch(DWORD a1) {
	return 1;
}

VOID __fastcall Bnet::NextGamePatch(Control* box, BOOL (__stdcall *FunCallBack)(Control*, DWORD, DWORD)) {
	wchar_t* wszLastGameName = nullptr; // Ensure it's initialized to nullptr

	if (Bnet::lastName.size() > 0) {
		// Type in the game name from last game if available
		wszLastGameName = AnsiToUnicode(Bnet::lastName.c_str());
	}
	else {
		// TBD Input the default game name
		wszLastGameName = AnsiToUnicode(Bnet::GetDefaultGamename().c_str() );
	}

	D2WIN_SetControlText(box, wszLastGameName);
	D2WIN_SelectEditBoxText(box);

	// original code
	D2WIN_SetEditBoxProc(box, FunCallBack);
	delete [] wszLastGameName;
}

VOID __fastcall Bnet::NextPassPatch(Control* box, BOOL(__stdcall *FunCallBack)(Control*, DWORD, DWORD)) {
	wchar_t* wszLastPass = nullptr; // Ensure it's initialized to nullptr

	if (Bnet::lastPass.size() > 0) {
		// Type in the password from last game if available
		wszLastPass = AnsiToUnicode(Bnet::lastPass.c_str());
	}
	else {
		// TBD Input the default password, but only if the lastGame is null, otherwise we might be inserting an undesired password
		if (Bnet::lastName.size() > 0) {
			return;
		}
		wszLastPass = AnsiToUnicode(Bnet::GetDefaultPassword().c_str());
	}	
	
	D2WIN_SetControlText(box, wszLastPass);
	
	// original code
	D2WIN_SetEditBoxProc(box, FunCallBack);
	delete[] wszLastPass;
}

VOID __fastcall Bnet::GameDescPatch(Control* box, BOOL(__stdcall *FunCallBack)(Control*, DWORD, DWORD)) {
	wchar_t* wszLastDesc = nullptr; // Ensure it's initialized to nullptr

	if (Bnet::lastDesc.size() > 0) {
		// Type in the gs (description) from last game if available
		wszLastDesc = AnsiToUnicode(Bnet::lastDesc.c_str());
	}
	else {
		wszLastDesc = AnsiToUnicode(Bnet::defaultGsString.c_str());
	}
	
	D2WIN_SetControlText(box, wszLastDesc);
	
	// original code
	D2WIN_SetEditBoxProc(box, FunCallBack);
	delete[] wszLastDesc;
}

void __declspec(naked) RemovePass_Interception() {
	__asm {
		PUSHAD
		CALL [Bnet::RemovePassPatch]
		POPAD

		; Original code
		XOR EAX, EAX
		SUB ECX, 01
		RET
	}
}

void Bnet::RemovePassPatch() {
	Control* box = *p_D2MULTI_PassBox;

	if (Bnet::lastPass.size() == 0 || box == nullptr) {
		return;
	}

	wchar_t *wszLastPass = AnsiToUnicode("");
	D2WIN_SetControlText(box, wszLastPass);
	delete[] wszLastPass;
}

void __declspec(naked) FailToJoin_Interception()
{
	/*
	Changes the amount of time, in milliseconds, that we wait for the loading
	door to open before the client confirms that it failed to join the game.
	*/
	__asm
	{
		cmp esi, Bnet::failToJoin;
		ret;
	}
}
