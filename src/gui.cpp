#include "backend.h"
#include "timing.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "implot.h"
#include <cstdarg>
#include <unistd.h>
#include <deque>

static std::deque<std::string> g_events;
static void ev(const char *fmt, ...) {
    char b[256]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    char t[32]; snprintf(t, sizeof t, "[%6.1f] ", SDL_GetTicks() / 1000.0);
    g_events.push_back(std::string(t) + b);
    if (g_events.size() > 300) g_events.pop_front();
}

static const ImVec4 kText(0.95f, 0.97f, 1.0f, 1), kDim(0.66f, 0.72f, 0.84f, 1), kWhite(1, 1, 1, 1);
static const ImVec4 kAccent(0.46f, 0.80f, 1.0f, 1), kGood(0.40f, 0.92f, 0.70f, 1), kWarn(1.0f, 0.78f, 0.36f, 1), kBad(1.0f, 0.42f, 0.45f, 1);
static const ImVec4 kI(0.50f, 0.82f, 1.0f, 0.85f), kQ(1.0f, 0.62f, 0.45f, 0.80f);
static const ImVec4 kBgTL(0.035f, 0.045f, 0.13f, 1), kBgTR(0.03f, 0.09f, 0.19f, 1), kBgBR(0.02f, 0.06f, 0.13f, 1), kBgBL(0.07f, 0.04f, 0.16f, 1);

static float g_dt = 1.f / 60.f;
static double g_time = 0;
static bool g_lite = false;

static ImU32 C32(ImVec4 c, float a = 1.f) { c.w *= a; return ImGui::GetColorU32(c); }
static ImVec4 mix(const ImVec4 &a, const ImVec4 &b, float t) { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t); }
static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static float ease_out(float t) { t = clamp01(t); return 1.f - (1.f - t) * (1.f - t) * (1.f - t); }
static float anim(ImGuiID id, float target, float speed = 14.f) {
    float *v = ImGui::GetStateStorage()->GetFloatRef(id, target);
    *v += (target - *v) * (1.f - std::exp(-speed * g_dt));
    return *v;
}
static void approach(float &v, float target, float speed) { v += (target - v) * (1.f - std::exp(-speed * g_dt)); }
static ImVec2 operator+(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }

static void glass(ImDrawList *dl, ImVec2 a, ImVec2 b, float r, float k = 1.f, ImVec4 tint = kWhite) {
    if (!g_lite)
        for (int i = 3; i >= 1; i--) dl->AddRectFilled(a + ImVec2(-i * 5.f, -i * 3.f + 8.f), b + ImVec2(i * 5.f, i * 5.f + 8.f), C32(ImVec4(0, 0, 0, 1), 0.05f * k), r + i * 5.f);
    dl->AddRectFilled(a, b, C32(tint, 0.07f * k), r);
    if (!g_lite) {
        const int S = 5; const float h = b.y - a.y;
        for (int i = 0; i < S; i++) {
            dl->PushClipRect(ImVec2(a.x, a.y + h * 0.55f * i / S), ImVec2(b.x, a.y + h * 0.55f * (i + 1) / S), true);
            dl->AddRectFilled(a, b, C32(kWhite, 0.055f * k * (1.f - (float)i / S)), r);
            dl->PopClipRect();
        }
        const ImU32 z = C32(kWhite, 0.f), w = C32(kWhite, 0.45f * std::min(k, 1.f));
        const float mx = (a.x + b.x) * 0.5f;
        dl->AddRectFilledMultiColor(ImVec2(a.x + r, a.y + 1), ImVec2(mx, a.y + 2), z, w, w, z);
        dl->AddRectFilledMultiColor(ImVec2(mx, a.y + 1), ImVec2(b.x - r, a.y + 2), w, z, z, w);
    }
    dl->AddRect(a, b, C32(kWhite, 0.14f * std::min(k, 1.5f)), r, 0, 1.f);
}
static void glow(ImDrawList *dl, ImVec2 c, float R, ImVec4 col, float a) {
    for (int i = 0; i < 12; i++) dl->AddCircleFilled(c, R * (1.f - i / 12.f), C32(col, a), 48);
}
static void backdrop(ImVec2 sz) {
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilledMultiColor(ImVec2(0, 0), sz, C32(kBgTL), C32(kBgTR), C32(kBgBR), C32(kBgBL));
    if (g_lite) return;
    const float t = (float)g_time, s = std::max(sz.x, sz.y);
    glow(dl, ImVec2(sz.x * (0.18f + 0.06f * std::sin(t * 0.11f)), sz.y * (0.22f + 0.08f * std::cos(t * 0.09f))), s * 0.42f, ImVec4(0.30f, 0.36f, 1.0f, 1), 0.020f);
    glow(dl, ImVec2(sz.x * (0.82f + 0.05f * std::cos(t * 0.08f)), sz.y * (0.30f + 0.08f * std::sin(t * 0.12f))), s * 0.36f, ImVec4(0.10f, 0.72f, 0.85f, 1), 0.016f);
    glow(dl, ImVec2(sz.x * (0.55f + 0.10f * std::sin(t * 0.07f)), sz.y * (0.95f + 0.04f * std::cos(t * 0.1f))), s * 0.40f, ImVec4(0.62f, 0.28f, 0.95f, 1), 0.016f);
}

static bool pill(const char *label, bool on, ImVec2 sz, ImVec4 tint = kAccent) {
    ImGui::PushID(label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##b", sz);
    bool hov = ImGui::IsItemHovered();
    float h = anim(ImGui::GetID("h"), hov ? 1.f : 0.f), a = anim(ImGui::GetID("a"), on ? 1.f : 0.f);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + sz, C32(mix(kWhite, tint, a), 0.07f + 0.05f * h + 0.16f * a), sz.y * 0.5f);
    dl->AddRect(p, p + sz, C32(mix(kWhite, tint, a), 0.12f + 0.30f * a), sz.y * 0.5f);
    ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(p.x + (sz.x - ts.x) * 0.5f, p.y + (sz.y - ts.y) * 0.5f), C32(mix(kDim, kText, std::max(a, h * 0.7f))), label);
    ImGui::PopID();
    return clicked;
}

static bool segmented(const char *id, const char *const *items, int n, int &sel, float item_w, float h) {
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float w = item_w * n, pad = 3.f;
    dl->AddRectFilled(p, p + ImVec2(w, h), C32(kWhite, 0.06f), h * 0.5f);
    dl->AddRect(p, p + ImVec2(w, h), C32(kWhite, 0.10f), h * 0.5f);
    float x = anim(ImGui::GetID("x"), sel * item_w, 18.f);
    const ImVec2 a = p + ImVec2(x + pad, pad), b = p + ImVec2(x + item_w - pad, h - pad);
    dl->AddRectFilled(a, b, C32(kWhite, 0.17f), (h - 2 * pad) * 0.5f);
    dl->AddRect(a, b, C32(kWhite, 0.32f), (h - 2 * pad) * 0.5f);
    bool changed = false;
    for (int i = 0; i < n; i++) {
        ImGui::SetCursorScreenPos(p + ImVec2(i * item_w, 0));
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##s", ImVec2(item_w, h)) && sel != i) { sel = i; changed = true; }
        bool hov = ImGui::IsItemHovered();
        float t = anim(ImGui::GetID("t"), sel == i ? 1.f : 0.f, 16.f);
        ImVec2 ts = ImGui::CalcTextSize(items[i]);
        dl->AddText(p + ImVec2(i * item_w + (item_w - ts.x) * 0.5f, (h - ts.y) * 0.5f), C32(mix(hov ? kText : kDim, kText, t)), items[i]);
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, h));
    ImGui::PopID();
    return changed;
}
static bool toggle(const char *id, bool *v) {
    ImGui::PushID(id);
    const float h = ImGui::GetFontSize() * 1.35f, w = h * 1.9f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##t", ImVec2(w, h));
    if (clicked) *v = !*v;
    float a = anim(ImGui::GetID("a"), *v ? 1.f : 0.f, 16.f);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + ImVec2(w, h), C32(mix(kWhite, kGood, a), 0.12f + 0.55f * a), h * 0.5f);
    dl->AddRect(p, p + ImVec2(w, h), C32(kWhite, 0.18f), h * 0.5f);
    const float r = h * 0.5f - 3.f;
    const ImVec2 c(p.x + h * 0.5f + a * (w - h), p.y + h * 0.5f);
    if (!g_lite) dl->AddCircleFilled(c + ImVec2(0, 1.5f), r + 1.f, C32(ImVec4(0, 0, 0, 1), 0.22f), 20);
    dl->AddCircleFilled(c, r, C32(kText), 20);
    ImGui::PopID();
    return clicked;
}
static void led(ImDrawList *dl, ImVec2 c, float r, ImVec4 col, bool pulse) {
    if (pulse && !g_lite) {
        float k = 0.5f + 0.5f * std::sin((float)g_time * 4.f);
        dl->AddCircleFilled(c, r * (1.7f + 0.7f * k), C32(col, 0.20f * (1.f - k * 0.5f)), 24);
    }
    dl->AddCircleFilled(c, r, C32(col), 20);
}
static void icon_play(ImDrawList *dl, ImVec2 c, float r, ImU32 col) { dl->AddTriangleFilled(ImVec2(c.x - r * 0.55f, c.y - r * 0.75f), ImVec2(c.x - r * 0.55f, c.y + r * 0.75f), ImVec2(c.x + r * 0.8f, c.y), col); }
static void icon_pause(ImDrawList *dl, ImVec2 c, float r, ImU32 col) {
    dl->AddRectFilled(ImVec2(c.x - r * 0.6f, c.y - r * 0.7f), ImVec2(c.x - r * 0.15f, c.y + r * 0.7f), col, 2.f);
    dl->AddRectFilled(ImVec2(c.x + r * 0.15f, c.y - r * 0.7f), ImVec2(c.x + r * 0.6f, c.y + r * 0.7f), col, 2.f);
}
static void icon_gear(ImDrawList *dl, ImVec2 c, float r, ImU32 col, float rot) {
    for (int k = 0; k < 8; k++) {
        float a = rot + k * (float)M_PI / 4.f, ca = std::cos(a), sa = std::sin(a);
        dl->AddLine(ImVec2(c.x + ca * r * 0.6f, c.y + sa * r * 0.6f), ImVec2(c.x + ca * r * 0.95f, c.y + sa * r * 0.95f), col, r * 0.38f);
    }
    dl->AddCircle(c, r * 0.58f, col, 24, r * 0.3f);
}
static void icon_close(ImDrawList *dl, ImVec2 c, float r, ImU32 col) {
    dl->AddLine(ImVec2(c.x - r * 0.6f, c.y - r * 0.6f), ImVec2(c.x + r * 0.6f, c.y + r * 0.6f), col, 2.4f);
    dl->AddLine(ImVec2(c.x - r * 0.6f, c.y + r * 0.6f), ImVec2(c.x + r * 0.6f, c.y - r * 0.6f), col, 2.4f);
}
template <class F> static bool round_button(const char *id, float d, F draw_icon) {
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##r", ImVec2(d, d));
    float h = anim(ImGui::GetID("h"), ImGui::IsItemHovered() ? 1.f : 0.f), press = ImGui::IsItemActive() ? 0.92f : 1.f;
    ImVec2 c(p.x + d * 0.5f, p.y + d * 0.5f);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(c, d * 0.5f * press, C32(kWhite, 0.07f + 0.09f * h), 32);
    dl->AddCircle(c, d * 0.5f * press, C32(kWhite, 0.14f + 0.16f * h), 32);
    draw_icon(dl, c, d * 0.3f * press, C32(mix(kDim, kText, h)), h);
    ImGui::PopID();
    return clicked;
}

static void scatter(const char *l, const float *x, const float *y, int n, float sz, ImVec4 c) {
#if defined(IMPLOT_VERSION_NUM) && IMPLOT_VERSION_NUM >= 10000
    ImPlot::PlotScatter(l, x, y, n, ImPlotSpec(ImPlotProp_Marker, ImPlotMarker_Circle, ImPlotProp_MarkerSize, sz, ImPlotProp_MarkerFillColor, c, ImPlotProp_MarkerLineColor, c, ImPlotProp_FillAlpha, 1.f));
#else
    ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, sz, c, IMPLOT_AUTO, c); ImPlot::PlotScatter(l, x, y, n);
#endif
}
static void line_c(const char *l, const float *x, const float *y, int n, ImVec4 c, float w = 1.8f) {
#if defined(IMPLOT_VERSION_NUM) && IMPLOT_VERSION_NUM >= 10000
    ImPlot::PlotLine(l, x, y, n, ImPlotSpec(ImPlotProp_LineColor, c, ImPlotProp_LineWeight, w));
#else
    ImPlot::SetNextLineStyle(c, w); ImPlot::PlotLine(l, x, y, n);
#endif
}
static void line_d(const char *l, const double *x, const double *y, int n, ImVec4 c, float w = 1.6f) {
#if defined(IMPLOT_VERSION_NUM) && IMPLOT_VERSION_NUM >= 10000
    ImPlot::PlotLine(l, x, y, n, ImPlotSpec(ImPlotProp_LineColor, c, ImPlotProp_LineWeight, w));
#else
    ImPlot::SetNextLineStyle(c, w); ImPlot::PlotLine(l, x, y, n);
#endif
}

static void line_idx(const char *l, const float *y, int n, ImVec4 c) {
    static std::vector<float> xs;
    if ((int)xs.size() < n) { size_t o = xs.size(); xs.resize(n); for (size_t i = o; i < xs.size(); i++) xs[i] = (float)i; }
    line_c(l, xs.data(), y, n, c);
}
static void inf_lines(const char *l, const double *x, int n, ImVec4 c, float w) {
#if defined(IMPLOT_VERSION_NUM) && IMPLOT_VERSION_NUM >= 10000
    ImPlot::PlotInfLines(l, x, n, ImPlotSpec(ImPlotProp_LineColor, c, ImPlotProp_LineWeight, w));
#else
    ImPlot::SetNextLineStyle(c, w); ImPlot::PlotInfLines(l, x, n);
#endif
}

constexpr int WF_ROWS = 160, WF_COLS = 256;
struct Waterfall { std::vector<float> db = std::vector<float>(WF_ROWS * WF_COLS, -20.f); };
static void fft256(std::vector<cplx> &a) {
    const int n = (int)a.size();
    for (int i = 1, j = 0; i < n; i++) { int bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (int len = 2; len <= n; len <<= 1)
        for (int i = 0; i < n; i += len)
            for (int k = 0; k < len / 2; k++) { cplx w = std::polar(1.f, -2.f * (float)M_PI * k / len), u = a[i + k], v = a[i + k + len / 2] * w; a[i + k] = u + v; a[i + k + len / 2] = u - v; }
}
static void wf_push(Waterfall &w, const std::vector<cplx> &x, int mode, const Result &r) {
    const int N = (int)x.size();
    if (N < 2 * TTI) return;
    int start = N - 2 * TTI;
    if (r.signal && r.edges.size() >= 2)
        for (size_t i = 0; i < r.edges.size(); i += 2)
            if (r.edges[i] + 2 * TTI <= N) start = (int)r.edges[i];
    const cplx *p = x.data() + start;
    std::memmove(&w.db[WF_COLS], &w.db[0], sizeof(float) * WF_COLS * (WF_ROWS - 1));
    float *row = &w.db[0];
    if (mode == 0) {
        const int per = 2 * TTI / WF_COLS;
        for (int c = 0; c < WF_COLS; c++) { double s = 0; for (int k = 0; k < per; k++) s += std::norm(p[c * per + k]); row[c] = 10.f * std::log10((float)(s / per) + 1.f); }
    } else {
        std::vector<double> acc(WF_COLS, 0.0);
        std::vector<cplx> f(WF_COLS);
        const int len = (start == N - 2 * TTI && !r.signal) ? 2 * TTI : BURST_LEN;
        int segs = 0;
        for (int o = 0; o + WF_COLS <= len; o += WF_COLS, segs++) {
            for (int k = 0; k < WF_COLS; k++) f[k] = p[o + k] * (0.5f - 0.5f * std::cos(2.f * (float)M_PI * k / WF_COLS));
            fft256(f);
            for (int k = 0; k < WF_COLS; k++) acc[(k + WF_COLS / 2) % WF_COLS] += std::norm(f[k]);
        }
        if (segs > 0) for (int c = 0; c < WF_COLS; c++) row[c] = 10.f * std::log10((float)(acc[c] / segs) + 1.f);
        else std::fill(row, row + WF_COLS, -200.f);
    }
}

constexpr size_t TAPE_MAX = 2000000;
static const char *kSceneName[] = {"Сигнал", "Символы", "Синхронизация", "Маркер", "Водопад"};
static const int kNumScenes = 5;
static const char *kTedShort[] = {"Энергия", "ZC", "Гарднер", "Мюллер и Мюллер"};
static const char *kTedHelp[] = {
    "Выбирается такт с максимальной энергией, дальше он не подстраивается.",
    "ZC (переходы через ноль): нужно 2 и более отсчёта на символ; без компенсации несущей не работает (гл. 6.4).",
    "Гарднер: 2 отсчёта на символ, фаза несущей не нужна; подходит для BPSK и QPSK (гл. 6.4.1).",
    "Мюллер и Мюллер: решающий детектор, формула (6.20), 1 отсчёт на символ; чувствителен к фазе несущей (гл. 6.4.2).",
};
struct Trail { std::vector<float> x, y; };
struct Ui {
    Params prm;
    Result res;
    std::vector<cplx> x;
    Waterfall wf;
    float rx_gain = 40.f, tx_gain = 30.f;
    bool tx_on = false, rec = false, paused = false, drawer = false;
    int scene = 0, last_scene = 0;
    float scene_t = 1.f, drawer_t = 0.f, gear_rot = 0.f;
    float span_ms = 50.f; int span_idx = 1;
    bool follow = true, show_i = true, show_q = true, show_abs = false, show_edges = true;
    float ym_s = 300.f;
    std::vector<int16_t> tape = std::vector<int16_t>(2 * TAPE_MAX, 0);
    uint64_t tape_seen = 0;
    std::deque<double> marks_ms;
    uint64_t snap_start = 0, last_edge = 0;
    std::vector<double> vx, vi, vq, vm;
    int64_t c_j0 = -1, c_j1 = -1; int c_px = 0; Uint32 c_t = 0;
    std::deque<Trail> trail;
    int naive_off = 0; std::vector<float> nx, ny;
    int res_serial = 0;
    int wf_mode = 0;
    float upd_hz = 5.f, ui_scale = 1.f, ui_scale_edit = 1.f;
    bool lite_user = false;
    double proc_ms = 0; float fps_hist[120] = {0}; int fps_pos = 0;
    std::string renderer;
    int last_state = -1;
    bool was_locked = false; float lock_t = 1.f;
};

static void tape_pull(Shared &S, Ui &u) {
    std::lock_guard<std::mutex> lk(S.m);
    const uint64_t tot = S.total;
    if (tot < u.tape_seen) { u.tape_seen = 0; u.marks_ms.clear(); u.last_edge = 0; }
    uint64_t from = u.tape_seen;
    if (tot - from > RING) {
        const uint64_t gap_to = tot - RING, gap_from = std::max<uint64_t>(from, gap_to > TAPE_MAX ? gap_to - TAPE_MAX : 0);
        for (uint64_t i = gap_from; i < gap_to; i++) { size_t w = (size_t)(i % TAPE_MAX); u.tape[2 * w] = 0; u.tape[2 * w + 1] = 0; }
        from = gap_to;
    }
    for (uint64_t i = from; i < tot; i++) {
        const size_t w = (size_t)(i % TAPE_MAX), rr = (size_t)(i % RING);
        u.tape[2 * w] = S.ring[2 * rr]; u.tape[2 * w + 1] = S.ring[2 * rr + 1];
    }
    u.tape_seen = tot;
    const double cutoff = (double)(tot > TAPE_MAX ? tot - TAPE_MAX : 0) / (FS / 1000.0);
    while (!u.marks_ms.empty() && u.marks_ms.front() < cutoff) u.marks_ms.pop_front();
}

static void tape_decimate(Ui &u, int64_t j0, int64_t j1, int px) {
    const Uint32 now = SDL_GetTicks();
    if (j0 == u.c_j0 && j1 == u.c_j1 && px == u.c_px) return;
    if (now - u.c_t < 33 && !u.vx.empty()) return;
    u.c_j0 = j0; u.c_j1 = j1; u.c_px = px; u.c_t = now;
    const double kms = FS / 1000.0;
    u.vx.clear(); u.vi.clear(); u.vq.clear(); u.vm.clear();
    const int64_t nvis = j1 - j0;
    if (nvis < 2) return;
    auto at = [&](int64_t j, int &I, int &Q) { size_t w = (size_t)(j % TAPE_MAX); I = u.tape[2 * w]; Q = u.tape[2 * w + 1]; };
    if (nvis <= 4LL * px) {
        for (int64_t j = j0; j < j1; j++) { int I, Q; at(j, I, Q); u.vx.push_back((double)j / kms); u.vi.push_back(I); u.vq.push_back(Q); u.vm.push_back(std::hypot((double)I, (double)Q)); }
        return;
    }
    const double step = (double)nvis / px;
    for (int b = 0; b < px; b++) {
        const int64_t a = j0 + (int64_t)(b * step);
        const int64_t e = std::min(j1, std::max(a + 1, j0 + (int64_t)((b + 1) * step)));
        size_t w = (size_t)(a % TAPE_MAX);
        int mi = 32767, Mi = -32768, mq = 32767, Mq = -32768; int64_t pm = 0;
        for (int64_t j = a; j < e; j++) {
            const int I = u.tape[2 * w], Q = u.tape[2 * w + 1];
            if (++w == TAPE_MAX) w = 0;
            mi = std::min(mi, I); Mi = std::max(Mi, I); mq = std::min(mq, Q); Mq = std::max(Mq, Q);
            pm = std::max<int64_t>(pm, (int64_t)I * I + (int64_t)Q * Q);
        }
        const double xc = (double)(a + e) / 2 / kms, mg = std::sqrt((double)pm);
        u.vx.push_back(xc); u.vi.push_back(mi); u.vq.push_back(mq); u.vm.push_back(mg);
        u.vx.push_back(xc); u.vi.push_back(Mi); u.vq.push_back(Mq); u.vm.push_back(mg);
    }
}

static void scene_time(Ui &u, ImVec2 avail) {
    const float fs = ImGui::GetFontSize(), kms = (float)(FS / 1000.0);
    static const char *spans[] = {"10 мс", "50 мс", "200 мс", "1 с", "2 с"};
    static const float span_v[] = {10.f, 50.f, 200.f, 1000.f, 2000.f};
    const float bh = fs * 1.75f;
    if (segmented("span", spans, 5, u.span_idx, fs * 3.6f, bh)) { u.span_ms = span_v[u.span_idx]; u.follow = !u.paused; }
    ImGui::SameLine(0, fs);
    if (pill("I", u.show_i, ImVec2(fs * 2.4f, bh), kI)) u.show_i = !u.show_i;
    ImGui::SameLine(); if (pill("Q", u.show_q, ImVec2(fs * 2.4f, bh), kQ)) u.show_q = !u.show_q;
    ImGui::SameLine(); if (pill("|x|", u.show_abs, ImVec2(fs * 2.8f, bh), kText)) u.show_abs = !u.show_abs;
    ImGui::SameLine(); if (pill("Пакеты", u.show_edges, ImVec2(fs * 5.4f, bh), kGood)) u.show_edges = !u.show_edges;
    const float follow_a = anim(ImGui::GetID("fa"), (!u.follow && !u.paused) ? 1.f : 0.f, 12.f);
    if (follow_a > 0.02f) {
        ImGui::SameLine(0, fs);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, follow_a);
        if (pill("Вернуться к текущему", true, ImVec2(fs * 11.f, bh))) u.follow = true;
        ImGui::PopStyleVar();
    }
    ImGui::Dummy(ImVec2(0, fs * 0.4f));

    const double t_end = (double)u.tape_seen / kms;
    const int64_t avail_n = (int64_t)std::min<uint64_t>(u.tape_seen, (uint64_t)TAPE_MAX);
    const int64_t first_abs = (int64_t)u.tape_seen - avail_n;
    const float peak_t = u.res.peak > 0 ? (float)std::max(100.0, u.res.peak * 1.25) : 300.f;
    approach(u.ym_s, peak_t, 4.f);
    ImPlotFlags pf = ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoMenus | ImPlotFlags_NoMouseText;
    if (ImPlot::BeginPlot("##tape", ImVec2(-1, -1), pf)) {
        ImPlot::SetupAxes("мс", "АЦП");
        ImPlot::SetupAxisLimits(ImAxis_X1, t_end - u.span_ms, t_end, u.follow ? ImPlotCond_Always : ImPlotCond_Once);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -u.ym_s, u.ym_s, u.paused ? ImPlotCond_Once : ImPlotCond_Always);
        if (u.follow && ImPlot::IsPlotHovered() && (ImGui::IsMouseDragging(ImGuiMouseButton_Left) || ImGui::GetIO().MouseWheel != 0.f)) u.follow = false;
        if (u.show_edges && !u.marks_ms.empty()) {
            static std::vector<double> vm;
            vm.assign(u.marks_ms.begin(), u.marks_ms.end());
            inf_lines("пакеты", vm.data(), (int)vm.size(), ImVec4(kGood.x, kGood.y, kGood.z, 0.40f), 1.5f);
        }
        const ImPlotRect lim = ImPlot::GetPlotLimits();
        const int px = std::max(100, (int)ImPlot::GetPlotSize().x);
        const int64_t j0 = std::clamp((int64_t)std::floor(lim.X.Min * kms), first_abs, (int64_t)u.tape_seen);
        const int64_t j1 = std::clamp((int64_t)std::ceil(lim.X.Max * kms), first_abs, (int64_t)u.tape_seen);
        tape_decimate(u, j0, j1, px);
        const int n = (int)u.vx.size();
        if (n > 1) {
            if (u.show_i) line_d("I", u.vx.data(), u.vi.data(), n, kI);
            if (u.show_q) line_d("Q", u.vx.data(), u.vq.data(), n, kQ);
            if (u.show_abs) line_d("|x|", u.vx.data(), u.vm.data(), n, ImVec4(0.9f, 0.9f, 0.95f, 0.7f), 1.3f);
        }
        if (u.paused) ImPlot::GetPlotDrawList()->AddText(ImPlot::GetPlotPos() + ImVec2(14, 10), C32(kWarn), "Пауза: график можно двигать и масштабировать");
        ImPlot::EndPlot();
    }
    (void)avail;
}

static void update_naive(Ui &u) {
    const Result &r = u.res;
    u.nx.clear(); u.ny.clear();
    if (!r.signal || r.mfi.empty()) return;
    const int N = (int)r.mfi.size();
    double e2 = 0;
    for (size_t b = 0; b + 1 < r.edges.size(); b += 2) {
        const int i0 = (int)r.edges[b] + 3 * SPS, i1 = (int)r.edges[b + 1] - 3 * SPS;
        for (int i = i0 + u.naive_off; i < i1 && i < N; i += SPS) { u.nx.push_back(r.mfi[i]); u.ny.push_back(r.mfq[i]); e2 += (double)r.mfi[i] * r.mfi[i] + (double)r.mfq[i] * r.mfq[i]; }
    }
    if (u.nx.empty()) return;
    const float rms = (float)std::sqrt(e2 / u.nx.size());
    if (rms <= 0) return;
    for (size_t i = 0; i < u.nx.size(); i++) { u.nx[i] /= rms; u.ny[i] /= rms; }
}
static void unit_circle_and_ideal() {
    static std::vector<float> cx, cy;
    if (cx.empty()) for (int i = 0; i <= 96; i++) { cx.push_back(std::cos(2 * (float)M_PI * i / 96)); cy.push_back(std::sin(2 * (float)M_PI * i / 96)); }
    line_c("##circle", cx.data(), cy.data(), (int)cx.size(), ImVec4(1, 1, 1, 0.22f), 1.2f);
    const float k = 0.70710678f, ix[4] = {k, -k, -k, k}, iy[4] = {k, k, -k, -k};
    scatter("идеал", ix, iy, 4, 9.f, ImVec4(1.f, 1.f, 1.f, 0.9f));
}
static void scene_symbols(Ui &u, ImVec2 avail) {
    const float fs = ImGui::GetFontSize();
    const float gap = fs, col_w = (avail.x - gap) * 0.5f;
    const ImPlotFlags pf = ImPlotFlags_Equal | ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoMenus | ImPlotFlags_NoMouseText;
    const float head = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
    const float plot_h = std::max(120.f, ImGui::GetContentRegionAvail().y - head);
    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Без синхронизации"); ImGui::SameLine();
    ImGui::SetNextItemWidth(col_w - ImGui::GetCursorPosX() + ImGui::GetStyle().WindowPadding.x - fs * 0.2f);
    if (ImGui::SliderInt("##off", &u.naive_off, 0, SPS - 1, "такт %d")) update_naive(u);
    if (ImPlot::BeginPlot("##naive", ImVec2(col_w, plot_h), pf)) {
        ImPlot::SetupAxes("I", "Q");
        ImPlot::SetupAxesLimits(-1.6, 1.6, -1.6, 1.6, ImPlotCond_Always);
        unit_circle_and_ideal();
        if (!u.nx.empty()) scatter("такт", u.nx.data(), u.ny.data(), (int)u.nx.size(), 3.4f, ImVec4(kQ.x, kQ.y, kQ.z, 0.75f));
        ImPlot::EndPlot();
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("После синхронизации"); ImGui::SameLine();
    ImGui::TextColored(kDim, "%s", kTedShort[std::clamp(u.prm.sync, 0, 3)]);
    if (ImPlot::BeginPlot("##synced", ImVec2(col_w, plot_h), pf)) {
        ImPlot::SetupAxes("I", "Q");
        ImPlot::SetupAxesLimits(-1.6, 1.6, -1.6, 1.6, ImPlotCond_Always);
        unit_circle_and_ideal();
        const int nt = (int)u.trail.size();
        for (int k = 0; k < nt; k++) {
            const float age = nt > 1 ? (float)k / (nt - 1) : 1.f;
            const Trail &t = u.trail[k];
            if (!t.x.empty()) scatter("##sym", t.x.data(), t.y.data(), (int)t.x.size(), 3.2f + 0.6f * age, ImVec4(kGood.x, kGood.y, kGood.z, 0.10f + 0.80f * age * age));
        }
        ImPlot::EndPlot();
    }
    ImGui::EndGroup();
}

static void slider_d(const char *label, double *v, double lo, double hi, const char *fmt, float w, bool log = false) {
    ImGui::SetNextItemWidth(w);
    ImGui::SliderScalar(label, ImGuiDataType_Double, v, &lo, &hi, fmt, log ? ImGuiSliderFlags_Logarithmic : 0);
}
static void scene_sync(Ui &u, ImVec2 avail) {
    const Result &r = u.res;
    Params &p = u.prm;
    const float fs = ImGui::GetFontSize(), bh = fs * 1.75f;
    float ted_w = 0;
    for (const char *n : kTedShort) ted_w = std::max(ted_w, ImGui::CalcTextSize(n).x);
    segmented("ted", kTedShort, 4, p.sync, ted_w + fs * 1.8f, bh);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kTedHelp[std::clamp(p.sync, 0, 3)]);
    ImGui::SameLine(0, fs * 1.5f);
    ImGui::AlignTextToFramePadding();
    if (p.sync == 0) {
        ImGui::TextColored(kDim, "Авто-такт"); ImGui::SameLine();
        toggle("autooff", &p.auto_off);
        if (!p.auto_off) { ImGui::SameLine(); ImGui::SetNextItemWidth(fs * 8); ImGui::SliderInt("##offs", &p.offset, 0, SPS - 1, "такт %d"); }
    } else {
        ImGui::TextColored(kDim, "Скорость петли"); ImGui::SameLine();
        slider_d("##bloop", &p.bloop, 0.001, 0.2, "%.3f", fs * 9.f, true);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Полоса петли B_loop. Больше: быстрее захват, больше шума.\nМеньше: плавнее, но дольше сходится.");
    }
    ImGui::Dummy(ImVec2(0, fs * 0.4f));
    const float gap = fs, left_w = (avail.x - gap) * 0.5f, h = ImGui::GetContentRegionAvail().y;
    const ImPlotFlags pf = ImPlotFlags_NoLegend | ImPlotFlags_NoMenus | ImPlotFlags_NoMouseText;
    if (ImPlot::BeginPlot("Глаз", ImVec2(left_w, h), pf)) {
        ImPlot::SetupAxes("отсчёты от строба", "I");
        ImPlot::SetupAxisLimits(ImAxis_X1, -SPS, SPS, ImPlotCond_Always);
        const int n = (int)r.mfi.size(), cnt = std::min<int>((int)r.sx.size(), g_lite ? 40 : 90);
        static float xs[2 * SPS + 1], ys[2 * SPS + 1];
        for (int k = 0; k < 2 * SPS + 1; k++) xs[k] = (float)(k - SPS);
        const int first = (int)r.sx.size() - cnt;
        for (int j = first; j < (int)r.sx.size(); j++) {
            const int c = (int)r.sx[j];
            if (c - SPS < 0 || c + SPS >= n) continue;
            for (int k = 0; k < 2 * SPS + 1; k++) ys[k] = r.mfi[c - SPS + k];
            const float age = cnt > 1 ? (float)(j - first) / (cnt - 1) : 1.f;
            line_c("##eye", xs, ys, 2 * SPS + 1, ImVec4(kI.x, kI.y, kI.z, 0.10f + 0.40f * age), 1.4f);
        }
        const double vx[1] = {0.0};
        inf_lines("строб", vx, 1, ImVec4(kGood.x, kGood.y, kGood.z, 0.85f), 2.f);
        ImPlot::EndPlot();
    }
    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    const float rh = (h - ImGui::GetStyle().ItemSpacing.y) * 0.5f, rw = ImGui::GetContentRegionAvail().x;
    if (p.sync == 0) {
        if (ImPlot::BeginPlot("Энергия по фазе отсчёта", ImVec2(rw, h), pf)) {
            ImPlot::SetupAxes("смещение, отсчёты", "E / Emax");
            ImPlot::SetupAxesLimits(-0.5, SPS - 0.5, 0, 1.05, ImPlotCond_Always);
            static double xs[SPS], ys[SPS];
            for (int i = 0; i < SPS; i++) { xs[i] = i; ys[i] = r.energy[i]; }
            ImPlot::PlotBars("E", xs, ys, SPS, 0.7);
            ImPlot::EndPlot();
        }
    } else {
        if (ImPlot::BeginPlot("Ошибка детектора e(k)", ImVec2(rw, rh), pf)) {
            ImPlot::SetupAxes("символ k", "e", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            if (!r.ted_err.empty()) line_idx("e", r.ted_err.data(), (int)r.ted_err.size(), kWarn);
            ImPlot::EndPlot();
        }
        if (ImPlot::BeginPlot("Дробная задержка μ(k)", ImVec2(rw, rh), pf)) {
            ImPlot::SetupAxes("символ k", "μ", ImPlotAxisFlags_AutoFit, 0);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1, ImPlotCond_Always);
            if (!r.mu.empty()) line_idx("μ", r.mu.data(), (int)r.mu.size(), kGood);
            ImPlot::EndPlot();
        }
    }
    ImGui::EndGroup();
}

static void scene_marker(Ui &u, ImVec2 avail) {
    const Result &r = u.res;
    const float fs = ImGui::GetFontSize();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kDim, "Порог");
    ImGui::SameLine();
    slider_d("##mthr", &u.prm.mark_thr, 0.3, 0.95, "%.2f", fs * 9.f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Пик корреляции выше порога считается началом пакета.");
    ImGui::SameLine(0, fs * 1.5f);
    ImGui::TextColored(r.nframes ? kGood : kWarn, "Найдено пакетов: %d", r.nframes);
    if (r.nframes) { ImGui::SameLine(0, fs); ImGui::TextColored(kDim, "ρ %.2f   фаза %+.0f°", r.mark_best, r.mark_deg); }
    ImGui::Dummy(ImVec2(0, fs * 0.4f));
    const ImPlotFlags pf = ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoMenus | ImPlotFlags_NoMouseText;
    if (ImPlot::BeginPlot("##corr", ImVec2(-1, -1), pf)) {
        const int n = (int)r.corr.size();
        ImPlot::SetupAxes("символ k", "ρ");
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, std::max(n, 1), ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1.05, ImPlotCond_Always);
        if (n > 1) {
            line_idx("ρ", r.corr.data(), n, kI);
            const float tx[2] = {0.f, (float)n}, ty[2] = {(float)u.prm.mark_thr, (float)u.prm.mark_thr};
            line_c("порог", tx, ty, 2, kWarn, 1.5f);
            static std::vector<float> px, py;
            px.clear(); py.clear();
            for (int k : r.mark_pos) if (k >= 0 && k < n) { px.push_back((float)k); py.push_back(r.corr[k]); }
            if (!px.empty()) scatter("пакеты", px.data(), py.data(), (int)px.size(), 9.f, kGood);
        }
        ImPlot::EndPlot();
    }
    (void)avail;
}

static void scene_waterfall(Ui &u, ImVec2 avail) {
    const float fs = ImGui::GetFontSize(), bh = fs * 1.75f;
    static const char *modes[] = {"Мощность", "Спектр"};
    segmented("wfmode", modes, 2, u.wf_mode, fs * 7.f, bh);
    ImGui::Dummy(ImVec2(0, fs * 0.4f));
    static std::vector<float> img(WF_ROWS * WF_COLS), t;
    t = u.wf.db;
    std::nth_element(t.begin(), t.begin() + t.size() / 20, t.end());
    const float lo = t[t.size() / 20];
    std::nth_element(t.begin(), t.begin() + t.size() * 99 / 100, t.end());
    const float hi = std::max(t[t.size() * 99 / 100], lo + 1.f);
    for (size_t i = 0; i < img.size(); i++) img[i] = std::clamp((u.wf.db[i] - lo) / (hi - lo), 0.f, 1.f);
    ImPlot::PushColormap(ImPlotColormap_Viridis);
    if (ImPlot::BeginPlot("##wf", ImVec2(avail.x - fs * 5.5f, -1), ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoMenus | ImPlotFlags_NoMouseText)) {
        ImPlot::SetupAxes(u.wf_mode ? "частота, бин" : "время в двух TTI", "строки", ImPlotAxisFlags_NoGridLines, ImPlotAxisFlags_NoGridLines);
        ImPlot::PlotHeatmap("##h", img.data(), WF_ROWS, WF_COLS, 0, 1, nullptr, ImPlotPoint(0, 0), ImPlotPoint(1, 1));
        ImPlot::EndPlot();
    }
    ImGui::SameLine();
    ImPlot::ColormapScale("##sc", lo, hi, ImVec2(fs * 4.5f, -1), "%.0f дБ");
    ImPlot::PopColormap();
}

static const ImGuiWindowFlags kNoScroll = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground;
static void toast_state(Shared &S, bool sim, int &st, std::string &txt, ImVec4 &col, bool &pulse) {
    st = S.state.load();
    pulse = false;
    if (st == 3) { txt = "Ошибка"; col = kBad; }
    else if (st == 2) { txt = sim ? "Симуляция" : "Приём"; col = kGood; pulse = true; }
    else {
        static const char *dots[] = {"", ".", "..", "..."};
        txt = std::string("Подключение") + dots[(int)(g_time * 3) % 4]; col = kWarn; pulse = true;
    }
}
static void top_bar(Ui &u, Shared &S, bool sim, float W, float y0, float bh, float mg) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float fs = ImGui::GetFontSize(), ib = bh - fs * 0.9f, iy = y0 + (bh - ib) * 0.5f;
    glass(dl, ImVec2(mg, y0), ImVec2(W - mg, y0 + bh), bh * 0.5f);
    float xr = W - mg - fs * 0.45f - ib;
    ImGui::SetCursorScreenPos(ImVec2(xr, iy));
    u.gear_rot += g_dt * (u.drawer ? 2.2f : 0.f);
    if (round_button("gear", ib, [&](ImDrawList *l, ImVec2 c, float r, ImU32 col, float) { icon_gear(l, c, r * 1.55f, col, u.gear_rot); })) u.drawer = !u.drawer;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Настройки (S)");
    xr -= ib + fs * 0.4f;
    ImGui::SetCursorScreenPos(ImVec2(xr, iy));
    if (round_button("pause", ib, [&](ImDrawList *l, ImVec2 c, float r, ImU32 col, float) { if (u.paused) icon_play(l, c, r * 1.4f, col); else icon_pause(l, c, r * 1.4f, col); }))
        u.paused = !u.paused, ev("пауза %s", u.paused ? "ON" : "OFF");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u.paused ? "Продолжить (Пробел)" : "Пауза (Пробел)");
    int st; std::string txt; ImVec4 col; bool pulse;
    toast_state(S, sim, st, txt, col, pulse);
    if (u.paused) { txt = "Пауза"; col = kWarn; pulse = false; }
    const float tw = ImGui::CalcTextSize(txt.c_str()).x, pw = tw + fs * 2.6f;
    xr -= pw + fs * 0.4f;
    ImGui::SetCursorScreenPos(ImVec2(xr, iy));
    ImGui::PushID("status");
    const bool click = ImGui::InvisibleButton("##st", ImVec2(pw, ib));
    ImGui::PopID();
    if (click && st == 3) u.drawer = true;
    const ImVec2 sp(xr, iy);
    dl->AddRectFilled(sp, sp + ImVec2(pw, ib), C32(col, 0.12f), ib * 0.5f);
    dl->AddRect(sp, sp + ImVec2(pw, ib), C32(col, 0.30f), ib * 0.5f);
    led(dl, sp + ImVec2(fs * 1.1f, ib * 0.5f), fs * 0.30f, col, pulse);
    dl->AddText(sp + ImVec2(fs * 2.0f, (ib - ImGui::GetTextLineHeight()) * 0.5f), C32(kText), txt.c_str());

    float item_w = 0;
    for (const char *s : kSceneName) item_w = std::max(item_w, ImGui::CalcTextSize(s).x);
    item_w += fs * 1.8f;
    const float seg_w = item_w * kNumScenes;
    float sx = (W - seg_w) * 0.5f;
    if (sx + seg_w > xr - fs) sx = std::max(mg + fs * 0.6f, xr - fs - seg_w);
    const float tl = ImGui::CalcTextSize("Pluto QPSK").x;
    if (mg + fs * 1.2f + tl + fs < sx) dl->AddText(ImVec2(mg + fs * 1.2f, y0 + (bh - ImGui::GetTextLineHeight()) * 0.5f), C32(kDim), "Pluto QPSK");
    ImGui::SetCursorScreenPos(ImVec2(sx, iy));
    int sel = u.scene;
    if (segmented("scenes", kSceneName, kNumScenes, sel, item_w, ib)) u.scene = sel;
}

static void stat_block(ImDrawList *dl, ImVec2 p, const char *label, const char *value, ImVec4 vc, float fs) {
    dl->AddText(p, C32(kDim), label);
    dl->AddText(ImGui::GetFont(), fs * 1.18f, p + ImVec2(0, ImGui::GetTextLineHeight() + 1), C32(vc), value);
}
static void bottom_bar(Ui &u, Shared &S, float W, float y0, float bh, float mg) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const Result &r = u.res;
    const float fs = ImGui::GetFontSize(), pad = fs * 1.1f;
    glass(dl, ImVec2(mg, y0), ImVec2(W - mg, y0 + bh), bh * 0.5f);
    const bool locked = r.signal && r.dec.n > 0 && r.dec.match >= 90.0;
    const bool weak = r.signal && !locked;
    if (locked && !u.was_locked) u.lock_t = 0.f;
    u.was_locked = locked;
    u.lock_t = std::min(1.f, u.lock_t + g_dt / 1.2f);

    const float ls = fs * 2.3f;
    float x = mg + pad + fs * 0.4f;
    const int nl = (int)strlen(NAME);
    for (int i = 0; i < nl; i++) {
        char want = NAME[i], got = (r.signal && i < (int)r.dec.text.size()) ? r.dec.text[i] : 0;
        const bool ok = got == want && r.signal;
        ImGui::PushID(i);
        const float a = anim(ImGui::GetID("l"), ok ? 1.f : 0.f, 9.f);
        ImGui::PopID();
        char b[2] = {got ? got : '.', 0};
        ImVec4 c = ok ? mix(kText, kGood, a) : (got ? kWarn : ImVec4(kDim.x, kDim.y, kDim.z, 0.5f));
        const float sz = ls * (0.88f + 0.12f * a);
        dl->AddText(ImGui::GetFont(), sz, ImVec2(x, y0 + (bh - sz) * 0.5f - (g_lite ? 0.f : a * 2.f)), C32(c), b);
        x += ls * 0.72f;
    }
    x += pad;

    {
        ImVec4 bc = locked ? kGood : (weak ? kWarn : kDim);
        const char *msg = locked ? "Захват" : (weak ? (r.nframes == 0 ? "Нет маркера" : "Есть ошибки") : "Поиск пакета");
        const float cw = ImGui::CalcTextSize(msg).x + fs * 2.4f, ch = fs * 1.9f, cy = y0 + (bh - ch) * 0.5f;
        dl->AddRectFilled(ImVec2(x, cy), ImVec2(x + cw, cy + ch), C32(bc, 0.14f), ch * 0.5f);
        dl->AddRect(ImVec2(x, cy), ImVec2(x + cw, cy + ch), C32(bc, 0.30f), ch * 0.5f);
        led(dl, ImVec2(x + fs * 1.0f, cy + ch * 0.5f), fs * 0.27f, bc, locked || !r.signal);
        dl->AddText(ImVec2(x + fs * 1.8f, cy + (ch - ImGui::GetTextLineHeight()) * 0.5f), C32(bc), msg);
        x += cw + pad * 1.4f;
    }

    const float ty = y0 + (bh - fs * 1.35f) * 0.5f;
    const float rec_x = W - mg - pad - fs * 6.4f, tx_x = rec_x - fs * 10.f;
    ImGui::SetCursorScreenPos(ImVec2(rec_x, ty));
    if (toggle("rec", &u.rec)) { S.rec_req = u.rec; ev("запись %s", u.rec ? "ON" : "OFF"); }
    ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::TextColored(u.rec ? kBad : kDim, "Запись");
    ImGui::SetCursorScreenPos(ImVec2(tx_x, ty));
    if (toggle("tx", &u.tx_on)) { S.tx_req = u.tx_on; ev("TX %s", u.tx_on ? "ON" : "OFF"); }
    ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::TextColored(u.tx_on ? kGood : kDim, "Передача");

    const float lw = ImGui::CalcTextSize("Уровень").x + ImGui::CalcTextSize("клиппинг 99.9 %").x + fs * 1.2f, lx = tx_x - lw - pad * 1.4f;
    const bool show_level = lx > x + pad;
    if (show_level) {
        const float pk = clamp01((float)r.peak / 2048.f), a = anim(ImGui::GetID("lvl"), pk, 10.f);
        const bool clip = r.clip > 0;
        const float by = y0 + bh * 0.5f + fs * 0.45f, bw = fs * 0.5f;
        dl->AddText(ImVec2(lx, y0 + bh * 0.5f - fs * 1.2f), C32(clip ? kBad : kDim), "Уровень");
        char b[40]; if (clip) snprintf(b, sizeof b, "клиппинг %.1f %%", r.clip); else snprintf(b, sizeof b, "%.0f / 2048", r.peak);
        const float vw = ImGui::CalcTextSize(b).x;
        dl->AddText(ImVec2(lx + lw - vw, y0 + bh * 0.5f - fs * 1.2f), C32(clip ? kBad : kDim), b);
        dl->AddRectFilled(ImVec2(lx, by), ImVec2(lx + lw, by + bw), C32(kWhite, 0.10f), bw * 0.5f);
        ImVec4 lc = clip ? kBad : (a > 0.8f ? kWarn : kGood);
        if (a > 0.01f) dl->AddRectFilled(ImVec2(lx, by), ImVec2(lx + std::max(bw, lw * a), by + bw), C32(lc), bw * 0.5f);
    }

    if (r.signal) {
        char v[3][32];
        snprintf(v[0], 32, "%.0f %%", r.dec.match);
        snprintf(v[1], 32, "%.1f %%", r.evm < 0 ? 0.0 : r.evm);
        snprintf(v[2], 32, "%+.0f Гц", r.cfo_hz);
        const char *lab[3] = {"Совпадение", "EVM", "Сдвиг частоты"};
        const float lim = (show_level ? lx : tx_x) - pad, ty2 = y0 + bh * 0.5f - ImGui::GetTextLineHeight() * 1.05f - 2;
        float sx = x;
        for (int i = 0; i < 3; i++) {
            const float w = std::max(ImGui::CalcTextSize(lab[i]).x, ImGui::CalcTextSize("-0000 Гц").x * 1.18f) + fs * 1.4f;
            if (sx + w > lim) break;
            stat_block(dl, ImVec2(sx, ty2), lab[i], v[i], kText, fs);
            sx += w;
        }
    } else if (!u.tx_on) {
        const float k = 0.5f + 0.5f * std::sin((float)g_time * 4.f);
        dl->AddText(ImVec2(x, y0 + (bh - ImGui::GetTextLineHeight()) * 0.5f), C32(kWarn, 0.45f + 0.55f * k), "Включите передачу");
    }

    if (!g_lite && u.lock_t < 1.f) {
        const float t = ease_out(u.lock_t), sw = fs * 8.f, sx = mg - sw + (W - 2 * mg + sw * 2) * t, al = 0.22f * std::sin(u.lock_t * (float)M_PI);
        dl->PushClipRect(ImVec2(mg + bh * 0.3f, y0), ImVec2(W - mg - bh * 0.3f, y0 + bh), true);
        const ImU32 z = C32(kWhite, 0.f), w = C32(kWhite, al);
        dl->AddRectFilledMultiColor(ImVec2(sx, y0), ImVec2(sx + sw * 0.5f, y0 + bh), z, w, w, z);
        dl->AddRectFilledMultiColor(ImVec2(sx + sw * 0.5f, y0), ImVec2(sx + sw, y0 + bh), w, z, z, w);
        dl->PopClipRect();
    }
}

static void drawer_ui(Ui &u, Shared &S, const std::string &rxu, const std::string &txu, uint64_t total, const std::vector<std::string> &blog, float W, float H) {
    const float fs = ImGui::GetFontSize();
    const float dw = std::min(W * 0.9f, fs * 22.f), t = ease_out(u.drawer_t);
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##dim", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    if (!g_lite) ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), C32(ImVec4(0.01f, 0.02f, 0.06f, 1), 0.5f * t));
    if (u.drawer && ImGui::InvisibleButton("##dimclick", ImVec2(W - dw, H))) u.drawer = false;
    ImGui::End();
    ImGui::PopStyleVar(2);
    const float m = fs * 0.6f, px = W - (dw + m) * t;
    ImGui::SetNextWindowPos(ImVec2(px, m)); ImGui::SetNextWindowSize(ImVec2(dw, H - 2 * m));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 1.1f, fs * 0.9f)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##drawer", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    {
        const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(wp, wp + ws, C32(ImVec4(0.04f, 0.06f, 0.14f, 1), 0.94f), fs * 1.2f);
        glass(dl, wp, wp + ws, fs * 1.2f, 1.6f);
    }
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Настройки");
    ImGui::SameLine(ImGui::GetWindowWidth() - fs * 3.0f);
    if (round_button("close", fs * 1.8f, [](ImDrawList *l, ImVec2 c, float r, ImU32 col, float) { icon_close(l, c, r * 1.6f, col); })) u.drawer = false;
    ImGui::Spacing();
    const float iw = -1;
    ImGui::SeparatorText("Приём");
    ImGui::TextColored(kDim, "Усиление приёма, дБ"); ImGui::SetNextItemWidth(iw);
    ImGui::SliderFloat("##rxg", &u.rx_gain, 0, 73, "%.0f");
    if (ImGui::IsItemDeactivatedAfterEdit()) { S.gain_req = u.rx_gain; ev("gain RX -> %.0f дБ (перезапуск main)", u.rx_gain); }
    ImGui::SeparatorText("Передача");
    ImGui::TextColored(kDim, "Усиление передачи, дБ"); ImGui::SetNextItemWidth(iw);
    ImGui::SliderFloat("##txg", &u.tx_gain, 0, 89, "%.0f");
    if (ImGui::IsItemDeactivatedAfterEdit()) { S.tx_gain_req = u.tx_gain; ev("gain TX -> %.0f дБ (перезапуск main)", u.tx_gain); }
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(kDim, "Применяется, когда отпустите ползунок. Приём и передача перезапустятся примерно на секунду.");
    ImGui::PopTextWrapPos();
    ImGui::SeparatorText("Отображение");
    ImGui::TextColored(kDim, "Обновление графиков, раз в секунду"); ImGui::SetNextItemWidth(iw);
    ImGui::SliderFloat("##upd", &u.upd_hz, 0.5f, 30.f, "%.1f", ImGuiSliderFlags_Logarithmic);
    ImGui::TextColored(kDim, "Размер интерфейса"); ImGui::SetNextItemWidth(iw);
    ImGui::SliderFloat("##scale", &u.ui_scale_edit, 0.8f, 2.2f, "x%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) u.ui_scale = u.ui_scale_edit;
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Упрощённая графика"); ImGui::SameLine();
    if (toggle("lite", &u.lite_user)) g_lite = u.lite_user;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Без стеклянных бликов, свечения и затемнений. Сама включается на программном OpenGL.");
    if (ImGui::CollapsingHeader("Обработка")) {
        ImGui::Checkbox("Убирать постоянную составляющую (DC)", &u.prm.remove_dc);
        ImGui::Checkbox("Компенсировать частоту и фазу несущей", &u.prm.comp_cfo);
        if (u.prm.sync != 0) {
            const float w = ImGui::GetContentRegionAvail().x;
            slider_d("##zeta", &u.prm.zeta, 0.3, 3.0, "ζ петли: %.2f", w);
            slider_d("##gd", &u.prm.gd, 0.2, 5.0, "G_D: %.2f", w, true);
            slider_d("##cb", &u.prm.cfo_bloop, 0.001, 0.1, "B_loop CFO: %.4f", w, true);
            slider_d("##cz", &u.prm.cfo_zeta, 0.5, 3.0, "ζ CFO: %.2f", w);
        } else ImGui::TextColored(kDim, "Параметры петли появятся при выборе ZC, Гарднера или М и М.");
    }
    if (ImGui::CollapsingHeader("Диагностика")) {
        ImGuiIO &io = ImGui::GetIO();
        const Result &r = u.res;
        ImGui::Text("%.1f FPS, обработка %.2f мс", io.Framerate, u.proc_ms);
        ImGui::TextColored(g_lite ? kWarn : kDim, "OpenGL: %s", u.renderer.empty() ? "?" : u.renderer.c_str());
        ImGui::TextColored(kDim, "RX %s  TX %s", rxu.empty() ? "-" : rxu.c_str(), txu.empty() ? "-" : txu.c_str());
        ImGui::TextColored(kDim, "принято отсчётов: %llu", (unsigned long long)total);
        ImGui::TextColored(kDim, "DC %.1f %+.1fj, CFO %.0f/%.0f Гц, фаза %.1f°", r.dc_re, r.dc_im, r.coarse_hz, r.fine_hz, r.phase_deg);
        ImGui::PlotLines("##fps", u.fps_hist, 120, u.fps_pos, nullptr, 0, 120, ImVec2(-1, fs * 2.f));
        ImGui::BeginChild("##log", ImVec2(0, fs * 9.f), ImGuiChildFlags_Borders);
        for (const auto &l : blog) ImGui::TextColored(ImVec4(0.6f, 0.8f, 1, 1), "main  %s", l.c_str());
        for (const auto &l : g_events) ImGui::TextUnformatted(l.c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.f);
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleVar(2); ImGui::PopStyleColor();
}

static void apply_style() {
    ImGuiStyle &s = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    s.WindowRounding = 0; s.FrameRounding = 12; s.GrabRounding = 12; s.ChildRounding = 12; s.PopupRounding = 12; s.ScrollbarRounding = 12;
    s.FramePadding = ImVec2(10, 6); s.ItemSpacing = ImVec2(10, 8); s.GrabMinSize = 18; s.WindowBorderSize = 0; s.ChildBorderSize = 0; s.FrameBorderSize = 0; s.PopupBorderSize = 1;
    ImVec4 *c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0); c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0); c[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.10f, 0.20f, 0.96f);
    c[ImGuiCol_Border] = ImVec4(1, 1, 1, 0.14f);
    c[ImGuiCol_FrameBg] = ImVec4(1, 1, 1, 0.08f); c[ImGuiCol_FrameBgHovered] = ImVec4(1, 1, 1, 0.13f); c[ImGuiCol_FrameBgActive] = ImVec4(1, 1, 1, 0.17f);
    c[ImGuiCol_Header] = ImVec4(1, 1, 1, 0.10f); c[ImGuiCol_HeaderHovered] = ImVec4(1, 1, 1, 0.16f); c[ImGuiCol_HeaderActive] = ImVec4(1, 1, 1, 0.22f);
    c[ImGuiCol_SliderGrab] = kAccent; c[ImGuiCol_SliderGrabActive] = ImVec4(0.7f, 0.9f, 1.f, 1); c[ImGuiCol_CheckMark] = kAccent;
    c[ImGuiCol_Text] = kText; c[ImGuiCol_TextDisabled] = kDim; c[ImGuiCol_Separator] = ImVec4(1, 1, 1, 0.10f);
    c[ImGuiCol_PlotLines] = kAccent;
    ImPlotStyle &ps = ImPlot::GetStyle();
    ImPlot::StyleColorsDark(&ps);
    ps.Colors[ImPlotCol_PlotBg] = ImVec4(0, 0, 0, 0.18f); ps.Colors[ImPlotCol_FrameBg] = ImVec4(0, 0, 0, 0);
    ps.Colors[ImPlotCol_PlotBorder] = ImVec4(1, 1, 1, 0.08f); ps.Colors[ImPlotCol_AxisGrid] = ImVec4(1, 1, 1, 0.07f);
    ps.Colors[ImPlotCol_AxisText] = kDim; ps.Colors[ImPlotCol_TitleText] = kText;
    ps.PlotPadding = ImVec2(8, 8); ps.PlotBorderSize = 1;
}
static std::string find_font() {
    if (const char *e = getenv("PLUTO_FONT")) if (access(e, R_OK) == 0) return e;
    static const char *cand[] = {
        "/usr/local/share/fonts/google-noto-vf/NotoSans[wght].ttf", "/usr/share/fonts/google-noto-vf/NotoSans[wght].ttf",
        "/usr/share/fonts/google-noto/NotoSans-Regular.ttf", "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf"};
    for (const char *c : cand) if (access(c, R_OK) == 0) return c;
    return "";
}

int main(int argc, char **argv) {
    Config cfg;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--sim") cfg.sim = true;
        else if (a == "--swap") cfg.swap = true;
        else if (a == "--rx" && i + 1 < argc) cfg.rx_uri = argv[++i];
        else if (a == "--tx" && i + 1 < argc) cfg.tx_uri = argv[++i];
        else { printf("Использование: %s [--sim] [--swap] [--rx usb:X.Y.Z] [--tx usb:X.Y.Z]\n", argv[0]); return 1; }
    }
    const int frames = getenv("GUI_TEST_FRAMES") ? atoi(getenv("GUI_TEST_FRAMES")) : 0;
    init_tx();
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    SDL_Window *win = SDL_CreateWindow("Pluto QPSK — приёмник", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1600, 950, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl = SDL_GL_CreateContext(win);
    SDL_GL_SetSwapInterval(1);
    ImGui::CreateContext(); ImPlot::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ImVector<ImWchar> gr;
    ImFontGlyphRangesBuilder gb;
    gb.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    gb.AddRanges(io.Fonts->GetGlyphRangesGreek());
    gb.AddText("—·×°");
    gb.BuildRanges(&gr);
    const std::string font = find_font();
    if (font.empty() || !io.Fonts->AddFontFromFileTTF(font.c_str(), 22.f, nullptr, gr.Data)) io.Fonts->AddFontDefault();
    apply_style();
    ImGui_ImplSDL2_InitForOpenGL(win, gl);
    ImGui_ImplOpenGL3_Init("#version 330");
    Ui u;
    { const char *rn = (const char *)glGetString(GL_RENDERER); u.renderer = rn ? rn : ""; }
    { std::string l = u.renderer; for (auto &ch : l) ch = (char)tolower((unsigned char)ch);
      if (l.find("llvmpipe") != std::string::npos || l.find("softpipe") != std::string::npos || l.find("swrast") != std::string::npos) { g_lite = u.lite_user = true; } }
    if (getenv("GUI_FORCE_GLASS")) g_lite = u.lite_user = false;
    if (g_lite) { ImGui::GetStyle().AntiAliasedLines = false; ImGui::GetStyle().AntiAliasedLinesUseTex = false; ImGui::GetStyle().AntiAliasedFill = false; }
    Shared S;
    S.gain_req = u.rx_gain; S.tx_gain_req = u.tx_gain;
    if (cfg.sim) { u.tx_on = true; S.tx_req = true; }
    std::thread th(session_thread, std::ref(S), cfg);
    ev("запуск: %s, OpenGL: %s", cfg.sim ? "симуляция" : "Pluto", u.renderer.c_str());
    bool run = true;
    Uint32 t_proc = 0, t_row = 0, nframe = 0, t_prev = SDL_GetTicks();
    while (run) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) { ImGui_ImplSDL2_ProcessEvent(&e); if (e.type == SDL_QUIT) run = false; }
        if (frames && ++nframe >= (Uint32)frames) run = false;
        const Uint32 now = SDL_GetTicks();
        g_dt = std::clamp((now - t_prev) / 1000.f, 0.001f, 0.1f); t_prev = now; g_time = now / 1000.0;
        if (!u.paused) tape_pull(S, u);
        const Uint32 period = (Uint32)(1000.f / std::max(0.5f, u.upd_hz));
        const bool live_tick = !u.paused && now - t_proc >= period;
        if (live_tick || u.res_serial == 0) {
            t_proc = now;
            u.snap_start = snapshot(S, u.x);
            const auto a = std::chrono::steady_clock::now();
            process(u.x, u.prm, u.res);
            u.proc_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
            u.res_serial++;
            if (u.res.signal) {
                for (double ed : u.res.edges) {
                    const uint64_t ab = u.snap_start + (uint64_t)ed;
                    if (ab > u.last_edge) { u.last_edge = ab; u.marks_ms.push_back((double)ab / (FS / 1000.0)); }
                }
                Trail t; t.x = u.res.px; t.y = u.res.py;
                u.trail.push_back(std::move(t));
                while (u.trail.size() > (g_lite ? 2u : 4u)) u.trail.pop_front();
            }
            update_naive(u);
        }
        if (!u.paused && now - t_row >= 1000u / 30u) { t_row = now; wf_push(u.wf, u.x, u.wf_mode, u.res); }
        const int st = S.state.load();
        if (st != u.last_state) { ev("состояние main: %d", st); u.last_state = st; }
        if (!io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) u.paused = !u.paused, ev("пауза %s", u.paused ? "ON" : "OFF");
            if (ImGui::IsKeyPressed(ImGuiKey_S, false)) u.drawer = !u.drawer;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) u.drawer = false;
            for (int k = 0; k < kNumScenes; k++) if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + k), false)) u.scene = k;
        }
        approach(u.drawer_t, u.drawer ? 1.f : 0.f, 12.f);
        if (u.scene != u.last_scene) { u.last_scene = u.scene; u.scene_t = 0.f; }
        u.scene_t = std::min(1.f, u.scene_t + g_dt / 0.28f);
#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 19200
        ImGui::GetStyle().FontScaleMain = u.ui_scale;
#else
        io.FontGlobalScale = u.ui_scale;
#endif
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
        u.fps_hist[u.fps_pos] = io.Framerate; u.fps_pos = (u.fps_pos + 1) % 120;
        std::string rxu, txu; std::vector<std::string> blog; uint64_t total; std::string err, note;
        { std::lock_guard<std::mutex> lk(S.m); rxu = S.rx_uri; txu = S.tx_uri; blog = S.log; total = S.total; err = S.err; note = S.note; }

        ImGuiViewport *vp = ImGui::GetMainViewport();
        const float W = vp->WorkSize.x, H = vp->WorkSize.y, fs = ImGui::GetFontSize();
        const float mg = fs * 0.9f, top_h = fs * 2.9f, bot_h = fs * 3.5f;
        backdrop(ImVec2(W, H));
        ImGui::SetNextWindowPos(vp->WorkPos); ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
        ImGui::PopStyleVar(2);
        top_bar(u, S, cfg.sim, W, mg * 0.8f, top_h, mg);

        const float a = ease_out(u.scene_t), slide = (1.f - a) * fs * 0.9f;
        const ImVec2 c0(mg, mg * 0.8f + top_h + mg * 0.7f), c1(W - mg, H - bot_h - mg * 1.5f);
        glass(ImGui::GetWindowDrawList(), c0, c1, fs * 1.3f);
        const float cp = fs * 1.0f;
        ImGui::SetCursorScreenPos(ImVec2(c0.x + cp, c0.y + cp + slide));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_lite ? 1.f : std::max(0.02f, a));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 8));
        ImGui::BeginChild("##scene", ImVec2(c1.x - c0.x - 2 * cp, c1.y - c0.y - 2 * cp - slide), ImGuiChildFlags_None, kNoScroll);
        if (S.state.load() == 3) {
            const float ba = anim(ImGui::GetID("err"), 1.f, 8.f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, C32(kBad, 0.14f * ba));
            ImGui::BeginChild("##err", ImVec2(0, fs * 4.6f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
            ImGui::TextColored(kBad, "Нет связи с Pluto");
            ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(err.c_str()); ImGui::PopTextWrapPos();
            ImGui::EndChild(); ImGui::PopStyleColor();
        }
        (void)note;
        const ImVec2 av = ImGui::GetContentRegionAvail();
        switch (u.scene) {
            case 0: scene_time(u, av); break;
            case 1: scene_symbols(u, av); break;
            case 2: scene_sync(u, av); break;
            case 3: scene_marker(u, av); break;
            default: scene_waterfall(u, av); break;
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        bottom_bar(u, S, W, H - bot_h - mg * 0.8f, bot_h, mg);
        ImGui::End();
        if (u.drawer_t > 0.003f) drawer_ui(u, S, rxu, txu, total, blog, W, H);
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(kBgTL.x, kBgTL.y, kBgTL.z, 1); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(win);
    }
    S.stop = true;
    th.join();
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImPlot::DestroyContext(); ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
