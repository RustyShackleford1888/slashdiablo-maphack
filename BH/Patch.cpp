#include "Patch.h"
#include "D2Version.h"
std::vector<Patch*> Patch::Patches;

Patch::Patch(PatchType type, Dll dll, Offsets offsets, int function, int length) 
: type(type), dll(dll), offsets(offsets), function(function), length(length) {
	oldCode = new BYTE[length];
	injected = false;
	Patches.push_back(this);
}

int Patch::GetDllOffset(Dll dll, int offset) {
	const wchar_t* szDlls[] = {L"D2CLIENT.dll", L"D2COMMON.dll", L"D2GFX.dll", L"D2LANG.dll",
							L"D2WIN.dll", L"D2NET.dll", L"D2GAME.dll", L"D2LAUNCH.dll",
							L"FOG.dll", L"BNCLIENT.dll", L"STORM.dll", L"D2CMP.dll", L"D2MULTI.dll", L"D2MCPCLIENT.dll", L"D2CMP.dll"};
	//Attempt to get the module of the given DLL
	HMODULE hModule = GetModuleHandleW(szDlls[dll]);

	//If DLL hasn't been loaded, then load it up!
	if (!hModule) {
		hModule = LoadLibraryW(szDlls[dll]);
	}

	//If the DLL isn't there, or failed to load, return.
	if (!hModule)
		return false;

	//Check if it is an ordinal, if so, get the proper address.
	if (offset  < 0)
		return (DWORD)GetProcAddress(hModule, (LPCSTR)(-offset));

	//If just regular offset, add the two and be done!
	return ((DWORD)hModule) + offset;
}

bool Patch::WriteBytes(int address, int len, BYTE* bytes) {
	DWORD dwOld;

	if(!VirtualProtect((void*)address, len, PAGE_READWRITE, &dwOld))
		return FALSE;

	::memcpy((void*)address, bytes, len);
	return !!VirtualProtect((void*)address, len, dwOld, &dwOld);
}

bool Patch::Install() {

	//Check if we have already installed this patch.
	if (IsInstalled())
		return true;

	//Initalize variables for the exactly commands we are injecting.
	BYTE* code = new BYTE[length];
	DWORD protect;

	// Select an offset based on D2 version
	int offset = *(&offsets._113c + D2Version::GetGameVersionID());

	//Get the proper address that we are patching
	int address = GetDllOffset(dll, offset);

	//Read the old code to allow proper patch removal
	ReadProcessMemory(GetCurrentProcess(), (void*)address, oldCode, length, NULL);
	
	//Set the code with all NOPs by default
	if (type == 0x68) {
		memset(code, 0x00, length);
	}
	else {
		memset(code, 0x90, length);
	}

	if (type != NOP) {
		//Set the opcode
		code[0] = type;

		//Set the address to redirect to
		if (type == Call || type == Jump) {
			*(DWORD*)&code[1] = function - (address + 5);
		} else {
			code[1] = function;
		}
	}

	//Write the patch in
	VirtualProtect((VOID*)address, length, PAGE_EXECUTE_READWRITE, &protect);
	memcpy_s((VOID*)address, length, code, length);
	VirtualProtect((VOID*)address, length, protect, &protect);


	//Set that we successfully patched
	injected = true;

	return true;
}

bool Patch::Remove() {
	if (!IsInstalled())
		return true;

	// Select an offset based on D2 version
	int offset = *(&offsets._113c + D2Version::GetGameVersionID());

	//Get the proper address
	int address = GetDllOffset(dll, offset);
	DWORD protect;

	//Revert to the previous code
	VirtualProtect((VOID*)address, length, PAGE_EXECUTE_READWRITE, &protect);
	memcpy_s((VOID*)address, length, oldCode, length);
	VirtualProtect((VOID*)address, length, protect, &protect);


	injected = false;

	return true;
}
