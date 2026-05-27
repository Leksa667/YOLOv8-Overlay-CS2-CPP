// ─────────────────────────────────────────────────────────────────────────────
//  YOLOv8 CS2 Overlay  v4
//  by Leksa667
// ─────────────────────────────────────────────────────────────────────────────

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdarg>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <setupapi.h>
#include <devguid.h>

#include "screen_capture.h"
#include "yolo_detector.h"
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "setupapi.lib")

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

static constexpr COLORREF C_PANEL_BG     = RGB( 10,  10,  16);
static constexpr COLORREF C_PANEL_BORDER = RGB( 45,  45,  65);
static constexpr COLORREF C_TEXT_BRIGHT  = RGB(220, 220, 235);
static constexpr COLORREF C_TEXT_DIM     = RGB( 95,  95, 120);
static constexpr COLORREF C_GREEN        = RGB(  0, 210, 100);
static constexpr COLORREF C_RED          = RGB(210,  50,  50);
static constexpr COLORREF C_ORANGE       = RGB(255, 160,   0);
static constexpr COLORREF COLOR_KEY      = RGB(  0,   0,   0);

static COLORREF g_class_colors[4] = {
    RGB( 40, 120, 255), RGB( 80, 190, 255),
    RGB(255,  50,  50), RGB(255, 150,  50),
};
static const COLORREF COLOR_PALETTE[] = {
    RGB( 40, 120, 255), RGB( 80, 190, 255), RGB(  0, 210, 100),
    RGB(255, 200,   0), RGB(255, 150,  50), RGB(255,  50,  50),
    RGB(200,  70, 190), RGB(230, 230, 240),
};
static const char* CLASS_NAMES[4] = { "CT", "CT HEAD", "T", "T HEAD" };

static constexpr UINT TIMER_ID = 1;
static constexpr UINT TIMER_MS = 8;
static constexpr float OVERLAY_REPAINT_DT = 1.0f / 120.0f;
static constexpr int DETECTION_CAPTURE_SIZE = 640;
static constexpr float FOV_MIN_PX = 10.f;
static constexpr float FOV_MAX_PX = 320.f;

static int g_sw = 0, g_sh = 0;

static std::atomic<bool>  g_det_on   {true};
static std::atomic<bool>  g_labels   {true};
static std::atomic<bool>  g_fov_vis  {true};
static std::atomic<bool>  g_crosshair_vis {true};
static std::atomic<bool>  g_aim_hold {false};
static std::atomic<bool>  g_running  {false};
static std::atomic<float> g_fps      {0.f};
static std::atomic<bool>  g_use_dxgi {false};
static std::atomic<float> g_cap_ms   {0.f};
static std::atomic<float> g_inf_ms   {0.f};
static std::atomic<float> g_conf_threshold {0.65f};

static std::atomic<float> g_fov_radius {250.f}; 
static std::atomic<float> g_aim_smooth {0.60f}; 
static std::atomic<bool>  g_aim_fov    {false}; 
static std::atomic<bool>  g_aim_lock   {false}; 
static std::atomic<bool>  g_aim_head   {true};   

static std::atomic<bool>  g_rcs_on        {false};
static std::atomic<float> g_rcs_strength  {0.50f};

static std::string g_arduino_port   = "\\\\.\\COM3";
static bool        g_arduino_mode   = false;
static HANDLE      g_arduino_serial = INVALID_HANDLE_VALUE;

static std::thread              g_thread;
static std::mutex               g_mutex;
static std::vector<Detection>   g_dets;
static std::unique_ptr<YoloDetector> g_detector;

static HWND  g_hwnd      = nullptr;
static HHOOK g_kbhook    = nullptr;
static HHOOK g_mousehook = nullptr;

static HDC     g_memDC  = nullptr;
static HBITMAP g_memBmp = nullptr;

static std::atomic<bool> g_hud_visible {true};
static int  g_hud_x = 12, g_hud_y = 12;
static bool g_hud_dragging  = false;
static int  g_drag_ox = 0,  g_drag_oy = 0;
static int  g_drag_start_x = 0, g_drag_start_y = 0;
static int  g_drag_hud_x = 0,   g_drag_hud_y = 0;
static int  g_slider_drag   = 0;
static bool g_debug_log     = false;
static int  g_hud_tab       = 0;
static std::atomic<bool> g_class_visible[4] = {true, true, true, true};
static std::atomic<bool> g_crop_viewer {false};
static int g_box_style = 0;
static int g_waiting_hotkey = -1;
static DWORD g_hotkey_det = VK_F1;
static DWORD g_hotkey_labels = VK_F2;
static DWORD g_hotkey_fov = VK_F3;
static DWORD g_hotkey_hold = 'K';
static DWORD g_hotkey_menu = VK_INSERT;

static constexpr int HUD_W = 520;
static constexpr int HUD_H = 480;

static constexpr int SLD_X1    =  22;
static constexpr int SLD_X2    = 498;
static constexpr int SLD_FOV_Y = 344;
static constexpr int SLD_TH    =   7;

struct HudBtn { int rx1, ry1, rx2, ry2; };
static constexpr HudBtn BTN_DET    = { 24, 128, 250, 166};
static constexpr HudBtn BTN_LABELS = {270, 128, 496, 166};
static constexpr HudBtn BTN_AIM    = { 24, 128, 250, 160};
static constexpr HudBtn BTN_LOCK   = {270, 128, 496, 160};
static constexpr HudBtn BTN_FOVVIS = { 24, 264, 250, 302};
static constexpr HudBtn BTN_CROP   = {270, 264, 496, 302};
static constexpr HudBtn BTN_STYLE  = { 24, 376, 250, 414};
static constexpr HudBtn BTN_CROSSHAIR = {270, 376, 496, 414};
static constexpr HudBtn BTN_HEAD   = { 24, 186, 250, 218};
static constexpr HudBtn BTN_CLOSE  = {493,  10, 510,  27};
static constexpr HudBtn BTN_RCS    = {270, 186, 496, 218};
static constexpr HudBtn TAB_AIM     = { 16,  52, 134,  84};
static constexpr HudBtn TAB_VISUALS = {140,  52, 258,  84};
static constexpr HudBtn TAB_MISC    = {264,  52, 382,  84};
static constexpr HudBtn TAB_CONFIG  = {388,  52, 504,  84};
static constexpr HudBtn FILTER_BTNS[4] = {
    { 22, 128, 246, 166},
    {274, 128, 498, 166},
    { 22, 184, 246, 222},
    {274, 184, 498, 222},
};
static constexpr HudBtn COLOR_BTNS[4] = {
    {210, 138, 240, 158},
    {462, 138, 492, 158},
    {210, 194, 240, 214},
    {462, 194, 492, 214},
};
static constexpr HudBtn HOTKEY_BTNS[5] = {
    { 22, 240, 246, 272},
    {274, 240, 498, 272},
    { 22, 284, 246, 316},
    {274, 284, 498, 316},
    { 22, 328, 246, 360},
};

static void Letterbox(const cv::Mat& src, cv::Mat& out, int sz,
                      float& sc, int& px, int& py)
{
    float rx = float(sz) / src.cols;
    float ry = float(sz) / src.rows;
    sc = std::min(rx, ry);
    int nw = int(std::round(src.cols * sc));
    int nh = int(std::round(src.rows * sc));
    px = (sz - nw) / 2;  py = (sz - nh) / 2;
    out.create(sz, sz, CV_8UC3);
    out.setTo(cv::Scalar(114,114,114));
    cv::Mat roi = out(cv::Rect(px, py, nw, nh));
    if (src.channels() == 4) {
        if (src.cols == nw && src.rows == nh) {
            cv::cvtColor(src, roi, cv::COLOR_BGRA2RGB);
        } else {
            static thread_local cv::Mat resized_bgra;
            cv::resize(src, resized_bgra, {nw, nh}, 0, 0, cv::INTER_LINEAR);
            cv::cvtColor(resized_bgra, roi, cv::COLOR_BGRA2RGB);
        }
    } else {
        if (src.cols == nw && src.rows == nh) src.copyTo(roi);
        else cv::resize(src, roi, {nw, nh}, 0, 0, cv::INTER_LINEAR);
    }
}

static void RequestRepaint();

static void SetOverlayClickThrough(bool clickThrough)
{
    if (!g_hwnd) return;
    LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    if (clickThrough) ex |= WS_EX_TRANSPARENT;
    else              ex &= ~WS_EX_TRANSPARENT;
    SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, ex);
    SetWindowPos(g_hwnd, nullptr, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED|SWP_NOACTIVATE);
    SetWindowPos(g_hwnd, nullptr, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED|SWP_NOACTIVATE);
    RequestRepaint();
    {
        char buf[128]; sprintf_s(buf, "[Overlay] ClickThrough=%d\n", (int)clickThrough);
        OutputDebugStringA(buf);
    }
}

static void Debugf(const char* fmt, ...)
{
    if (!g_debug_log) return;

    char buf[512];
    va_list ap; va_start(ap, fmt);
    vsprintf_s(buf, fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
    static std::ofstream logf("log.txt", std::ios::app);
    logf << buf;
    logf.flush();
}

static void RequestRepaint()
{
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
}

static inline float fclamp01(float v);
static inline int iclamp(int v, int lo, int hi);

static const char* BoxStyleName()
{
    static const char* names[] = {"Full box", "Corners", "Skeleton"};
    return names[iclamp(g_box_style, 0, 2)];
}

static void KeyName(DWORD vk, char* out, size_t outSize)
{
    if (vk >= 'A' && vk <= 'Z') { sprintf_s(out, outSize, "%c", char(vk)); return; }
    if (vk >= '0' && vk <= '9') { sprintf_s(out, outSize, "%c", char(vk)); return; }
    if (vk >= VK_F1 && vk <= VK_F24) { sprintf_s(out, outSize, "F%u", unsigned(vk - VK_F1 + 1)); return; }
    switch (vk) {
    case VK_INSERT: sprintf_s(out, outSize, "Insert"); return;
    case VK_END: sprintf_s(out, outSize, "End"); return;
    case VK_SHIFT: sprintf_s(out, outSize, "Shift"); return;
    case VK_CONTROL: sprintf_s(out, outSize, "Ctrl"); return;
    case VK_MENU: sprintf_s(out, outSize, "Alt"); return;
    case VK_SPACE: sprintf_s(out, outSize, "Space"); return;
    default: sprintf_s(out, outSize, "VK%u", unsigned(vk)); return;
    }
}

static void EndHudInteraction()
{
    g_slider_drag = 0;
    g_hud_dragging = false;
    ReleaseCapture();
}

static bool HandleHudMouse(UINT msg, int x, int y)
{
    if (!g_hud_visible.load(std::memory_order_relaxed) && !g_hud_dragging && g_slider_drag == 0)
        return false;

    if (g_slider_drag != 0) {
        if (msg == WM_MOUSEMOVE) {
            float t = fclamp01(float(x - g_hud_x - SLD_X1) / float(SLD_X2 - SLD_X1));
            if (g_slider_drag == 1) g_fov_radius.store(FOV_MIN_PX + t * (FOV_MAX_PX - FOV_MIN_PX));
            if (g_slider_drag == 2) g_conf_threshold.store(0.10f + t * 0.85f);
            if (g_slider_drag == 3) g_rcs_strength.store(t);
            RequestRepaint();
            return true;
        }
        if (msg == WM_LBUTTONUP) {
            EndHudInteraction();
            return true;
        }
    }

    if (g_hud_dragging) {
        if (msg == WM_MOUSEMOVE) {
            g_hud_x = iclamp(g_drag_hud_x + (x - g_drag_start_x), 0, g_sw - HUD_W);
            g_hud_y = iclamp(g_drag_hud_y + (y - g_drag_start_y), 0, g_sh - HUD_H);
            RequestRepaint();
            return true;
        }
        if (msg == WM_LBUTTONUP) {
            EndHudInteraction();
            return true;
        }
    }

    if (msg == WM_LBUTTONUP) {
        EndHudInteraction();
        return false;
    }

    if (msg != WM_LBUTTONDOWN || !g_hud_visible.load(std::memory_order_relaxed))
        return false;

    RECT hud_rect = {g_hud_x, g_hud_y, g_hud_x + HUD_W, g_hud_y + HUD_H};
    POINT pt{x, y};
    if (!PtInRect(&hud_rect, pt)) return false;

    int rx = x - g_hud_x;
    int ry = y - g_hud_y;

    if (ry < 44) {
        if (rx >= BTN_CLOSE.rx1 && rx <= BTN_CLOSE.rx2 &&
            ry >= BTN_CLOSE.ry1 && ry <= BTN_CLOSE.ry2) {
            g_hud_visible.store(false);
            SetOverlayClickThrough(true);
        } else {
            g_hud_dragging = true;
            g_drag_start_x = x;
            g_drag_start_y = y;
            g_drag_hud_x = g_hud_x;
            g_drag_hud_y = g_hud_y;
            SetCapture(g_hwnd);
        }
        RequestRepaint();
        return true;
    }

    auto inBtn = [&](const HudBtn& b) {
        return rx >= b.rx1 && rx <= b.rx2 && ry >= b.ry1 && ry <= b.ry2;
    };
    if (inBtn(TAB_AIM))     { g_hud_tab = 0; RequestRepaint(); return true; }
    if (inBtn(TAB_VISUALS)) { g_hud_tab = 1; RequestRepaint(); return true; }
    if (inBtn(TAB_MISC))    { g_hud_tab = 2; RequestRepaint(); return true; }
    if (inBtn(TAB_CONFIG))  { g_hud_tab = 3; RequestRepaint(); return true; }

    if (g_hud_tab == 0) {
        if (inBtn(BTN_AIM))  { bool on = !g_aim_fov.load(); g_aim_fov.store(on); if (on) g_aim_lock.store(false); RequestRepaint(); return true; }
        if (inBtn(BTN_LOCK)) { bool on = !g_aim_lock.load(); g_aim_lock.store(on); if (on) g_aim_fov.store(false); RequestRepaint(); return true; }
        if (inBtn(BTN_HEAD)) { g_aim_head.store(!g_aim_head.load()); RequestRepaint(); return true; }
        if (inBtn(BTN_RCS))  { g_rcs_on.store(!g_rcs_on.load()); RequestRepaint(); return true; }
        if (ry >= 256 && ry <= 280 && rx >= SLD_X1 && rx <= SLD_X2) {
            g_slider_drag = 3;
            float t = fclamp01(float(rx - SLD_X1) / float(SLD_X2 - SLD_X1));
            g_rcs_strength.store(t);
            SetCapture(g_hwnd);
            RequestRepaint();
            return true;
        }
        if (ry >= 326 && ry <= 362 && rx >= SLD_X1 && rx <= SLD_X2) {
            g_slider_drag = 1;
            float t = fclamp01(float(rx - SLD_X1) / float(SLD_X2 - SLD_X1));
            g_fov_radius.store(FOV_MIN_PX + t * (FOV_MAX_PX - FOV_MIN_PX));
            SetCapture(g_hwnd);
            RequestRepaint();
            return true;
        }
    } else if (g_hud_tab == 1) {
        if (inBtn(BTN_DET))  { g_det_on.store(!g_det_on.load()); RequestRepaint(); return true; }
        if (inBtn(BTN_LABELS)) { g_labels.store(!g_labels.load()); RequestRepaint(); return true; }
        if (inBtn(BTN_FOVVIS)) { g_fov_vis.store(!g_fov_vis.load()); RequestRepaint(); return true; }
        if (inBtn(BTN_CROP)) { g_crop_viewer.store(!g_crop_viewer.load()); RequestRepaint(); return true; }
        if (inBtn(BTN_STYLE)) { g_box_style = (g_box_style + 1) % 3; RequestRepaint(); return true; }
        if (inBtn(BTN_CROSSHAIR)) { g_crosshair_vis.store(!g_crosshair_vis.load()); RequestRepaint(); return true; }
    } else if (g_hud_tab == 2) {
        for (int i = 0; i < 4; ++i) {
            if (inBtn(COLOR_BTNS[i])) {
                int paletteCount = int(sizeof(COLOR_PALETTE) / sizeof(COLOR_PALETTE[0]));
                int next = 0;
                for (int p = 0; p < paletteCount; ++p) {
                    if (g_class_colors[i] == COLOR_PALETTE[p]) { next = (p + 1) % paletteCount; break; }
                }
                g_class_colors[i] = COLOR_PALETTE[next];
                RequestRepaint();
                return true;
            }
            if (inBtn(FILTER_BTNS[i])) {
                g_class_visible[i].store(!g_class_visible[i].load(std::memory_order_relaxed));
                RequestRepaint();
                return true;
            }
        }
        if (ry >= 326 && ry <= 362 && rx >= SLD_X1 && rx <= SLD_X2) {
            g_slider_drag = 2;
            float t = fclamp01(float(rx - SLD_X1) / float(SLD_X2 - SLD_X1));
            g_conf_threshold.store(0.10f + t * 0.85f);
            SetCapture(g_hwnd);
            RequestRepaint();
            return true;
        }
    } else if (g_hud_tab == 3) {
        for (int i = 0; i < 5; ++i) {
            if (inBtn(HOTKEY_BTNS[i])) {
                g_waiting_hotkey = i;
                RequestRepaint();
                return true;
            }
        }
    }

    return true;
}

static inline float fclamp01(float v)
{
    if (v <= 0.f) return 0.f;
    if (v >= 1.f) return 1.f;
    return v;
}

static inline int iclamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void GdiRoundRect(HDC hdc, int x1, int y1, int x2, int y2,
                          COLORREF pen_clr, int pen_w,
                          COLORREF fill_clr, bool filled)
{
    HPEN   pen = CreatePen(PS_SOLID, pen_w, pen_clr);
    HBRUSH br  = filled ? CreateSolidBrush(fill_clr)
                        : (HBRUSH)GetStockObject(NULL_BRUSH);
    HGDIOBJ op = SelectObject(hdc, pen);
    HGDIOBJ ob = SelectObject(hdc, br);
    RoundRect(hdc, x1, y1, x2, y2, 8, 8);
    SelectObject(hdc, op); SelectObject(hdc, ob);
    DeleteObject(pen);
    if (filled) DeleteObject(br);
}

static void GdiText(HDC hdc, const char* txt, RECT rc, COLORREF clr, HFONT font,
                    UINT fmt = DT_LEFT|DT_VCENTER|DT_SINGLELINE)
{
    HGDIOBJ old = SelectObject(hdc, font);
    SetTextColor(hdc, clr);
    DrawTextA(hdc, txt, -1, &rc, fmt);
    SelectObject(hdc, old);
}

struct SkeletonTrack {
    int class_id = -1;
    float cx = 0.f, cy = 0.f;
    float vx = 0.f, vy = 0.f;
    ULONGLONG tick = 0;
};

static std::vector<SkeletonTrack> g_skeleton_tracks;

static bool IsBodyClass(int class_id)
{
    return class_id == 0 || class_id == 2;
}

static int HeadClassForBody(int class_id)
{
    if (class_id == 0) return 1;
    if (class_id == 2) return 3;
    return -1;
}

static const Detection* FindMatchingHead(const std::vector<Detection>& dets, const Detection& body)
{
    int head_class = HeadClassForBody(body.class_id);
    if (head_class < 0 || !g_class_visible[head_class].load(std::memory_order_relaxed))
        return nullptr;

    int bw = std::max(1, body.x2 - body.x1);
    int bh = std::max(1, body.y2 - body.y1);
    float body_cx = (body.x1 + body.x2) * 0.5f;
    float best_score = FLT_MAX;
    const Detection* best = nullptr;

    for (const auto& h : dets) {
        if (h.class_id != head_class) continue;
        float hx = (h.x1 + h.x2) * 0.5f;
        float hy = (h.y1 + h.y2) * 0.5f;
        float dx = std::fabs(hx - body_cx);
        float ideal_y = float(body.y1) - std::max(4.f, float(h.y2 - h.y1) * 0.15f);
        bool plausible_x = dx <= std::max(48.f, bw * 0.75f);
        bool plausible_y = hy >= body.y1 - bh * 0.45f && hy <= body.y1 + bh * 0.30f;
        if (!plausible_x || !plausible_y) continue;

        float score = dx + std::fabs(hy - ideal_y) * 0.55f - h.score * 18.f;
        if (score < best_score) {
            best_score = score;
            best = &h;
        }
    }
    return best;
}

static float UpdateSkeletonSway(const Detection& body)
{
    ULONGLONG now = GetTickCount64();
    float cx = (body.x1 + body.x2) * 0.5f;
    float cy = (body.y1 + body.y2) * 0.5f;
    float bh = float(std::max(1, body.y2 - body.y1));

    int best = -1;
    float best_dist = std::max(90.f, bh * 0.85f);
    for (int i = 0; i < (int)g_skeleton_tracks.size(); ++i) {
        auto& tr = g_skeleton_tracks[i];
        if (tr.class_id != body.class_id) continue;
        float dx = cx - tr.cx;
        float dy = cy - tr.cy;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }

    if (best < 0) {
        SkeletonTrack tr;
        tr.class_id = body.class_id;
        tr.cx = cx; tr.cy = cy; tr.tick = now;
        g_skeleton_tracks.push_back(tr);
        return float(std::sin(now * 0.006 + cx * 0.025) * 2.0);
    }

    auto& tr = g_skeleton_tracks[best];
    float dx = cx - tr.cx;
    float dy = cy - tr.cy;
    tr.vx = tr.vx * 0.70f + dx * 0.30f;
    tr.vy = tr.vy * 0.70f + dy * 0.30f;
    tr.cx = cx; tr.cy = cy; tr.tick = now;

    g_skeleton_tracks.erase(std::remove_if(g_skeleton_tracks.begin(), g_skeleton_tracks.end(),
        [now](const SkeletonTrack& tr) { return now - tr.tick > 900; }), g_skeleton_tracks.end());

    float idle = float(std::sin(now * 0.006 + cx * 0.025) * 2.0);
    float sway = tr.vx * 0.35f + idle;
    float max_sway = std::max(3.f, float(body.x2 - body.x1) * 0.18f);
    if (sway < -max_sway) sway = -max_sway;
    if (sway >  max_sway) sway =  max_sway;
    return sway;
}

static void DrawSkeleton(HDC hdc, const Detection& body, const Detection* head, COLORREF clr)
{
    int bw = std::max(1, body.x2 - body.x1);
    int bh = std::max(1, body.y2 - body.y1);
    int cx = body.x1 + bw / 2;
    float sway_f = UpdateSkeletonSway(body);
    int sway = int(std::round(sway_f));
    int counter = int(std::round(-sway_f * 0.65f));

    int hx = cx + sway / 4;
    int hy = body.y1 - std::max(8, bh / 8);
    int hr = std::max(4, std::min(bw, bh) / 9);
    if (head) {
        hx = (head->x1 + head->x2) / 2;
        hy = (head->y1 + head->y2) / 2;
        hr = std::max(4, std::min(head->x2 - head->x1, head->y2 - head->y1) / 2);
    }

    int neck_y = std::max(body.y1, hy + hr + 2);
    int chest_y = body.y1 + bh * 18 / 100;
    int hip_y = body.y1 + bh * 58 / 100;
    int knee_y = body.y1 + bh * 78 / 100;
    int foot_y = body.y2;
    int shoulder_l = body.x1 + bw * 30 / 100;
    int shoulder_r = body.x1 + bw * 70 / 100;
    int hand_l = body.x1 + bw * 18 / 100 + counter;
    int hand_r = body.x1 + bw * 82 / 100 + sway;
    int hip_l = body.x1 + bw * 40 / 100;
    int hip_r = body.x1 + bw * 60 / 100;
    int knee_l = body.x1 + bw * 39 / 100 + sway / 2;
    int knee_r = body.x1 + bw * 61 / 100 + counter / 2;
    int foot_l = body.x1 + bw * 32 / 100 + counter;
    int foot_r = body.x1 + bw * 68 / 100 + sway;

    HPEN p = CreatePen(PS_SOLID, 1, clr);
    HGDIOBJ old_pen = SelectObject(hdc, p);
    HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));

    Ellipse(hdc, hx - hr, hy - hr, hx + hr, hy + hr);
    MoveToEx(hdc, hx, hy + hr, nullptr); LineTo(hdc, cx, neck_y);
    MoveToEx(hdc, cx, neck_y, nullptr); LineTo(hdc, cx + sway / 3, hip_y);
    MoveToEx(hdc, shoulder_l, chest_y, nullptr); LineTo(hdc, shoulder_r, chest_y);
    MoveToEx(hdc, shoulder_l, chest_y, nullptr); LineTo(hdc, hand_l, body.y1 + bh * 47 / 100);
    MoveToEx(hdc, shoulder_r, chest_y, nullptr); LineTo(hdc, hand_r, body.y1 + bh * 47 / 100);
    MoveToEx(hdc, hip_l, hip_y, nullptr); LineTo(hdc, hip_r, hip_y);
    MoveToEx(hdc, hip_l, hip_y, nullptr); LineTo(hdc, knee_l, knee_y); LineTo(hdc, foot_l, foot_y);
    MoveToEx(hdc, hip_r, hip_y, nullptr); LineTo(hdc, knee_r, knee_y); LineTo(hdc, foot_r, foot_y);

    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    DeleteObject(p);
}

static void SendMouseMove(int dx, int dy) {
    if (dx == 0 && dy == 0) return;
    if (g_arduino_mode && g_arduino_serial != INVALID_HANDLE_VALUE) {
        char buf[32];
        int len = sprintf_s(buf, sizeof(buf), "M %d %d\n", dx, dy);
        if (len > 0) {
            DWORD written = 0;
            if (!WriteFile(g_arduino_serial, buf, (DWORD)len, &written, NULL) || written == 0) {
                CloseHandle(g_arduino_serial);
                g_arduino_serial = INVALID_HANDLE_VALUE;
            }
        }
    } else {
        INPUT inp{};
        inp.type       = INPUT_MOUSE;
        inp.mi.dx      = dx;
        inp.mi.dy      = dy;
        inp.mi.dwFlags = MOUSEEVENTF_MOVE;
        SendInput(1, &inp, sizeof(INPUT));
    }
}

static void doAimbot(const std::vector<Detection>& dets)
{
    bool bind = g_aim_hold.load(std::memory_order_relaxed);
    bool fov_toggle = g_aim_fov.load(std::memory_order_relaxed);
    bool lock_toggle = g_aim_lock.load(std::memory_order_relaxed);

    bool fov_on  = bind && fov_toggle;
    bool lock_on = lock_toggle;
    if ((!fov_on && !lock_on) || dets.empty()) return;

    bool aim_head = g_aim_head.load(std::memory_order_relaxed);
    static std::mt19937 rng{ std::random_device{}() };
    static std::uniform_real_distribution<float> head_dist(0.08f, 0.20f);
    static std::uniform_real_distribution<float> body_dist(0.50f, 0.70f);
    static std::uniform_real_distribution<float> jitter_dist(-1.2f, 1.2f);
    static std::uniform_int_distribution<int> skip_dist(0, 32);

    if (skip_dist(rng) == 0) return;

    float aim_ratio  = aim_head ? head_dist(rng) : body_dist(rng);
    float aim_offset = aim_head ? 15.0f : 0.0f;
    float sm = g_aim_smooth.load(std::memory_order_relaxed);

    POINT cur; GetCursorPos(&cur);
    static auto last_move = std::chrono::steady_clock::now() - std::chrono::milliseconds(100);

    auto sendRelMove = [](float dx, float dy, float sm) {
        long ix = long(dx * sm);
        long iy = long(dy * sm);
        if (ix == 0 && iy == 0) return;
        SendMouseMove(int(ix), int(iy));
    };

    if (lock_on) {
        const Detection* best = nullptr;
        for (const auto& d : dets) {
            if (cur.x >= d.x1 && cur.x <= d.x2 &&
                cur.y >= d.y1 && cur.y <= d.y2) {
                best = &d;
                break;
            }
        }
        if (!best) {
            float best_d2 = 80.f * 80.f;
            for (const auto& d : dets) {
                float ax = (d.x1 + d.x2) * 0.5f;
                float ay = d.y1 + (d.y2 - d.y1) * aim_ratio + aim_offset;
                float ddx = ax - cur.x, ddy = ay - cur.y;
                float d2 = ddx*ddx + ddy*ddy;
                if (d2 < best_d2) { best_d2 = d2; best = &d; }
            }
        }
        if (!best) return;
        float ax = (best->x1 + best->x2) * 0.5f;
        float ay = best->y1 + (best->y2 - best->y1) * aim_ratio + aim_offset;
        float dx = ax - cur.x + jitter_dist(rng);
        float dy = ay - cur.y + jitter_dist(rng);
        if (g_rcs_on.load(std::memory_order_relaxed) &&
            (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
            dy = 0.f;
        }
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<float>(now - last_move).count() > 0.01f) {
            sendRelMove(dx, dy, sm);
            last_move = now;
        }
        return;
    }

    if (fov_on) {
        float cx = g_sw * 0.5f, cy = g_sh * 0.5f;
        float fov = g_fov_radius.load(std::memory_order_relaxed);
        float best_d2 = fov * fov;
        const Detection* best = nullptr;

        for (const auto& d : dets) {
            float ax = (d.x1 + d.x2) * 0.5f;
            float ay = d.y1 + (d.y2 - d.y1) * aim_ratio + aim_offset;
            float dx = ax - cx, dy = ay - cy;
            float d2 = dx*dx + dy*dy;
            if (d2 < best_d2) { best_d2 = d2; best = &d; }
        }
        if (!best) return;

        float ax = (best->x1 + best->x2) * 0.5f;
        float ay = best->y1 + (best->y2 - best->y1) * aim_ratio + aim_offset;
        float dx = ax - cur.x + jitter_dist(rng);
        float dy = ay - cur.y + jitter_dist(rng);
        sendRelMove(dx, dy, sm);
    }
}

static void drawOverlay(HDC hdc, const std::vector<Detection>& dets)
{
    {
        RECT rc = {0, 0, g_sw, g_sh};
        HBRUSH bg = CreateSolidBrush(COLOR_KEY);
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);
    }
    SetBkMode(hdc, TRANSPARENT);

    static HFONT fBold = nullptr;
    static HFONT fMono = nullptr;
    static HFONT fTitle = nullptr;
    static HFONT fSmall = nullptr;
    if (!fBold) {
        fBold  = CreateFontA(14,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,"Segoe UI");
        fMono  = CreateFontA(13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH|FF_DONTCARE,"Consolas");
        fTitle = CreateFontA(16,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,"Segoe UI");
        fSmall = CreateFontA(12,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,"Segoe UI");
    }

    bool det_on  = g_det_on.load(std::memory_order_relaxed);
    bool fov_vis = g_fov_vis.load(std::memory_order_relaxed);
    bool fov_toggle = g_aim_fov.load(std::memory_order_relaxed);
    bool lock_toggle = g_aim_lock.load(std::memory_order_relaxed);

    if (fov_vis && fov_toggle) {
        int cr = int(g_fov_radius.load(std::memory_order_relaxed));
        int cx = g_sw/2, cy = g_sh/2;
        HPEN fp = CreatePen(PS_DOT, 1, RGB(255,200,0));
        SelectObject(hdc, fp);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Ellipse(hdc, cx-cr, cy-cr, cx+cr, cy+cr);
        SelectObject(hdc, GetStockObject(WHITE_PEN));
        DeleteObject(fp);
    }

    if (g_crosshair_vis.load(std::memory_order_relaxed)) {
        int cx = g_sw/2, cy = g_sh/2, cs = 7;
        HPEN cp = CreatePen(PS_SOLID, 1, RGB(200,200,200));
        SelectObject(hdc, cp);
        MoveToEx(hdc, cx-cs, cy, nullptr); LineTo(hdc, cx-2, cy);
        MoveToEx(hdc, cx+2,  cy, nullptr); LineTo(hdc, cx+cs, cy);
        MoveToEx(hdc, cx, cy-cs, nullptr); LineTo(hdc, cx, cy-2);
        MoveToEx(hdc, cx, cy+2,  nullptr); LineTo(hdc, cx, cy+cs);
        SelectObject(hdc, GetStockObject(WHITE_PEN));
        DeleteObject(cp);
    }

    if (g_crop_viewer.load(std::memory_order_relaxed)) {
        int sz = std::min(DETECTION_CAPTURE_SIZE, std::min(g_sw, g_sh));
        int x1 = (g_sw - sz) / 2;
        int y1 = (g_sh - sz) / 2;
        HPEN pen = CreatePen(PS_DOT, 1, RGB(90, 170, 255));
        SelectObject(hdc, pen);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, x1, y1, x1 + sz, y1 + sz);
        SelectObject(hdc, GetStockObject(WHITE_PEN));
        DeleteObject(pen);
    }

    for (const auto& d : dets) {
        if (d.class_id < 0 || d.class_id >= 4 ||
            !g_class_visible[d.class_id].load(std::memory_order_relaxed)) {
            continue;
        }
        COLORREF clr = g_class_colors[d.class_id];
        int style = iclamp(g_box_style, 0, 2);
        if (style == 2 && !IsBodyClass(d.class_id)) {
            continue;
        }
        if (style == 0) {
            GdiRoundRect(hdc, d.x1, d.y1, d.x2, d.y2, clr, 1, 0, false);
        } else if (style == 1) {
            HPEN p = CreatePen(PS_SOLID, 1, clr);
            SelectObject(hdc, p);
            int w = std::max(8, (d.x2 - d.x1) / 4);
            int h = std::max(8, (d.y2 - d.y1) / 4);
            MoveToEx(hdc, d.x1, d.y1 + h, nullptr); LineTo(hdc, d.x1, d.y1); LineTo(hdc, d.x1 + w, d.y1);
            MoveToEx(hdc, d.x2 - w, d.y1, nullptr); LineTo(hdc, d.x2, d.y1); LineTo(hdc, d.x2, d.y1 + h);
            MoveToEx(hdc, d.x1, d.y2 - h, nullptr); LineTo(hdc, d.x1, d.y2); LineTo(hdc, d.x1 + w, d.y2);
            MoveToEx(hdc, d.x2 - w, d.y2, nullptr); LineTo(hdc, d.x2, d.y2); LineTo(hdc, d.x2, d.y2 - h);
            SelectObject(hdc, GetStockObject(WHITE_PEN));
            DeleteObject(p);
        } else if (style == 2) {
            DrawSkeleton(hdc, d, FindMatchingHead(dets, d), clr);
        }
        if (style == 0) {
            int bw = int((d.x2-d.x1)*d.score);
            HBRUSH bb = CreateSolidBrush(clr);
            RECT br = {d.x1, d.y2-3, d.x1+bw, d.y2};
            FillRect(hdc, &br, bb); DeleteObject(bb);
        }
        if (g_labels.load(std::memory_order_relaxed)) {
            char buf[32];
            sprintf_s(buf, "%s  %.0f%%", CLASS_NAMES[d.class_id], d.score*100.f);
            SIZE ts; GetTextExtentPoint32A(hdc, buf, (int)strlen(buf), &ts);
            int lx = d.x1, ly = std::max(d.y1-18, 0);
            GdiRoundRect(hdc, lx-1, ly, lx+ts.cx+8, ly+17, clr, 1, C_PANEL_BG, true);
            RECT tr = {lx+4, ly+1, lx+ts.cx+8, ly+17};
            GdiText(hdc, buf, tr, clr, fBold);
        }
    }

    if (g_hud_visible.load(std::memory_order_relaxed))
    {
        float fps    = g_fps.load(std::memory_order_relaxed);
        float cap_ms = g_cap_ms.load(std::memory_order_relaxed);
        float inf_ms = g_inf_ms.load(std::memory_order_relaxed);
        bool  dxgi   = g_use_dxgi.load(std::memory_order_relaxed);
        bool  cuda   = g_detector && g_detector->isUsingGPU();
        float fov_r  = g_fov_radius.load(std::memory_order_relaxed);
        size_t ndets = dets.size();

        const int px = g_hud_x, py = g_hud_y;
        const int pw = HUD_W,   ph = HUD_H;

        {
            auto drawLine = [&](int y) {
                HPEN s = CreatePen(PS_SOLID, 1, C_PANEL_BORDER);
                SelectObject(hdc, s);
                MoveToEx(hdc, px + 18, py + y, nullptr);
                LineTo(hdc, px + pw - 18, py + y);
                SelectObject(hdc, GetStockObject(WHITE_PEN));
                DeleteObject(s);
            };
            auto drawCaption = [&](const char* text, int y) {
                RECT r = {px + 24, py + y, px + pw - 24, py + y + 16};
                GdiText(hdc, text, r, C_TEXT_DIM, fSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            };
            auto drawToggle = [&](const HudBtn& b, bool on, COLORREF col, const char* label, const char* sub) {
                int x1 = px + b.rx1, y1 = py + b.ry1, x2 = px + b.rx2, y2 = py + b.ry2;
                COLORREF bg = on ? RGB(30, 35, 42) : RGB(18, 18, 27);
                COLORREF br = on ? col : RGB(52, 52, 70);
                GdiRoundRect(hdc, x1, y1, x2, y2, br, 1, bg, true);
                HPEN np = CreatePen(PS_NULL, 0, 0);
                HBRUSH led = CreateSolidBrush(on ? col : RGB(42, 42, 55));
                SelectObject(hdc, np); SelectObject(hdc, led);
                Ellipse(hdc, x1 + 12, y1 + 14, x1 + 22, y1 + 24);
                DeleteObject(led); DeleteObject(np);
                SelectObject(hdc, (HBRUSH)GetStockObject(NULL_BRUSH));
                SelectObject(hdc, GetStockObject(WHITE_PEN));
                RECT lr = {x1 + 32, y1 + 5, x2 - 12, y1 + 21};
                RECT sr = {x1 + 32, y1 + 21, x2 - 12, y2 - 3};
                GdiText(hdc, label, lr, on ? C_TEXT_BRIGHT : RGB(120, 120, 138), fBold);
                GdiText(hdc, sub, sr, C_TEXT_DIM, fSmall);
            };

            GdiRoundRect(hdc, px, py, px + pw, py + ph, C_PANEL_BORDER, 1, C_PANEL_BG, true);
            HBRUSH head = CreateSolidBrush(RGB(17, 18, 28));
            RECT hr = {px + 1, py + 1, px + pw - 1, py + 44};
            FillRect(hdc, &hr, head); DeleteObject(head);

            RECT title = {px + 22, py + 8, px + pw - 48, py + 27};
            RECT subtitle = {px + 22, py + 27, px + pw - 48, py + 42};
            char subline[96];
            {
                char mode_str[48];
                if (g_arduino_mode && g_arduino_serial != INVALID_HANDLE_VALUE) {
                    const char* pn = strrchr(g_arduino_port.c_str(), '\\');
                    sprintf_s(mode_str, "Arduino %s", pn ? pn + 1 : "HID");
                } else {
                    strcpy_s(mode_str, g_arduino_mode ? "Arduino (disconnected)" : "Normal");
                }
                sprintf_s(subline, "640 center crop  |  conf >= %.0f%%  |  %s",
                    g_conf_threshold.load(std::memory_order_relaxed) * 100.f, mode_str);
            }
            GdiText(hdc, "YOLOv8 Overlay", title, C_TEXT_BRIGHT, fTitle);
            GdiText(hdc, subline, subtitle, C_TEXT_DIM, fSmall);
            RECT close = {px + BTN_CLOSE.rx1, py + BTN_CLOSE.ry1, px + BTN_CLOSE.rx2, py + BTN_CLOSE.ry2};
            GdiRoundRect(hdc, close.left, close.top, close.right, close.bottom, RGB(90,35,35), 1, RGB(55,20,24), true);
            GdiText(hdc, "x", close, RGB(220,120,120), fBold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            struct TabDef { const HudBtn& b; const char* label; int id; };
            TabDef tabs[] = {{TAB_AIM, "AIM", 0}, {TAB_VISUALS, "VISUALS", 1}, {TAB_MISC, "MISC", 2}, {TAB_CONFIG, "CONFIG", 3}};
            for (const auto& t : tabs) {
                bool active = g_hud_tab == t.id;
                COLORREF bg = active ? RGB(32, 38, 48) : RGB(18, 18, 27);
                COLORREF br = active ? RGB(80, 160, 255) : RGB(48, 48, 65);
                RECT r = {px + t.b.rx1, py + t.b.ry1, px + t.b.rx2, py + t.b.ry2};
                GdiRoundRect(hdc, r.left, r.top, r.right, r.bottom, br, 1, bg, true);
                GdiText(hdc, t.label, r, active ? C_TEXT_BRIGHT : C_TEXT_DIM, fBold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            drawLine(94);

            if (g_hud_tab == 0) {
                char key_hold[32];
                KeyName(g_hotkey_hold, key_hold, sizeof(key_hold));
                char sub_hold[64]; sprintf_s(sub_hold, "%s - hold action", key_hold);

                drawCaption("TARGETING", 108);
                drawToggle(BTN_AIM,  fov_toggle,  C_ORANGE,       "FOV mode",  sub_hold);
                drawToggle(BTN_LOCK, lock_toggle, RGB(200,70,190), "Lock mode", "manual toggle");

                drawCaption("AIM POINT", 170);
                bool head_on = g_aim_head.load(std::memory_order_relaxed);
                drawToggle(BTN_HEAD, head_on, RGB(220,120,60), head_on ? "Head aim" : "Body aim", head_on ? "upper chest/head" : "center mass");
                bool rcs_on = g_rcs_on.load(std::memory_order_relaxed);
                drawToggle(BTN_RCS, rcs_on, RGB(180,100,60), "RCS", rcs_on ? "recoil compensation active" : "off");

                drawCaption("RECOIL CONTROL", 234);
                float rcs_str = g_rcs_strength.load(std::memory_order_relaxed);
                char rv[32]; sprintf_s(rv, "%.0f%%", rcs_str * 100);
                RECT rvl = {px + 24, py + 252, px + pw - 24, py + 268};
                GdiText(hdc, "STRENGTH", rvl, C_TEXT_DIM, fSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                GdiText(hdc, rv, rvl, C_TEXT_BRIGHT, fSmall, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                { int tx1 = px + SLD_X1, tx2 = px + SLD_X2;
                int cy4 = py + 272;
                RECT track4 = {tx1, cy4, tx2, cy4 + SLD_TH};
                HBRUSH tbg4 = CreateSolidBrush(RGB(25,25,40));
                FillRect(hdc, &track4, tbg4); DeleteObject(tbg4);
                float t4 = rcs_str;
                int fx4 = tx1 + int(t4 * (tx2 - tx1));
                HBRUSH fill4 = CreateSolidBrush(RGB(180,100,60));
                RECT fr4 = {tx1, cy4, fx4, cy4 + SLD_TH};
                FillRect(hdc, &fr4, fill4); DeleteObject(fill4);
                HPEN np4 = CreatePen(PS_NULL, 0, 0);
                HBRUSH knob4 = CreateSolidBrush(RGB(230,230,240));
                SelectObject(hdc, np4); SelectObject(hdc, knob4);
                int hy4 = cy4 + SLD_TH / 2;
                Ellipse(hdc, fx4 - 6, hy4 - 6, fx4 + 6, hy4 + 6);
                DeleteObject(knob4); DeleteObject(np4);
                SelectObject(hdc, (HBRUSH)GetStockObject(NULL_BRUSH));
                SelectObject(hdc, GetStockObject(WHITE_PEN)); }

                drawCaption("FOV RADIUS", 320);
                char fv[32]; sprintf_s(fv, "%.0f px", fov_r);
                RECT vr = {px + 380, py + 320, px + pw - 22, py + 336};
                GdiText(hdc, fv, vr, C_TEXT_BRIGHT, fSmall, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                { int tx1 = px + SLD_X1, tx2 = px + SLD_X2;
                RECT track = {tx1, py + SLD_FOV_Y, tx2, py + SLD_FOV_Y + SLD_TH};
                HBRUSH tbg = CreateSolidBrush(RGB(25,25,40));
                FillRect(hdc, &track, tbg); DeleteObject(tbg);
                float tv = fclamp01((fov_r - FOV_MIN_PX) / (FOV_MAX_PX - FOV_MIN_PX));
                int fx = tx1 + int(tv * (tx2 - tx1));
                HBRUSH fill = CreateSolidBrush(RGB(80,160,255));
                RECT fr = {tx1, py + SLD_FOV_Y, fx, py + SLD_FOV_Y + SLD_TH};
                FillRect(hdc, &fr, fill); DeleteObject(fill);
                HPEN np = CreatePen(PS_NULL, 0, 0);
                HBRUSH knob = CreateSolidBrush(RGB(230,230,240));
                SelectObject(hdc, np); SelectObject(hdc, knob);
                int hy = py + SLD_FOV_Y + SLD_TH / 2;
                Ellipse(hdc, fx - 6, hy - 6, fx + 6, hy + 6);
                DeleteObject(knob); DeleteObject(np);
                SelectObject(hdc, (HBRUSH)GetStockObject(NULL_BRUSH));
                SelectObject(hdc, GetStockObject(WHITE_PEN)); }
            } else if (g_hud_tab == 1) {
                char key_det[32], key_labels[32], key_fov[32];
                char sub_det[64], sub_labels[64], sub_fov[64];
                KeyName(g_hotkey_det, key_det, sizeof(key_det));
                KeyName(g_hotkey_labels, key_labels, sizeof(key_labels));
                KeyName(g_hotkey_fov, key_fov, sizeof(key_fov));
                sprintf_s(sub_det, "%s - boxes and scores", key_det);
                sprintf_s(sub_labels, "%s - class names", key_labels);
                sprintf_s(sub_fov, "%s - radius preview", key_fov);

                drawCaption("DETECTION", 108);
                drawToggle(BTN_DET, det_on, C_GREEN, "Detection", sub_det);
                drawToggle(BTN_LABELS, g_labels.load(std::memory_order_relaxed), RGB(80,160,255), "Labels", sub_labels);

                drawCaption("DISPLAY", 176);
                drawToggle(BTN_FOVVIS, fov_vis, RGB(80,160,255), "FOV circle", sub_fov);
                drawToggle(BTN_CROP, g_crop_viewer.load(std::memory_order_relaxed), RGB(80,160,255), "Crop viewer", "show 640x640 area");

                drawCaption("STYLE", 244);
                drawToggle(BTN_STYLE, true, RGB(80,160,255), "Box style", BoxStyleName());
                bool cross_on = g_crosshair_vis.load(std::memory_order_relaxed);
                drawToggle(BTN_CROSSHAIR, cross_on, RGB(80,160,255), "Crosshair", cross_on ? "visible" : "hidden");
            } else if (g_hud_tab == 2) {
                drawCaption("CLASS VISIBILITY", 108);
                for (int i = 0; i < 4; ++i) {
                    bool on = g_class_visible[i].load(std::memory_order_relaxed);
                    char sub[48]; sprintf_s(sub, on ? "visible on overlay" : "hidden on overlay");
                    drawToggle(FILTER_BTNS[i], on, g_class_colors[i], CLASS_NAMES[i], sub);
                    const HudBtn& cb = COLOR_BTNS[i];
                    RECT sw = {px + cb.rx1, py + cb.ry1, px + cb.rx2, py + cb.ry2};
                    GdiRoundRect(hdc, sw.left, sw.top, sw.right, sw.bottom, RGB(80,80,95), 1, g_class_colors[i], true);
                }

                drawCaption("CONFIDENCE", 284);
                float conf = g_conf_threshold.load(std::memory_order_relaxed);
                char cv[32]; sprintf_s(cv, "%.0f%%", conf * 100.f);
                RECT cvr = {px + 390, py + 284, px + pw - 22, py + 300};
                GdiText(hdc, cv, cvr, C_TEXT_BRIGHT, fSmall, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                { int tx1 = px + SLD_X1, tx2 = px + SLD_X2;
                int cy = py + 344;
                RECT track = {tx1, cy, tx2, cy + SLD_TH};
                HBRUSH tbg = CreateSolidBrush(RGB(25,25,40));
                FillRect(hdc, &track, tbg); DeleteObject(tbg);
                float ct = fclamp01((conf - 0.10f) / 0.85f);
                int fx = tx1 + int(ct * (tx2 - tx1));
                HBRUSH fill = CreateSolidBrush(RGB(80,160,255));
                RECT fr = {tx1, cy, fx, cy + SLD_TH};
                FillRect(hdc, &fr, fill); DeleteObject(fill);
                HPEN np = CreatePen(PS_NULL, 0, 0);
                HBRUSH knob = CreateSolidBrush(RGB(230,230,240));
                SelectObject(hdc, np); SelectObject(hdc, knob);
                int hy = cy + SLD_TH / 2;
                Ellipse(hdc, fx - 6, hy - 6, fx + 6, hy + 6);
                DeleteObject(knob); DeleteObject(np);
                SelectObject(hdc, (HBRUSH)GetStockObject(NULL_BRUSH));
                SelectObject(hdc, GetStockObject(WHITE_PEN)); }

                RECT note = {px + 24, py + 374, px + pw - 24, py + 410};
                GdiText(hdc, "Click a class to show/hide it. Click its color swatch to cycle colors.", note, C_TEXT_DIM, fSmall, DT_LEFT | DT_WORDBREAK);
            } else {
                drawCaption("PERFORMANCE", 108);
                char line1[96], line2[96], line3[96];
                sprintf_s(line1, "Capture: %s  %.1f ms", dxgi ? "DXGI" : "GDI", cap_ms);
                sprintf_s(line2, "Inference: %s  %.1f ms", cuda ? "CUDA" : "CPU", inf_ms);
                sprintf_s(line3, "Overlay: %.0f FPS    Visible targets: %zu", fps, ndets);
                RECT p1 = {px + 24, py + 136, px + pw - 24, py + 156};
                RECT p2 = {px + 24, py + 164, px + pw - 24, py + 184};
                RECT p3 = {px + 24, py + 192, px + pw - 24, py + 212};
                COLORREF pc = (dxgi && cuda) ? C_GREEN : C_ORANGE;
                GdiText(hdc, line1, p1, pc, fMono);
                GdiText(hdc, line2, p2, pc, fMono);
                GdiText(hdc, line3, p3, C_TEXT_DIM, fMono);

                drawCaption("SHORTCUTS", 250);
                struct HK { const char* label; DWORD vk; };
                HK hks[] = {
                    {"Detection", g_hotkey_det}, {"Labels", g_hotkey_labels},
                    {"FOV circle", g_hotkey_fov}, {"Hold action", g_hotkey_hold},
                    {"Menu", g_hotkey_menu},
                };
                for (int i = 0; i < 5; ++i) {
                    const HudBtn& hb = HOTKEY_BTNS[i];
                    RECT r = {px + hb.rx1, py + hb.ry1, px + hb.rx2, py + hb.ry2};
                    bool editing = g_waiting_hotkey == i;
                    GdiRoundRect(hdc, r.left, r.top, r.right, r.bottom,
                                 editing ? C_ORANGE : RGB(55,55,75), 1,
                                 editing ? RGB(45,35,20) : RGB(18,18,27), true);
                    char key[32]; KeyName(hks[i].vk, key, sizeof(key));
                    char txt[80]; sprintf_s(txt, "%s: %s", hks[i].label, editing ? "press key..." : key);
                    GdiText(hdc, txt, r, editing ? C_ORANGE : C_TEXT_BRIGHT, fSmall, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                RECT k2 = {px + 24, py + 380, px + pw - 24, py + 396};
                GdiText(hdc, "Click a hotkey field, then press the new key.", k2, C_TEXT_DIM, fSmall, DT_LEFT | DT_WORDBREAK);
            }
        }

    }

}

static void InferenceLoop()
{
    DXGICapture cap;
    bool use_dxgi = cap.Init(0);
    g_use_dxgi.store(use_dxgi, std::memory_order_relaxed);
    OutputDebugStringA(use_dxgi ? "[Overlay] DXGI OK\n" : "[Overlay] DXGI FAILED -- GDI\n");

    auto last_repaint = std::chrono::steady_clock::now();
    cv::Mat letterboxed;

    while (g_running.load(std::memory_order_relaxed))
    {
        if (use_dxgi && !cap.IsValid()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            use_dxgi = cap.Init(0);
            g_use_dxgi.store(use_dxgi, std::memory_order_relaxed);
            continue;
        }

        auto t0 = std::chrono::steady_clock::now();
        cv::Mat frame;
        const int cap_size = std::min(DETECTION_CAPTURE_SIZE, std::min(g_sw, g_sh));
        const int cap_x = (g_sw - cap_size) / 2;
        const int cap_y = (g_sh - cap_size) / 2;
        if (use_dxgi) {
            frame = cap.CaptureBgraRegion(cap_x, cap_y, cap_size, cap_size);
            if (frame.empty()) {
                std::this_thread::sleep_for(std::chrono::microseconds(300));
                continue;
            }
        } else {
            frame = captureScreen(cap_x, cap_y, cap_size, cap_size);
        }
        auto t1 = std::chrono::steady_clock::now();
        g_cap_ms.store(std::chrono::duration<float,std::milli>(t1-t0).count(),
                       std::memory_order_relaxed);

        std::vector<Detection> dets;
        auto now = std::chrono::steady_clock::now();
        if (g_det_on.load(std::memory_order_relaxed) && g_detector && g_detector->isLoaded())
        {
            float sc; int lpx, lpy;
            Letterbox(frame, letterboxed, YoloDetector::INPUT_SIZE, sc, lpx, lpy);
            auto ti0 = std::chrono::steady_clock::now();
            try { dets = g_detector->detect(letterboxed); } catch (...) {}
            auto ti1 = std::chrono::steady_clock::now();
            float infer_ms = std::chrono::duration<float,std::milli>(ti1-ti0).count();
            g_inf_ms.store(infer_ms, std::memory_order_relaxed);
            g_fps.store(infer_ms > 0.f ? 1000.f / infer_ms : 0.f,
                        std::memory_order_relaxed);
            float min_conf = g_conf_threshold.load(std::memory_order_relaxed);
            dets.erase(std::remove_if(dets.begin(), dets.end(),
                [min_conf](const Detection& d) { return d.score < min_conf; }), dets.end());
            for (auto& d : dets) {
                d.x1 = iclamp(cap_x + int((d.x1-lpx)/sc), 0, g_sw-1);
                d.y1 = iclamp(cap_y + int((d.y1-lpy)/sc), 0, g_sh-1);
                d.x2 = iclamp(cap_x + int((d.x2-lpx)/sc), 0, g_sw-1);
                d.y2 = iclamp(cap_y + int((d.y2-lpy)/sc), 0, g_sh-1);
            }
            { std::lock_guard<std::mutex> lk(g_mutex); g_dets = std::move(dets); }
        } else {
            std::lock_guard<std::mutex> lk(g_mutex);
            g_dets.clear();
        }

        static int rcs_bullet = 0;
        static auto rcs_last = std::chrono::steady_clock::now();
        if (g_rcs_on.load(std::memory_order_relaxed)) {
            if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
                auto rcs_now = std::chrono::steady_clock::now();
                float rcs_dt = std::chrono::duration<float>(rcs_now - rcs_last).count();
                if (rcs_dt >= 0.035f) {
                    rcs_bullet = std::min(rcs_bullet + 1, 30);
                    float strength = g_rcs_strength.load(std::memory_order_relaxed);
                    float factor = std::min(rcs_bullet / 7.0f, 1.0f);
                    float pull = factor * strength * 3.5f + 0.3f;
                    int dy = int(pull);
                    if (dy > 0) {
                        SendMouseMove(0, dy);
                    }
                    rcs_last = rcs_now;
                }
            } else {
                rcs_bullet = 0;
            }
        }

        float since = std::chrono::duration<float>(now-last_repaint).count();
        if (since >= OVERLAY_REPAINT_DT && g_hwnd) {
            RequestRepaint();
            last_repaint = now;
        }
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (g_memDC) {
            std::vector<Detection> snap;
            { std::lock_guard<std::mutex> lk(g_mutex); snap = g_dets; }
            drawOverlay(g_memDC, snap);
            BitBlt(hdc, 0, 0, g_sw, g_sh, g_memDC, 0, 0, SRCCOPY);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_TIMER: {
        if (wp != TIMER_ID) break;
        std::vector<Detection> snap;
        { std::lock_guard<std::mutex> lk(g_mutex); snap = g_dets; }
        if (!g_hud_visible.load(std::memory_order_relaxed)) {
            doAimbot(snap);
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_MOUSEMOVE:
    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(lp);
        int y = GET_Y_LPARAM(lp);
        if (HandleHudMouse(msg, x, y)) return 0;
        break;
    }
    case WM_CAPTURECHANGED:
        g_slider_drag = 0;
        g_hud_dragging = false;
        return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wp, LPARAM lp)
{
    if (nCode == HC_ACTION) {
        const auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);
        bool down = (wp==WM_KEYDOWN||wp==WM_SYSKEYDOWN);
        bool up   = (wp==WM_KEYUP  ||wp==WM_SYSKEYUP);
        if (down) {
            if (g_waiting_hotkey >= 0) {
                switch (g_waiting_hotkey) {
                case 0: g_hotkey_det = kb->vkCode; break;
                case 1: g_hotkey_labels = kb->vkCode; break;
                case 2: g_hotkey_fov = kb->vkCode; break;
                case 3: g_hotkey_hold = kb->vkCode; break;
                case 4: g_hotkey_menu = kb->vkCode; break;
                }
                g_waiting_hotkey = -1;
                RequestRepaint();
                return 1;
            }

            if (kb->vkCode == g_hotkey_det) {
                g_det_on.store(!g_det_on.load());
            } else if (kb->vkCode == g_hotkey_labels) {
                g_labels.store(!g_labels.load());
            } else if (kb->vkCode == g_hotkey_fov) {
                if (!g_hud_visible.load(std::memory_order_relaxed))
                    g_fov_vis.store(!g_fov_vis.load());
            } else if (kb->vkCode == g_hotkey_hold) {
                if (!g_hud_visible.load(std::memory_order_relaxed))
                    g_aim_hold.store(true);
                else Debugf("[Overlay] K pressed but HUD visible - ignored\n");
            } else if (kb->vkCode == g_hotkey_menu) {
                g_hud_visible.store(!g_hud_visible.load());
                Debugf("[Overlay] HUD visible=%d\n", (int)g_hud_visible.load());
                if (g_hwnd) {
                    if (g_hud_visible.load()) SetOverlayClickThrough(false);
                    else                       SetOverlayClickThrough(true);
                    RequestRepaint();
                    ReleaseCapture();
                }
            } else if (kb->vkCode == VK_END) {
                g_running.store(false);
                PostMessage(g_hwnd, WM_DESTROY, 0, 0);
            }
        }
        if (up && kb->vkCode == g_hotkey_hold) {
            if (!g_hud_visible.load(std::memory_order_relaxed)) g_aim_hold.store(false);
            else Debugf("[Overlay] K released but HUD visible - ignored\n");
        }
    }
    return CallNextHookEx(g_kbhook, nCode, wp, lp);
}

static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wp, LPARAM lp)
{
    if (nCode != HC_ACTION)
        return CallNextHookEx(g_mousehook, nCode, wp, lp);

    const auto* ms = reinterpret_cast<MSLLHOOKSTRUCT*>(lp);
    POINT pt = ms->pt;

    if (g_slider_drag != 0) {
        if (wp == WM_MOUSEMOVE) {
            float t = fclamp01(float(pt.x - g_hud_x - SLD_X1) / float(SLD_X2-SLD_X1));
            if (g_slider_drag == 1) g_fov_radius.store(FOV_MIN_PX + t * (FOV_MAX_PX - FOV_MIN_PX));
            if (g_slider_drag == 2) g_conf_threshold.store(0.10f + t * 0.85f);
            if (g_slider_drag == 3) g_rcs_strength.store(t);
            RequestRepaint();
            return 1;
        }
        if (wp == WM_LBUTTONUP) { g_slider_drag = 0; ReleaseCapture(); Debugf("[Overlay] Slider drag ended\n"); return 1; }
    }

    if (g_hud_dragging) {
        if (wp == WM_MOUSEMOVE) {
            int new_x = iclamp(g_drag_hud_x + (pt.x - g_drag_start_x), 0, g_sw - HUD_W);
            int new_y = iclamp(g_drag_hud_y + (pt.y - g_drag_start_y), 0, g_sh - HUD_H);
            if (new_x != g_hud_x || new_y != g_hud_y) {
                Debugf("[Overlay] Drag: cursor=%d,%d -> HUD moved to %d,%d\n", pt.x, pt.y, new_x, new_y);
            }
            g_hud_x = new_x;
            g_hud_y = new_y;
            RequestRepaint();
            return 1;
        }
        if (wp == WM_LBUTTONUP) { g_hud_dragging = false; ReleaseCapture(); Debugf("[Overlay] Drag ended\n"); return 1; }
    }

    if (!g_hud_visible.load(std::memory_order_relaxed))
        return CallNextHookEx(g_mousehook, nCode, wp, lp);

    RECT hud_rect = {g_hud_x, g_hud_y, g_hud_x+HUD_W, g_hud_y+HUD_H};
    if (!PtInRect(&hud_rect, pt)) {
        return CallNextHookEx(g_mousehook, nCode, wp, lp);
    }

    if (wp == WM_LBUTTONDOWN) {
        int rx = pt.x - g_hud_x;
        int ry = pt.y - g_hud_y;

        if (ry < 36) {
            if (rx>=BTN_CLOSE.rx1 && rx<=BTN_CLOSE.rx2 &&
                ry>=BTN_CLOSE.ry1 && ry<=BTN_CLOSE.ry2) {
                g_hud_visible.store(false);
                if (g_hwnd) { SetOverlayClickThrough(true); RequestRepaint(); Debugf("[Overlay] HUD closed via X\n"); }
            } else {
                g_hud_dragging=true; g_drag_ox=rx; g_drag_oy=ry;
                g_drag_start_x=pt.x; g_drag_start_y=pt.y;
                g_drag_hud_x=g_hud_x; g_drag_hud_y=g_hud_y;
                SetCapture(g_hwnd);
                Debugf("[Overlay] Drag started: offset=(%d,%d) HUD_pos=(%d,%d)\n", rx, ry, g_hud_x, g_hud_y);
            }
            return 1;
        }

        auto inBtn=[&](const HudBtn& b){
            return rx>=b.rx1&&rx<=b.rx2&&ry>=b.ry1&&ry<=b.ry2;
        };
        if (g_hud_tab == 1 && inBtn(BTN_DET))  { g_det_on.store(!g_det_on.load());   RequestRepaint(); return 1; }
        if (g_hud_tab == 1 && inBtn(BTN_LABELS)) { g_labels.store(!g_labels.load()); RequestRepaint(); return 1; }
        if (g_hud_tab == 0 && inBtn(BTN_AIM))  { bool on = !g_aim_fov.load(); g_aim_fov.store(on); if (on) g_aim_lock.store(false); RequestRepaint(); return 1; }
        if (g_hud_tab == 0 && inBtn(BTN_LOCK)) { bool on = !g_aim_lock.load(); g_aim_lock.store(on); if (on) g_aim_fov.store(false); RequestRepaint(); return 1; }
        if (g_hud_tab == 1 && inBtn(BTN_FOVVIS)) { g_fov_vis.store(!g_fov_vis.load()); RequestRepaint(); return 1; }
        if (g_hud_tab == 1 && inBtn(BTN_CROP)) { g_crop_viewer.store(!g_crop_viewer.load()); RequestRepaint(); return 1; }
        if (g_hud_tab == 1 && inBtn(BTN_STYLE)) { g_box_style = (g_box_style + 1) % 3; RequestRepaint(); return 1; }
        if (g_hud_tab == 1 && inBtn(BTN_CROSSHAIR)) { g_crosshair_vis.store(!g_crosshair_vis.load()); RequestRepaint(); return 1; }
        if (g_hud_tab == 0 && inBtn(BTN_HEAD)) { g_aim_head.store(!g_aim_head.load()); RequestRepaint(); return 1; }
        if (g_hud_tab == 0 && inBtn(BTN_RCS)) { g_rcs_on.store(!g_rcs_on.load()); RequestRepaint(); return 1; }

        if (ry>=256 && ry<=280 && rx>=SLD_X1 && rx<=SLD_X2) {
            g_slider_drag=3;
            float t=fclamp01(float(rx-SLD_X1)/float(SLD_X2-SLD_X1));
            g_rcs_strength.store(t);
            SetCapture(g_hwnd);
            RequestRepaint();
            return 1;
        }
        if (ry>=326 && ry<=362 && rx>=SLD_X1 && rx<=SLD_X2) {
            g_slider_drag=1;
            float t=fclamp01(float(rx-SLD_X1)/float(SLD_X2-SLD_X1));
            g_fov_radius.store(FOV_MIN_PX + t * (FOV_MAX_PX - FOV_MIN_PX));
            SetCapture(g_hwnd);
            RequestRepaint();
            return 1;
        }

        return 1;
    }

    if (wp == WM_LBUTTONUP) {
        g_slider_drag  = 0;
        g_hud_dragging = false;
        ReleaseCapture();
        return 1;
    }

    return CallNextHookEx(g_mousehook, nCode, wp, lp);
}
struct ComPortEntry {
    std::string name;
    std::string friendlyName;
    bool        isUsb; 
    int         priority;
};

struct ModeDlgState {
    const std::vector<ComPortEntry>* ports;
    int  result     = 100;
    bool hovNormal  = false;
    bool hovArduino = false;
    RECT btnNormal  = {};
    RECT btnArduino = {};
};

static LRESULT CALLBACK ModeDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* s = (ModeDlgState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {

    case WM_CREATE: {
        auto* cs = (CREATESTRUCTW*)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }

    case WM_ERASEBKGND:
        return TRUE;

    case WM_SIZE: {
        if (!s) break;
        int W = LOWORD(lp);
        int H = HIWORD(lp);
        int bw = (W - 60) / 2;
        int by = H - 90;
        s->btnNormal  = {20,      by, 20 + bw,      by + 68};
        s->btnArduino = {40 + bw, by, 40 + bw + bw, by + 68};
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    }

    case WM_PAINT: {
        if (!s) break;
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);

        HBRUSH bgBr = CreateSolidBrush(RGB(10,10,16));
        FillRect(hdc, &rc, bgBr); DeleteObject(bgBr);

        HBRUSH hdrBr = CreateSolidBrush(RGB(17,18,28));
        RECT hdr = {0, 0, rc.right, 44};
        FillRect(hdc, &hdr, hdrBr); DeleteObject(hdrBr);

        HPEN sp = CreatePen(PS_SOLID, 1, RGB(45,45,65));
        SelectObject(hdc, sp);
        MoveToEx(hdc, 0, 44, nullptr); LineTo(hdc, rc.right, 44);
        SelectObject(hdc, GetStockObject(WHITE_PEN)); DeleteObject(sp);

        SetBkMode(hdc, TRANSPARENT);

        static HFONT fTitle = CreateFontA(16,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FF_DONTCARE,"Segoe UI");
        static HFONT fBold  = CreateFontA(14,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FF_DONTCARE,"Segoe UI");
        static HFONT fNorm  = CreateFontA(13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FF_DONTCARE,"Segoe UI");
        static HFONT fSmall = CreateFontA(12,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FF_DONTCARE,"Segoe UI");

        {
            RECT tr = {20, 8, rc.right-20, 38};
            SelectObject(hdc, fTitle);
            SetTextColor(hdc, RGB(220,220,235));
            DrawTextA(hdc, "YOLOv8 CS2 Overlay", -1, &tr, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        }

        {
            RECT ir = {20, 54, rc.right-20, 76};
            SelectObject(hdc, fBold);
            SetTextColor(hdc, RGB(220,220,235));
            DrawTextA(hdc, "Select mouse output mode", -1, &ir, DT_LEFT|DT_SINGLELINE);
        }

        {
            RECT dr = {20, 82, rc.right-20, 160};
            SelectObject(hdc, fNorm);
            SetTextColor(hdc, RGB(95,95,120));
            DrawTextA(hdc,
                "Normal Mode: standard Windows SendInput API.\r\n"
                "Arduino Mode: sends serial commands to an ATmega32U4 acting as a USB HID mouse.\r\n"
                "The Arduino bypasses software-level input detection.",
                -1, &dr, DT_LEFT|DT_WORDBREAK);
        }

        {
            char portline[512];
            if (s->ports && !s->ports->empty()) {
                int off = sprintf_s(portline, "Detected ports:  ");
                for (auto& p : *s->ports) {
                    const char* tag = (p.priority == 0) ? "[Arduino] "
                                    : (p.isUsb)         ? "[USB] "
                                    :                     "";
                    off += sprintf_s(portline+off, sizeof(portline)-off,
                                     "%s%s  ", tag, p.name.c_str());
                }
            } else {
                sprintf_s(portline, "No COM ports detected - plug in the Arduino first");
            }
            RECT pr = {20, 160, rc.right-20, 178};
            SelectObject(hdc, fSmall);
            SetTextColor(hdc, s->ports && !s->ports->empty() ? RGB(80,160,255) : RGB(210,50,50));
            DrawTextA(hdc, portline, -1, &pr, DT_LEFT|DT_SINGLELINE);
        }

        auto drawBtn = [&](const RECT& br, const char* title, const char* sub,
                           bool hover, COLORREF accent)
        {
            HBRUSH bb = CreateSolidBrush(hover ? RGB(30,38,48) : RGB(18,18,27));
            FillRect(hdc, &br, bb); DeleteObject(bb);
            HPEN bp = CreatePen(PS_SOLID, 1, hover ? accent : RGB(52,52,70));
            HGDIOBJ op = SelectObject(hdc, bp);
            HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            RoundRect(hdc, br.left, br.top, br.right, br.bottom, 6, 6);
            SelectObject(hdc, op); SelectObject(hdc, ob); DeleteObject(bp);
            HPEN np = CreatePen(PS_NULL, 0, 0);
            HBRUSH lb = CreateSolidBrush(hover ? accent : RGB(42,42,55));
            SelectObject(hdc, np); SelectObject(hdc, lb);
            int my = (br.top + br.bottom) / 2;
            Ellipse(hdc, br.left+14, my-7, br.left+28, my+7);
            DeleteObject(lb); DeleteObject(np);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            SelectObject(hdc, GetStockObject(WHITE_PEN));
            RECT tt = {br.left+38, br.top+10, br.right-8, br.top+30};
            SelectObject(hdc, fBold);
            SetTextColor(hdc, RGB(220,220,235));
            DrawTextA(hdc, title, -1, &tt, DT_LEFT|DT_SINGLELINE);
            RECT st = {br.left+38, br.top+32, br.right-8, br.top+52};
            SelectObject(hdc, fSmall);
            SetTextColor(hdc, RGB(95,95,120));
            DrawTextA(hdc, sub, -1, &st, DT_LEFT|DT_SINGLELINE);
        };

        drawBtn(s->btnNormal,  "Normal Mode",  "SendInput - Windows API", s->hovNormal,  RGB(80,160,255));
        drawBtn(s->btnArduino, "Arduino Mode", "ATmega32U4 USB HID",      s->hovArduino, RGB(0,210,100));

        {
            RECT fr = {0, rc.bottom-22, rc.right-12, rc.bottom-4};
            SelectObject(hdc, fSmall);
            SetTextColor(hdc, RGB(55,55,75));
            DrawTextA(hdc, "ESC or close = Normal Mode", -1, &fr, DT_RIGHT|DT_SINGLELINE);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!s) break;
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        bool hn = !!PtInRect(&s->btnNormal,  pt);
        bool ha = !!PtInRect(&s->btnArduino, pt);
        if (hn != s->hovNormal || ha != s->hovArduino) {
            s->hovNormal = hn; s->hovArduino = ha;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        break;
    }
    case WM_MOUSELEAVE: {
        if (s) { s->hovNormal = s->hovArduino = false; InvalidateRect(hwnd, nullptr, FALSE); }
        break;
    }

    case WM_LBUTTONDOWN: {
        if (!s) break;
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (PtInRect(&s->btnNormal,  pt)) { s->result = 100; PostMessageW(hwnd, WM_CLOSE, 0, 0); }
        if (PtInRect(&s->btnArduino, pt)) { s->result = 101; PostMessageW(hwnd, WM_CLOSE, 0, 0); }
        break;
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE || wp == VK_RETURN)
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

struct PortDlgState {
    const std::vector<ComPortEntry>* ports;
    std::string result;
    int  hovered = -2;
    std::vector<RECT> portBtns;
    RECT btnAuto   = {};
    RECT btnCancel = {};
};

static void PortDlgLayout(HWND hwnd, PortDlgState* s)
{
    RECT cr; GetClientRect(hwnd, &cr);
    int W = cr.right, H = cr.bottom;
    int portCount = s ? (int)std::min(s->ports->size(), (size_t)8) : 0;
    if (s) s->portBtns.resize(portCount);
    int py = 152;
    for (int i = 0; i < portCount; i++) {
        if (s) s->portBtns[i] = {20, py, W - 20, py + 44};
        py += 52;
    }
    int bby = H - 82;
    int bw  = (W - 56) / 2;
    if (s) {
        s->btnAuto   = {20,       bby, 20 + bw,       bby + 46};
        s->btnCancel = {36 + bw,  bby, 36 + bw + bw,  bby + 46};
    }
}

static LRESULT CALLBACK PortDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* s = (PortDlgState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        auto* cs = (CREATESTRUCTW*)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }
    case WM_ERASEBKGND: return TRUE;
    case WM_SIZE:
        PortDlgLayout(hwnd, s);
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    case WM_PAINT: {
        if (!s) break;
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT cr; GetClientRect(hwnd, &cr);
        HBRUSH bgBr = CreateSolidBrush(RGB(10,10,16));
        FillRect(hdc, &cr, bgBr); DeleteObject(bgBr);
        HBRUSH hdrBr = CreateSolidBrush(RGB(17,18,28));
        RECT hdr = {0,0,cr.right,44}; FillRect(hdc,&hdr,hdrBr); DeleteObject(hdrBr);
        HPEN sp = CreatePen(PS_SOLID,1,RGB(45,45,65)); SelectObject(hdc,sp);
        MoveToEx(hdc,0,44,nullptr); LineTo(hdc,cr.right,44);
        SelectObject(hdc,GetStockObject(WHITE_PEN)); DeleteObject(sp);
        SetBkMode(hdc, TRANSPARENT);
        static HFONT fT = CreateFontA(16,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FF_DONTCARE,"Segoe UI");
        static HFONT fB = CreateFontA(14,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FF_DONTCARE,"Segoe UI");
        static HFONT fN = CreateFontA(13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FF_DONTCARE,"Segoe UI");
        static HFONT fS = CreateFontA(12,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FF_DONTCARE,"Segoe UI");
        { RECT tr={20,8,cr.right-20,38}; SelectObject(hdc,fT); SetTextColor(hdc,RGB(220,220,235));
          DrawTextA(hdc,"Arduino Port Selection",-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE); }
        { RECT ir={20,54,cr.right-20,76}; SelectObject(hdc,fB); SetTextColor(hdc,RGB(220,220,235));
          DrawTextA(hdc,"Select the COM port of your Arduino Leonardo / Micro",-1,&ir,DT_LEFT|DT_SINGLELINE); }
        { RECT dr={20,82,cr.right-20,136}; SelectObject(hdc,fN); SetTextColor(hdc,RGB(95,95,120));
          DrawTextA(hdc,
            "Open Device Manager (Win+X -> Ports (COM & LPT)) to find which port\r\n"
            "is labelled \"Arduino Leonardo\" or \"Arduino Micro\".\r\n"
            "If unsure, use \"Try All Automatically\".",
            -1,&dr,DT_LEFT|DT_WORDBREAK); }
        { RECT pl={20,138,cr.right-20,152}; SelectObject(hdc,fS); SetTextColor(hdc,RGB(60,80,110));
          DrawTextA(hdc, s->ports->empty() ? "NO COM PORTS DETECTED - plug in the Arduino first"
                                           : "DETECTED COM PORTS", -1,&pl,DT_LEFT|DT_SINGLELINE); }
        auto drawBtn = [&](const RECT& br, const char* label, const char* sub,
                           bool hov, COLORREF accent) {
            HBRUSH bb = CreateSolidBrush(hov ? RGB(30,38,48) : RGB(18,18,27));
            FillRect(hdc,&br,bb); DeleteObject(bb);
            HPEN bp = CreatePen(PS_SOLID,1,hov ? accent : RGB(52,52,70));
            SelectObject(hdc,bp); SelectObject(hdc,GetStockObject(NULL_BRUSH));
            RoundRect(hdc,br.left,br.top,br.right,br.bottom,6,6);
            SelectObject(hdc,GetStockObject(WHITE_PEN)); DeleteObject(bp);
            HPEN np = CreatePen(PS_NULL,0,0);
            HBRUSH lb = CreateSolidBrush(hov ? accent : RGB(42,42,55));
            SelectObject(hdc,np); SelectObject(hdc,lb);
            int my=(br.top+br.bottom)/2;
            Ellipse(hdc,br.left+14,my-7,br.left+28,my+7);
            DeleteObject(lb); DeleteObject(np);
            SelectObject(hdc,GetStockObject(NULL_BRUSH)); SelectObject(hdc,GetStockObject(WHITE_PEN));
            RECT tt={br.left+38,br.top+4,br.right-8,br.top+24};
            SelectObject(hdc,fB); SetTextColor(hdc,RGB(220,220,235));
            DrawTextA(hdc,label,-1,&tt,DT_LEFT|DT_SINGLELINE);
            if (sub && sub[0]) {
                RECT st={br.left+38,br.top+24,br.right-8,br.bottom-4};
                SelectObject(hdc,fS); SetTextColor(hdc,RGB(95,95,120));
                DrawTextA(hdc,sub,-1,&st,DT_LEFT|DT_SINGLELINE);
            }
        };
        for (int i = 0; i < (int)s->portBtns.size(); i++) {
            const auto& e = (*s->ports)[i];
            const char* sub = e.friendlyName.empty()
                ? (e.isUsb ? "USB serial port" : "Serial port")
                : e.friendlyName.c_str();
            COLORREF accent = (e.priority == 0) ? RGB(0,210,100)
                            : (e.priority == 1) ? RGB(80,200,80)
                            : (e.priority == 2) ? RGB(80,160,255)
                            :                     RGB(110,110,130);
            drawBtn(s->portBtns[i], e.name.c_str(), sub, s->hovered==i, accent);
        }
        drawBtn(s->btnAuto,   "Try All Automatically", "Arduino ports tried first", s->hovered==-1, RGB(160,120,60));
        drawBtn(s->btnCancel, "Cancel (Normal Mode)",  nullptr, s->hovered==-3, RGB(210,50,50));
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!s) break;
        POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        int nh = -2;
        if (PtInRect(&s->btnAuto,   pt)) nh = -1;
        else if (PtInRect(&s->btnCancel, pt)) nh = -3;
        else for (int i=0;i<(int)s->portBtns.size();i++) if (PtInRect(&s->portBtns[i],pt)){nh=i;break;}
        if (nh != s->hovered) { s->hovered = nh; InvalidateRect(hwnd,nullptr,FALSE); }
        TRACKMOUSEEVENT tme={sizeof(tme),TME_LEAVE,hwnd,0}; TrackMouseEvent(&tme);
        break;
    }
    case WM_MOUSELEAVE:
        if (s) { s->hovered=-2; InvalidateRect(hwnd,nullptr,FALSE); }
        break;
    case WM_LBUTTONDOWN: {
        if (!s) break;
        POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        if (PtInRect(&s->btnCancel, pt)) { s->result="cancel"; PostMessageW(hwnd,WM_CLOSE,0,0); }
        else if (PtInRect(&s->btnAuto, pt)) { s->result=""; PostMessageW(hwnd,WM_CLOSE,0,0); }
        else for (int i=0;i<(int)s->portBtns.size();i++) {
            if (PtInRect(&s->portBtns[i],pt)) { s->result=(*s->ports)[i].name; PostMessageW(hwnd,WM_CLOSE,0,0); break; }
        }
        break;
    }
    case WM_KEYDOWN:
        if (wp==VK_ESCAPE) { if(s) s->result="cancel"; PostMessageW(hwnd,WM_CLOSE,0,0); }
        break;
    case WM_CLOSE:   DestroyWindow(hwnd); return 0;
    case WM_DESTROY: PostQuitMessage(0);  return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static std::string ShowPortDialog(const std::vector<ComPortEntry>& ports)
{
    const wchar_t* CLS = L"YOLOv8PortSelectorDlg";
    WNDCLASSEXW wc2={};
    wc2.cbSize=sizeof(wc2); wc2.lpfnWndProc=PortDlgProc;
    wc2.hInstance=GetModuleHandleW(nullptr); wc2.lpszClassName=CLS;
    wc2.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    RegisterClassExW(&wc2);

    int portCount = (int)std::min(ports.size(),(size_t)8);
    RECT cr = {0, 0, 480, 200 + portCount*52 + 100};
    if (cr.bottom < 340) cr.bottom = 340;
    DWORD ws = WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU;
    AdjustWindowRect(&cr, ws, FALSE);
    int W=cr.right-cr.left, H=cr.bottom-cr.top;

    PortDlgState state;
    state.ports  = &ports;
    state.result = "cancel";
    state.portBtns.resize(portCount);

    HWND dlg = CreateWindowExW(
        WS_EX_TOPMOST|WS_EX_DLGMODALFRAME, CLS, L"Arduino Port Selection",
        ws, (GetSystemMetrics(SM_CXSCREEN)-W)/2, (GetSystemMetrics(SM_CYSCREEN)-H)/2,
        W, H, nullptr, nullptr, GetModuleHandleW(nullptr), &state);

    if (!dlg) { UnregisterClassW(CLS,GetModuleHandleW(nullptr)); return "cancel"; }
    ShowWindow(dlg, SW_SHOW); UpdateWindow(dlg);

    MSG m={};
    while (GetMessageW(&m,nullptr,0,0)>0) { TranslateMessage(&m); DispatchMessageW(&m); }
    UnregisterClassW(CLS, GetModuleHandleW(nullptr));
    return state.result;
}


static int ShowModeDialog(const std::vector<ComPortEntry>& ports)
{
    const wchar_t* CLS = L"YOLOv8ModeSelectorDlg";
    WNDCLASSEXW wc2 = {};
    wc2.cbSize        = sizeof(wc2);
    wc2.lpfnWndProc   = ModeDlgProc;
    wc2.hInstance     = GetModuleHandleW(nullptr);
    wc2.lpszClassName = CLS;
    wc2.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc2);

    RECT cr = {0, 0, 640, 330};
    DWORD ws = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRect(&cr, ws, FALSE);
    int dlgW = cr.right - cr.left;
    int dlgH = cr.bottom - cr.top;
    int dlgX = (GetSystemMetrics(SM_CXSCREEN) - dlgW) / 2;
    int dlgY = (GetSystemMetrics(SM_CYSCREEN) - dlgH) / 2;

    ModeDlgState state;
    state.ports = &ports;

    HWND dlg = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_DLGMODALFRAME,
        CLS, L"YOLOv8 CS2 Overlay",
        ws, dlgX, dlgY, dlgW, dlgH,
        nullptr, nullptr, GetModuleHandleW(nullptr), &state);

    if (!dlg) { UnregisterClassW(CLS, GetModuleHandleW(nullptr)); return 100; }

    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);

    MSG m = {};
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    UnregisterClassW(CLS, GetModuleHandleW(nullptr));
    return state.result;
}


static HANDLE OpenArduinoSerial(const char* port)
{
    HANDLE h = CreateFileA(port, GENERIC_READ | GENERIC_WRITE,
                           0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    EscapeCommFunction(h, CLRDTR);
    EscapeCommFunction(h, CLRRTS);
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT);

    DCB dcb = {};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(h, &dcb)) { CloseHandle(h); return INVALID_HANDLE_VALUE; }
    dcb.BaudRate    = CBR_115200;
    dcb.ByteSize    = 8;
    dcb.StopBits    = ONESTOPBIT;
    dcb.Parity      = NOPARITY;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    if (!SetCommState(h, &dcb)) { CloseHandle(h); return INVALID_HANDLE_VALUE; }

    COMMTIMEOUTS to = {};
    to.WriteTotalTimeoutConstant   = 5;
    to.WriteTotalTimeoutMultiplier = 2;
    if (!SetCommTimeouts(h, &to)) { CloseHandle(h); return INVALID_HANDLE_VALUE; }

    return h;
}

static std::vector<ComPortEntry> EnumComPorts()
{
    std::vector<ComPortEntry> result;
    HDEVINFO devInfo = SetupDiGetClassDevsA(
        &GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (devInfo == INVALID_HANDLE_VALUE) return result;

    SP_DEVINFO_DATA dd = {};
    dd.cbSize = sizeof(dd);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &dd); ++i) {
        HKEY hk = SetupDiOpenDevRegKey(devInfo, &dd,
                                        DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (hk == INVALID_HANDLE_VALUE) continue;
        char portName[32] = {};
        DWORD sz = sizeof(portName), regType;
        RegQueryValueExA(hk, "PortName", nullptr, &regType,
                         (LPBYTE)portName, &sz);
        RegCloseKey(hk);
        if (strncmp(portName, "COM", 3) != 0) continue;

        char friendly[256] = {};
        SetupDiGetDeviceRegistryPropertyA(devInfo, &dd, SPDRP_FRIENDLYNAME,
            nullptr, (PBYTE)friendly, sizeof(friendly), nullptr);

        char enumerator[64] = {};
        SetupDiGetDeviceRegistryPropertyA(devInfo, &dd, SPDRP_ENUMERATOR_NAME,
            nullptr, (PBYTE)enumerator, sizeof(enumerator), nullptr);

        bool isUsb = (_stricmp(enumerator, "USB") == 0);

        std::string fn = friendly;
        bool isArduino =
            fn.find("Arduino")  != std::string::npos ||
            fn.find("Leonardo") != std::string::npos ||
            fn.find("Pro Micro") != std::string::npos;
        bool isUsbChip = isUsb && (
            fn.find("CH340")        != std::string::npos ||
            fn.find("CH341")        != std::string::npos ||
            fn.find("CP210")        != std::string::npos ||
            fn.find("FTDI")         != std::string::npos ||
            fn.find("FT232")        != std::string::npos ||
            fn.find("USB Serial")   != std::string::npos ||
            fn.find("USB-SERIAL")   != std::string::npos);

        int priority = isArduino ? 0
                     : isUsbChip ? 1
                     : isUsb     ? 2
                     :             3;

        result.push_back({ std::string(portName), std::string(friendly),
                           isUsb, priority });
    }
    SetupDiDestroyDeviceInfoList(devInfo);

    std::sort(result.begin(), result.end(),
        [](const ComPortEntry& a, const ComPortEntry& b){
            return a.priority < b.priority;
        });
    return result;
}


int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    g_sw = GetSystemMetrics(SM_CXSCREEN);
    g_sh = GetSystemMetrics(SM_CYSCREEN);

    WNDCLASSEXW wc{};
    wc.cbSize=sizeof(wc); wc.lpfnWndProc=WndProc;
    wc.hInstance=hInst;   wc.lpszClassName=L"YOLOv8OverlayV4";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(
        WS_EX_LAYERED|WS_EX_TOPMOST|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,
        L"YOLOv8OverlayV4", L"YOLOv8 CS2 Overlay v4",
        WS_POPUP, 0, 0, g_sw, g_sh, nullptr, nullptr, hInst, nullptr);

    if (!g_hwnd) {
        MessageBoxA(nullptr,"CreateWindowExW failed.","Overlay",MB_ICONERROR);
        return 1;
    }

    SetLayeredWindowAttributes(g_hwnd, COLOR_KEY, 0, LWA_COLORKEY);
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, g_sw, g_sh, SWP_NOACTIVATE);

    {
        HDC hdc = GetDC(g_hwnd);
        g_memDC  = CreateCompatibleDC(hdc);
        g_memBmp = CreateCompatibleBitmap(hdc, g_sw, g_sh);
        SelectObject(g_memDC, g_memBmp);
        ReleaseDC(g_hwnd, hdc);
    }

    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(g_hwnd);
    SetWindowDisplayAffinity(g_hwnd, WDA_EXCLUDEFROMCAPTURE);

    SetOverlayClickThrough(!g_hud_visible.load(std::memory_order_relaxed));

    {
        auto comPorts = EnumComPorts();
        int nButton = ShowModeDialog(comPorts);

        if (nButton == 101) {
            g_arduino_mode = true;

            std::string picked = ShowPortDialog(comPorts);

            if (picked == "cancel") {
                g_arduino_mode = false;
            } else {
                std::vector<std::string> toTry;
                if (!picked.empty()) {
                    toTry.push_back(picked);
                } else {
                    for (auto& e : comPorts) toTry.push_back(e.name);
                    for (int i = 1; i <= 30; ++i) {
                        char buf[8]; sprintf_s(buf, "COM%d", i);
                        bool dup = false;
                        for (auto& e : comPorts) if (e.name == buf) { dup = true; break; }
                        if (!dup) toTry.push_back(buf);
                    }
                }

                std::string foundPort;
                for (auto& portName : toTry) {
                    std::string full = "\\\\.\\" + portName;
                    HANDLE h = OpenArduinoSerial(full.c_str());
                    if (h != INVALID_HANDLE_VALUE) {
                        CloseHandle(h);
                        foundPort = full;
                        break;
                    }
                }

                if (!foundPort.empty()) {
                    g_arduino_port = foundPort;
                    Sleep(2500);
                    for (int retry = 0; retry < 15; ++retry) {
                        g_arduino_serial = OpenArduinoSerial(g_arduino_port.c_str());
                        if (g_arduino_serial != INVALID_HANDLE_VALUE) break;
                        Sleep(150);
                    }

                    if (g_arduino_serial != INVALID_HANDLE_VALUE) {
                        PurgeComm(g_arduino_serial, PURGE_RXCLEAR | PURGE_TXCLEAR);
                        const char* pn = strrchr(g_arduino_port.c_str(), '\\');
                        pn = pn ? pn + 1 : g_arduino_port.c_str();
                        char okMsg[256];
                        sprintf_s(okMsg,
                            "Arduino connected on %s\n\n"
                            "The LED should have blinked 3 times.\n"
                            "Ready to use.", pn);
                        MessageBoxA(g_hwnd, okMsg, "Arduino Connected",
                                    MB_OK | MB_ICONINFORMATION);
                    } else {
                        MessageBoxA(g_hwnd,
                            "Found the Arduino port but could not reconnect after boot.\n\n"
                            "Try:\n"
                            "  1. Unplug and replug the Arduino\n"
                            "  2. Restart the overlay\n\n"
                            "Falling back to Normal Mode.",
                            "Reconnection Failed", MB_OK | MB_ICONWARNING);
                        g_arduino_mode = false;
                    }
                } else {
                    char triedStr[256] = "";
                    int off = 0;
                    for (int i=0; i<(int)toTry.size() && i<12; i++)
                        off += sprintf_s(triedStr+off, sizeof(triedStr)-off, "%s ", toTry[i].c_str());
                    char failMsg[640];
                    sprintf_s(failMsg,
                        "Could not open any COM port.\n\n"
                        "Tried: %s\n\n"
                        "Make sure:\n"
                        "  1. Arduino is connected via USB\n"
                        "  2. arduino_mouse.ino sketch is flashed\n"
                        "  3. Arduino IDE is closed (it blocks the port)\n\n"
                        "Falling back to Normal Mode.", triedStr);
                    MessageBoxA(g_hwnd, failMsg, "Arduino Not Found",
                                MB_OK | MB_ICONWARNING);
                    g_arduino_mode = false;
                }
            }
        }
    }
    {
        wchar_t exe_dir[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe_dir, MAX_PATH);
        if (wchar_t* sep=wcsrchr(exe_dir,L'\\')) *(sep+1)=L'\0';
        SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        AddDllDirectory(exe_dir);

        wchar_t* local_env = nullptr;
        if (_wdupenv_s(&local_env, nullptr, L"LOCALAPPDATA") == 0 && local_env) {
            const wchar_t* pyvers[]={L"Python311",L"Python312",L"Python310",L"Python313"};
            for (auto* pv : pyvers) {
                wchar_t tlib[MAX_PATH*2]{};
                swprintf_s(tlib,sizeof(tlib)/sizeof(wchar_t),L"%s\\Programs\\Python\\%s\\Lib\\site-packages\\torch\\lib",local_env,pv);
                if (GetFileAttributesW(tlib)!=INVALID_FILE_ATTRIBUTES) {
                    AddDllDirectory(tlib);
                    OutputDebugStringW(L"[Overlay] PyTorch lib: ");
                    OutputDebugStringW(tlib); OutputDebugStringW(L"\n");
                    break;
                }
            }
            free(local_env);
        }
    }
    {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr,exe,MAX_PATH);
        if (wchar_t* sep=wcsrchr(exe,L'\\')) *(sep+1)=L'\0';
        std::wstring mp=std::wstring(exe)+L"best.onnx";
        try {
            g_detector=std::make_unique<YoloDetector>(mp, true);
        } catch (const std::exception& e) {
            char msg[512];
            sprintf_s(msg,"best.onnx introuvable.\n\n%s\n\nOverlay actif sans détection.",e.what());
            MessageBoxA(g_hwnd,msg,"Modèle manquant",MB_OK|MB_ICONWARNING);
        } catch (...) {
            MessageBoxA(g_hwnd,"best.onnx introuvable.\nOverlay sans détection.",
                        "Modèle manquant",MB_OK|MB_ICONWARNING);
        }
    }

    g_kbhook    = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, nullptr, 0);
    g_mousehook = nullptr;

    SetTimer(g_hwnd, TIMER_ID, TIMER_MS, nullptr);

    g_running.store(true);
    g_thread = std::thread(InferenceLoop);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_running.store(false);
    if (g_thread.joinable()) g_thread.join();
    KillTimer(g_hwnd, TIMER_ID);
    if (g_kbhook)    UnhookWindowsHookEx(g_kbhook);
    if (g_mousehook) UnhookWindowsHookEx(g_mousehook);
    if (g_memBmp) DeleteObject(g_memBmp);
    if (g_memDC)  DeleteDC(g_memDC);
    if (g_arduino_serial != INVALID_HANDLE_VALUE) CloseHandle(g_arduino_serial);

    return int(msg.wParam);
}
