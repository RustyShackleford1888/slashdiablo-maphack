#pragma once
#include "../Module.h"

class ItemLink : public Module {
public:
	ItemLink() : Module("Item Link") {}

	void OnLoad();
	void OnUnload();
	void OnLoop();
	void OnGameExit();

	void OnDraw();
	void OnOOGDraw();

	void OnLeftClick(bool up, unsigned int x, unsigned int y, bool* block);
	void OnChatMsg(const char* user, const char* msg, bool fromGame, bool* block);
	void OnChatPacketRecv(BYTE* packet, bool* block);
};

// Called from the in-game chat submit path. Appends hidden item data for any
// link sitting in the outgoing message.
void ItemLink_ExpandOutgoing(wchar_t* msg, char* packet);

// 0x26 is sized by strlen. Strip the blob and slide the following packets
// forward before that scan, or the leftover blob is read as game packets.
// Returns true when the visible text left behind is only "continued".
bool ItemLink_PrepareGameChat(BYTE* packet, DWORD size);

// Strip a whisper's blob. Returns true when the line is only a continued
// chunk and should not be drawn. *chat1 whispers are reprinted as
// "chat1 | message" before the normal hide runs, so that reprint uses this.
bool ItemLink_FilterWhisperText(char* text);

// Chat is open and Backspace is pressed. Deletes a whole [Item] when the
// caret is sitting against it, and forgets that link. Returns true when the
// key should not also delete one more character.
bool ItemLink_Backspace();
