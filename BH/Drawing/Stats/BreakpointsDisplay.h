#pragma once

#include <Windows.h>
#include <algorithm>
#include <string>
#include <list>
#include "../Hook.h"
#include "../../MPQInit.h"
#include "../../Drawing.h"
#include "../../Config.h"


namespace Drawing {
	class BreakpointsDisplay;

	class BreakpointsDisplay : public HookGroup {
	private:
		std::map<std::string, Toggle> Toggles;
		std::string name;
		unsigned int x, y, xSize, ySize;
		unsigned int breakpointKey;
		unsigned int breakpointKey2;
		bool active, minimized;
		CRITICAL_SECTION crit;
	public:
		static BreakpointsDisplay* display;
		BreakpointsDisplay(std::string name);
		~BreakpointsDisplay();

		void LoadConfig();

		void Lock() { EnterCriticalSection(&crit); };
		void Unlock() { LeaveCriticalSection(&crit); };

		std::string GetName() { return name; };
		unsigned int GetX() { return x; };
		unsigned int GetY() { return y; };
		unsigned int GetXSize() { return xSize; };
		unsigned int GetYSize() { return ySize; };
		bool IsActive() { return active; };
		bool IsMinimized() { return minimized; };

		bool InRange(unsigned int x, unsigned int y);

		void SetX(unsigned int newX);
		void SetY(unsigned int newY);
		void SetXSize(unsigned int newXSize);
		void SetYSize(unsigned int newYSize);
		void SetName(std::string newName) { Lock(); name = newName;  Unlock(); };
		void SetActive(bool newState) { Lock(); active = newState; Unlock(); };
		void SetMinimized(bool newState) { Lock(); minimized = newState; Unlock(); };

		void OnDraw();
		static void Draw();

		bool OnClick(bool up, unsigned int mouseX, unsigned int mouseY);
		static bool Click(bool up, unsigned int mouseX, unsigned int mouseY);
		static bool KeyClick(bool up, unsigned char key, long lParam);
	};
};
