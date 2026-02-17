#include "termui.h"

#include <string>
#include <vector>
#include <deque>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <iostream>

static constexpr int GALAXY_JUMP_RANGE = 3;
static constexpr int SYSTEM_JUMP_RANGE = 6;

// Balance knobs (tweak later)
static constexpr int GALAXY_FUEL_PER_JUMP = 3;
static constexpr int SYSTEM_FUEL_PER_JUMP = 1;

// Chebyshev distance = max(|dx|, |dy|) (fits square jump range)
static int chebyshev(int x0,int y0,int x1,int y1){
    return std::max(std::abs(x0-x1), std::abs(y0-y1));
}
static int jumpsRequired(int dist, int range){
    return (dist + range - 1) / range;
}

// Move from (x,y) toward (tx,ty) by at most `range` in Chebyshev metric
static void stepToward(int& x, int& y, int tx, int ty, int range){
    int dx = tx - x;
    int dy = ty - y;
    int stepX = (dx==0 ? 0 : (dx>0 ? 1 : -1));
    int stepY = (dy==0 ? 0 : (dy>0 ? 1 : -1));

    // Take up to `range` unit steps (diagonal allowed)
    for(int i=0;i<range;i++){
        if (x==tx && y==ty) break;
        if (x!=tx) x += stepX;
        if (y!=ty) y += stepY;
    }
}


// ---------------- Helpers ----------------
static std::wstring ellipsize(const std::wstring& s, int maxw) {
    if ((int)s.size() <= maxw) return s;
    if (maxw <= 1) return L"…";
    return s.substr(0, maxw - 1) + L"…";
}
static int manhattan(int x0,int y0,int x1,int y1){ return std::abs(x0-x1)+std::abs(y0-y1); }

// ---------------- Time ----------------
struct GameDate {
    int year=2336, month=0, week=0; // Jan 2336
    void advanceWeeks(int n) {
        for(int i=0;i<n;i++){
            week++;
            if(week>=4){ week=0; month++; if(month>=12){ month=0; year++; } }
        }
    }
    std::wstring toString() const {
        static const wchar_t* M[12]={L"Jan",L"Feb",L"Mar",L"Apr",L"May",L"Jun",L"Jul",L"Aug",L"Sep",L"Oct",L"Nov",L"Dec"};
        std::wstringstream oss;
        oss<<M[month]<<L" "<<year<<L"  W"<<(week+1)<<L"/4";
        return oss.str();
    }
};

// ---------------- Goods / Market ----------------
enum class Good : int { Food, Water, Ore, Fuel, Electronics, Meds, COUNT };
static const wchar_t* GOOD_NAME[] = { L"Food", L"Water", L"Ore", L"Fuel", L"Electronics", L"Meds" };

struct Market {
    int price[(int)Good::COUNT]{};
    int stock[(int)Good::COUNT]{};
    int priceOf(Good g) const { return price[(int)g]; }
};

// ---------------- POIs ----------------
enum class PoiType { Planet, Station, Outpost };

static std::wstring poiTypeNameW(PoiType t) {
    switch (t) {
        case PoiType::Planet:  return L"Planet";
        case PoiType::Station: return L"Station";
        case PoiType::Outpost: return L"Outpost";
        default:               return L"POI";
    }
}

struct SystemPoi {
    std::wstring name;
    PoiType type;
    int x=0, y=0;
    Market market;
};

struct StarSystem {
    std::wstring name;
    int gx=0, gy=0;
    std::vector<SystemPoi> pois;
};

// ---------------- Player ----------------
struct Player {
    int credits = 2500;

    int fuel = 40;
    int fuelMax = 60;

    int cargoMax = 40;
    int cargo[(int)Good::COUNT]{};

    int crew = 1;       // NEW: starting crew = 1
    int crewMax = 12;

    int cargoUsed() const {
        int s=0;
        for(int i=0;i<(int)Good::COUNT;i++){
            if ((Good)i == Good::Fuel) continue;
            s += cargo[i];
        }
        return s;
    }
};

// ---------------- Missions ----------------
struct Mission {
    bool active = false;
    bool completed = false;

    int fromSystem = -1;
    int fromPoi    = -1;

    int toSystem   = -1;
    int toPoi      = -1;   // NEW: delivery POI within destination system

    Good good = Good::Ore;
    int amount = 0;
    int reward = 0;
    int deadlineWeeks = 0;
};

static std::wstring goodNameW(Good g){ return GOOD_NAME[(int)g]; }

// ---------------- Game ----------------
enum class Screen { Galaxy, System, Market };
enum class SidebarPage { Status, Cargo, Missions }; // NEW: Missions page

struct GameState {
    GameDate date;

    int incomeWeekly = 0; // weekly credits set to zero (crew pay comes later)
    int prestige = 10;
    int prestigeWeekly = 0;


    Player P;
    std::vector<StarSystem> galaxy;

    Screen screen = Screen::Galaxy;
    SidebarPage sidePage = SidebarPage::Status;

    // Galaxy
    int gCurX=0, gCurY=0, gCamX=0, gCamY=0;
    int currentSystem = 0;

    // System
    int sCurX=0, sCurY=0, sCamX=0, sCamY=0;
    int shipX=0, shipY=0;

    // Market
    int marketSel = 0;
    bool marketModeBuy = true;

    // Log
    std::deque<std::wstring> log;

    // Missions
    std::vector<Mission> activeMissions;

    // NEW: offers available at current POI
    int dockPoiIndex = 0;
    std::vector<Mission> poiOffers;
    int offerSel = 0;

    void pushLog(const std::wstring& s) {
        log.push_front(s);
        while(log.size()>200) log.pop_back();
    }
    void clearLog() { log.clear(); }
	
	// Galaxy ship position (can be empty space)
	int shipGX = 0, shipGY = 0;
	
	// Route overlay
	std::vector<std::pair<int,int>> routeGalaxy;
	std::vector<std::pair<int,int>> routeSystem;
	bool showRouteGalaxy = false;
	bool showRouteSystem = false;

};

static std::vector<std::pair<int,int>> buildRoute(int sx,int sy,int tx,int ty,int range){
    std::vector<std::pair<int,int>> out;
    int x = sx, y = sy;
    while (!(x==tx && y==ty)) {
        stepToward(x, y, tx, ty, range);
        out.push_back({x,y}); // each hop endpoint
        if ((int)out.size() > 256) break; // safety
    }
    return out;
}
static bool routeContains(const std::vector<std::pair<int,int>>& r, int x, int y){
    for (auto& p : r) if (p.first==x && p.second==y) return true;
    return false;
}


// Returns true if any active mission targets this system. Also counts how many.
static int countMissionsToSystem(const GameState& S, int systemIndex) {
    int c = 0;
    for (const auto& m : S.activeMissions) {
        if (!m.active || m.completed) continue;
        if (m.toSystem == systemIndex) c++;
    }
    return c;
}

// Returns true if any active mission targets this exact POI in the current system.
// If found, fills out short details for display.
static bool firstMissionToPoiHere(const GameState& S, int poiIndex, Mission& out) {
    for (const auto& m : S.activeMissions) {
        if (!m.active || m.completed) continue;
        if (m.toSystem == S.currentSystem && m.toPoi == poiIndex) {
            out = m;
            return true;
        }
    }
    return false;
}

// ---------------- RNG ----------------
static uint32_t hash32(uint32_t x){
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static Market makeMarket(uint32_t seed, PoiType t) {
    Market m{};
    uint32_t s = hash32(seed);
    auto rand01 = [&]() -> int { s = hash32(s); return (int)(s % 100); };

    int base[(int)Good::COUNT] = { 18, 10, 32, 25, 80, 60 };
    int mod[(int)Good::COUNT]{};

    if (t == PoiType::Planet) {
        mod[(int)Good::Food] = -4; mod[(int)Good::Water] = -2;
        mod[(int)Good::Ore]  = +4; mod[(int)Good::Fuel]  = +2;
    } else if (t == PoiType::Station) {
        mod[(int)Good::Fuel] = -6; mod[(int)Good::Electronics] = -5;
    } else {
        mod[(int)Good::Meds] = +10; mod[(int)Good::Fuel] = +8;
    }

    for(int i=0;i<(int)Good::COUNT;i++){
        int jitter = (rand01() - 50) / 5;
        int p = base[i] + mod[i] + jitter;
        if (p < 1) p = 1;
        m.price[i] = p;
        m.stock[i] = 50 + rand01();
    }
    return m;
}

// ---------------- POI helpers ----------------
static int poiIndexAt(const StarSystem& sys, int x, int y) {
    for (int i=0;i<(int)sys.pois.size();i++) if (sys.pois[i].x==x && sys.pois[i].y==y) return i;
    return -1;
}
static int nearestPoiIndex(const StarSystem& sys, int x, int y) {
    int best=-1, bestD=1e9;
    for(int i=0;i<(int)sys.pois.size();i++){
        int d = manhattan(x,y,sys.pois[i].x,sys.pois[i].y);
        if (d < bestD){ bestD=d; best=i; }
    }
    return best;
}

// ---------------- Economy & travel ----------------
static void advanceWeek(GameState& S, int weeks) {
    S.date.advanceWeeks(weeks);
    S.P.credits += S.incomeWeekly * weeks;   // currently 0
    S.prestige  += S.prestigeWeekly * weeks;
}
static int ftlFuelCost(int dist) { return std::max(1, dist / 3); }

// ---------------- Missions: deadlines + completion ----------------
static void tickMissionDeadlines(GameState& S, int weeksAdvanced) {
    for (auto& m : S.activeMissions) {
        if (!m.active || m.completed) continue;
        m.deadlineWeeks -= weeksAdvanced;
    }
    for (auto& m : S.activeMissions) {
        if (!m.active || m.completed) continue;
        if (m.deadlineWeeks < 0) {
            m.active = false;
            std::wstringstream oss;
            oss << L"Mission FAILED: Delivery to " << S.galaxy[m.toSystem].name << L" expired.";
            S.pushLog(oss.str());
        }
    }
}

static void tryCompleteMissionsOnDock(GameState& S) {
    const StarSystem& sys = S.galaxy[S.currentSystem];

    for (auto& m : S.activeMissions) {
        if (!m.active || m.completed) continue;

        if (m.toSystem != S.currentSystem) continue;
        if (m.toPoi != S.dockPoiIndex) continue;

        int have = S.P.cargo[(int)m.good];
        if (have >= m.amount) {
            S.P.cargo[(int)m.good] -= m.amount;
            S.P.credits += m.reward;
            m.completed = true;
            m.active = false;

            std::wstringstream oss;
            oss << L"Mission COMPLETE: Delivered " << m.amount << L" " << goodNameW(m.good)
                << L" to " << sys.pois[m.toPoi].name << L" (+" << m.reward << L" CR).";
            S.pushLog(oss.str());
        } else {
            std::wstringstream oss;
            oss << L"Delivery pending at " << sys.pois[m.toPoi].name << L": Need "
                << (m.amount - have) << L" more " << goodNameW(m.good) << L".";
            S.pushLog(oss.str());
        }
    }
}


// ---------------- NEW: generate offers at a POI ----------------
static void generateOffersForDock(GameState& S) {
    const StarSystem& sys = S.galaxy[S.currentSystem];
    int poi = S.dockPoiIndex;

    // deterministic-ish per (system, poi, date)
    uint32_t seed = 0xBADC0DEu;
    seed ^= (uint32_t)S.currentSystem * 0x9E3779B9u;
    seed ^= (uint32_t)poi * 0x85EBCA6Bu;
    seed ^= (uint32_t)(S.date.year * 131u + S.date.month * 17u + S.date.week);

    uint32_t r = hash32(seed);

    auto roll = [&](int mod)->int {
        r = hash32(r + (uint32_t)mod);
        return (int)(r % 100);
    };

    S.poiOffers.clear();

    int count = (roll(1) % 4); // 0..3 offers
    for (int k=0;k<count;k++) {
		Mission m{};
		m.active = true;
		m.fromSystem = S.currentSystem;
		m.fromPoi    = poi;

		int destSys = (int)(hash32(r + 100 + k) % (uint32_t)S.galaxy.size());
		if (destSys == S.currentSystem) destSys = (destSys + 1) % (int)S.galaxy.size();
		m.toSystem = destSys;

		// NEW: pick a destination POI inside that system
		const StarSystem& dst = S.galaxy[m.toSystem];
		int destPoi = (int)(hash32(r + 150 + k) % (uint32_t)dst.pois.size());
		m.toPoi = destPoi;

		int gi = (int)(hash32(r + 200 + k) % (uint32_t)((int)Good::COUNT - 1));
		Good g = (Good)gi;
		if (g == Good::Fuel) g = Good::Ore;
		m.good = g;

		m.amount = 3 + (int)(hash32(r + 300 + k) % 10); // 3..12

		int dist = manhattan(S.galaxy[S.currentSystem].gx, S.galaxy[S.currentSystem].gy,
							 S.galaxy[m.toSystem].gx,      S.galaxy[m.toSystem].gy);

		m.deadlineWeeks = 6 + dist * 2;
		m.reward = 150 + m.amount * (25 + (int)(hash32(r + 400 + k) % 45)) + dist * 10;

		S.poiOffers.push_back(m);
	}

    S.offerSel = 0;

    // prompt: missions available here (sidebar menu)
    if (!S.poiOffers.empty()) {
        std::wstringstream oss;
        oss << L"New contracts available at " << sys.pois[poi].name << L". Press E to open Missions.";
        S.pushLog(oss.str());
    } else {
        std::wstringstream oss;
        oss << L"No contracts posted at " << sys.pois[poi].name << L" this week.";
        S.pushLog(oss.str());
    }
}

static void dockAtPoi(GameState& S, int poiIndex, bool autoOpenMissions) {
    const StarSystem& sys = S.galaxy[S.currentSystem];

    S.dockPoiIndex = poiIndex;
    S.shipX = sys.pois[poiIndex].x;
    S.shipY = sys.pois[poiIndex].y;

    // NEW: attempt mission completions on docking
    tryCompleteMissionsOnDock(S);

    // Generate new offers at this dock
    generateOffersForDock(S);

    if (autoOpenMissions && !S.poiOffers.empty()) {
        S.sidePage = SidebarPage::Missions;
    }
}


static void acceptSelectedOffer(GameState& S) {
    if (S.poiOffers.empty()) return;
    S.offerSel = termui::clampi(S.offerSel, 0, (int)S.poiOffers.size()-1);

    Mission m = S.poiOffers[S.offerSel];
    S.activeMissions.push_back(m);

    const StarSystem& sys = S.galaxy[S.currentSystem];
	const StarSystem& dst = S.galaxy[m.toSystem];

	std::wstringstream oss;
	oss << L"Accepted mission from " << sys.pois[m.fromPoi].name << L": deliver "
		<< m.amount << L" " << goodNameW(m.good)
		<< L" to " << dst.name << L" / " << dst.pois[m.toPoi].name
		<< L" (" << m.deadlineWeeks << L"w).";
	S.pushLog(oss.str());

    S.poiOffers.erase(S.poiOffers.begin() + S.offerSel);
    if (S.offerSel >= (int)S.poiOffers.size()) S.offerSel = std::max(0, (int)S.poiOffers.size()-1);
}

static void declineSelectedOffer(GameState& S) {
    if (S.poiOffers.empty()) return;
    S.offerSel = termui::clampi(S.offerSel, 0, (int)S.poiOffers.size()-1);

    Mission m = S.poiOffers[S.offerSel];
    const StarSystem& dst = S.galaxy[m.toSystem];
	std::wstringstream oss;
	oss << L"Declined mission: deliver " << m.amount << L" " << goodNameW(m.good)
		<< L" to " << dst.name << L" / " << dst.pois[m.toPoi].name << L".";
	S.pushLog(oss.str());


    S.poiOffers.erase(S.poiOffers.begin() + S.offerSel);
    if (S.offerSel >= (int)S.poiOffers.size()) S.offerSel = std::max(0, (int)S.poiOffers.size()-1);
}

// ---------------- World init ----------------
static void initGalaxy(GameState& S) {
    S.galaxy = {
        { L"Sol",     12, 10, {} },
        { L"Arcadia", 28,  8, {} },
        { L"Kestrel", 36, 16, {} },
        { L"Orpheon", 18, 22, {} },
        { L"Vesta",    6, 20, {} },
        { L"Helios",  30, 24, {} },
    };

    auto addPois = [&](StarSystem& sys, uint32_t sysSeed) {
        sys.pois.push_back({ sys.name + L" Prime", PoiType::Planet, 10, 8,  makeMarket(sysSeed + 1, PoiType::Planet) });
        sys.pois.push_back({ L"Highport Station",  PoiType::Station,22, 6,  makeMarket(sysSeed + 2, PoiType::Station) });
        sys.pois.push_back({ L"Outer Belt",        PoiType::Outpost,32, 14, makeMarket(sysSeed + 3, PoiType::Outpost) });
        if (sys.name == L"Sol")   sys.pois.push_back({ L"Luna Yard",  PoiType::Station,16, 10, makeMarket(sysSeed + 4, PoiType::Station) });
        if (sys.name == L"Vesta") sys.pois.push_back({ L"Red Clinic", PoiType::Outpost,26, 12, makeMarket(sysSeed + 5, PoiType::Outpost) });
    };

    for (size_t i=0;i<S.galaxy.size();i++) addPois(S.galaxy[i], (uint32_t)(0xC0FFEEu + i*1337u));

    S.currentSystem = 0;
    S.gCurX = S.galaxy[0].gx; S.gCurY = S.galaxy[0].gy;

    // Start docked at first POI
    S.sCurX = S.galaxy[0].pois[0].x;
    S.sCurY = S.galaxy[0].pois[0].y;

    S.clearLog();
    S.pushLog(L"Welcome to Space Trader.");
    S.pushLog(L"TAB: Galaxy/System (Market TAB toggles Buy/Sell).");
    S.pushLog(L"E: Sidebar page (Status/Cargo/Missions).");
    S.pushLog(L"In Missions page: Up/Down select, ENTER/Y accept, N decline, Q back.");

    dockAtPoi(S, 0, /*autoOpenMissions=*/false);
}

// ---------------- UI helpers ----------------
static void panelPrintLine(termui::Canvas& C, const termui::Rect& r, int& y, const std::wstring& s, WORD attr=termui::FG_WHITE) {
    if (y >= r.y + r.h - 1) return;
    int x = r.x + 2;
    int w = r.w - 4;
    C.gotoXY((SHORT)x, (SHORT)y);
    C.setAttr(attr);
    std::wstring out = ellipsize(s, w);
    if ((int)out.size() < w) out += std::wstring(w - out.size(), L' ');
    C.writeW(out);
    C.setAttr(termui::FG_WHITE);
    y++;
}

// ---------------- System best-price helpers (word-of-mouth) ----------------
struct BestInfo {
    int minPrice = 0, maxPrice = 0;
    int minPoi = -1, maxPoi = -1;
};

static BestInfo computeBestInSystem(const StarSystem& sys, Good g) {
    BestInfo bi{};
    bi.minPrice = 1e9; bi.maxPrice = -1e9;

    for (int i=0;i<(int)sys.pois.size();i++) {
        int p = sys.pois[i].market.priceOf(g);
        if (p < bi.minPrice) { bi.minPrice = p; bi.minPoi = i; }
        if (p > bi.maxPrice) { bi.maxPrice = p; bi.maxPoi = i; }
    }
    if (bi.minPoi < 0) bi.minPoi = 0;
    if (bi.maxPoi < 0) bi.maxPoi = 0;
    return bi;
}

// ---------------- Rendering ----------------
static void renderHUD(termui::Canvas& C, const termui::Rect& r, const GameState& S) {
    C.drawBox(r, L"HUD");
    C.clearInside(r, termui::FG_WHITE);

    int x = r.x + 2, y = r.y + 1;

    std::wstring date = S.date.toString();
    std::wstring dateChunk = L" " + date + L" ";
    int dateX = r.x + r.w - 2 - (int)dateChunk.size();

    C.gotoXY((SHORT)x,(SHORT)y);
    C.setAttr(termui::FG_BRIGHT | termui::FG_WHITE);

    std::wstringstream left;
    left << L"CR: " << S.P.credits
         << L"  Crew: " << S.P.crew << L"/" << S.P.crewMax
         << L"  Fuel: " << S.P.fuel << L"/" << S.P.fuelMax
         << L"  Cargo: " << S.P.cargoUsed() << L"/" << S.P.cargoMax
         << L"  CR/wk: " << S.incomeWeekly;

    C.writeW(ellipsize(left.str(), std::max(0, dateX - x - 2)));

    C.gotoXY((SHORT)dateX,(SHORT)y);
    C.setAttr(termui::FG_BRIGHT | termui::FG_WHITE);
    C.writeW(dateChunk);
    C.setAttr(termui::FG_WHITE);
}

static void galaxyEnsureCursorVisible(GameState& S, int viewCols, int viewRows, int worldW, int worldH) {
    if (worldW <= viewCols) S.gCamX = 0;
    else { if (S.gCurX < S.gCamX) S.gCamX = S.gCurX; if (S.gCurX >= S.gCamX + viewCols) S.gCamX = S.gCurX - viewCols + 1; }
    if (worldH <= viewRows) S.gCamY = 0;
    else { if (S.gCurY < S.gCamY) S.gCamY = S.gCurY; if (S.gCurY >= S.gCamY + viewRows) S.gCamY = S.gCurY - viewRows + 1; }
    S.gCamX = termui::clampi(S.gCamX, 0, std::max(0, worldW - viewCols));
    S.gCamY = termui::clampi(S.gCamY, 0, std::max(0, worldH - viewRows));
}
static void systemEnsureCursorVisible(GameState& S, int viewCols, int viewRows, int worldW, int worldH) {
    if (worldW <= viewCols) S.sCamX = 0;
    else { if (S.sCurX < S.sCamX) S.sCamX = S.sCurX; if (S.sCurX >= S.sCamX + viewCols) S.sCamX = S.sCurX - viewCols + 1; }
    if (worldH <= viewRows) S.sCamY = 0;
    else { if (S.sCurY < S.sCamY) S.sCamY = S.sCurY; if (S.sCurY >= S.sCamY + viewRows) S.sCamY = S.sCurY - viewRows + 1; }
    S.sCamX = termui::clampi(S.sCamX, 0, std::max(0, worldW - viewCols));
    S.sCamY = termui::clampi(S.sCamY, 0, std::max(0, worldH - viewRows));
}

// Galaxy QoL markers: Cursor=■, Cursor-on-system=□, Ship=▲, overlap=▣
static void renderGalaxyMap(termui::Canvas& C, const termui::Rect& r, GameState& S) {
    C.drawBox(r, L"GALAXY MAP  (ENTER=FTL  TAB=System)");
    C.clearInside(r, termui::FG_WHITE);

    const int GW=45, GH=30;
    S.gCurX = termui::clampi(S.gCurX, 0, GW-1);
    S.gCurY = termui::clampi(S.gCurY, 0, GH-1);

    int ix = r.x + 1, iy = r.y + 1, iw = r.w - 2, ih = r.h - 2;
    int cellW = 2;
    int cols = std::max(1, iw / cellW);
    int rows = std::max(1, ih);

    galaxyEnsureCursorVisible(S, cols, rows, GW, GH);

    auto sysAt = [&](int x,int y)->int{
        for(int i=0;i<(int)S.galaxy.size();i++)
            if (S.galaxy[i].gx==x && S.galaxy[i].gy==y) return i;
        return -1;
    };

    int shipGX = S.galaxy[S.currentSystem].gx;
    int shipGY = S.galaxy[S.currentSystem].gy;

    for(int row=0; row<rows; row++){
        int gy = S.gCamY + row;
        C.gotoXY((SHORT)ix, (SHORT)(iy+row));
        std::wstring line; line.reserve((size_t)cols * (size_t)cellW);

        for(int col=0; col<cols; col++){
            int gx = S.gCamX + col;

            int si = sysAt(gx, gy);
            bool isSystem = (si >= 0);

            wchar_t base = isSystem ? L'✦' : L'·';

            bool isShip = (gx == shipGX && gy == shipGY);
            bool isCur  = (gx == S.gCurX && gy == S.gCurY);

            wchar_t g = base;
            if (isShip && isCur) g = L'▣';
            else if (isShip)     g = L'▲';
            else if (isCur)      g = isSystem ? L'□' : L'■';

            line.push_back(g);
            line.push_back(L' ');
        }

        if ((int)line.size() > iw) line.resize(iw);
        C.writeW(line);
    }
}

static void renderSystemMap(termui::Canvas& C, const termui::Rect& r, GameState& S) {
    const StarSystem& sys = S.galaxy[S.currentSystem];
    std::wstring title =
        L"SYSTEM: " + sys.name + L"  (ENTER=STL  SPACE=Market  TAB=Galaxy)";
    C.drawBox(r, title);
    C.clearInside(r, termui::FG_WHITE);

    const int SW=40, SH=20;
    S.sCurX = termui::clampi(S.sCurX, 0, SW-1);
    S.sCurY = termui::clampi(S.sCurY, 0, SH-1);

    int ix = r.x + 1, iy = r.y + 1, iw = r.w - 2, ih = r.h - 2;
    int cellW = 2;
    int cols = std::max(1, iw / cellW);
    int rows = std::max(1, ih);

    systemEnsureCursorVisible(S, cols, rows, SW, SH);

    for(int row=0; row<rows; row++){
        int sy = S.sCamY + row;
        C.gotoXY((SHORT)ix, (SHORT)(iy+row));
        std::wstring line; line.reserve((size_t)cols * (size_t)cellW);

        for(int col=0; col<cols; col++){
            int sx = S.sCamX + col;

            wchar_t base = L'·';
            int pi = poiIndexAt(sys, sx, sy);
            if (pi >= 0) {
                if (sys.pois[pi].type == PoiType::Planet) base = L'◉';
                else if (sys.pois[pi].type == PoiType::Station) base = L'⛯';
                else base = L'◈';
            }

            bool isShip = (sx == S.shipX && sy == S.shipY);
            bool isCur  = (sx == S.sCurX  && sy == S.sCurY);

            wchar_t g = base;
            if (isShip && isCur) g = L'▣';
            else if (isShip)     g = L'▲';
            else if (isCur)      g = L'■';

            line.push_back(g);
            line.push_back(L' ');
        }
        if ((int)line.size() > iw) line.resize(iw);
        C.writeW(line);
    }
}

static void renderMarket(termui::Canvas& C, const termui::Rect& r, GameState& S) {
    const StarSystem& sys = S.galaxy[S.currentSystem];

    int shipPoi = poiIndexAt(sys, S.shipX, S.shipY);
    if (shipPoi < 0) shipPoi = nearestPoiIndex(sys, S.shipX, S.shipY);
    const SystemPoi& poi = sys.pois[shipPoi];

    std::wstring title = L"MARKET: " + poi.name + L"  (TAB=Buy/Sell, ENTER=Trade, Q=Back)";
    C.drawBox(r, title);
    C.clearInside(r, termui::FG_WHITE);

    int x0 = r.x + 2;
    int y0 = r.y + 1;
    int w  = r.w - 4;

    // Header
    C.gotoXY((SHORT)x0, (SHORT)y0);
    C.setAttr(termui::FG_BRIGHT | termui::FG_WHITE);
    {
        std::wstringstream oss;
        oss << (S.marketModeBuy ? L"[BUY] " : L"[SELL] ")
            << L"Credits: " << S.P.credits
            << L"  Fuel: " << S.P.fuel << L"/" << S.P.fuelMax
            << L"  Cargo: " << S.P.cargoUsed() << L"/" << S.P.cargoMax;
        std::wstring line = ellipsize(oss.str(), w);
        if ((int)line.size() < w) line += std::wstring(w - line.size(), L' ');
        C.writeW(line);
    }
    C.setAttr(termui::FG_WHITE);

    // Rumor lines for selected good (no prices)
    Good selG = (Good)S.marketSel;
    BestInfo selBI = computeBestInSystem(sys, selG);
    {
        std::wstringstream a, b;
        a << L"Rumor: " << GOOD_NAME[(int)selG] << L" is cheapest at";
        b << L"       " << sys.pois[selBI.minPoi].name << L"; priciest at " << sys.pois[selBI.maxPoi].name << L".";

        C.gotoXY((SHORT)x0, (SHORT)(y0 + 1));
        C.setAttr(termui::FG_BRIGHT | termui::FG_WHITE);
        std::wstring la = ellipsize(a.str(), w);
        if ((int)la.size() < w) la += std::wstring(w - la.size(), L' ');
        C.writeW(la);

        C.gotoXY((SHORT)x0, (SHORT)(y0 + 2));
        C.setAttr(termui::FG_WHITE);
        std::wstring lb = ellipsize(b.str(), w);
        if ((int)lb.size() < w) lb += std::wstring(w - lb.size(), L' ');
        C.writeW(lb);

        C.setAttr(termui::FG_WHITE);
    }

    int rowStart = y0 + 4;

    for(int i=0;i<(int)Good::COUNT && (rowStart+i) < (r.y+r.h-2); i++){
        Good g = (Good)i;
        int price = poi.market.priceOf(g);

        BestInfo bi = computeBestInSystem(sys, g);
        bool cheapestHere = (bi.minPoi == shipPoi);
        bool priciestHere = (bi.maxPoi == shipPoi);

        C.gotoXY((SHORT)x0, (SHORT)(rowStart+i));
        bool sel = (i == S.marketSel);
        C.setAttr(sel ? (termui::FG_BRIGHT | termui::FG_WHITE) : termui::FG_WHITE);

        std::wstringstream oss;
        oss << (sel ? L"> " : L"  ")
            << std::left << std::setw(12) << GOOD_NAME[i]
            << L" Price: " << std::setw(4) << price;

        if (cheapestHere)      oss << L"  [CHEAP HERE]";
        else if (priciestHere) oss << L"  [EXPENSIVE HERE]";
        else                   oss << L"               ";

        if (g == Good::Fuel) {
            oss << L" You: " << std::setw(3) << S.P.fuel;
            if (S.marketModeBuy) {
                int maxBuy = std::min(S.P.credits / std::max(1,price), S.P.fuelMax - S.P.fuel);
                oss << L" MaxBuy: " << maxBuy;
            } else {
                oss << L" MaxSell: " << S.P.fuel;
            }
        } else {
            oss << L" You: " << std::setw(3) << S.P.cargo[i];
            if (S.marketModeBuy) {
                int maxBuy = std::min(S.P.credits / std::max(1,price), S.P.cargoMax - S.P.cargoUsed());
                oss << L" MaxBuy: " << maxBuy;
            } else {
                oss << L" MaxSell: " << S.P.cargo[i];
            }
        }

        std::wstring line = ellipsize(oss.str(), w);
        if ((int)line.size() < w) line += std::wstring(w - line.size(), L' ');
        C.writeW(line);
    }

    int fy = r.y + r.h - 2;
    C.gotoXY((SHORT)x0, (SHORT)fy);
    C.setAttr(termui::FG_WHITE);
    std::wstring help = L"Up/Down: select | ENTER: trade 1 | TAB: buy/sell | Q: back | E: sidebar | L: clear log";
    help = ellipsize(help, w);
    if ((int)help.size() < w) help += std::wstring(w - help.size(), L' ');
    C.writeW(help);
}

static void renderSidebar(termui::Canvas& C, const termui::Rect& r, const GameState& S) {
    std::wstring title;
    if (S.sidePage == SidebarPage::Status)   title = L"SIDEBAR: STATUS (E)";
    if (S.sidePage == SidebarPage::Cargo)    title = L"SIDEBAR: CARGO (E)";
    if (S.sidePage == SidebarPage::Missions) title = L"SIDEBAR: MISSIONS (E)";
    C.drawBox(r, title);
    C.clearInside(r, termui::FG_WHITE);

    int y = r.y + 1;
    auto section = [&](const std::wstring& t){
        panelPrintLine(C, r, y, t, termui::FG_BRIGHT | termui::FG_WHITE);
    };

    const StarSystem& sys = S.galaxy[S.currentSystem];

    if (S.sidePage == SidebarPage::Cargo) {
        section(L"Cargo Hold");
        {
            std::wstringstream oss;
            oss << L"Used: " << S.P.cargoUsed() << L"/" << S.P.cargoMax;
            panelPrintLine(C, r, y, oss.str());
        }
        panelPrintLine(C, r, y, L"");
        for(int i=0;i<(int)Good::COUNT;i++){
            Good g = (Good)i;
            if (g == Good::Fuel) continue;
            std::wstringstream oss;
            oss << std::left << std::setw(12) << GOOD_NAME[i] << L": " << S.P.cargo[i];
            panelPrintLine(C, r, y, oss.str());
        }
        panelPrintLine(C, r, y, L"");
        section(L"Fuel Tank");
        {
            std::wstringstream oss;
            oss << L"Fuel: " << S.P.fuel << L"/" << S.P.fuelMax;
            panelPrintLine(C, r, y, oss.str());
        }
        return;
    }

    if (S.sidePage == SidebarPage::Missions) {
        section(L"Available Here");
        panelPrintLine(C, r, y, sys.pois[S.dockPoiIndex].name, termui::FG_BRIGHT | termui::FG_WHITE);
        panelPrintLine(C, r, y, L"Up/Down select  ENTER/Y accept  N decline  Q back");
        panelPrintLine(C, r, y, L"");

        if (S.poiOffers.empty()) {
            panelPrintLine(C, r, y, L"(no contracts posted)");
        } else {
            for (int i=0; i<(int)S.poiOffers.size() && y < r.y + r.h - 1; i++) {
                const auto& m = S.poiOffers[i];
                std::wstringstream oss;
                oss << (i==S.offerSel ? L"> " : L"  ")
					<< m.amount << L" " << goodNameW(m.good)
					<< L" to " << S.galaxy[m.toSystem].name << L" / " << S.galaxy[m.toSystem].pois[m.toPoi].name
					<< L" (" << m.deadlineWeeks << L"w)";
                panelPrintLine(C, r, y, oss.str(), (i==S.offerSel) ? (termui::FG_BRIGHT|termui::FG_WHITE) : termui::FG_WHITE);
            }
        }

        panelPrintLine(C, r, y, L"");
        section(L"Active Missions");
        int shown = 0;
        for (const auto& m : S.activeMissions) {
            if (!m.active || m.completed) continue;
            std::wstringstream oss;
            oss << L"To " << S.galaxy[m.toSystem].name << L"/" << S.galaxy[m.toSystem].pois[m.toPoi].name
				<< L": " << m.amount << L" " << goodNameW(m.good)
				<< L" (" << m.deadlineWeeks << L"w)";
            panelPrintLine(C, r, y, oss.str());
            if (++shown >= 4) break;
        }
        if (shown == 0) panelPrintLine(C, r, y, L"(none)");
        return;
    }

    // STATUS page
	section(L"Current System");
	panelPrintLine(C, r, y, sys.name, termui::FG_BRIGHT | termui::FG_WHITE);

	// Show docked POI + type
	{
		const auto& dock = sys.pois[S.dockPoiIndex];
		panelPrintLine(C, r, y, L"Ship @ " + dock.name + L" (" + poiTypeNameW(dock.type) + L")");
	}

	panelPrintLine(C, r, y, L"");
	section(L"Cursor / Hover");

	if (S.screen == Screen::Galaxy) {
		std::wstringstream oss;
		oss << L"Cursor: (" << S.gCurX << L"," << S.gCurY << L")";
		panelPrintLine(C, r, y, oss.str());

		int hovered = -1;
		for(int i=0;i<(int)S.galaxy.size();i++){
			if (S.galaxy[i].gx==S.gCurX && S.galaxy[i].gy==S.gCurY) { hovered=i; break; }
		}

		if (hovered >= 0) {
			panelPrintLine(C, r, y, L"Target: " + S.galaxy[hovered].name, termui::FG_BRIGHT | termui::FG_WHITE);

			int dist = chebyshev(S.galaxy[S.currentSystem].gx, S.galaxy[S.currentSystem].gy, S.gCurX, S.gCurY);
			int jumps = jumpsRequired(dist, GALAXY_JUMP_RANGE);
			std::wstringstream t;
			t << L"Dist: " << dist
			  << L"  Jumps: " << jumps
			  << L"  Fuel/j: " << GALAXY_FUEL_PER_JUMP
			  << L"  EstFuel: " << (jumps * GALAXY_FUEL_PER_JUMP);
			panelPrintLine(C, r, y, t.str());


			// NEW: mission indicator for hovered system
			int mcount = countMissionsToSystem(S, hovered);
			if (mcount > 0) {
				std::wstringstream ms;
				ms << L"Contracts due here: " << mcount;
				panelPrintLine(C, r, y, ms.str(), termui::FG_BRIGHT | termui::FG_WHITE);
			}
		} else {
			panelPrintLine(C, r, y, L"Target: (empty)");
		}
	}
	else if (S.screen == Screen::System) {
		std::wstringstream oss;
		oss << L"Cursor: (" << S.sCurX << L"," << S.sCurY << L")";
		panelPrintLine(C, r, y, oss.str());

		int piExact = poiIndexAt(sys, S.sCurX, S.sCurY);
		if (piExact >= 0) {
			const SystemPoi& p = sys.pois[piExact];
			panelPrintLine(C, r, y, L"POI: " + p.name + L" (" + poiTypeNameW(p.type) + L")", termui::FG_BRIGHT | termui::FG_WHITE);

			int dist = chebyshev(S.shipX, S.shipY, p.x, p.y);
			int jumps = jumpsRequired(dist, SYSTEM_JUMP_RANGE);
			std::wstringstream t;
			t << L"Dist: " << dist
			  << L"  Jumps: " << jumps
			  << L"  Fuel/j: " << SYSTEM_FUEL_PER_JUMP
			  << L"  EstFuel: " << (jumps * SYSTEM_FUEL_PER_JUMP);
			panelPrintLine(C, r, y, t.str());



			// NEW: if this POI is a delivery target, show first matching mission summary
			Mission mm{};
			if (firstMissionToPoiHere(S, piExact, mm)) {
				std::wstringstream ms;
				ms << L"Delivery due: " << mm.amount << L" " << goodNameW(mm.good);
				panelPrintLine(C, r, y, ms.str(), termui::FG_BRIGHT | termui::FG_WHITE);
			}

			panelPrintLine(C, r, y, L"(ENTER to travel, SPACE market)");
		} else {
			int pi = nearestPoiIndex(sys, S.sCurX, S.sCurY);
			const SystemPoi& p = sys.pois[pi];
			panelPrintLine(C, r, y, L"Nearest: " + p.name + L" (" + poiTypeNameW(p.type) + L")");

			int dist = chebyshev(S.shipX, S.shipY, p.x, p.y);
			int jumps = jumpsRequired(dist, SYSTEM_JUMP_RANGE);
			std::wstringstream t;
			t << L"Dist: " << dist
			  << L"  Jumps: " << jumps
			  << L"  Fuel/j: " << SYSTEM_FUEL_PER_JUMP
			  << L"  EstFuel: " << (jumps * SYSTEM_FUEL_PER_JUMP);
			panelPrintLine(C, r, y, t.str());


			// NEW: also show mission indicator for nearest (helps when cursor is empty space)
			Mission mm{};
			if (firstMissionToPoiHere(S, pi, mm)) {
				std::wstringstream ms;
				ms << L"Delivery due: " << mm.amount << L" " << goodNameW(mm.good);
				panelPrintLine(C, r, y, ms.str(), termui::FG_BRIGHT | termui::FG_WHITE);
			}
		}
	}
	else { // Market screen
		panelPrintLine(C, r, y, L"Docked at:", termui::FG_BRIGHT | termui::FG_WHITE);
		panelPrintLine(C, r, y, sys.pois[S.dockPoiIndex].name + L" (" + poiTypeNameW(sys.pois[S.dockPoiIndex].type) + L")");
	}

	panelPrintLine(C, r, y, L"");
	section(L"Controls");
	panelPrintLine(C, r, y, L"TAB: Galaxy/System");
	panelPrintLine(C, r, y, L"E: Sidebar page (Status/Cargo/Missions)");
	panelPrintLine(C, r, y, L"L: Clear log");
	panelPrintLine(C, r, y, L"ESC: Quit");
}

static void renderLog(termui::Canvas& C, const termui::Rect& r, const GameState& S) {
    C.drawBox(r, L"LOG  (L: clear)");
    C.clearInside(r, termui::FG_WHITE);

    int x = r.x + 2;
    int y = r.y + 1;
    int w = r.w - 4;
    int h = r.h - 2;

    for(int i=0;i<h;i++){
        C.gotoXY((SHORT)x, (SHORT)(y+i));
        std::wstring line = (i < (int)S.log.size()) ? S.log[i] : L"";
        line = ellipsize(line, w);
        if ((int)line.size() < w) line += std::wstring(w - line.size(), L' ');
        C.writeW(line);
    }
}

static void renderAll(termui::Canvas& C, const termui::Layout& L, GameState& S) {
    renderHUD(C, L.hud, S);

    if (S.screen == Screen::Galaxy) renderGalaxyMap(C, L.map, S);
    else if (S.screen == Screen::System) renderSystemMap(C, L.map, S);
    else renderMarket(C, L.map, S);

    renderSidebar(C, L.side, S);
    renderLog(C, L.log, S);
}

// ---------------- Game actions ----------------
static void doGalaxyJump(GameState& S) {
    // Find system at cursor
    int target = -1;
    for(int i=0;i<(int)S.galaxy.size();i++){
        if (S.galaxy[i].gx==S.gCurX && S.galaxy[i].gy==S.gCurY) { target=i; break; }
    }
    if (target < 0) { S.pushLog(L"Jump: No system at cursor."); return; }
    if (target == S.currentSystem) { S.pushLog(L"Jump: Already here."); return; }

    const auto& cur = S.galaxy[S.currentSystem];
    const auto& dst = S.galaxy[target];

    int dist = chebyshev(cur.gx, cur.gy, dst.gx, dst.gy);
    if (dist > GALAXY_JUMP_RANGE) {
        std::wstringstream oss;
        oss << L"Jump out of range. Need " << jumpsRequired(dist, GALAXY_JUMP_RANGE)
            << L" jumps (range " << GALAXY_JUMP_RANGE << L").";
        S.pushLog(oss.str());
        return;
    }

    if (S.P.fuel < GALAXY_FUEL_PER_JUMP) { S.pushLog(L"Jump: Not enough fuel."); return; }

    S.clearLog();                 // clear log on travel
    S.P.fuel -= GALAXY_FUEL_PER_JUMP;
    advanceWeek(S, 1);
    tickMissionDeadlines(S, 1);

    S.currentSystem = target;

    std::wstringstream oss;
    oss << L"FTL jump to " << dst.name << L" (1 week, -" << GALAXY_FUEL_PER_JUMP << L" fuel).";
    S.pushLog(oss.str());

    // On arrival, place you at POI #0 and dock (offers, potential delivery completion)
    const auto& sys = S.galaxy[S.currentSystem];
    S.sCurX = sys.pois[0].x;
    S.sCurY = sys.pois[0].y;

    dockAtPoi(S, 0, /*autoOpenMissions=*/true);
}


static void doSystemJump(GameState& S) {
    const StarSystem& sys = S.galaxy[S.currentSystem];

    // Jump target is the cursor position (clamped to system bounds)
    const int SW=40, SH=20;
    int tx = termui::clampi(S.sCurX, 0, SW-1);
    int ty = termui::clampi(S.sCurY, 0, SH-1);

    int dist = chebyshev(S.shipX, S.shipY, tx, ty);
    if (dist == 0) { S.pushLog(L"Jump: You are already there."); return; }

    // We allow a jump only if within range; otherwise, we jump *toward* cursor by range
    int nx = S.shipX;
    int ny = S.shipY;
    stepToward(nx, ny, tx, ty, SYSTEM_JUMP_RANGE);

    if (S.P.fuel < SYSTEM_FUEL_PER_JUMP) { S.pushLog(L"Jump: Not enough fuel."); return; }

    S.clearLog();                 // clear log on travel
    S.P.fuel -= SYSTEM_FUEL_PER_JUMP;
    advanceWeek(S, 1);
    tickMissionDeadlines(S, 1);

    S.shipX = nx;
    S.shipY = ny;

    // If we landed on a POI, dock (mission completion + offers)
    int pi = poiIndexAt(sys, S.shipX, S.shipY);
    if (pi >= 0) {
        std::wstringstream oss;
        oss << L"STL jump to " << sys.pois[pi].name
            << L" (1 week, -" << SYSTEM_FUEL_PER_JUMP << L" fuel).";
        S.pushLog(oss.str());

        dockAtPoi(S, pi, /*autoOpenMissions=*/true);
    } else {
        std::wstringstream oss;
        oss << L"STL jump (1 week, -" << SYSTEM_FUEL_PER_JUMP << L" fuel).";
        S.pushLog(oss.str());
        // not docked; keep existing dockPoiIndex unchanged
    }
}


static void marketTradeOne(GameState& S) {
    const StarSystem& sys = S.galaxy[S.currentSystem];
    const SystemPoi& poi = sys.pois[S.dockPoiIndex]; // dock market

    Good g = (Good)S.marketSel;
    int price = poi.market.priceOf(g);

    if (S.marketModeBuy) {
        if (g == Good::Fuel) {
            if (S.P.fuel >= S.P.fuelMax) { S.pushLog(L"Market: Fuel tank full."); return; }
            if (S.P.credits < price) { S.pushLog(L"Market: Not enough credits."); return; }
            S.P.credits -= price; S.P.fuel += 1;
            S.pushLog(L"Bought 1 Fuel.");
            return;
        }

        if (S.P.cargoUsed() >= S.P.cargoMax) { S.pushLog(L"Market: Cargo full."); return; }
        if (S.P.credits < price) { S.pushLog(L"Market: Not enough credits."); return; }
        S.P.credits -= price;
        S.P.cargo[(int)g] += 1;

        std::wstringstream oss; oss << L"Bought 1 " << GOOD_NAME[(int)g] << L".";
        S.pushLog(oss.str());
    } else {
        if (g == Good::Fuel) {
            if (S.P.fuel <= 0) { S.pushLog(L"Market: No fuel to sell."); return; }
            S.P.fuel -= 1; S.P.credits += price;
            S.pushLog(L"Sold 1 Fuel.");
            return;
        }

        if (S.P.cargo[(int)g] <= 0) { S.pushLog(L"Market: You have none to sell."); return; }
        S.P.cargo[(int)g] -= 1;
        S.P.credits += price;

        std::wstringstream oss; oss << L"Sold 1 " << GOOD_NAME[(int)g] << L".";
        S.pushLog(oss.str());
    }
}

// ---------------- Main ----------------
int main() {
	std::cin.get();
	
    termui::Canvas C;
    C.configure(true, false);
    termui::Input I(C.in());

    GameState S;
    initGalaxy(S);

    auto sz = C.windowSize();
    termui::Layout L = termui::computeLayout(sz.w, sz.h);
    C.clearAll(termui::FG_WHITE);
    renderAll(C, L, S);

    while (true) {
        termui::Action a = I.readActionBlocking();

        if (a.type == termui::ActionType::Quit) break;

        if (a.type == termui::ActionType::Resize) {
            sz = C.windowSize();
            L = termui::computeLayout(sz.w, sz.h);
            C.clearAll(termui::FG_WHITE);
            renderAll(C, L, S);
            continue;
        }

        if (a.type == termui::ActionType::ClearLog) {
            S.clearLog();
            S.pushLog(L"(log cleared)");
            renderAll(C, L, S);
            continue;
        }

        if (a.type == termui::ActionType::SidebarToggle) {
            // NEW: cycle 3 pages
            if (S.sidePage == SidebarPage::Status) S.sidePage = SidebarPage::Cargo;
            else if (S.sidePage == SidebarPage::Cargo) S.sidePage = SidebarPage::Missions;
            else S.sidePage = SidebarPage::Status;
            renderAll(C, L, S);
            continue;
        }

        // Missions page interaction (works from any screen)
        if (S.sidePage == SidebarPage::Missions) {
            if (a.type == termui::ActionType::Back) {
                S.sidePage = SidebarPage::Status;
                renderAll(C, L, S);
                continue;
            }
            if (a.type == termui::ActionType::Move && !S.poiOffers.empty()) {
                if (a.dy != 0) S.offerSel += a.dy;
                else if (a.dx != 0) S.offerSel += a.dx;
                S.offerSel = termui::clampi(S.offerSel, 0, (int)S.poiOffers.size()-1);
                renderAll(C, L, S);
                continue;
            }
            if ((a.type == termui::ActionType::Confirm || a.type == termui::ActionType::Yes) && !S.poiOffers.empty()) {
                acceptSelectedOffer(S);
                renderAll(C, L, S);
                continue;
            }
            if (a.type == termui::ActionType::No && !S.poiOffers.empty()) {
                declineSelectedOffer(S);
                renderAll(C, L, S);
                continue;
            }
        }

        // TAB behavior
        if (a.type == termui::ActionType::TabRight || a.type == termui::ActionType::TabLeft) {
            if (S.screen == Screen::Market) {
                S.marketModeBuy = !S.marketModeBuy;
                S.pushLog(S.marketModeBuy ? L"Market: BUY mode." : L"Market: SELL mode.");
            } else {
                S.screen = (S.screen == Screen::Galaxy) ? Screen::System : Screen::Galaxy;
            }
            renderAll(C, L, S);
            continue;
        }

        // Screen-specific input
        if (S.screen == Screen::Galaxy) {
            if (a.type == termui::ActionType::Move) { S.gCurX += a.dx; S.gCurY += a.dy; renderAll(C, L, S); continue; }
            if (a.type == termui::ActionType::Confirm) { doGalaxyJump(S); renderAll(C, L, S); continue; }
        }
        else if (S.screen == Screen::System) {
            if (a.type == termui::ActionType::Move) { S.sCurX += a.dx; S.sCurY += a.dy; renderAll(C, L, S); continue; }
            if (a.type == termui::ActionType::Confirm) { doSystemJump(S); renderAll(C, L, S); continue; }
            if (a.type == termui::ActionType::Select) { S.screen = Screen::Market; S.marketSel = 0; S.marketModeBuy = true; renderAll(C, L, S); continue; }
        }
        else { // Market
            if (a.type == termui::ActionType::Back) { S.screen = Screen::System; renderAll(C, L, S); continue; }
            if (a.type == termui::ActionType::Move) {
                if (a.dy != 0) S.marketSel += a.dy;
                else if (a.dx != 0) S.marketSel += a.dx;
                S.marketSel = termui::clampi(S.marketSel, 0, (int)Good::COUNT - 1);
                renderAll(C, L, S);
                continue;
            }
            if (a.type == termui::ActionType::Confirm) { marketTradeOne(S); renderAll(C, L, S); continue; }
        }
    }

    return 0;
}
