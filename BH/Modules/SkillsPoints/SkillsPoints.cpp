#include "SkillsPoints.h"
#include "../../BH.h"
#include "../../Config.h"
#include "../../Constants.h"
#include "../../D2Helpers.h"
#include "../../D2Ptrs.h"
#include "../../D2Version.h"
#include "../../Drawing.h"
#include "../ItemMover/ItemMover.h"
#include <cstdlib>

DWORD SkillsPoints_ShiftClickLimit = 0x7FFFFFFF;

// Must outlive LoadConfig; Config::ReadInt keeps a pointer for config->Write().
static int configShiftClickSkillLimit = 0;
static Drawing::Inputhook* skillShiftLimitInput = nullptr;

static void ApplyShiftClickSkillLimit() {
	SkillsPoints_ShiftClickLimit = (configShiftClickSkillLimit > 0)
		? (DWORD)configShiftClickSkillLimit
		: 0x7FFFFFFF;
}

static void SyncShiftClickSkillLimitFromInput() {
	if (!skillShiftLimitInput)
		return;

	int parsed = atoi(skillShiftLimitInput->GetText().c_str());
	if (parsed < 0)
		parsed = 0;

	configShiftClickSkillLimit = parsed;
	ApplyShiftClickSkillLimit();
}

static void RefreshShiftClickSkillLimitInput() {
	if (!skillShiftLimitInput)
		return;

	char buf[16];
	sprintf_s(buf, "%d", configShiftClickSkillLimit);
	skillShiftLimitInput->SetText("%s", buf);
}

static const BYTE PACKET_ADDSKILL = 0x3B;
static const size_t PENDING_PACKET_MAX = 16;

typedef void(__stdcall *SendPacketFn)(size_t aLen, DWORD arg1, BYTE* aPacket);

static SendPacketFn RealSendPacket = nullptr;
static ULONG_PTR* sendPacketIatSlot = nullptr;
static ULONG_PTR sendPacketIatOriginal = 0;
static bool sendHookInstalled = false;

static BYTE pendingPacket[PENDING_PACKET_MAX] = {};
static size_t pendingLen = 0;
static DWORD pendingArg1 = 0;
static int pendingExtra = 0;
static bool pendingDuplicate = false;

static int GetSendPacketOrdinal() {
	return (D2Version::GetGameVersionID() == VERSION_113d) ? 10015 : 10024;
}

static bool HookSendPacketIat() {
	HMODULE hClient = GetModuleHandleW(L"D2CLIENT.dll");
	if (!hClient)
		return false;

	auto* dos = (PIMAGE_DOS_HEADER)hClient;
	auto* nt = (PIMAGE_NT_HEADERS)((BYTE*)hClient + dos->e_lfanew);
	DWORD importRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
	if (!importRva)
		return false;

	auto* imp = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)hClient + importRva);
	const int targetOrdinal = GetSendPacketOrdinal();

	for (; imp->Name; ++imp) {
		const char* dllName = (const char*)hClient + imp->Name;
		if (_stricmp(dllName, "D2NET.dll") != 0)
			continue;

		auto* thunk = (PIMAGE_THUNK_DATA)((BYTE*)hClient + imp->FirstThunk);
		auto* origThunk = imp->OriginalFirstThunk
			? (PIMAGE_THUNK_DATA)((BYTE*)hClient + imp->OriginalFirstThunk)
			: thunk;

		for (; origThunk->u1.Ordinal; ++thunk, ++origThunk) {
			if (!IMAGE_SNAP_BY_ORDINAL(origThunk->u1.Ordinal))
				continue;
			if ((int)IMAGE_ORDINAL(origThunk->u1.Ordinal) != targetOrdinal)
				continue;

			sendPacketIatSlot = &thunk->u1.Function;
			sendPacketIatOriginal = thunk->u1.Function;
			RealSendPacket = (SendPacketFn)sendPacketIatOriginal;

			DWORD oldProtect = 0;
			if (!VirtualProtect(sendPacketIatSlot, sizeof(ULONG_PTR), PAGE_READWRITE, &oldProtect))
				return false;

			thunk->u1.Function = (ULONG_PTR)SkillsPoints_SendPacketHook;
			VirtualProtect(sendPacketIatSlot, sizeof(ULONG_PTR), oldProtect, &oldProtect);

			sendHookInstalled = true;
			return true;
		}
	}

	return false;
}

void __stdcall SkillsPoints_SendPacketHook(size_t aLen, DWORD arg1, BYTE* aPacket) {
	RealSendPacket(aLen, arg1, aPacket);

	if (SkillsPoints_ShiftClickLimit == 0x7FFFFFFF || !aPacket || aLen < 1)
		return;
	if (aPacket[0] != PACKET_ADDSKILL)
		return;
	if ((GetKeyState(VK_SHIFT) & 0x8000) == 0)
		return;

	size_t copyLen = aLen;
	if (copyLen > PENDING_PACKET_MAX)
		copyLen = PENDING_PACKET_MAX;

	memcpy(pendingPacket, aPacket, copyLen);
	pendingLen = aLen;
	pendingArg1 = arg1;
	pendingExtra = (int)SkillsPoints_ShiftClickLimit - 1;
	pendingDuplicate = true;
}

static void FlushPendingDuplicates() {
	if (!pendingDuplicate)
		return;

	pendingDuplicate = false;
	if (pendingExtra <= 0)
		return;
	if (!D2CLIENT_GetUIState(UI_SKILLTREE))
		return;

	int remaining = 0;
	UnitAny* player = D2CLIENT_GetPlayerUnit();
	if (player)
		remaining = (int)D2COMMON_GetUnitStat(player, STAT_NEWSKILLS, 0);

	int extra = pendingExtra;
	if (remaining > 0 && extra > remaining - 1)
		extra = remaining - 1;

	for (int i = 0; i < extra; ++i)
		D2NET_SendPacket(pendingLen, pendingArg1, pendingPacket);
}

SkillsPoints::SkillsPoints() : Module("SkillsPoints") {}

void SkillsPoints::BuildSettingsUI() {
	ItemMover* mover = (ItemMover*)BH::moduleManager->Get("item-mover");
	if (!mover || !mover->GetInteractionTab())
		return;

	Drawing::UITab* tab = mover->GetInteractionTab();
	unsigned int x = mover->GetInteractionRightColumnX();
	unsigned int& y = mover->GetInteractionRightColumnY();

	new Drawing::Texthook(tab, x, (y += 17), "Skills per Shift+Click:");
	skillShiftLimitInput = new Drawing::Inputhook(tab, x + 140, y - 4, 36, "%s", "");
	skillShiftLimitInput->SetFont(0);
	RefreshShiftClickSkillLimitInput();
}

bool SkillsPoints::InstallSendPacketHook() {
	if (sendHookInstalled)
		return true;
	return HookSendPacketIat();
}

void SkillsPoints::RemoveSendPacketHook() {
	if (!sendHookInstalled || !sendPacketIatSlot)
		return;

	DWORD oldProtect = 0;
	if (VirtualProtect(sendPacketIatSlot, sizeof(ULONG_PTR), PAGE_READWRITE, &oldProtect)) {
		*sendPacketIatSlot = sendPacketIatOriginal;
		VirtualProtect(sendPacketIatSlot, sizeof(ULONG_PTR), oldProtect, &oldProtect);
	}

	sendPacketIatSlot = nullptr;
	sendPacketIatOriginal = 0;
	RealSendPacket = nullptr;
	sendHookInstalled = false;
	pendingDuplicate = false;
}

void SkillsPoints::OnLoad() {
	LoadConfig();
}

void SkillsPoints::OnUnload() {
	RemoveSendPacketHook();
}

void SkillsPoints::OnGameJoin() {
	pendingDuplicate = false;
	InstallSendPacketHook();
}

void SkillsPoints::LoadConfig() {
	BH::config->ReadInt("Shift Click Skill Points Limit", configShiftClickSkillLimit);
	ApplyShiftClickSkillLimit();
	RefreshShiftClickSkillLimitInput();
}

void SkillsPoints_FlushSettingsInput() {
	SyncShiftClickSkillLimitFromInput();
}

void SkillsPoints::OnLoop() {
	if (!IsGameReady())
		return;

	if (!sendHookInstalled)
		InstallSendPacketHook();

	FlushPendingDuplicates();
}
