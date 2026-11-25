#include "backend.h"
#include "timing.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
TxData g_tx;
static std::vector<cplx> g_ref;
static std::vector<cplx> build_packet() {
    std::vector<cplx> pkt;
    const float k = 0.70710678f;
    for (int i = 0; i < NMARK; i++) {
        cplx s(BARKER13[i] * k, BARKER13[i] * k);
        for (int j = 0; j < SPS; j++) pkt.push_back(s);
    }
    for (const char *c = NAME; *c; c++)
        for (int bt = 7; bt >= 1; bt -= 2) {
            int b0 = ((unsigned char)*c >> bt) & 1, b1 = ((unsigned char)*c >> (bt - 1)) & 1;
            cplx s(b0 ? -k : k, b1 ? -k : k);
            for (int j = 0; j < SPS; j++) pkt.push_back(s);
        }
    return pkt;
}
void init_tx() {
    std::vector<cplx> pkt = build_packet();
    g_ref.clear();
    for (int k = 0; k < NSYM; k++) g_ref.push_back(pkt[(NMARK + k) * SPS]);
    g_tx.wave.assign(2 * TTI, cplx(0, 0));
    int at = 0;
    for (int i = (NPKT - GUARD) * SPS; i < PKT; i++) g_tx.wave[at++] = pkt[i];
    for (int r = 0; r < REPS; r++) { std::copy(pkt.begin(), pkt.end(), g_tx.wave.begin() + at); at += PKT; }
    for (int i = 0; i < GUARD * SPS; i++) g_tx.wave[at++] = pkt[i];
    auto q = [](float v) { long t = std::lround(v * TX_AMP) * (1L << TX_SHIFT); return (int16_t)std::clamp(t, -32768L, 32767L); };
    g_tx.pk.assign(2 * TTI, 0);
    for (int i = 0; i < TTI; i++) { g_tx.pk[2 * i] = q(g_tx.wave[i].real()); g_tx.pk[2 * i + 1] = q(g_tx.wave[i].imag()); }
    int n = 2 * TTI;
    g_tx.idx.resize(n); g_tx.ti.resize(n); g_tx.tq.resize(n); g_tx.tm.resize(n);
    for (int i = 0; i < n; i++) { g_tx.idx[i] = (float)i; g_tx.ti[i] = g_tx.wave[i].real(); g_tx.tq[i] = g_tx.wave[i].imag(); g_tx.tm[i] = std::abs(g_tx.wave[i]); }
}
static void set_err(Shared &S, const std::string &msg) { std::lock_guard<std::mutex> lk(S.m); S.err = msg; S.state = 3; }
static void push_chunk(Shared &S, const int16_t *iq, int n) {
    std::lock_guard<std::mutex> lk(S.m);
    for (int i = 0; i < n; i++) { size_t pos = (size_t)((S.total + i) % RING); S.ring[2 * pos] = iq[2 * i]; S.ring[2 * pos + 1] = iq[2 * i + 1]; }
    S.total += n;
}
uint64_t snapshot(Shared &S, std::vector<cplx> &x) {
    std::lock_guard<std::mutex> lk(S.m);
    uint64_t cnt = std::min<uint64_t>(SNAP, S.total);
    uint64_t start = S.total - cnt;
    x.resize(cnt);
    for (uint64_t i = 0; i < cnt; i++) { size_t pos = (size_t)((start + i) % RING); x[i] = cplx(S.ring[2 * pos], S.ring[2 * pos + 1]); }
    return start;
}
static void record_step(Shared &S, FILE *&f, const int16_t *iq, int n) {
    bool want = S.rec_req.load();
    if (want && !f) f = fopen("rx_gui_rec.pcm", "wb");
    if (!want && f) { fclose(f); f = nullptr; }
    if (f) fwrite(iq, sizeof(int16_t), (size_t)2 * n, f);
}
struct Proc { pid_t pid = -1; int log_fd = -1; std::string role; bool active = false, ready = false, dead = false; int status = 0; std::string linebuf; std::deque<std::string> tail; };
static void close_fd(int &fd) { if (fd >= 0) { close(fd); fd = -1; } }
static std::string backend_path() {
    if (const char *e = getenv("PLUTO_MAIN")) return e;
    char buf[4096]; ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = 0; std::string p(buf); size_t k = p.rfind('/'); if (k != std::string::npos) return p.substr(0, k + 1) + "main"; }
    return "./main";
}
static bool spawn_proc(Proc &p, const std::string &dir, const std::vector<std::string> &args, const char *role, std::string &why) {
    std::vector<char *> argv;
    for (const auto &a : args) argv.push_back(const_cast<char *>(a.c_str()));
    argv.push_back(nullptr);
    int lg[2];
    if (pipe2(lg, O_CLOEXEC) != 0) { why = std::string("pipe: ") + strerror(errno); return false; }
    pid_t pid = fork();
    if (pid < 0) { why = std::string("fork: ") + strerror(errno); close(lg[0]); close(lg[1]); return false; }
    if (pid == 0) {
        prctl(PR_SET_PDEATHSIG, SIGINT);
        if (chdir(dir.c_str()) != 0) _exit(126);
        int nul = open("/dev/null", O_RDONLY | O_CLOEXEC);
        dup2(nul, 0);
        if (nul > 2) close(nul);
        dup2(lg[1], 1); dup2(lg[1], 2);
        execv(argv[0], argv.data());
        _exit(127);
    }
    close(lg[1]);
    fcntl(lg[0], F_SETFL, fcntl(lg[0], F_GETFL) | O_NONBLOCK);
    p = Proc(); p.pid = pid; p.log_fd = lg[0]; p.role = role; p.active = true;
    return true;
}
static void proc_read_log(Proc &p) {
    if (p.log_fd < 0) return;
    char tmp[1024];
    for (int it = 0; it < 16; it++) {
        ssize_t n = read(p.log_fd, tmp, sizeof(tmp));
        if (n <= 0) { if (n == 0) close_fd(p.log_fd); return; }
        for (ssize_t i = 0; i < n; i++) {
            if (tmp[i] != '\n') { if (p.linebuf.size() < 400) p.linebuf.push_back(tmp[i]); continue; }
            if (p.linebuf.find("Start Full-Duplex Loop") != std::string::npos) p.ready = true;
            if (!p.linebuf.empty()) { p.tail.push_back(p.linebuf); if (p.tail.size() > 8) p.tail.pop_front(); }
            p.linebuf.clear();
        }
    }
}
static void proc_poll_exit(Proc &p) { if (!p.active || p.dead) return; int st; if (waitpid(p.pid, &st, WNOHANG) == p.pid) { p.dead = true; p.status = st; } }
static std::string describe_exit(const Proc &p) {
    std::string s = p.role + ": процесс main завершился";
    if (WIFEXITED(p.status)) { int c = WEXITSTATUS(p.status); s += c == 127 ? " (не удалось запустить)" : " с кодом " + std::to_string(c); }
    else if (WIFSIGNALED(p.status)) s += " по сигналу " + std::to_string(WTERMSIG(p.status));
    size_t from = p.tail.size() > 3 ? p.tail.size() - 3 : 0;
    for (size_t i = from; i < p.tail.size(); i++) s += (i == from ? ". " : " | ") + p.tail[i];
    return s;
}
static std::vector<std::string> find_plutos(std::string &err) {
    std::vector<std::string> uris;
    FILE *f = popen("SoapySDRUtil --find=\"driver=plutosdr\" 2>/dev/null", "r");
    if (!f) { err = "не удалось запустить SoapySDRUtil"; return uris; }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        const char *k = strstr(line, "uri = "); if (!k) continue;
        std::string u(k + 6);
        while (!u.empty() && (u.back() == '\n' || u.back() == '\r' || u.back() == ' ')) u.pop_back();
        if (!u.empty()) uris.push_back(u);
    }
    pclose(f);
    std::sort(uris.begin(), uris.end());
    uris.erase(std::unique(uris.begin(), uris.end()), uris.end());
    return uris;
}
static bool write_tx_file(const std::string &path) {
    FILE *f = fopen(path.c_str(), "wb"); if (!f) return false;
    size_t n = g_tx.pk.size();
    bool ok = fwrite(g_tx.pk.data(), sizeof(int16_t), n, f) == n;
    return fclose(f) == 0 && ok;
}
static std::string fmt1(double v) { char b[32]; snprintf(b, sizeof(b), "%.1f", v); return b; }
static void sim_loop(Shared &S) {
    std::mt19937 rng(1); std::normal_distribution<float> nd(0.f, 1.f);
    std::vector<int16_t> buf(2 * TTI);
    uint64_t n = 0, k = 0; const double cfo = 3155.0;
    FILE *rec = nullptr;
    auto next = std::chrono::steady_clock::now();
    while (!S.stop) {
        double g = S.gain_req.load(), gt = S.tx_gain_req.load();
        bool on = S.tx_req.load(); S.tx_alive = on;
        float amp = 40.f * (float)std::pow(10.0, (g - 20.0) / 20.0) * (float)std::pow(10.0, (gt - 30.0) / 20.0);
        float sigma = 0.5f * (float)std::pow(10.0, (g - 20.0) / 45.0);
        for (int i = 0; i < TTI; i++, n++) {
            cplx s = (on && k % 2 == 0) ? g_tx.wave[i] * amp : cplx(0, 0);
            double ph = 2 * M_PI * cfo * (double)n / FS + 0.5;
            s *= cplx((float)std::cos(ph), (float)std::sin(ph));
            s += cplx(-5.7f, -6.6f) + sigma * cplx(nd(rng), nd(rng));
            buf[2 * i] = (int16_t)std::clamp(std::lround(s.real()), -2048L, 2047L);
            buf[2 * i + 1] = (int16_t)std::clamp(std::lround(s.imag()), -2048L, 2047L);
        }
        k++;
        push_chunk(S, buf.data(), TTI); record_step(S, rec, buf.data(), TTI);
        next += std::chrono::microseconds(TTI);
        std::this_thread::sleep_until(next);
    }
    if (rec) fclose(rec);
    S.tx_alive = false;
}
void session_thread(Shared &S, Config cfg) {
    if (cfg.sim) { { std::lock_guard<std::mutex> lk(S.m); S.note = "симуляция"; } S.state = 2; sim_loop(S); return; }
    const std::string be = backend_path();
    if (access(be.c_str(), X_OK) != 0) { set_err(S, "Не найден main: " + be + ". Соберите его или задайте путь в переменной PLUTO_MAIN."); return; }
    std::string rx_uri = cfg.rx_uri, tx_uri = cfg.tx_uri, note;
    if (rx_uri.empty()) {
        std::string lerr; auto uris = find_plutos(lerr);
        if (S.stop) return;
        if (uris.empty()) { set_err(S, "Платы Pluto не найдены (поиск через SoapySDRUtil). Проверьте USB-кабель и права доступа (udev)." + (lerr.empty() ? std::string() : " " + lerr)); return; }
        rx_uri = uris[cfg.swap && uris.size() > 1 ? 1 : 0];
        if (uris.size() >= 2) { tx_uri = uris[cfg.swap ? 0 : 1]; note = "найдено плат: " + std::to_string(uris.size()); if (uris.size() > 2) note += " (используются первые две)"; }
        else note = "найдена одна плата: работает только приём";
    } else if (tx_uri.empty()) note = "задана одна плата: работает только приём";
    { std::lock_guard<std::mutex> lk(S.m); S.rx_uri = rx_uri; S.tx_uri = tx_uri; S.note = note; }
    char tpl[] = "/tmp/pluto_gui_XXXXXX";
    if (!mkdtemp(tpl)) { set_err(S, "Не удалось создать рабочий каталог в /tmp"); return; }
    const std::string dir = tpl;
    const std::string f_tx = dir + "/tx_name.pcm", f_rx = dir + "/rx_from_800.pcm", f_tx_rx = dir + "/rx_from_840.pcm";
    int rx_fifo = -1, tx_fifo = -1;
    auto cleanup_dir = [&]() { close_fd(rx_fifo); close_fd(tx_fifo); unlink(f_tx.c_str()); unlink(f_rx.c_str()); unlink(f_tx_rx.c_str()); rmdir(dir.c_str()); };
    if (!write_tx_file(f_tx) || mkfifo(f_rx.c_str(), 0600) != 0 || mkfifo(f_tx_rx.c_str(), 0600) != 0) { set_err(S, std::string("Не удалось подготовить файлы обмена с main: ") + strerror(errno)); cleanup_dir(); return; }
    rx_fifo = open(f_rx.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    tx_fifo = open(f_tx_rx.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (rx_fifo < 0 || tx_fifo < 0) { set_err(S, std::string("Не удалось открыть канал обмена с main: ") + strerror(errno)); cleanup_dir(); return; }
    fcntl(rx_fifo, F_SETPIPE_SZ, 1 << 20);
    fcntl(tx_fifo, F_SETPIPE_SZ, 1 << 20);
    const bool has_tx = !tx_uri.empty();
    Proc rx, tx;
    std::vector<int16_t> sbuf(2 * 16384); size_t have = 0; FILE *rec = nullptr;
    auto pump = [&]() {
        proc_read_log(rx); proc_read_log(tx);
        char junk[4096]; while (read(tx_fifo, junk, sizeof(junk)) > 0) {}
        for (int it = 0; it < 16; it++) {
            ssize_t n = read(rx_fifo, (uint8_t *)sbuf.data() + have, sbuf.size() * sizeof(int16_t) - have);
            if (n <= 0) break;
            have += (size_t)n;
            size_t frames = have / 4; if (!frames) continue;
            push_chunk(S, sbuf.data(), (int)frames); record_step(S, rec, sbuf.data(), (int)frames);
            size_t used = frames * 4;
            memmove(sbuf.data(), (uint8_t *)sbuf.data() + used, have - used); have -= used;
        }
        proc_poll_exit(rx); proc_poll_exit(tx);
    };
    auto halt = [&](Proc &p) {
        if (!p.active) return;
        kill(p.pid, SIGINT);
        for (int phase = 0; phase < 3 && !p.dead; phase++) {
            for (int i = 0; i < (phase == 0 ? 100 : 40) && !p.dead; i++) { pump(); if (!p.dead) usleep(50000); }
            if (!p.dead) kill(p.pid, phase == 0 ? SIGTERM : SIGKILL);
        }
        if (!p.dead) { int st; waitpid(p.pid, &st, 0); p.dead = true; }
        close_fd(p.log_fd); p.active = false;
    };
    auto launch_rx = [&](double g, std::string &why) { for (int t = 0; t < 3; t++) { if (spawn_proc(rx, dir, {be, "rx", rx_uri, "0", fmt1(g)}, "приём", why)) return true; usleep(200000); } return false; };
    auto launch_tx = [&](double tg, std::string &why) { for (int t = 0; t < 3; t++) { if (spawn_proc(tx, dir, {be, "tx", tx_uri, fmt1(tg), "30"}, "передача", why)) return true; usleep(200000); } return false; };
    double g_run = S.gain_req.load(), tg_run = S.tx_gain_req.load();
    std::string fail, why;
    if (!launch_rx(g_run, why) || (has_tx && !launch_tx(tg_run, why))) fail = "Не удалось запустить main: " + why;
    const auto t0 = std::chrono::steady_clock::now();
    bool all_ready = false;
    while (!S.stop && fail.empty()) {
        pump();
        double g = S.gain_req.load(), tg = S.tx_gain_req.load();
        bool txr = S.tx_req.load();
        if (rx.active && g != g_run) { halt(rx); g_run = g; if (!S.stop && !launch_rx(g_run, why)) fail = "Не удалось запустить main: " + why; }
        if (has_tx && fail.empty() && !S.stop) {
            if (tx.active && (!txr || tg != tg_run)) halt(tx);
            if (!tx.active && txr) { tg_run = tg; if (!launch_tx(tg_run, why)) fail = "Не удалось запустить main: " + why; }
        }
        for (Proc *p : {&rx, &tx}) if (p->active && p->dead && fail.empty()) fail = describe_exit(*p);
        if (!all_ready) {
            if (rx.ready && (!has_tx || tx.ready || !S.tx_req)) { all_ready = true; S.state = 2; }
            else if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(30) && fail.empty()) {
                fail = "main не запустился за 30 с";
                for (Proc *p : {&rx, &tx}) if (p->active && !p->ready) { fail += ". " + describe_exit(*p); break; }
            }
        }
        S.tx_alive = has_tx && tx.active && tx.ready && txr;
        { std::lock_guard<std::mutex> lk(S.m); S.log.clear();
          for (const Proc *p : {&rx, &tx}) for (size_t i = p->tail.size() > 3 ? p->tail.size() - 3 : 0; i < p->tail.size(); i++) S.log.push_back(p->role + ": " + p->tail[i]); }
        pollfd pf = {rx_fifo, POLLIN, 0}; poll(&pf, 1, 5);
    }
    if (!fail.empty() && !S.stop) set_err(S, fail);
    halt(rx); halt(tx);
    if (rec) fclose(rec);
    cleanup_dir();
    S.tx_alive = false;
}
static std::vector<cplx> movavg(const std::vector<cplx> &x, int L) {
    int N = (int)x.size();
    std::vector<cd> ps(N + 1, cd(0, 0));
    for (int i = 0; i < N; i++) ps[i + 1] = ps[i] + cd(x[i]);
    std::vector<cplx> y(N);
    for (int n = 0; n < N; n++) { int a = std::max(0, n - L / 2), b = std::min(N, n - L / 2 + L); y[n] = cplx(ps[b] - ps[a]) / (float)(b - a); }
    return y;
}
static void fftx(std::vector<cd> &a) {
    int n = (int)a.size();
    for (int i = 1, j = 0; i < n; i++) { int bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2 * M_PI / len; cd wl(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) { cd w(1, 0); for (int k = 0; k < len / 2; k++) { cd u = a[i + k], v = a[i + k + len / 2] * w; a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl; } }
    }
}
static double coarse_cfo(const std::vector<cplx> &xd, int a, int b) {
    int L = b - a, N = 1;
    while (N < 2 * L && N < 8192) N <<= 1;
    std::vector<cd> z(N, cd(0, 0));
    for (int i = 0; i < L && i < N; i++) { cd q = cd(xd[a + i]); q *= q; q *= q; z[i] = q; }
    fftx(z);
    int best = 0; double bm = -1;
    for (int i = 0; i < N; i++) { double m = std::abs(z[i]); if (m > bm) { bm = m; best = i; } }
    int idx = best > N / 2 ? best - N : best;
    return idx * (FS / N) / 4.0;
}
struct MarkerResult {
    std::vector<float> rho;
    std::vector<cplx> c;
    std::vector<int> pos;
    double best = 0, phase = 0;
};
static MarkerResult find_markers(const std::vector<cplx> &s, double thr) {
    MarkerResult m;
    const int n = (int)s.size(), K = n - NMARK + 1;
    if (K <= 0) return m;
    m.rho.assign(K, 0.f);
    m.c.assign(K, cplx(0, 0));
    std::vector<double> pe(n + 1, 0.0);
    for (int i = 0; i < n; i++) pe[i + 1] = pe[i] + std::norm(s[i]);
    const float h = 0.70710678f;
    for (int k = 0; k < K; k++) {
        cplx acc(0, 0);
        for (int i = 0; i < NMARK; i++) acc += s[k + i] * cplx(BARKER13[i] * h, -BARKER13[i] * h);
        const double e = pe[k + NMARK] - pe[k];
        m.c[k] = acc;
        m.rho[k] = e > 0 ? (float)(std::abs(acc) / std::sqrt(e * NMARK)) : 0.f;
    }
    std::vector<int> cand;
    for (int k = 0; k < K; k++) {
        if (m.rho[k] < thr || k + NPKT > n) continue;
        bool top = true;
        for (int j = std::max(0, k - NPKT / 2); j <= std::min(K - 1, k + NPKT / 2) && top; j++)
            if (m.rho[j] > m.rho[k] || (m.rho[j] == m.rho[k] && j < k)) top = false;
        if (top) cand.push_back(k);
    }
    if (cand.empty()) return m;
    int p0 = cand[0];
    for (int k : cand) if (m.rho[k] > m.rho[p0]) p0 = k;
    for (int k : cand) {
        int d = ((k - p0) % NPKT + NPKT) % NPKT;
        if (d > NPKT / 2) d -= NPKT;
        if (std::abs(d) <= 1) m.pos.push_back(k);
    }
    cplx sum(0, 0);
    for (int k : m.pos) { sum += m.c[k]; m.best = std::max<double>(m.best, m.rho[k]); }
    m.phase = std::arg(sum);
    return m;
}
static Decoded decode_frames(const std::vector<cplx> &s, const MarkerResult &mk) {
    Decoded d;
    const int m = (int)g_ref.size();
    std::vector<int> neg0(m, 0), neg1(m, 0);
    int ok = 0, tot = 0, fr = 0;
    for (int k : mk.pos) {
        const float a = std::abs(mk.c[k]);
        if (a <= 0) continue;
        const cplx rot = std::conj(mk.c[k]) / a;
        fr++;
        for (int i = 0; i < m; i++) {
            const cplx v = s[k + NMARK + i] * rot;
            const bool n0 = v.real() < 0, n1 = v.imag() < 0;
            neg0[i] += n0; neg1[i] += n1;
            ok += (n0 == (g_ref[i].real() < 0) && n1 == (g_ref[i].imag() < 0));
            tot++;
        }
    }
    if (!fr) return d;
    std::vector<int> bits(2 * m);
    for (int i = 0; i < m; i++) { bits[2 * i] = neg0[i] * 2 > fr; bits[2 * i + 1] = neg1[i] * 2 > fr; }
    for (int i = 0; i + 8 <= 2 * m; i += 8) { int v = 0; for (int j = 0; j < 8; j++) v = (v << 1) | bits[i + j]; d.text += (v >= 32 && v < 127) ? (char)v : '?'; }
    d.match = 100.0 * ok / tot; d.nerr = tot - ok; d.n = tot;
    return d;
}
void process(const std::vector<cplx> &x, const Params &p, Result &r) {
    const int N = (int)x.size();
    r.signal = false; r.px.clear(); r.py.clear(); r.edges.clear();
    r.sx.clear(); r.sy.clear(); r.ted_err.clear(); r.mu.clear();
    r.corr.clear(); r.mark_pos.clear(); r.nframes = 0; r.mark_best = 0; r.mark_deg = 0;
    r.evm = -1; r.cfo_hz = 0; r.coarse_hz = 0; r.fine_hz = 0; r.phase_deg = 0; r.nbursts = 0; r.nsym = 0; r.dec = Decoded();
    for (int o = 0; o < SPS; o++) r.energy[o] = 0;
    r.idx.resize(N); r.ti.resize(N); r.tq.resize(N); r.tm.resize(N);
    float dcI = 0, dcQ = 0;
    if (N > 0) {
        std::vector<float> t(N);
        for (int i = 0; i < N; i++) t[i] = x[i].real();
        std::nth_element(t.begin(), t.begin() + N / 2, t.end()); dcI = t[N / 2];
        for (int i = 0; i < N; i++) t[i] = x[i].imag();
        std::nth_element(t.begin(), t.begin() + N / 2, t.end()); dcQ = t[N / 2];
    }
    const cplx dc0(dcI, dcQ);
    double pk = 0; int clipc = 0;
    std::vector<cplx> ab(N);
    for (int i = 0; i < N; i++) {
        r.idx[i] = (float)i;
        r.ti[i] = x[i].real(); r.tq[i] = x[i].imag(); r.tm[i] = std::abs(x[i]);
        ab[i] = cplx(std::abs(x[i] - dc0), 0);
        double m = std::max(std::fabs(x[i].real()), std::fabs(x[i].imag()));
        pk = std::max(pk, m); if (m >= CLIP_LEVEL) clipc++;
    }
    r.peak = pk; r.clip = N ? 100.0 * clipc / N : 0;
    if (N < 2 * TTI) return;
    std::vector<cplx> envc = movavg(ab, 20);
    std::vector<float> env(N);
    for (int i = 0; i < N; i++) env[i] = envc[i].real();
    float emax = *std::max_element(env.begin(), env.end());
    std::vector<float> srt(env);
    std::nth_element(srt.begin(), srt.begin() + N / 10, srt.end());
    float floor_ = srt[N / 10];
    if (!(emax > 30.f && emax > 5.f * floor_)) return;
    std::vector<std::pair<int, int>> bursts;
    const float thr = 0.3f * emax;
    for (int i = 0; i < N;) {
        if (env[i] > thr) { int a = i; while (i < N && env[i] > thr) i++; int len = i - a; if (a >= 20 && i <= N - 20 && len >= BURST_LEN * 0.85 && len <= BURST_LEN * 1.15) bursts.push_back({a, i}); }
        else i++;
    }
    if (bursts.empty()) return;
    cd dc(0, 0); int nn = 0;
    for (int i = 0; i < N; i++) if (env[i] < 0.15f * emax) { dc += cd(x[i]); nn++; }
    if (p.remove_dc && nn >= 50) dc /= (double)nn; else dc = cd(0, 0);
    r.dc_re = dc.real(); r.dc_im = dc.imag();
    std::vector<cplx> xd(N);
    for (int i = 0; i < N; i++) xd[i] = x[i] - cplx(dc);
    double coarse = 0;
    if (p.comp_cfo) {
        for (const auto &bu : bursts) coarse += coarse_cfo(xd, bu.first + 3 * SPS, bu.second - 3 * SPS);
        coarse /= bursts.size();
        r.coarse_hz = coarse;
        for (int i = 0; i < N; i++) { double ph = -2.0 * M_PI * coarse * i / FS; xd[i] *= cplx((float)std::cos(ph), (float)std::sin(ph)); }
    }
    std::vector<cplx> y = movavg(xd, SPS);
    r.mfi.resize(N); r.mfq.resize(N);
    for (int i = 0; i < N; i++) { r.mfi[i] = y[i].real(); r.mfq[i] = y[i].imag(); }
    std::vector<cplx> all;
    double last_fine = 0;
    for (size_t bi = 0; bi < bursts.size(); bi++) {
        int a = bursts[bi].first, b = bursts[bi].second;
        r.edges.push_back(a); r.edges.push_back(b);
        bool last = (bi + 1 == bursts.size());
        int i0 = a + 3 * SPS, i1 = b - 3 * SPS;
        double es[SPS] = {0}; int ec[SPS] = {0};
        for (int i = i0; i < i1; i++) { int o = (i - i0) % SPS; es[o] += std::norm(y[i]); ec[o]++; }
        double en[SPS]; double emx = 0; int best = 0;
        for (int o = 0; o < SPS; o++) { en[o] = ec[o] ? es[o] / ec[o] : 0; if (en[o] > emx) { emx = en[o]; best = o; } }
        if (last) { for (int o = 0; o < SPS; o++) r.energy[o] = emx > 0 ? en[o] / emx : 0; r.best = best; }
        int off = p.auto_off ? best : std::clamp(p.offset, 0, SPS - 1);
        std::vector<cplx> s; std::vector<int> pos;
        if (p.sync == 0) {
            for (int i = i0 + off; i < i1; i += SPS) { s.push_back(y[i]); pos.push_back(i); }
        } else {
            SyncOut so;
            std::vector<cplx> yb = y;
            if (p.sync != 2) {
                cd m4(0, 0);
                for (int i = i0; i < i1; i++) { cd q = cd(y[i]) * cd(y[i]); m4 += q * q; }
                double phi = (std::arg(m4) - M_PI) / 4.0;
                cplx rot((float)std::cos(-phi), (float)std::sin(-phi));
                for (int i = i0; i < i1; i++) yb[i] *= rot;
            }
            TimingPLL pll;
            pll.run(yb, i0, i1, SPS, p.sync - 1, p.bloop, p.zeta, p.gd, off, so);
            pll.run(yb, i0, i1, SPS, p.sync - 1, p.bloop, p.zeta, p.gd, off, so);
            s = so.s; pos = so.pos;
            if (last) { r.ted_err = so.err; r.mu = so.mu; }
        }
        for (int q : pos) { r.sx.push_back((float)q); r.sy.push_back(y[q].real()); }
        if (s.size() < (size_t)NPKT) continue;
        std::vector<cplx> cor = s;
        double fine = 0, phase = 0;
        if (p.comp_cfo) {
            CostasOut co;
            CostasPLL cpll;
            std::vector<cplx> sn = s;
            double pw2 = 0;
            for (auto &v : sn) pw2 += std::norm(v);
            pw2 = std::sqrt(pw2 / sn.size());
            if (pw2 > 0) for (auto &v : sn) v /= (float)pw2;
            cpll.run(sn, p.cfo_bloop, p.cfo_zeta, co);
            cor = co.s; fine = co.freq_hz; phase = co.phase_deg;
            if (last) { r.fine_hz = fine; r.phase_deg = phase; }
        }
        MarkerResult mk = find_markers(cor, p.mark_thr);
        if (last) {
            r.dec = decode_frames(cor, mk);
            r.corr = mk.rho; r.mark_pos = mk.pos; r.nframes = (int)mk.pos.size();
            r.mark_best = mk.best; r.mark_deg = mk.phase * 180.0 / M_PI;
        }
        if (!mk.pos.empty()) {
            const cplx rot((float)std::cos(-mk.phase), (float)std::sin(-mk.phase));
            for (auto &v : cor) v *= rot;
        }
        last_fine = fine;
        all.insert(all.end(), cor.begin(), cor.end());
    }
    if (all.empty()) return;
    double pw = 0;
    for (auto &v : all) pw += std::norm(v);
    pw = std::sqrt(pw / all.size());
    if (pw <= 0) return;
    double e2 = 0;
    const float k = 0.70710678f;
    for (auto &v : all) {
        cplx u = v / (float)pw;
        r.px.push_back(u.real()); r.py.push_back(u.imag());
        cplx id(u.real() >= 0 ? k : -k, u.imag() >= 0 ? k : -k);
        e2 += std::norm(u - id);
    }
    if (p.comp_cfo) r.evm = 100.0 * std::sqrt(e2 / all.size());
    r.cfo_hz = r.coarse_hz + last_fine;
    r.nbursts = (int)bursts.size(); r.nsym = (int)all.size(); r.signal = true;
}
