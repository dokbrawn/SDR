import argparse
import sys
from collections import Counter

import numpy as np

import sdr_common as cm

FS = 1e6

DESC = ("Офлайн-приёмник записей .pcm: всплески, частотная и символьная синхронизация "
        "по преамбуле, согласованный фильтр, созвездие, биты, текст. "
        "Цепочка обработки описана в scripts/README.md, история отладки в CHANGELOG.md")

def find_bursts(z, smooth=256):
    env = np.abs(z)
    if env.size > smooth:
        env = np.convolve(env, np.ones(smooth) / smooth, mode="same")
    hi = np.percentile(env, 99.5)
    if hi < 8 * max(np.median(env), 1e-9):
        return [(0, len(z))]
    mask = env > 0.4 * hi
    bursts, start = [], None
    for i, m in enumerate(mask):
        if m and start is None:
            start = i
        elif not m and start is not None:
            if i - start >= 512:
                bursts.append((start, i))
            start = None
    if start is not None and len(mask) - start >= 512:
        bursts.append((start, len(mask)))
    return bursts or [(0, len(z))]

def corr_metric(zd, tmpl):
    cc = np.convolve(zd, tmpl.conj()[::-1], mode="valid")
    return cc, float(np.sort(np.abs(cc))[-4:].sum())


def freq_search(zb, tmpl):
    best_df, cc, m = 0.0, None, -1.0
    for df in np.arange(-16000, 16001, 400):
        c, mm = corr_metric(derotate(zb, df), tmpl)
        if mm > m:
            best_df, cc, m = df, c, mm
    for df in best_df + np.arange(-400, 401, 50):
        c, mm = corr_metric(derotate(zb, df), tmpl)
        if mm > m:
            best_df, cc, m = df, c, mm
    return best_df, cc


def corr_peaks(cc, min_dist):
    corr = np.nan_to_num(np.abs(cc))
    if corr.size == 0 or corr.max() <= 0:
        return []
    idx = np.where(corr > 0.5 * corr.max())[0]
    if idx.size == 0:
        return []
    groups, cur = [], [idx[0]]
    for a, b in zip(idx, idx[1:]):
        if b - a <= 2:
            cur.append(b)
        else:
            groups.append(cur)
            cur = [b]
    groups.append(cur)
    peaks = [int(g[int(np.argmax(corr[g]))]) for g in groups]
    out = []
    for p in peaks:
        if not out or p - out[-1] >= min_dist:
            out.append(p)
    return out

def derotate(zb, df):
    t = np.arange(zb.size)
    return zb * np.exp(-2j * np.pi * df * t / FS)

def frame_symbols(v, s0, sps, gain, theta):
    out, ns, miss = [], [], 0
    j = cm.PREABLE_LEN
    while True:
        n = s0 + j * sps + sps - 1
        if n >= v.size:
            break
        s = v[n] * np.exp(-1j * theta(n)) / gain
        if abs(s) < 0.25:
            miss += 1
            if miss > sps:
                break
            j += 1
            continue
        miss = 0
        out.append(s)
        ns.append(n)
        j += 1
    return np.array(out, dtype=np.complex64), np.array(ns)


def decode_burst(zb, sps):
    tmpl = cm.pulse_shape_rect(cm.upsample(cm.preamble_symbols(), sps), sps)
    df, cc = freq_search(zb, tmpl)
    starts = corr_peaks(cc, min_dist=cm.SILENCE_SAMPLES)
    if not starts:
        return df, [], np.array([], dtype=np.complex64)

    if len(starts) >= 2:
        ph = np.unwrap(np.angle(cc[starts]))
        per = np.diff(starts)
        lim = 0.45 * FS / float(np.mean(per))
        est = np.diff(ph) / (2 * np.pi) * FS / per
        delta = float(np.median(est)) if np.std(est) < 0.25 * lim else 0.0
        delta = ((delta + lim) % (2 * lim)) - lim
        cc2, m2 = corr_metric(derotate(zb, df + delta), tmpl)
        if m2 >= np.sort(np.abs(cc))[-4:].sum():
            df += delta
            cc = cc2
            starts = corr_peaks(cc, min_dist=cm.SILENCE_SAMPLES) or starts
    zd = derotate(zb, df)

    v = cm.matched_filter_rect(zd, sps)
    texts, symbols, slopes = [], [], []
    for s0 in starts:
        phi0 = np.angle(cc[s0])
        pre_n = [n for n in (s0 + j * sps + sps - 1 for j in range(cm.PREABLE_LEN)) if n < v.size]
        if not pre_n:
            continue
        gain = np.mean(np.abs(v[pre_n])) or 1.0

        syms, ns = frame_symbols(v, s0, sps, gain, lambda n: phi0)
        if syms.size == 0:
            continue
        ideal = np.array([cm.BITS_TO_SYM[k] for k in ((0, 0), (0, 1), (1, 1), (1, 0))])
        dec = ideal[np.argmin(np.abs(syms[:, None] - ideal[None, :]), axis=1)]
        res = np.unwrap(np.angle(syms * np.conj(dec)))
        slope, icept = np.polyfit(ns - s0, res, 1) if syms.size > 4 else (0.0, 0.0)
        slopes.append(slope * FS / (2 * np.pi))

        theta = lambda n: phi0 + icept + slope * (n - s0)
        syms2, _ = frame_symbols(v, s0, sps, gain, theta)
        if syms2.size == 0:
            continue
        symbols.extend(syms2)
        texts.append(cm.bits_to_name(cm.demodulate_qpsk(syms2)))
    if slopes:
        df += float(np.mean(slopes))
    return df, texts, np.array(symbols, dtype=np.complex64)

def process(path, sps=cm.SPS):
    z = cm.read_cs16(path)
    results = []
    for b0, b1 in find_bursts(z):
        df, texts, syms = decode_burst(z[b0:b1], sps)
        results.append(dict(span=(b0, b1), df=df, texts=texts, symbols=syms))
    return results

def cmd_decode(res):
    all_texts = []
    for r in res:
        print(f"всплеск {r['span'][0]}..{r['span'][1]}: df = {r['df']:+.1f} Гц, кадров: {len(r['texts'])}")
        for t in r["texts"]:
            print(f"   кадр: {t!r}")
            all_texts.append(t)
    if not all_texts:
        print("НИЧЕГО НЕ ДЕКОДИРОВАНО")
        return 1
    vote = Counter(all_texts).most_common(1)[0][0]
    print(f"\nИТОГ (голосование по {len(all_texts)} кадрам): {vote!r}")
    return 0

def cmd_constellation(res, save):
    import matplotlib
    if save:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    syms = np.concatenate([r["symbols"] for r in res]) if res else np.array([])
    if syms.size == 0:
        print("нет символов для созвездия")
        return 1
    fig, ax = plt.subplots(figsize=(8, 8))
    ax.scatter(syms.real, syms.imag, s=14, alpha=0.6, color="tab:blue", label="принятые символы")
    ideal = [cm.BITS_TO_SYM[k] for k in ((0, 0), (0, 1), (1, 1), (1, 0))]
    for i, s in enumerate(ideal):
        ax.plot(s.real, s.imag, "rx", markersize=16, mew=3, label="идеальный QPSK" if i == 0 else None)
    ax.axhline(0, color="k", ls="--", alpha=.3)
    ax.axvline(0, color="k", ls="--", alpha=.3)
    ax.set_aspect("equal")
    ax.grid(alpha=.4)
    ax.set_xlabel("I")
    ax.set_ylabel("Q")
    ax.set_title("Созвездие после синхронизации")
    ax.legend()
    if save:
        fig.savefig(save, dpi=150)
        print(f"сохранено: {save}")
    else:
        plt.show()
    return 0

def cmd_magnitude(path, res, save, sps):
    import matplotlib
    if save:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    if not res:
        return 1
    df = max(res, key=lambda x: x["span"][1] - x["span"][0])["df"]
    z = cm.read_cs16(path)
    b0, b1 = find_bursts(z)[0]
    zb = derotate(z[b0:b1], df)
    v = np.abs(cm.matched_filter_rect(zb, sps))
    k0 = int(np.argmax(v[:4000])) if v.size > 4000 else int(np.argmax(v))
    n0 = max(0, k0 - 200)
    seg = v[n0:n0 + 3000]
    fig, ax = plt.subplots(figsize=(14, 5))
    ax.plot(np.arange(seg.size), seg, lw=1.0, color="black", label="|согласованный фильтр|")
    for j in range(cm.PREABLE_LEN + 12):
        n = k0 + j * sps + sps - 1 - n0
        if 0 <= n < seg.size:
            color = "tab:green" if j < cm.PREABLE_LEN else "tab:red"
            ax.vlines(n, 0, seg[n], color=color, lw=1.2)
            ax.plot(n, seg[n], "o", color=color, ms=4)
    ax.plot([], [], "o", color="tab:green", label="отсчёты преамбулы")
    ax.plot([], [], "o", color="tab:red", label="символьные отсчёты данных")
    ax.set_xlabel("номер отсчёта внутри всплеска")
    ax.set_ylabel("|MF|")
    ax.set_title("Выбор символьных отсчётов после согласованного фильтра")
    ax.grid(alpha=.4)
    ax.legend()
    if save:
        fig.savefig(save, dpi=150)
        print(f"сохранено: {save}")
    else:
        plt.show()
    return 0

def cmd_selftest(save):
    rng = np.random.default_rng(42)
    text = "TIMUR"
    pre = cm.preamble_symbols()
    data = cm.modulate_qpsk(cm.name_to_bits(text))
    x = cm.pulse_shape_rect(cm.upsample(np.concatenate([pre, data]), cm.SPS), cm.SPS)
    frame = np.concatenate([np.zeros(cm.SILENCE_SAMPLES, dtype=np.complex64), x])
    stream = np.concatenate([frame, frame, frame])

    df, tau, phi, amp, sigma = 1700.0, 1234, 0.7, 2500.0, 60.0
    n = np.arange(tau + stream.size)
    z = np.zeros(n.size, dtype=np.complex64)
    z[tau:] = stream * amp * np.exp(1j * (2 * np.pi * df * n[tau:] / FS + phi))
    z += (rng.normal(0, sigma, n.size) + 1j * rng.normal(0, sigma, n.size)).astype(np.complex64)

    path = "/tmp/selftest_rx.pcm"
    cm.save_cs16(path, z, scale=1.0)
    res = process(path)
    texts = [t for r in res for t in r["texts"]]
    ok = text in texts and abs(res[0]["df"] - df) < 150
    print(f"самотест: df_оценка = {res[0]['df']:+.1f} Гц (истина {df:+.1f}), "
          f"кадров: {len(texts)}, тексты: {texts}")
    print("РЕЗУЛЬТАТ:", "ОК" if ok else "ОШИБКА")
    if save:
        cmd_constellation(res, save)
    return 0 if ok else 1

def main():
    p = argparse.ArgumentParser(description=DESC)
    p.add_argument("pcm", nargs="?", help="файл записи CS16")
    p.add_argument("cmd", nargs="?", choices=["decode", "constellation", "magnitude"], default="decode")
    p.add_argument("--sps", type=int, default=cm.SPS)
    p.add_argument("--save", help="куда сохранить рисунок вместо показа окна")
    p.add_argument("--selftest", action="store_true", help="синтетическая проверка тракта")
    args = p.parse_args()

    if args.selftest:
        return cmd_selftest(args.save)
    if not args.pcm:
        p.error("нужен файл записи или --selftest")
    res = process(args.pcm, args.sps)
    if not res:
        print("всплесков не найдено")
        return 1
    if args.cmd == "decode":
        return cmd_decode(res)
    if args.cmd == "constellation":
        return cmd_constellation(res, args.save)
    return cmd_magnitude(args.pcm, res, args.save, args.sps)

if __name__ == "__main__":
    sys.exit(main())
