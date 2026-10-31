#include "inkview.h"
#include "platform/pocketbook/inkview_devices.h"

namespace {

// Draws one block of text in the given font (a missing font leaves the system's default) and returns the font to be closed, or null.
ifont* draw_block(const char* font_name, int size, int y, int height, const std::string& text) {
    ifont* font = OpenFont(font_name, size, 1);
    if (font) SetFont(font, BLACK);
    DrawTextRect(ScreenWidth() / 10, y, ScreenWidth() * 8 / 10, height, text.c_str(), ALIGN_CENTER | VALIGN_TOP);
    return font;
}

}  // namespace

void show_notice(const std::string& title, const std::string& text) {
    OpenScreen();
    ClearScreen();
    ifont* title_font = draw_block("LiberationSans-Bold", 56, ScreenHeight() / 4, ScreenHeight() / 8, title);
    ifont* text_font = draw_block("LiberationSans", 40, ScreenHeight() / 4 + ScreenHeight() / 6, ScreenHeight() / 3, text);
    FullUpdate();
    if (text_font) CloseFont(text_font);
    if (title_font) CloseFont(title_font);
}
