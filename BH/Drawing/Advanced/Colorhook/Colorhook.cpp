#include "../../../D2Ptrs.h"
#include "../../../Common.h"
#include "Colorhook.h"
#include "../../Basic/Boxhook/Boxhook.h"
#include "../../Basic/Framehook/Framehook.h"
#include "../../Basic/Texthook/Texthook.h"
#include "../../Basic/Crosshook/Crosshook.h"

using namespace Drawing;

Colorhook* Colorhook::current;

/* Basic Hook Initializer
 *		Used for just drawing basics.
 */
Colorhook::Colorhook(HookVisibility visibility, unsigned int x, unsigned int y, unsigned int* color, std::string formatString, ...) :
Hook(visibility, x, y) {
	//Correctly format the string from the given arguments.
	currentColor = color;
	char buffer[4096];
	va_list arg;
	va_start(arg, formatString);
	vsprintf_s(buffer, 4096, formatString.c_str(), arg);
	va_end(arg);
	text = buffer;
	// Set very high z-order so color boxes draw on top of menu background
	// Use 10000 to ensure they're drawn after all other UI elements
	SetZOrder(10000);
}

/* Group Hook Initializer
 *		Used in conjuction with other basic hooks to create an advanced hook.
 */
Colorhook::Colorhook(HookGroup *group, unsigned int x, unsigned int y, unsigned int* color, std::string formatString, ...) :
Hook(group, x, y) {
	//Correctly format the string from the given arguments.
	currentColor = color;
	char buffer[4096];
	va_list arg;
	va_start(arg, formatString);
	vsprintf_s(buffer, 4096, formatString.c_str(), arg);
	va_end(arg);
	text = buffer;
	// Set very high z-order so color boxes draw on top of menu background
	// Use 10000 to ensure they're drawn after all other UI elements
	SetZOrder(10000);
}

void Colorhook::SetText(std::string newText) {
	char buffer[4096];
	va_list arg;
	va_start(arg, newText);
	vsprintf_s(buffer, 4096, newText.c_str(), arg);
	va_end(arg);
	Lock();
	text = buffer;
	Unlock();
}

void Colorhook::SetColor(unsigned int newColor) {
	if (newColor < 0 || newColor > 255)
		return;
	Lock();
	*currentColor = newColor;
	Unlock();
}

bool Colorhook::OnLeftClick(bool up, unsigned int x, unsigned int y) {
	if (Colorhook::current == this && x >= 310 && y >= 205 && x <= 490 && y <= 385 && up) {
		SetColor(curColor);
		Colorhook::current = false;
		return true;
	} else if (InRange(x,y) && Colorhook::current == false) {
		if (up)
			Colorhook::current = this;
		return true;
	}
	return false;
}

bool Colorhook::OnRightClick(bool up, unsigned int x, unsigned int y) {
	if (Colorhook::current == this) {
		Colorhook::current = false;
		return true;
	}
	return false;
}

/* GetXSize()
 *	Returns how long the text is.
 */
unsigned int Colorhook::GetXSize() {
	DWORD width, fileNo;
	wchar_t* wString = AnsiToUnicode(GetText().c_str());
	DWORD oldFont = D2WIN_SetTextSize(0);
	D2WIN_GetTextWidthFileNo(wString, &width, &fileNo);
	D2WIN_SetTextSize(oldFont);
	delete[] wString;
	return width; 
}

/* GetYSize()
 *	Returns how tall the text is.
 */
unsigned int Colorhook::GetYSize() {
	return 10;
}

void Colorhook::OnDraw() {
	Lock();
	if (Colorhook::current == this) {
		//Draw the shaded background
		Boxhook::Draw(0, 0, Hook::GetScreenWidth(), Hook::GetScreenHeight(), 0, BTOneHalf);
		
		// Calculate the bounds of the color picker area
		// Color grid: 16 columns (rows) from 321 to 481, ~17 rows (cols) from 190 to 360
		// Add padding for title (at 186) and instructions (at 368, 384)
		unsigned int pickerX = 310;
		unsigned int pickerY = 180;
		unsigned int pickerWidth = 190;
		unsigned int pickerHeight = 220;
		
		//Draw black opaque background in sections AROUND the color grid area
		//This ensures the background covers the UI menu but doesn't cover the color boxes
		//Top section (above color grid)
		D2GFX_DrawRectangle(pickerX, pickerY, pickerX + pickerWidth, 190, 0, BTFull);
		//Left section (left of color grid)
		D2GFX_DrawRectangle(pickerX, 190, 321, pickerY + pickerHeight, 0, BTFull);
		//Right section (right of color grid)
		D2GFX_DrawRectangle(481, 190, pickerX + pickerWidth, pickerY + pickerHeight, 0, BTFull);
		//Bottom section (below color grid)
		D2GFX_DrawRectangle(321, 360, 481, pickerY + pickerHeight, 0, BTFull);
		
		//Draw title on top of background
		Texthook::Draw(360, 186, false, 0, White, "Choose Color");
		
		//Draw color boxes in the clear area (321-481 x 190-360)
		//These should be visible since the background doesn't cover this area
		int col = 1, boxX1, boxX2, boxY1, boxY2;
		int mX = (*p_D2CLIENT_MouseX);
		int mY = (*p_D2CLIENT_MouseY);
		for (int n = 1, row = 1; n <= 255; n++, row++) {
			if (row == 16) {
				col++;
				row = 0;
			}
			//Color square begin/end pixel coordinates
			boxX1 = 321 + (row * 10);
			boxX2 = 331 + (row * 10);
			boxY1 = 190 + (col * 10);
			boxY2 = 200 + (col * 10);
			//Set current color based on mouse location
			if (mX >= boxX1 && mY >= boxY1 && mX <= boxX2 && mY <= boxY2)
				curColor = n;
			//Draw each color box - use D2GFX_DrawRectangle with BTNormal (5)
			//BTNormal seems to work better for colored rectangles
			//Color n is the palette index (1-255)
			D2GFX_DrawRectangle(boxX1, boxY1, boxX2, boxY2, n, BTNormal);
		}
		//Draw the +ish symbol showing the currently hovered color
		CHAR szLines[][2] = { 0,-2, 4,-4, 8,-2, 4,0, 8,2, 4,4, 0,2, -4,4, -8,2, -4,0, -8,-2, -4,-4, 0,-2 };
		for (unsigned int x = 0; x < 12; x++)
			D2GFX_DrawLine(457 + szLines[x][0], 380 + szLines[x][1], 457 + szLines[x + 1][0], 380 + szLines[x + 1][1], curColor, -1);
		//Draw instructions
		Texthook::Draw(320, 384, false, 0, White, "Left Click - Select");
		Texthook::Draw(320, 368, false, 0, White, "Right Click - Close");
	} else {
		DWORD size = D2WIN_SetTextSize(0);
		wchar_t* wText = AnsiToUnicode(GetText().c_str());
		D2WIN_DrawText(wText, GetX() + 13, GetY() + 10, InRange(*p_D2CLIENT_MouseX, *p_D2CLIENT_MouseY)?7:4, 0);
		delete[] wText;
		D2WIN_SetTextSize(size);
		// Draw the colored X shape - this should be on top due to high z-order (10000)
		// Draw it after text to ensure it's visible
		Crosshook::Draw(GetX(), GetY() + 4, GetColor());
	}
	Unlock();
}
