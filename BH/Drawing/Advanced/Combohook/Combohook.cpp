#include "Combohook.h"
#include "../../Basic/Framehook/Framehook.h"
#include "../../Basic/Texthook/Texthook.h"
#include "../../../D2Ptrs.h"

using namespace Drawing;
using namespace std;

Combohook* Combohook::currentActive = nullptr;

Combohook::Combohook(HookVisibility visibility, unsigned int x, unsigned int y, unsigned int xSize, unsigned int* index, std::vector<std::string> opts, unsigned int cols)
	: Hook(visibility, x, y) {
		SetXSize(xSize);
		currentIndex = index;
		options = opts;
		SetFont(0);
		active = false;
		columns = (cols < 1) ? 1 : cols;  // Ensure at least 1 column
		originalZOrder = GetZOrder();  // Store original z-order
}

Combohook::Combohook(HookGroup* group, unsigned int x, unsigned int y, unsigned int xSize, unsigned int* index, std::vector<std::string> opts, unsigned int cols)
	: Hook(group, x, y) {
		SetXSize(xSize);
		currentIndex = index;
		options = opts;
		SetFont(0);
		active = false;
		columns = (cols < 1) ? 1 : cols;  // Ensure at least 1 column
		originalZOrder = GetZOrder();  // Store original z-order
}

bool Combohook::OnLeftClick(bool up, unsigned int x, unsigned int y) {
	// If another dropdown is already active, prevent opening this one
	// The active dropdown will handle clicks within its area and block clicks outside
	if (currentActive && currentActive != this && !active) {
		// Another dropdown is active and this one isn't - block clicks on this dropdown's button
		// This prevents opening another dropdown when one is already open
		if (InHook(x, y)) {
			return true; // Block click on this dropdown's button
		}
	}
	
	// Check if we clicked on a inactive combo box.
	if (InHook(x, y) && !active) {
		if (up) {
			// Close any other active dropdown (shouldn't happen due to check above, but keep for safety)
			if (currentActive && currentActive != this) {
				currentActive->active = false;
				currentActive->SetZOrder(currentActive->originalZOrder);
			}
			active = true;
			currentActive = this;
			// Set very high z-order when active so dropdown draws on top of everything
			SetZOrder(50000);
		}

		return true;
	}
	if (active) {
		unsigned int itemsPerColumn = (options.size() + columns - 1) / columns;  // Ceiling division
		unsigned int maxHeight = itemsPerColumn * (GetYSize() + 4);
		if (x >= GetX() && y >= GetY() && x <= GetX() + GetXSize() && y <= GetY() + GetYSize() + 4 + maxHeight) {
		int n = 0;
			unsigned int columnWidth = GetXSize() / columns;
		for (vector<string>::iterator it = options.begin(); it < options.end(); it++,n++) {
				unsigned int col = n % columns;
				unsigned int row = n / columns;
				unsigned int textX = GetX() + (col * columnWidth);
				unsigned int textY = (GetY() + GetYSize() + 4 + (row * (GetYSize() + 4)));
				bool hovering = x >= textX && y >= textY && x <= textX + columnWidth && y <= textY + GetYSize() + 4;
			if (hovering && up) {
				SetSelectedIndex(n);
				active = false;
					if (currentActive == this)
						currentActive = nullptr;
					// Restore original z-order when closing
					SetZOrder(originalZOrder);
				return true;
			}
		}
			if (up) {
				active = false;
				if (currentActive == this)
					currentActive = nullptr;
				// Restore original z-order when closing
				SetZOrder(originalZOrder);
			}
			return true;
		} else {
			// Click is outside the dropdown area - close it and block the click
			if (up) {
			active = false;
				if (currentActive == this)
					currentActive = nullptr;
				// Restore original z-order when closing
				SetZOrder(originalZOrder);
			}
			// Return true to block clicks behind the dropdown when it's open
		return true;
	}
	}
	return false;
}

void Combohook::OnDraw() {
	Framehook::Draw(GetX(), GetY(), GetXSize(), GetYSize() + 4, 0, BTNormal);
	Texthook::Draw(GetX() + 5, GetY() + 3, 0, GetFont(), Gold, options.at(GetSelectedIndex()));
	//Framehook::Draw(GetX() + GetXSize() - 16, GetY(), 8, GetYSize() + 4, 0, BTNormal);
	Texthook::Draw(GetX() + GetXSize() - 8, GetY() + 3, 0, GetFont(), InHook((*p_D2CLIENT_MouseX), (*p_D2CLIENT_MouseY))||active?Tan:Gold, "v");

	if (active) {
		unsigned int itemsPerColumn = (options.size() + columns - 1) / columns;  // Ceiling division
		unsigned int dropdownHeight = itemsPerColumn * (GetYSize() + 4);
		unsigned int dropdownY = GetY() + GetYSize() + 4;
		
		// Use D2GFX_DrawRectangle for background to ensure proper rendering order
		// Draw black opaque background to hide everything behind it
		D2GFX_DrawRectangle(GetX(), dropdownY, GetX() + GetXSize(), dropdownY + dropdownHeight, 0, 5);
		
		// Then draw text on top of the background
		unsigned int mouseX = (*p_D2CLIENT_MouseX);
		unsigned int mouseY = (*p_D2CLIENT_MouseY);
		unsigned int n = 0;
		unsigned int columnWidth = GetXSize() / columns;
		for (vector<string>::iterator it = options.begin(); it < options.end(); it++,n++) {
			unsigned int col = n % columns;
			unsigned int row = n / columns;
			unsigned int textX = GetX() + (col * columnWidth) + 5;
			unsigned int textY = dropdownY + (row * (GetYSize() + 4));
			bool hovering = mouseX >= GetX() + (col * columnWidth) && mouseY >= textY && 
			                mouseX <= GetX() + ((col + 1) * columnWidth) && mouseY <= textY + GetYSize() + 4;
			// Draw text AFTER background so it appears on top
			Texthook::Draw(textX, textY + 2, 0, GetFont(), hovering?Tan:Gold, *it);
		}
	}
}
