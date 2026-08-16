// FlowTask - polished TinyTask-style macro recorder
// Custom-drawn dark toolbar UI, anti-aliased icons, full Prefs menu.
#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <stdio.h>

#define PI 3.14159265358979
#define IDI_APPICON 101

/* ---------- ids ---------- */
#define BTN_OPEN   0
#define BTN_SAVE   1
#define BTN_REC    2
#define BTN_PLAY   3
#define BTN_EXE    4
#define BTN_PREFS  5
#define BTN_COUNT  6

#define WM_HOTKEY_REC  (WM_APP + 1)
#define WM_HOTKEY_PLAY (WM_APP + 2)
#define WM_HOTKEY_STOP (WM_APP + 3)
#define ID_TIMER_BLINK 1

/* menu ids */
#define M_SPEED_HALF   2001
#define M_SPEED_1X     2002
#define M_SPEED_2X     2003
#define M_SPEED_100X   2004
#define M_SPEED_CUSTOM 2005
#define M_CUSTOM_BASE  2010   /* +index into custom speed presets */
#define M_CONTINUOUS   2020
#define M_LOOPS_BASE   2030   /* +index */
#define M_RECHK_BASE   2100   /* +0..11 => F1..F12 */
#define M_PLAYHK_BASE  2200
#define M_STOPHK_BASE  2250
#define M_ONTOP        2300
#define M_CAPTIONS     2301
#define M_SMOOTH       2302
#define M_ABOUT        2400

#define MAX_EVENTS 60000

#define EV_MOVE    0
#define EV_LDOWN   1
#define EV_LUP     2
#define EV_RDOWN   3
#define EV_RUP     4
#define EV_WHEEL   5
#define EV_KEYDOWN 6
#define EV_KEYUP   7

typedef struct {
    DWORD delayMs;
    BYTE  type;
    LONG  x, y;
    DWORD vk;
} MacroEvent;

/* ---------- state ---------- */
static MacroEvent g_events[MAX_EVENTS];
static int   g_count = 0;
static DWORD g_lastTime = 0, g_lastMoveTime = 0;
static BOOL  g_recording = FALSE, g_playing = FALSE, g_stopRequested = FALSE;
static HHOOK g_mouseHook = NULL, g_kbHook = NULL;
static HWND  g_hwnd = NULL;
static HFONT g_fontLabel = NULL, g_fontStatus = NULL;
static BOOL  g_blinkOn = FALSE;
static int   g_hover = -1, g_pressed = -1;
static char  g_status[128] = "Ready - press Rec to start recording";

static double g_speed = 1.0;
static int    g_customSpeed = 15;
static BOOL   g_useCustomSpeed = FALSE;
static BOOL   g_continuous = FALSE;
static int    g_loops = 1;
static DWORD  g_recHotkey = VK_F9;
static DWORD  g_playHotkey = VK_F10;
static DWORD  g_stopHotkey = VK_F8;
static BOOL   g_alwaysOnTop = FALSE;
static BOOL   g_showCaptions = TRUE;
static BOOL   g_smooth = TRUE;

static const int  g_customPresets[] = { 5, 10, 15, 25, 50 };
static const int  g_loopPresets[]   = { 1, 2, 3, 5, 10, 25, 100 };

/* ---------- theme ---------- */
#define C_BG        RGB(0x0b,0x0b,0x0d)
#define C_TB_TOP    RGB(0x18,0x19,0x1d)
#define C_TB_BOT    RGB(0x11,0x11,0x14)
#define C_HAIRLINE  RGB(0x26,0x27,0x2d)
#define C_SEP       RGB(0x1e,0x1f,0x24)
#define C_HOVER     RGB(0x1f,0x21,0x27)
#define C_PRESS     RGB(0x2a,0x2d,0x34)
#define C_TEXT      RGB(0xe6,0xe9,0xee)
#define C_ICON      RGB(0x9a,0xa1,0xac)
#define C_ICON_HOT  RGB(0xf2,0xf5,0xf9)
#define C_MUTED     RGB(0x6e,0x75,0x80)

#define C_ACCENT    RGB(0x2d,0xe2,0xc5)
#define C_REC       RGB(0xff,0x3b,0x47)

/* ---------- layout ---------- */
#define BTN_W    58
#define TB_PAD   8
#define GRP_GAP  11
#define ICON_PX  22

static int TB_H(void)   { return g_showCaptions ? 62 : 44; }
static int STATUS_H     = 27;

/* buttons are grouped: [Open Save] | [Rec Play] | [.exe Prefs] */
static int GroupOffset(int i) {
    if (i < 2) return 0;
    if (i < 4) return GRP_GAP;
    return GRP_GAP * 2;
}
static int CLIENT_W(void) { return TB_PAD * 2 + BTN_COUNT * BTN_W + GRP_GAP * 2; }
static int CLIENT_H(void) { return TB_H() + STATUS_H; }

static RECT BtnRect(int i) {
    RECT r;
    r.left = TB_PAD + i * BTN_W + GroupOffset(i);
    r.top = TB_PAD - 2;
    r.right = r.left + BTN_W;
    r.bottom = TB_H() - TB_PAD + 2;
    return r;
}

static void SetStatus(const char* s) {
    lstrcpynA(g_status, s, sizeof(g_status));
    if (g_hwnd) {
        RECT r; GetClientRect(g_hwnd, &r);
        RECT sr = { 0, TB_H(), r.right, r.bottom };
        InvalidateRect(g_hwnd, &sr, FALSE);
    }
}

/* ---------- recording ---------- */
static BOOL PointInOwnWindow(POINT pt) {
    RECT r; GetWindowRect(g_hwnd, &r);
    return PtInRect(&r, pt);
}

static void PushEvent(BYTE type, LONG x, LONG y, DWORD vk) {
    if (g_count >= MAX_EVENTS) return;
    DWORD now = GetTickCount();
    DWORD delay = (g_count == 0) ? 0 : (now - g_lastTime);
    g_lastTime = now;
    MacroEvent* e = &g_events[g_count++];
    e->delayMs = delay; e->type = type; e->x = x; e->y = y; e->vk = vk;

    char buf[96];
    wsprintfA(buf, "Recording  -  %d events", g_count);
    SetStatus(buf);
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_recording) {
        MSLLHOOKSTRUCT* p = (MSLLHOOKSTRUCT*)lParam;
        if (!PointInOwnWindow(p->pt)) {
            switch (wParam) {
                case WM_MOUSEMOVE: {
                    DWORD now = GetTickCount();
                    if (now - g_lastMoveTime >= 8) {
                        g_lastMoveTime = now;
                        PushEvent(EV_MOVE, p->pt.x, p->pt.y, 0);
                    }
                    break;
                }
                case WM_LBUTTONDOWN: PushEvent(EV_LDOWN, p->pt.x, p->pt.y, 0); break;
                case WM_LBUTTONUP:   PushEvent(EV_LUP,   p->pt.x, p->pt.y, 0); break;
                case WM_RBUTTONDOWN: PushEvent(EV_RDOWN, p->pt.x, p->pt.y, 0); break;
                case WM_RBUTTONUP:   PushEvent(EV_RUP,   p->pt.x, p->pt.y, 0); break;
                case WM_MOUSEWHEEL:
                    PushEvent(EV_WHEEL, p->pt.x, p->pt.y, (DWORD)HIWORD(p->mouseData));
                    break;
            }
        }
    }
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)lParam;
        BOOL down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        if (k->vkCode == g_recHotkey) {
            if (down) PostMessage(g_hwnd, WM_HOTKEY_REC, 0, 0);
            return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
        }
        if (k->vkCode == g_playHotkey) {
            if (down) PostMessage(g_hwnd, WM_HOTKEY_PLAY, 0, 0);
            return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
        }
        if (k->vkCode == g_stopHotkey) {
            if (down) {
                g_stopRequested = TRUE;          /* stops playback right away */
                PostMessage(g_hwnd, WM_HOTKEY_STOP, 0, 0);
            }
            return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
        }
        if (g_recording)
            PushEvent(down ? EV_KEYDOWN : EV_KEYUP, 0, 0, k->vkCode);
    }
    return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
}

static void RedrawToolbar(void) {
    if (!g_hwnd) return;
    RECT r; GetClientRect(g_hwnd, &r);
    RECT tr = { 0, 0, r.right, TB_H() };
    InvalidateRect(g_hwnd, &tr, FALSE);
}

static void ToggleRecording(void) {
    if (g_playing) return;
    if (!g_recording) {
        g_count = 0;
        g_recording = TRUE;
        g_stopRequested = FALSE;
        g_lastTime = GetTickCount();
        g_lastMoveTime = 0;
        g_mouseHook = SetWindowsHookExA(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandleA(NULL), 0);
        SetStatus("Recording ...");
        SetTimer(g_hwnd, ID_TIMER_BLINK, 450, NULL);
    } else {
        if (g_mouseHook) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = NULL; }
        g_recording = FALSE;
        KillTimer(g_hwnd, ID_TIMER_BLINK);
        g_blinkOn = FALSE;
        char buf[96];
        wsprintfA(buf, "Ready  -  %d events recorded", g_count);
        SetStatus(buf);
    }
    RedrawToolbar();
}

static double CurrentSpeed(void) {
    return g_useCustomSpeed ? (double)g_customSpeed : g_speed;
}

/* ---------- smooth playback engine ---------- */

/* move-point table so we can spline through the recorded path */
static int   g_moveOrd[MAX_EVENTS];
static POINT g_movePts[MAX_EVENTS];
static int   g_moveCount = 0;

static void BuildMoveTable(void) {
    g_moveCount = 0;
    for (int i = 0; i < g_count; i++) {
        if (g_events[i].type == EV_MOVE) {
            g_moveOrd[i] = g_moveCount;
            g_movePts[g_moveCount].x = g_events[i].x;
            g_movePts[g_moveCount].y = g_events[i].y;
            g_moveCount++;
        } else {
            g_moveOrd[i] = -1;
        }
    }
}

/* Catmull-Rom: smooth curve through the sampled points */
static double CatmullRom(double p0, double p1, double p2, double p3, double t) {
    double t2 = t * t, t3 = t2 * t;
    return 0.5 * ((2.0 * p1)
                + (-p0 + p2) * t
                + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2
                + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

/* sleep with sub-millisecond accuracy: coarse sleep, then short spin */
static void PreciseSleep(double ms) {
    if (ms <= 0.05) return;
    LARGE_INTEGER f, s, n;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&s);
    double target = ms / 1000.0 * (double)f.QuadPart;
    if (ms > 2.0) Sleep((DWORD)(ms - 1.5));
    for (;;) {
        QueryPerformanceCounter(&n);
        if ((double)(n.QuadPart - s.QuadPart) >= target) break;
        if (g_stopRequested) break;
        YieldProcessor();
    }
}

static int g_scrW = 0, g_scrH = 0;

static void MoveCursorAbs(double x, double y) {
    INPUT mv; ZeroMemory(&mv, sizeof(mv));
    mv.type = INPUT_MOUSE;
    mv.mi.dx = (LONG)(x * (65535.0 / g_scrW) + 0.5);
    mv.mi.dy = (LONG)(y * (65535.0 / g_scrH) + 0.5);
    mv.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    SendInput(1, &mv, sizeof(INPUT));
}

static DWORD WINAPI PlaybackThread(LPVOID param) {
    g_playing = TRUE;
    g_stopRequested = FALSE;
    RedrawToolbar();

    g_scrW = GetSystemMetrics(SM_CXSCREEN);
    g_scrH = GetSystemMetrics(SM_CYSCREEN);
    double sp = CurrentSpeed();
    if (sp <= 0) sp = 1.0;

    BuildMoveTable();

    /* ask Windows for 1 ms timer resolution so the timing is tight */
    timeBeginPeriod(1);

    int totalLoops = g_continuous ? 0x7FFFFFFF : (g_loops < 1 ? 1 : g_loops);

    POINT startPos;
    GetCursorPos(&startPos);
    POINT prevPt = startPos;

    for (int rep = 0; rep < totalLoops && !g_stopRequested; rep++) {
        for (int i = 0; i < g_count && !g_stopRequested; i++) {
            MacroEvent* e = &g_events[i];
            double dms = (double)e->delayMs / sp;

            if (e->type == EV_MOVE) {
                int m = g_moveOrd[i];
                POINT P1 = prevPt;
                POINT P2 = { e->x, e->y };
                POINT P0 = (m > 1) ? g_movePts[m - 2] : P1;
                POINT P3 = (m >= 0 && m + 1 < g_moveCount) ? g_movePts[m + 1] : P2;

                double dx = (double)(P2.x - P1.x), dy = (double)(P2.y - P1.y);
                double dist = __builtin_sqrt(dx * dx + dy * dy);

                int steps = 1;
                if (g_smooth && dms >= 2.0) {
                    int byTime = (int)(dms / 4.0);
                    int byDist = (int)(dist / 6.0);
                    steps = byTime > byDist ? byTime : byDist;
                    if (steps < 2) steps = 2;
                    if (steps > 80) steps = 80;
                }

                double stepMs = dms / steps;
                for (int s = 1; s <= steps && !g_stopRequested; s++) {
                    double t = (double)s / (double)steps;
                    PreciseSleep(stepMs);
                    if (g_smooth && steps > 1) {
                        double x = CatmullRom(P0.x, P1.x, P2.x, P3.x, t);
                        double y = CatmullRom(P0.y, P1.y, P2.y, P3.y, t);
                        MoveCursorAbs(x, y);
                    } else {
                        MoveCursorAbs(P2.x, P2.y);
                    }
                }
                prevPt = P2;

            } else if (e->type <= EV_WHEEL) {
                /* click / wheel: glide over to the spot, then fire */
                double dx = (double)(e->x - prevPt.x), dy = (double)(e->y - prevPt.y);
                double dist = __builtin_sqrt(dx * dx + dy * dy);

                int steps = 1;
                if (g_smooth && dms >= 2.0 && dist > 2.0) {
                    int byTime = (int)(dms / 4.0);
                    int byDist = (int)(dist / 6.0);
                    steps = byTime > byDist ? byTime : byDist;
                    if (steps < 2) steps = 2;
                    if (steps > 80) steps = 80;
                }
                double stepMs = dms / steps;
                for (int s = 1; s <= steps && !g_stopRequested; s++) {
                    double t = (double)s / (double)steps;
                    /* ease-out so the pointer settles instead of slamming */
                    double te = 1.0 - (1.0 - t) * (1.0 - t);
                    PreciseSleep(stepMs);
                    MoveCursorAbs(prevPt.x + dx * te, prevPt.y + dy * te);
                }
                MoveCursorAbs(e->x, e->y);
                prevPt.x = e->x; prevPt.y = e->y;

                INPUT in; ZeroMemory(&in, sizeof(in));
                in.type = INPUT_MOUSE;
                switch (e->type) {
                    case EV_LDOWN: in.mi.dwFlags = MOUSEEVENTF_LEFTDOWN; break;
                    case EV_LUP:   in.mi.dwFlags = MOUSEEVENTF_LEFTUP; break;
                    case EV_RDOWN: in.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN; break;
                    case EV_RUP:   in.mi.dwFlags = MOUSEEVENTF_RIGHTUP; break;
                    case EV_WHEEL: in.mi.dwFlags = MOUSEEVENTF_WHEEL;
                                   in.mi.mouseData = (DWORD)(short)e->vk; break;
                }
                SendInput(1, &in, sizeof(INPUT));

            } else {
                PreciseSleep(dms);
                INPUT in; ZeroMemory(&in, sizeof(in));
                in.type = INPUT_KEYBOARD;
                in.ki.wVk = (WORD)e->vk;
                in.ki.dwFlags = (e->type == EV_KEYUP) ? KEYEVENTF_KEYUP : 0;
                SendInput(1, &in, sizeof(INPUT));
            }

            if ((i & 31) == 0) {
                char buf[96];
                if (g_continuous)
                    wsprintfA(buf, "Playing (continuous)  -  %d / %d", i + 1, g_count);
                else
                    wsprintfA(buf, "Playing %d/%d  -  %d / %d", rep + 1, totalLoops, i + 1, g_count);
                SetStatus(buf);
            }
        }
    }

    timeEndPeriod(1);

    g_playing = FALSE;
    SetStatus(g_stopRequested ? "Playback stopped." : "Playback finished.");
    RedrawToolbar();
    return 0;
}

/* panic key: kills playback and recording, whatever is running */
static void StopEverything(void) {
    BOOL didSomething = FALSE;

    if (g_playing) {
        g_stopRequested = TRUE;      /* playback thread exits on next step */
        didSomething = TRUE;
    }
    if (g_recording) {
        if (g_mouseHook) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = NULL; }
        g_recording = FALSE;
        KillTimer(g_hwnd, ID_TIMER_BLINK);
        g_blinkOn = FALSE;
        char buf[96];
        wsprintfA(buf, "Stopped  -  %d events recorded", g_count);
        SetStatus(buf);
        didSomething = TRUE;
    }
    if (!didSomething) SetStatus("Nothing running.");
    RedrawToolbar();
}

static void TogglePlay(void) {
    if (g_recording) return;
    if (!g_playing) {
        if (g_count == 0) { SetStatus("Nothing recorded - press Rec first."); return; }
        CreateThread(NULL, 0, PlaybackThread, NULL, 0, NULL);
    } else {
        g_stopRequested = TRUE;
    }
}

/* ---------- file io ---------- */
static void DoSave(void) {
    if (g_count == 0) { SetStatus("Nothing to save."); return; }
    char filename[MAX_PATH] = "macro.flow";
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = "FlowTask Macro (*.flow)\0*.flow\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = "flow";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameA(&ofn)) {
        FILE* f = fopen(filename, "wb");
        if (f) {
            fwrite("FLOW", 1, 4, f);
            fwrite(&g_count, sizeof(int), 1, f);
            fwrite(g_events, sizeof(MacroEvent), g_count, f);
            fclose(f);
            SetStatus("Macro saved.");
        } else SetStatus("Saving failed.");
    }
}

static void DoOpen(void) {
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = "FlowTask Macro (*.flow)\0*.flow\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) {
        FILE* f = fopen(filename, "rb");
        if (f) {
            char magic[4] = {0};
            fread(magic, 1, 4, f);
            if (!memcmp(magic, "FLOW", 4)) {
                int cnt = 0;
                fread(&cnt, sizeof(int), 1, f);
                if (cnt < 0) cnt = 0;
                if (cnt > MAX_EVENTS) cnt = MAX_EVENTS;
                fread(g_events, sizeof(MacroEvent), cnt, f);
                g_count = cnt;
                char buf[96];
                wsprintfA(buf, "Macro loaded  -  %d events", g_count);
                SetStatus(buf);
            } else SetStatus("Invalid file.");
            fclose(f);
        }
    }
}

/* ---------- drawing helpers ---------- */
static void FillRR(HDC hdc, int l, int t, int r, int b, int rad, COLORREF col) {
    HBRUSH br = CreateSolidBrush(col);
    HPEN pen = CreatePen(PS_SOLID, 1, col);
    HBRUSH ob = SelectObject(hdc, br);
    HPEN op = SelectObject(hdc, pen);
    RoundRect(hdc, l, t, r, b, rad, rad);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(br); DeleteObject(pen);
}

static COLORREF Lerp(COLORREF a, COLORREF b, double t) {
    if (t < 0) t = 0; if (t > 1) t = 1;
    int r = (int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t);
    int g = (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t);
    int bl = (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t);
    return RGB(r, g, bl);
}

/* stroke pen with round caps/joins */
static HPEN MakeStroke(COLORREF col, int width) {
    LOGBRUSH lb; lb.lbStyle = BS_SOLID; lb.lbColor = col; lb.lbHatch = 0;
    return ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND,
                        width, &lb, 0, NULL);
}

#define PCT(v) ((int)((v) * W / 100))

/* ---- icon shapes, drawn in a W x W box, monochrome stroke style ---- */

static void StrokePolyline(HDC hdc, POINT* p, int n, COLORREF col, int w) {
    HPEN pen = MakeStroke(col, w);
    HPEN op = SelectObject(hdc, pen);
    Polyline(hdc, p, n);
    SelectObject(hdc, op);
    DeleteObject(pen);
}

static void ShapeOpen(HDC hdc, int W, COLORREF c) {
    int sw = PCT(8);
    HPEN pen = MakeStroke(c, sw);
    HPEN op = SelectObject(hdc, pen);
    HBRUSH ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    /* folder outline with tab */
    POINT f[6] = { {PCT(12),PCT(76)}, {PCT(12),PCT(26)}, {PCT(40),PCT(26)},
                   {PCT(48),PCT(38)}, {PCT(88),PCT(38)}, {PCT(88),PCT(76)} };
    Polyline(hdc, f, 6);
    MoveToEx(hdc, PCT(12), PCT(76), NULL);
    LineTo(hdc, PCT(88), PCT(76));
    SelectObject(hdc, op); SelectObject(hdc, ob);
    DeleteObject(pen);
}

static void ShapeSave(HDC hdc, int W, COLORREF c) {
    int sw = PCT(8);
    HPEN pen = MakeStroke(c, sw);
    HPEN op = SelectObject(hdc, pen);
    HBRUSH ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    /* floppy outline */
    POINT f[5] = { {PCT(16),PCT(18)}, {PCT(70),PCT(18)}, {PCT(84),PCT(32)},
                   {PCT(84),PCT(82)}, {PCT(16),PCT(82)} };
    Polyline(hdc, f, 5);
    LineTo(hdc, PCT(16), PCT(18));
    /* shutter */
    Rectangle(hdc, PCT(32), PCT(18), PCT(64), PCT(38));
    /* label */
    Rectangle(hdc, PCT(30), PCT(56), PCT(70), PCT(82));
    SelectObject(hdc, op); SelectObject(hdc, ob);
    DeleteObject(pen);
}

static void ShapeRec(HDC hdc, int W, COLORREF c, BOOL active, COLORREF bg, double glow) {
    HBRUSH br; HPEN pen; HBRUSH ob; HPEN op;
    /* soft glow halo when armed */
    if (active && glow > 0.01) {
        for (int i = 8; i >= 1; i--) {
            double t = (double)i / 8.0;
            COLORREF gc = Lerp(bg, C_REC, glow * 0.30 * (1.0 - t));
            int r = (int)(PCT(34) + PCT(26) * t);
            HBRUSH gb = CreateSolidBrush(gc);
            HPEN gp = CreatePen(PS_SOLID, 1, gc);
            ob = SelectObject(hdc, gb); op = SelectObject(hdc, gp);
            Ellipse(hdc, W/2 - r, W/2 - r, W/2 + r, W/2 + r);
            SelectObject(hdc, ob); SelectObject(hdc, op);
            DeleteObject(gb); DeleteObject(gp);
        }
    }
    br = CreateSolidBrush(c);
    pen = CreatePen(PS_SOLID, 1, c);
    ob = SelectObject(hdc, br); op = SelectObject(hdc, pen);
    if (active) RoundRect(hdc, PCT(30), PCT(30), PCT(70), PCT(70), PCT(10), PCT(10));
    else        Ellipse(hdc, PCT(24), PCT(24), PCT(76), PCT(76));
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(br); DeleteObject(pen);
}

static void ShapePlay(HDC hdc, int W, COLORREF c, BOOL active) {
    HBRUSH br = CreateSolidBrush(c);
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HBRUSH ob = SelectObject(hdc, br);
    HPEN op = SelectObject(hdc, pen);
    if (active) {
        RoundRect(hdc, PCT(30), PCT(30), PCT(70), PCT(70), PCT(10), PCT(10));
    } else {
        POINT tri[3] = { {PCT(30),PCT(20)}, {PCT(30),PCT(80)}, {PCT(80),PCT(50)} };
        Polygon(hdc, tri, 3);
    }
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(br); DeleteObject(pen);
}

static void ShapeExe(HDC hdc, int W, COLORREF c) {
    int sw = PCT(8);
    HPEN pen = MakeStroke(c, sw);
    HPEN op = SelectObject(hdc, pen);
    MoveToEx(hdc, PCT(50), PCT(18), NULL);
    LineTo(hdc, PCT(50), PCT(60));
    POINT ar[3] = { {PCT(34),PCT(46)}, {PCT(50),PCT(62)}, {PCT(66),PCT(46)} };
    Polyline(hdc, ar, 3);
    POINT tray[4] = { {PCT(20),PCT(64)}, {PCT(20),PCT(80)}, {PCT(80),PCT(80)}, {PCT(80),PCT(64)} };
    Polyline(hdc, tray, 4);
    SelectObject(hdc, op);
    DeleteObject(pen);
}

/* sliders icon - reads as "settings" without the generic gear */
static void ShapePrefs(HDC hdc, int W, COLORREF c) {
    int sw = PCT(8);
    HPEN pen = MakeStroke(c, sw);
    HPEN op = SelectObject(hdc, pen);
    int ys[3] = { PCT(28), PCT(50), PCT(72) };
    int knob[3] = { PCT(62), PCT(38), PCT(56) };
    for (int i = 0; i < 3; i++) {
        MoveToEx(hdc, PCT(16), ys[i], NULL);
        LineTo(hdc, PCT(84), ys[i]);
    }
    SelectObject(hdc, op);
    DeleteObject(pen);

    HBRUSH br = CreateSolidBrush(c);
    HPEN kp = CreatePen(PS_SOLID, 1, c);
    HBRUSH ob = SelectObject(hdc, br);
    op = SelectObject(hdc, kp);
    for (int i = 0; i < 3; i++) {
        int r = PCT(10);
        Ellipse(hdc, knob[i] - r, ys[i] - r, knob[i] + r, ys[i] + r);
    }
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(br); DeleteObject(kp);
}

/* draw icon supersampled for smooth edges */
static void DrawIconAA(HDC dest, int cx, int cy, int size, int which,
                       COLORREF bg, COLORREF ink, double glow) {
    const int S = 4;
    int W = size * S;
    HDC mem = CreateCompatibleDC(dest);
    HBITMAP bmp = CreateCompatibleBitmap(dest, W, W);
    HBITMAP oldBmp = SelectObject(mem, bmp);
    RECT r = { 0, 0, W, W };
    HBRUSH bb = CreateSolidBrush(bg);
    FillRect(mem, &r, bb);
    DeleteObject(bb);

    switch (which) {
        case BTN_OPEN:  ShapeOpen(mem, W, ink); break;
        case BTN_SAVE:  ShapeSave(mem, W, ink); break;
        case BTN_REC:   ShapeRec(mem, W, g_recording ? C_REC : ink, g_recording, bg,
                                 g_blinkOn ? 1.0 : 0.35); break;
        case BTN_PLAY:  ShapePlay(mem, W, g_playing ? C_REC : (g_count > 0 ? C_ACCENT : ink),
                                  g_playing); break;
        case BTN_EXE:   ShapeExe(mem, W, ink); break;
        case BTN_PREFS: ShapePrefs(mem, W, ink); break;
    }

    SetStretchBltMode(dest, HALFTONE);
    SetBrushOrgEx(dest, 0, 0, NULL);
    StretchBlt(dest, cx - size / 2, cy - size / 2, size, size, mem, 0, 0, W, W, SRCCOPY);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static const char* BtnLabel(int i) {
    switch (i) {
        case BTN_OPEN:  return "OPEN";
        case BTN_SAVE:  return "SAVE";
        case BTN_REC:   return g_recording ? "STOP" : "REC";
        case BTN_PLAY:  return g_playing ? "STOP" : "PLAY";
        case BTN_EXE:   return "EXE";
        case BTN_PREFS: return "PREFS";
    }
    return "";
}

static const char* BtnHint(int i) {
    switch (i) {
        case BTN_OPEN:  return "Load macro from file";
        case BTN_SAVE:  return "Save macro to file";
        case BTN_REC:   return "Start / stop recording";
        case BTN_PLAY:  return "Play the recorded macro";
        case BTN_EXE:   return "Export as standalone file";
        case BTN_PREFS: return "Settings";
    }
    return "";
}

static void PaintAll(HDC hdc, RECT client) {
    int tbh = TB_H();

    /* toolbar gradient */
    TRIVERTEX v[2];
    v[0].x = 0; v[0].y = 0;
    v[0].Red = GetRValue(C_TB_TOP) << 8; v[0].Green = GetGValue(C_TB_TOP) << 8;
    v[0].Blue = GetBValue(C_TB_TOP) << 8; v[0].Alpha = 0;
    v[1].x = client.right; v[1].y = tbh;
    v[1].Red = GetRValue(C_TB_BOT) << 8; v[1].Green = GetGValue(C_TB_BOT) << 8;
    v[1].Blue = GetBValue(C_TB_BOT) << 8; v[1].Alpha = 0;
    GRADIENT_RECT gr = { 0, 1 };
    GradientFill(hdc, v, 2, &gr, 1, GRADIENT_FILL_RECT_V);

    /* status strip */
    RECT sb = { 0, tbh, client.right, client.bottom };
    HBRUSH bgbr = CreateSolidBrush(C_BG);
    FillRect(hdc, &sb, bgbr);
    DeleteObject(bgbr);

    /* 1px top highlight + bottom hairline of toolbar */
    HPEN hp = CreatePen(PS_SOLID, 1, C_HAIRLINE);
    HPEN op = SelectObject(hdc, hp);
    MoveToEx(hdc, 0, 0, NULL); LineTo(hdc, client.right, 0);
    SelectObject(hdc, op); DeleteObject(hp);

    HPEN sp = CreatePen(PS_SOLID, 1, RGB(0x05,0x05,0x07));
    op = SelectObject(hdc, sp);
    MoveToEx(hdc, 0, tbh, NULL); LineTo(hdc, client.right, tbh);
    SelectObject(hdc, op); DeleteObject(sp);

    /* group separators */
    HPEN gp = CreatePen(PS_SOLID, 1, C_SEP);
    op = SelectObject(hdc, gp);
    for (int g = 1; g <= 2; g++) {
        RECT b = BtnRect(g * 2);
        int x = b.left - GRP_GAP / 2 - 1;
        MoveToEx(hdc, x, 14, NULL);
        LineTo(hdc, x, tbh - 14);
    }
    SelectObject(hdc, op); DeleteObject(gp);

    /* buttons */
    for (int i = 0; i < BTN_COUNT; i++) {
        RECT b = BtnRect(i);
        COLORREF bg = Lerp(C_TB_TOP, C_TB_BOT, 0.45);
        COLORREF ink = C_ICON;

        if (g_pressed == i) {
            FillRR(hdc, b.left, b.top, b.right, b.bottom, 9, C_PRESS);
            bg = C_PRESS; ink = C_ICON_HOT;
        } else if (g_hover == i) {
            FillRR(hdc, b.left, b.top, b.right, b.bottom, 9, C_HOVER);
            bg = C_HOVER; ink = C_ICON_HOT;
        }

        int cx = (b.left + b.right) / 2;
        int cy = g_showCaptions ? b.top + 20 : (b.top + b.bottom) / 2;
        DrawIconAA(hdc, cx, cy, ICON_PX, i, bg, ink, 1.0);

        if (g_showCaptions) {
            RECT tr = { b.left, b.top + 35, b.right, b.bottom };
            SetBkMode(hdc, TRANSPARENT);
            COLORREF tc = C_MUTED;
            if (i == BTN_REC && g_recording) tc = C_REC;
            else if (i == BTN_PLAY && g_playing) tc = C_REC;
            else if (g_hover == i || g_pressed == i) tc = C_TEXT;
            SetTextColor(hdc, tc);
            HFONT of = SelectObject(hdc, g_fontLabel);
            SetTextCharacterExtra(hdc, 1);
            DrawTextA(hdc, BtnLabel(i), -1, &tr, DT_CENTER | DT_TOP | DT_SINGLELINE);
            SetTextCharacterExtra(hdc, 0);
            SelectObject(hdc, of);
        }

        /* accent underline on the active transport button */
        if ((i == BTN_REC && g_recording) || (i == BTN_PLAY && g_playing)) {
            int uw = 18;
            FillRR(hdc, cx - uw/2, b.bottom - 3, cx + uw/2, b.bottom - 1, 2,
                   g_recording ? C_REC : C_ACCENT);
        }
    }

    /* ---- status strip ---- */
    int sy = (tbh + client.bottom) / 2;

    /* LED */
    COLORREF led = C_MUTED;
    if (g_recording) led = g_blinkOn ? C_REC : Lerp(C_BG, C_REC, 0.35);
    else if (g_playing) led = C_ACCENT;
    else if (g_count > 0) led = Lerp(C_BG, C_ACCENT, 0.55);
    HBRUSH lb = CreateSolidBrush(led);
    HPEN lp = CreatePen(PS_SOLID, 1, led);
    HBRUSH ob2 = SelectObject(hdc, lb);
    HPEN op2 = SelectObject(hdc, lp);
    Ellipse(hdc, 12, sy - 3, 18, sy + 3);
    SelectObject(hdc, ob2); SelectObject(hdc, op2);
    DeleteObject(lb); DeleteObject(lp);

    SetBkMode(hdc, TRANSPARENT);
    HFONT of = SelectObject(hdc, g_fontStatus);

    /* left: status or hover hint */
    const char* txt = g_status;
    if (!g_recording && !g_playing && g_hover >= 0) txt = BtnHint(g_hover);
    SetTextColor(hdc, g_recording ? C_REC : C_MUTED);
    RECT tr = { 26, tbh, client.right - 96, client.bottom };
    DrawTextA(hdc, txt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    /* right: events + speed */
    char meta[64];
    double s = CurrentSpeed();
    char spd[16];
    if (s == 0.5) lstrcpyA(spd, "0.5x"); else wsprintfA(spd, "%dx", (int)s);
    wsprintfA(meta, "%d  \xB7  %s", g_count, spd);
    RECT mr = { client.right - 92, tbh, client.right - 12, client.bottom };
    SetTextColor(hdc, g_count > 0 ? C_ACCENT : C_MUTED);
    DrawTextA(hdc, meta, -1, &mr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, of);
}

/* ---------- prefs menu ---------- */
static void AppendCheck(HMENU m, UINT id, const char* text, BOOL checked) {
    AppendMenuA(m, MF_STRING | (checked ? MF_CHECKED : 0), id, text);
}

static const char* VkName(DWORD vk) {
    static char buf[8];
    wsprintfA(buf, "F%d", (int)(vk - VK_F1 + 1));
    return buf;
}

static void ShowPrefsMenu(void) {
    HMENU m = CreatePopupMenu();
    double s = CurrentSpeed();

    AppendCheck(m, M_SPEED_HALF, "Play Speed:  1/2", !g_useCustomSpeed && g_speed == 0.5);
    AppendCheck(m, M_SPEED_1X,   "Play Speed:  1x",  !g_useCustomSpeed && g_speed == 1.0);
    AppendCheck(m, M_SPEED_2X,   "Play Speed:  2x",  !g_useCustomSpeed && g_speed == 2.0);
    AppendCheck(m, M_SPEED_100X, "Play Speed:  100x",!g_useCustomSpeed && g_speed == 100.0);

    HMENU custom = CreatePopupMenu();
    for (int i = 0; i < (int)(sizeof(g_customPresets)/sizeof(int)); i++) {
        char t[32]; wsprintfA(t, "%dx", g_customPresets[i]);
        AppendMenuA(custom, MF_STRING | ((g_useCustomSpeed && g_customSpeed == g_customPresets[i]) ? MF_CHECKED : 0),
                    M_CUSTOM_BASE + i, t);
    }
    char ct[64]; wsprintfA(ct, "Custom Speed:  %dx", g_customSpeed);
    AppendMenuA(m, MF_POPUP | (g_useCustomSpeed ? MF_CHECKED : 0), (UINT_PTR)custom, ct);

    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendCheck(m, M_CONTINUOUS, "Continuous Playback", g_continuous);

    HMENU loops = CreatePopupMenu();
    for (int i = 0; i < (int)(sizeof(g_loopPresets)/sizeof(int)); i++) {
        char t[32]; wsprintfA(t, "Repeat %dx", g_loopPresets[i]);
        AppendMenuA(loops, MF_STRING | (g_loops == g_loopPresets[i] ? MF_CHECKED : 0), M_LOOPS_BASE + i, t);
    }
    char lt[64]; wsprintfA(lt, "Playback Loops ...  (%d)", g_loops);
    AppendMenuA(m, MF_POPUP, (UINT_PTR)loops, lt);

    AppendMenuA(m, MF_SEPARATOR, 0, NULL);

    HMENU recHk = CreatePopupMenu();
    HMENU playHk = CreatePopupMenu();
    for (int i = 0; i < 12; i++) {
        char t[8]; wsprintfA(t, "F%d", i + 1);
        AppendMenuA(recHk,  MF_STRING | (g_recHotkey  == (DWORD)(VK_F1 + i) ? MF_CHECKED : 0), M_RECHK_BASE + i, t);
        AppendMenuA(playHk, MF_STRING | (g_playHotkey == (DWORD)(VK_F1 + i) ? MF_CHECKED : 0), M_PLAYHK_BASE + i, t);
    }
    HMENU stopHk = CreatePopupMenu();
    for (int i = 0; i < 12; i++) {
        char t2[8]; wsprintfA(t2, "F%d", i + 1);
        AppendMenuA(stopHk, MF_STRING | (g_stopHotkey == (DWORD)(VK_F1 + i) ? MF_CHECKED : 0),
                    M_STOPHK_BASE + i, t2);
    }

    char rt[48], pt[48], sthk[48];
    wsprintfA(rt, "Recording Hotkey  (%s)", VkName(g_recHotkey));
    wsprintfA(pt, "Playback Hotkey  (%s)", VkName(g_playHotkey));
    wsprintfA(sthk, "Stop Hotkey  (%s)", VkName(g_stopHotkey));
    AppendMenuA(m, MF_POPUP, (UINT_PTR)recHk, rt);
    AppendMenuA(m, MF_POPUP, (UINT_PTR)playHk, pt);
    AppendMenuA(m, MF_POPUP, (UINT_PTR)stopHk, sthk);

    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendCheck(m, M_ONTOP, "Always on Top", g_alwaysOnTop);
    AppendCheck(m, M_CAPTIONS, "Show Captions", g_showCaptions);
    AppendCheck(m, M_SMOOTH, "Smooth Cursor Motion", g_smooth);

    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, M_ABOUT, "About FlowTask 1.0");

    RECT b = BtnRect(BTN_PREFS);
    POINT pt2 = { b.left, b.bottom };
    ClientToScreen(g_hwnd, &pt2);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt2.x, pt2.y, 0, g_hwnd, NULL);
    DestroyMenu(m);
}

static void ApplyWindowSize(void) {
    DWORD style = (DWORD)GetWindowLongPtr(g_hwnd, GWL_STYLE);
    RECT wr = { 0, 0, CLIENT_W(), CLIENT_H() };
    AdjustWindowRect(&wr, style, FALSE);
    SetWindowPos(g_hwnd, NULL, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
                 SWP_NOMOVE | SWP_NOZORDER);
    InvalidateRect(g_hwnd, NULL, TRUE);
}

static void HandleMenuCmd(UINT id) {
    if (id >= M_CUSTOM_BASE && id < M_CUSTOM_BASE + 5) {
        g_customSpeed = g_customPresets[id - M_CUSTOM_BASE];
        g_useCustomSpeed = TRUE;
        InvalidateRect(g_hwnd, NULL, FALSE);
        return;
    }
    if (id >= M_LOOPS_BASE && id < M_LOOPS_BASE + 7) {
        g_loops = g_loopPresets[id - M_LOOPS_BASE];
        g_continuous = FALSE;
        char b[64]; wsprintfA(b, "Playback loops: %dx", g_loops);
        SetStatus(b);
        return;
    }
    if (id >= M_RECHK_BASE && id < M_RECHK_BASE + 12) {
        g_recHotkey = VK_F1 + (id - M_RECHK_BASE);
        char b[64]; wsprintfA(b, "Recording hotkey: %s", VkName(g_recHotkey));
        SetStatus(b);
        return;
    }
    if (id >= M_STOPHK_BASE && id < M_STOPHK_BASE + 12) {
        g_stopHotkey = VK_F1 + (id - M_STOPHK_BASE);
        char b[64]; wsprintfA(b, "Stop hotkey: %s", VkName(g_stopHotkey));
        SetStatus(b);
        return;
    }
    if (id >= M_PLAYHK_BASE && id < M_PLAYHK_BASE + 12) {
        g_playHotkey = VK_F1 + (id - M_PLAYHK_BASE);
        char b[64]; wsprintfA(b, "Playback hotkey: %s", VkName(g_playHotkey));
        SetStatus(b);
        return;
    }
    switch (id) {
        case M_SPEED_HALF: g_speed = 0.5; g_useCustomSpeed = FALSE; break;
        case M_SPEED_1X:   g_speed = 1.0; g_useCustomSpeed = FALSE; break;
        case M_SPEED_2X:   g_speed = 2.0; g_useCustomSpeed = FALSE; break;
        case M_SPEED_100X: g_speed = 100.0; g_useCustomSpeed = FALSE; break;
        case M_CONTINUOUS:
            g_continuous = !g_continuous;
            SetStatus(g_continuous ? "Continuous playback: on" : "Continuous playback: off");
            break;
        case M_ONTOP:
            g_alwaysOnTop = !g_alwaysOnTop;
            SetWindowPos(g_hwnd, g_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                         0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            SetStatus(g_alwaysOnTop ? "Always on top: on" : "Always on top: off");
            break;
        case M_SMOOTH:
            g_smooth = !g_smooth;
            SetStatus(g_smooth ? "Smooth cursor motion: on" : "Smooth cursor motion: off");
            break;
        case M_CAPTIONS:
            g_showCaptions = !g_showCaptions;
            ApplyWindowSize();
            break;
        case M_ABOUT:
            MessageBoxA(g_hwnd,
                "FlowTask 1.0\n\n"
                "Records mouse movement, clicks, scrolling and\n"
                "keystrokes, then replays them exactly.\n\n"
                "Rec / Play / Stop can be triggered by hotkey.\n"
                "Stop aborts continuous playback anytime.\n"
                "Macros can be saved and loaded (.flow).",
                "About FlowTask", MB_OK | MB_ICONINFORMATION);
            break;
    }
    InvalidateRect(g_hwnd, NULL, FALSE);
}

/* ---------- window proc ---------- */
static int HitTest(int x, int y) {
    for (int i = 0; i < BTN_COUNT; i++) {
        RECT b = BtnRect(i);
        POINT p = { x, y };
        if (PtInRect(&b, p)) return i;
    }
    return -1;
}

static void DoAction(int i) {
    switch (i) {
        case BTN_OPEN:  DoOpen(); break;
        case BTN_SAVE:  DoSave(); break;
        case BTN_REC:   ToggleRecording(); break;
        case BTN_PLAY:  TogglePlay(); break;
        case BTN_EXE:
            MessageBoxA(g_hwnd,
                "Exporting a macro as a standalone .exe\n"
                "is planned for a later version.\n\n"
                "For now: use Save to store it as .flow.",
                "FlowTask", MB_OK | MB_ICONINFORMATION);
            break;
        case BTN_PREFS: ShowPrefsMenu(); break;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            g_fontLabel = CreateFontA(11, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
            g_fontStatus = CreateFontA(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
            return 0;

        case WM_ERASEBKGND:
            return TRUE;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT c; GetClientRect(hwnd, &c);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, c.right, c.bottom);
            HBITMAP ob = SelectObject(mem, bmp);
            PaintAll(mem, c);
            BitBlt(hdc, 0, 0, c.right, c.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, ob);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_MOUSEMOVE: {
            int i = HitTest(LOWORD(lParam), HIWORD(lParam));
            if (i != g_hover) {
                g_hover = i;
                RedrawToolbar();
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            g_hover = -1; g_pressed = -1;
            RedrawToolbar();
            return 0;

        case WM_LBUTTONDOWN: {
            int i = HitTest(LOWORD(lParam), HIWORD(lParam));
            g_pressed = i;
            RedrawToolbar();
            return 0;
        }
        case WM_LBUTTONUP: {
            int i = HitTest(LOWORD(lParam), HIWORD(lParam));
            int was = g_pressed;
            g_pressed = -1;
            RedrawToolbar();
            if (i >= 0 && i == was) DoAction(i);
            return 0;
        }

        case WM_HOTKEY_REC:  ToggleRecording(); return 0;
        case WM_HOTKEY_PLAY: TogglePlay(); return 0;
        case WM_HOTKEY_STOP: StopEverything(); return 0;

        case WM_COMMAND:
            HandleMenuCmd(LOWORD(wParam));
            return 0;

        case WM_TIMER:
            if (wParam == ID_TIMER_BLINK) {
                g_blinkOn = !g_blinkOn;
                RedrawToolbar();
            }
            return 0;

        case WM_DESTROY:
            if (g_mouseHook) UnhookWindowsHookEx(g_mouseHook);
            if (g_kbHook) UnhookWindowsHookEx(g_kbHook);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* dark title bar on Win10/11 */
static void EnableDarkTitleBar(HWND hwnd) {
    HMODULE dwm = LoadLibraryA("dwmapi.dll");
    if (!dwm) return;
    typedef HRESULT (WINAPI *SetAttr)(HWND, DWORD, LPCVOID, DWORD);
    SetAttr fn = (SetAttr)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (fn) {
        BOOL dark = TRUE;
        fn(hwnd, 20, &dark, sizeof(dark));   /* DWMWA_USE_IMMERSIVE_DARK_MODE */
        fn(hwnd, 19, &dark, sizeof(dark));   /* older builds */
    }
    FreeLibrary(dwm);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow) {
    const char CLS[] = "FlowTaskMainWindow";
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLS;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.hIcon = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(IDI_APPICON), IMAGE_ICON,
                                 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    RegisterClassA(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT wr = { 0, 0, CLIENT_W(), CLIENT_H() };
    AdjustWindowRect(&wr, style, FALSE);

    g_hwnd = CreateWindowExA(0, CLS, "FlowTask", style,
        (GetSystemMetrics(SM_CXSCREEN) - (wr.right - wr.left)) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - (wr.bottom - wr.top)) / 3,
        wr.right - wr.left, wr.bottom - wr.top,
        NULL, NULL, hInstance, NULL);

    {
        HICON big = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(IDI_APPICON), IMAGE_ICON,
                                      GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
        HICON small = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(IDI_APPICON), IMAGE_ICON,
                                        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
        if (big)   SendMessage(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)big);
        if (small) SendMessage(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small);
    }

    EnableDarkTitleBar(g_hwnd);

    /* global keyboard hook: hotkeys + recording */
    g_kbHook = SetWindowsHookExA(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleA(NULL), 0);

    char st[128];
    wsprintfA(st, "Ready  -  %s Rec   %s Play   %s Stop", VkName(g_recHotkey), VkName(g_playHotkey), VkName(g_stopHotkey));
    lstrcpynA(g_status, st, sizeof(g_status));

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
