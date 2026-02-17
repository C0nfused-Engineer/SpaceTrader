#define WIN32_LEAN_AND_MEAN
#include "termui.h"
#include <algorithm>

namespace termui {

int clampi(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

Canvas::Canvas()
: hOut_(GetStdHandle(STD_OUTPUT_HANDLE)),
  hIn_(GetStdHandle(STD_INPUT_HANDLE)) {}

void Canvas::configure(bool maximizeWindow, bool hideCursor) {
    SetConsoleOutputCP(CP_UTF8);
    setAttr(FG_WHITE);

    if (hideCursor) {
        CONSOLE_CURSOR_INFO ci{};
        GetConsoleCursorInfo(hOut_, &ci);
        ci.bVisible = FALSE;
        SetConsoleCursorInfo(hOut_, &ci);
    }

    DWORD mode = 0;
    GetConsoleMode(hIn_, &mode);
    mode |= ENABLE_WINDOW_INPUT;
    mode |= ENABLE_EXTENDED_FLAGS;
    mode &= ~ENABLE_QUICK_EDIT_MODE;
    SetConsoleMode(hIn_, mode);

    if (maximizeWindow) {
        HWND hwnd = GetConsoleWindow();
        if (hwnd) ShowWindow(hwnd, SW_MAXIMIZE);
    }
}

Size Canvas::windowSize() const {
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    GetConsoleScreenBufferInfo(hOut_, &csbi);
    int w = csbi.srWindow.Right  - csbi.srWindow.Left + 1;
    int h = csbi.srWindow.Bottom - csbi.srWindow.Top  + 1;
    return { w, h };
}

void Canvas::setAttr(WORD fg) { SetConsoleTextAttribute(hOut_, fg); }

void Canvas::gotoXY(short x, short y) {
    COORD c{ x, y };
    SetConsoleCursorPosition(hOut_, c);
}

void Canvas::clearRect(int x, int y, int w, int h, WORD attr) {
    DWORD written = 0;
    for (int row = 0; row < h; row++) {
        COORD pos{ (SHORT)x, (SHORT)(y + row) };
        FillConsoleOutputCharacterW(hOut_, L' ', w, pos, &written);
        FillConsoleOutputAttribute(hOut_, attr, w, pos, &written);
    }
}

void Canvas::clearAll(WORD attr) {
    auto s = windowSize();
    clearRect(0, 0, s.w, s.h, attr);
}

void Canvas::writeW(const std::wstring& s) {
    DWORD written = 0;
    WriteConsoleW(hOut_, s.c_str(), (DWORD)s.size(), &written, nullptr);
}

void Canvas::writeWAt(int x, int y, const std::wstring& s) {
    gotoXY((SHORT)x, (SHORT)y);
    writeW(s);
}

void Canvas::drawBox(const Rect& r, const std::wstring& title) {
    if (r.w < 2 || r.h < 2) return;

    const wchar_t tl = L'┌', tr = L'┐', bl = L'└', br = L'┘';
    const wchar_t hz = L'─', vt = L'│';

    gotoXY((SHORT)r.x, (SHORT)r.y);
    writeW(std::wstring(1, tl));
    writeW(std::wstring(r.w - 2, hz));
    writeW(std::wstring(1, tr));

    for (int y = 1; y < r.h - 1; y++) {
        gotoXY((SHORT)r.x, (SHORT)(r.y + y));
        writeW(std::wstring(1, vt));
        gotoXY((SHORT)(r.x + r.w - 1), (SHORT)(r.y + y));
        writeW(std::wstring(1, vt));
    }

    gotoXY((SHORT)r.x, (SHORT)(r.y + r.h - 1));
    writeW(std::wstring(1, bl));
    writeW(std::wstring(r.w - 2, hz));
    writeW(std::wstring(1, br));

    if (!title.empty() && r.w >= 6) {
        int maxTitle = r.w - 4;
        std::wstring t = title;
        if ((int)t.size() > maxTitle) t = t.substr(0, maxTitle - 1) + L"…";
        writeWAt(r.x + 2, r.y, t);
    }
}

void Canvas::clearInside(const Rect& r, WORD attr) {
    if (r.w < 3 || r.h < 3) return;
    clearRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, attr);
}

Layout computeLayout(int W, int H) {
    Layout L{};
    W = std::max(W, 70);
    H = std::max(H, 22);

    int hudH = 3;
    int logH = 7;
    logH = std::min(logH, H - hudH - 6);

    int midH = H - hudH - logH;

    int sideW = std::max(28, W / 3);
    sideW = std::min(sideW, W - 30);
    int mapW  = W - sideW;

    L.hud  = { 0, 0, W, hudH };
    L.map  = { 0, hudH, mapW, midH };
    L.side = { mapW, hudH, sideW, midH };
    L.log  = { 0, hudH + midH, W, logH };
    return L;
}

Input::Input(HANDLE hIn) : hIn_(hIn) {}

Action Input::readActionBlocking() {
    INPUT_RECORD ir{};
    DWORD read = 0;

    while (ReadConsoleInputW(hIn_, &ir, 1, &read) && read == 1) {
        if (ir.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            return { ActionType::Resize, 0, 0 };
        }
        if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
            WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;

            switch (vk) {
                case VK_ESCAPE: return { ActionType::Quit, 0, 0 };
                case VK_RETURN: return { ActionType::Confirm, 0, 0 };
                case VK_SPACE:  return { ActionType::Select, 0, 0 };
                case VK_BACK:   return { ActionType::Back, 0, 0 };
                case VK_TAB:
                    if (ir.Event.KeyEvent.dwControlKeyState & SHIFT_PRESSED)
                        return { ActionType::TabLeft, 0, 0 };
                    else
                        return { ActionType::TabRight, 0, 0 };
                case VK_LEFT:   return { ActionType::Move, -1, 0 };
                case VK_RIGHT:  return { ActionType::Move, +1, 0 };
                case VK_UP:     return { ActionType::Move, 0, -1 };
                case VK_DOWN:   return { ActionType::Move, 0, +1 };
                default: break;
            }

            wchar_t ch = ir.Event.KeyEvent.uChar.UnicodeChar;
            if (ch == L'a' || ch == L'A') return { ActionType::Move, -1, 0 };
            if (ch == L'd' || ch == L'D') return { ActionType::Move, +1, 0 };
            if (ch == L'w' || ch == L'W') return { ActionType::Move, 0, -1 };
            if (ch == L's' || ch == L'S') return { ActionType::Move, 0, +1 };

            if (ch == L'q' || ch == L'Q') return { ActionType::Back, 0, 0 };
            if (ch == L'l' || ch == L'L') return { ActionType::ClearLog, 0, 0 };
            if (ch == L'e' || ch == L'E') return { ActionType::SidebarToggle, 0, 0 };
            if (ch == L'n' || ch == L'N') return { ActionType::No, 0, 0 };
        }
    }
    return { ActionType::None, 0, 0 };
}

} // namespace termui
