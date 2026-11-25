#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>
using cplx = std::complex<float>;
using cd = std::complex<double>;
static constexpr int TTI = 1920;
static constexpr int SPS = 10;
static constexpr double FS = 1e6;
static const char *const NAME = "TIMUR";
static constexpr int NSYM = 20;
static constexpr int NMARK = 13;
static constexpr int NPKT = NMARK + NSYM;
static constexpr int GUARD = 3;
static constexpr int PKT = NPKT * SPS;
static constexpr int REPS = (TTI / SPS - 2 * GUARD) / NPKT;
static constexpr int BURST_LEN = (REPS * NPKT + 2 * GUARD) * SPS;
static constexpr int BARKER13[NMARK] = {1, 1, 1, 1, 1, -1, -1, 1, 1, -1, 1, -1, 1};
static constexpr float TX_AMP = 1500.f;
static constexpr int TX_SHIFT = 4;
static constexpr size_t RING = 64 * TTI;
static constexpr size_t SNAP = 6 * TTI;
static constexpr int CLIP_LEVEL = 2040;
static constexpr int TED_GARDNER_DEFAULT = 2;
struct TxData {
    std::vector<cplx> wave;
    std::vector<int16_t> pk;
    std::vector<float> idx, ti, tq, tm;
};
extern TxData g_tx;
struct Shared {
    std::mutex m;
    std::vector<int16_t> ring = std::vector<int16_t>(2 * RING, 0);
    uint64_t total = 0;
    std::string err, rx_uri, tx_uri, note;
    std::vector<std::string> log;
    std::atomic<int> state{0};
    std::atomic<double> gain_req{40.0}, tx_gain_req{30.0};
    std::atomic<bool> stop{false}, rec_req{false}, tx_req{false}, tx_alive{false};
};
struct Config {
    bool sim = false;
    std::string rx_uri, tx_uri;
    bool swap = false;
};
struct Params {
    bool remove_dc = true;
    bool comp_cfo = true;
    bool auto_off = true;
    int offset = 0;
    int sync = TED_GARDNER_DEFAULT;
    double bloop = 0.02, zeta = 0.707, gd = 1.0;
    double cfo_bloop = 0.01, cfo_zeta = 1.3;
    double mark_thr = 0.7;
};
struct Decoded {
    double match = 0;
    int nerr = 0, n = 0, rot = 0, shift = 0;
    std::string text;
};
struct Result {
    bool signal = false;
    double energy[SPS] = {0};
    int best = 0, nbursts = 0, nsym = 0;
    double dc_re = 0, dc_im = 0, cfo_hz = 0, coarse_hz = 0, fine_hz = 0, phase_deg = 0, evm = -1, peak = 0, clip = 0;
    Decoded dec;
    std::vector<float> idx, ti, tq, tm;
    std::vector<float> px, py;
    std::vector<float> mfi, mfq;
    std::vector<float> sx, sy;
    std::vector<float> ted_err, mu;
    std::vector<double> edges;
    std::vector<float> corr;
    std::vector<int> mark_pos;
    int nframes = 0;
    double mark_best = 0, mark_deg = 0;
};
void init_tx();
uint64_t snapshot(Shared &S, std::vector<cplx> &x);
void process(const std::vector<cplx> &x, const Params &p, Result &r);
void session_thread(Shared &S, Config cfg);
struct CostasOut {
    std::vector<cplx> s;
    double freq_hz = 0, phase_deg = 0;
};
struct CostasPLL {
    void run(const std::vector<cplx> &in, double bloop, double zeta, CostasOut &o) {
        const double M = 1, K = 2;
        const double th = bloop / (M * (zeta + 0.25 / zeta)), D = 1 + 2 * zeta * th + th * th;
        const double gp = 4 * zeta * th / (D * M * K);
        const double gi = 4 * th * th / (D * M * K);
        o.s.assign(in.size(), cplx(0, 0));
        double lf = 0, acc = 0, ph = 0, prev_ph = 0, dsum = 0; int dn = 0;
        for (size_t k = 0; k < in.size(); k++) {
            cplx y = in[k] * cplx((float)std::cos(ph), (float)std::sin(ph));
            o.s[k] = y;
            double e = (y.real() >= 0 ? 1 : -1) * y.imag() - (y.imag() >= 0 ? 1 : -1) * y.real();
            lf += gi * e;
            acc += gp * e + lf;
            double np = -acc;
            if (k) { double d = np - prev_ph; while (d > M_PI) d -= 2 * M_PI; while (d < -M_PI) d += 2 * M_PI; dsum += d; dn++; }
            prev_ph = np; ph = np;
        }
        o.freq_hz = dn ? -(dsum / dn) / (2 * M_PI) * FS / SPS : 0;
        o.phase_deg = ph * 180.0 / M_PI;
    }
};
