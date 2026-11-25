#pragma once
#include "backend.h"
enum Ted { TED_ZC = 0, TED_GARDNER = 1, TED_MM = 2 };
struct SyncOut {
    std::vector<cplx> s;
    std::vector<int> pos;
    std::vector<float> err, mu;
    int stuffed = 0;
};

#ifndef TIMING_GAP_MIN
#define TIMING_GAP_MIN(N) ((N) - 1)
#endif
struct TimingPLL {
    double counter = 0, integ = 0, mu = 0;
    bool trig = false;
    void run(const std::vector<cplx> &y, int i0, int i1, int N, int ted, double bloop, double zeta, double gd, int off, SyncOut &o) {
        const float a = 0.5f;
        const double th = bloop / (N * (zeta + 0.25 / zeta)), D = 1 + 2 * zeta * th + th * th;
        const double kp = -4 * zeta * th / (gd * N * D), ki = -4 * th * th / (gd * N * D);
        auto sg = [](float v) { return v < 0 ? -1.f : 1.f; };
        double pw = 0;
        for (int i = i0; i < i1; i++) pw += std::norm(y[i]);
        const float scale = pw > 0 ? (float)std::sqrt((double)(i1 - i0) / pw) : 1.f;
        std::vector<cplx> hist(N + 1, cplx(0, 0));
        cplx prev(0, 0);
        bool has_prev = false;
        const int gap_min = std::max(2, (int)TIMING_GAP_MIN(N));
        int since = N;
        o = SyncOut();
        counter = (off + 1.5) / N; mu = 0; trig = false;
        for (int n = i0; n < i1; n++) {
            auto Y = [&](int k) { return y[std::max(0, n - k)]; };
            const float m = (float)mu, m2 = m * m;
            cplx out = (a * m2 - a * m) * Y(0) + (-a * m2 + (1 + a) * m) * Y(1) +
                       (-a * m2 - (1 - a) * m + 1) * Y(2) + (a * m2 - a * m) * Y(3);
            std::rotate(hist.begin(), hist.begin() + 1, hist.end());
            hist.back() = out * scale;
            double e = 0;
            if (trig) {
                const cplx raw = out;
                out *= scale;
                if (since >= gap_min) {
                    const cplx mid = hist[hist.size() - 1 - N / 2];
                    if (!has_prev) e = 0;
                    else if (ted == TED_GARDNER)
                        e = mid.real() * (prev.real() - out.real()) + mid.imag() * (prev.imag() - out.imag());
                    else if (ted == TED_ZC)
                        e = mid.real() * (sg(prev.real()) - sg(out.real())) + mid.imag() * (sg(prev.imag()) - sg(out.imag()));
                    else
                        e = out.real() * sg(prev.real()) - prev.real() * sg(out.real()) +
                            out.imag() * sg(prev.imag()) - prev.imag() * sg(out.imag());
                    prev = out; has_prev = true;
                } else {
                    o.stuffed++;
                }
                since = 0;
                o.s.push_back(raw); o.pos.push_back(n - 2);
                o.err.push_back((float)e); o.mu.push_back((float)mu);
            }
            since++;
            const double g = kp * e + integ;
            integ += ki * e;
            const double d = std::clamp(g + 1.0 / N, 0.5 / N, 2.0 / N);
            trig = counter < d;
            if (trig) mu = counter / d;
            counter -= d;
            if (counter < 0) counter += 1;
        }
    }
};
