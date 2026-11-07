#pragma once

// QR codes on the screen (for example: join the hotspot, open the web page).
// Draw the code of text at (x, y): each module is px x px pixels, with a white
// border of 2 modules. Returns the side in pixels (0 = the text is too long).
// The code is kept for the same text, so a call each frame is cheap.
int qr_draw(int x, int y, int px, const char *text);
// The side in pixels, with no drawing (0 = the text is too long).
int qr_side(int px, const char *text);
