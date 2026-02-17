#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace termui {

struct Size { int w = 0, h = 0; };
struct Rect { int x = 0, y = 0, w = 0, h = 0; };
struct Layout { Rect hud, map, side, log; };

constexpr WORD FG_WHITE  = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
constexpr WORD FG_BRIGHT = FOREGROUND_INTENSITY;
constexpr WORD FG_GREEN  = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
constexpr WORD FG_RED    = FOREGROUND_RED   | FOREGROUND_INTENSITY;

int clampi(int v, int lo, int hi);

class Canvas {
public:
    Canvas();

    HANDLE out() const { return hOut_; }
    HANDLE in()  const { return hIn_;  }

    void configure(bool maximizeWindow = true, bool hideCursor = true);
    Size windowSize() const;

    void setAttr(WORD fg);
    void gotoXY(short x, short y);

    void clearRect(int x, int y, int w, int h, WORD attr = FG_WHITE);
    void clearAll(WORD attr = FG_WHITE);

    void writeW(const std::wstring& s);
    void writeWAt(int x, int y, const std::wstring& s);

    void drawBox(const Rect& r, const std::wstring& title = L"");
    void clearInside(const Rect& r, WORD attr = FG_WHITE);

private:
    HANDLE hOut_;
    HANDLE hIn_;
};

Layout computeLayout(int W, int H);

enum class ActionType {
    None, Resize, Quit,
    Move, Confirm, Select, Back,
    TabLeft, TabRight,
    ClearLog, Yes, No,
    SidebarToggle, PlotRoute,
};

struct Action {
    ActionType type = ActionType::None;
    int dx = 0;
    int dy = 0;
};

class Input {
public:
    explicit Input(HANDLE hIn);
    Action readActionBlocking();
private:
    HANDLE hIn_;
};

} // namespace termui
