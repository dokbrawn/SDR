#include "backend.h"
#include "timing.h"
#include <cstdio>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

struct Chan {
    double cfo = 3155.0, phase = 0.5, amp = 400.0, sigma = 1.4;
    cplx dc = cplx(-5.7f, -6.6f);
    unsigned seed = 1;
    bool tx = true;
};

static std::vector<cplx> make_snapshot(const Chan &c) {
    std::vector<cplx> x(SNAP);
    std::mt19937 rng(c.seed);
    std::normal_distribution<float> nd(0.f, 1.f);
    for (size_t n = 0; n < x.size(); n++) {
        const size_t t = n / TTI, i = n % TTI;
        cplx s = (c.tx && t % 2 == 0) ? g_tx.wave[i] * (float)c.amp : cplx(0, 0);
        const double ph = 2 * M_PI * c.cfo * (double)n / FS + c.phase;
        s *= cplx((float)std::cos(ph), (float)std::sin(ph));
        s += c.dc + (float)c.sigma * cplx(nd(rng), nd(rng));
        x[n] = s;
    }
    return x;
}

static std::vector<cplx> moving_average(const std::vector<cplx> &x, int L) {
    const int n = (int)x.size();
    std::vector<cd> ps(n + 1, cd(0, 0));
    for (int i = 0; i < n; i++) ps[i + 1] = ps[i] + cd(x[i]);
    std::vector<cplx> y(n);
    for (int k = 0; k < n; k++) {
        const int a = std::max(0, k - L / 2), b = std::min(n, k - L / 2 + L);
        y[k] = cplx(ps[b] - ps[a]) / (float)(b - a);
    }
    return y;
}

static std::vector<cplx> qpsk_burst(int nsym, unsigned seed, int pad, int &i0, int &i1) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> bit(0, 1);
    const float k = 0.70710678f;
    std::vector<cplx> x(pad + nsym * SPS + pad, cplx(0, 0));
    for (int s = 0; s < nsym; s++) {
        const cplx v(bit(rng) ? -k : k, bit(rng) ? -k : k);
        for (int j = 0; j < SPS; j++) x[pad + s * SPS + j] = v;
    }
    i0 = pad + 3 * SPS;
    i1 = pad + nsym * SPS - 3 * SPS;
    return moving_average(x, SPS);
}

static void barker_sidelobes_are_at_most_one() {
    int peak = 0;
    for (int i = 0; i < NMARK; i++) peak += BARKER13[i] * BARKER13[i];
    CHECK(peak == 13);
    for (int lag = 1; lag < NMARK; lag++) {
        int s = 0;
        for (int i = 0; i + lag < NMARK; i++) s += BARKER13[i] * BARKER13[i + lag];
        CHECK(std::abs(s) <= 1);
    }
}

static void packet_layout_fits_in_one_tti() {
    CHECK(REPS >= 1);
    CHECK(BURST_LEN <= TTI);
    CHECK((int)g_tx.wave.size() >= TTI);
    int tail = 0;
    for (size_t i = BURST_LEN; i < g_tx.wave.size(); i++) tail += std::abs(g_tx.wave[i]) > 0;
    CHECK(tail == 0);
}

static void decodes_name_with_every_timing_detector() {
    for (int sync = 0; sync < 4; sync++) {
        Params p; p.sync = sync;
        Result r;
        process(make_snapshot(Chan()), p, r);
        std::printf("    sync=%d frames=%d match=%.0f text=%s\n", sync, r.nframes, r.dec.match, r.dec.text.c_str());
        CHECK(r.signal);
        CHECK(r.nframes == REPS);
        CHECK(r.dec.text == NAME);
        CHECK(r.dec.match >= 99.0);
    }
}

static void frequency_offset_is_estimated() {
    Params p; Result r;
    process(make_snapshot(Chan()), p, r);
    std::printf("    cfo=%.0f coarse=%.0f fine=%.0f\n", r.cfo_hz, r.coarse_hz, r.fine_hz);
    CHECK(std::abs(r.cfo_hz - 3155.0) < 150.0);
}

static void decoding_does_not_depend_on_amplitude() {
    for (double amp : {60.0, 400.0, 6000.0}) {
        Chan c; c.amp = amp; c.sigma = amp * 0.0035;
        Params p; Result r;
        process(make_snapshot(c), p, r);
        std::printf("    amp=%.0f match=%.0f text=%s\n", amp, r.dec.match, r.dec.text.c_str());
        CHECK(r.dec.text == NAME);
    }
}

static void marker_removes_phase_ambiguity() {
    for (double ph : {0.0, M_PI / 2, M_PI, 3 * M_PI / 2, 1.0, 2.5}) {
        Chan c; c.phase = ph;
        Params p; Result r;
        process(make_snapshot(c), p, r);
        std::printf("    phase=%.2f text=%s\n", ph, r.dec.text.c_str());
        CHECK(r.dec.text == NAME);
    }
}

static void works_without_carrier_correction_when_offset_is_zero() {
    Chan c; c.cfo = 0;
    Params p; p.comp_cfo = false;
    Result r;
    process(make_snapshot(c), p, r);
    CHECK(r.dec.text == NAME);
}

static void noise_only_gives_no_signal() {
    Chan c; c.tx = false;
    Params p; Result r;
    process(make_snapshot(c), p, r);
    CHECK(!r.signal);
    CHECK(r.nframes == 0);
}

static void data_symbols_never_look_like_a_marker() {
    Params p; Result r;
    process(make_snapshot(Chan()), p, r);
    CHECK(r.nframes == REPS);
    for (size_t k = 0; k < r.corr.size(); k++) {
        bool near = false;
        for (int m : r.mark_pos) near = near || std::abs((int)k - m) <= 1;
        if (!near) CHECK(r.corr[k] < p.mark_thr);
    }
}

static void first_strobe_gives_zero_error() {
    int i0, i1;
    auto y = qpsk_burst(60, 5, 60, i0, i1);
    TimingPLL pll; SyncOut so;
    pll.run(y, i0, i1, SPS, TED_MM, 0.02, 0.707, 1.0, 2, so);
    CHECK(!so.err.empty());
    if (!so.err.empty()) CHECK(so.err[0] == 0.0f);
}

static void fast_loop_does_not_skip_strobes() {
    int i0, i1;
    auto y = qpsk_burst(180, 7, 60, i0, i1);
    for (int ted : {TED_ZC, TED_GARDNER, TED_MM}) {
        TimingPLL pll; SyncOut so;
        pll.run(y, i0, i1, SPS, ted, 0.4, 0.707, 1.0, 3, so);
        pll.run(y, i0, i1, SPS, ted, 0.4, 0.707, 1.0, 3, so);
        std::printf("    ted=%d symbols=%zu stuffed=%d\n", ted, so.s.size(), so.stuffed);
        CHECK(so.stuffed <= 5);
    }
}

struct Case { const char *name; void (*fn)(); };

int main() {
    init_tx();
    const Case cases[] = {
        {"barker_sidelobes_are_at_most_one", barker_sidelobes_are_at_most_one},
        {"packet_layout_fits_in_one_tti", packet_layout_fits_in_one_tti},
        {"decodes_name_with_every_timing_detector", decodes_name_with_every_timing_detector},
        {"frequency_offset_is_estimated", frequency_offset_is_estimated},
        {"decoding_does_not_depend_on_amplitude", decoding_does_not_depend_on_amplitude},
        {"marker_removes_phase_ambiguity", marker_removes_phase_ambiguity},
        {"works_without_carrier_correction_when_offset_is_zero", works_without_carrier_correction_when_offset_is_zero},
        {"noise_only_gives_no_signal", noise_only_gives_no_signal},
        {"data_symbols_never_look_like_a_marker", data_symbols_never_look_like_a_marker},
        {"first_strobe_gives_zero_error", first_strobe_gives_zero_error},
        {"fast_loop_does_not_skip_strobes", fast_loop_does_not_skip_strobes},
    };
    int bad = 0;
    for (const Case &c : cases) {
        const int before = g_fail;
        std::printf("[ RUN  ] %s\n", c.name);
        c.fn();
        const bool ok = g_fail == before;
        bad += !ok;
        std::printf("[ %s ] %s\n", ok ? " OK " : "FAIL", c.name);
    }
    std::printf("\n%zu cases, %d failed\n", sizeof(cases) / sizeof(cases[0]), bad);
    return bad ? 1 : 0;
}
