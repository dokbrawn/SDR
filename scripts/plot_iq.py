import argparse
import os
import sys

import numpy as np
import matplotlib.pyplot as plt

FS = 1e6

def parse_args():
    p = argparse.ArgumentParser(description="I, Q и огибающая из записи .pcm (CS16)",
                                formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    p.add_argument("pcm", help="файл записи, например rx_from_800.pcm")
    p.add_argument("--window", type=int, default=100_000, help="сколько отсчётов рисовать в окне")
    p.add_argument("--offset", type=int, default=None, help="начало окна; по умолчанию середина записи")
    p.add_argument("--fs", type=float, default=FS, help="частота дискретизации, Гц")
    p.add_argument("--full-envelope", action="store_true", help="огибающая по всей записи с прореживанием")
    p.add_argument("--save", metavar="PNG", help="сохранить рисунок в файл вместо показа окна")
    return p.parse_args()

def load_iq(path):
    raw = np.fromfile(path, dtype=np.int16)
    if raw.size % 2:
        print(f"warning: нечётное число int16 ({raw.size}), последний отсчёт отброшен", file=sys.stderr)
        raw = raw[:-1]
    iq = raw.reshape(-1, 2)
    return iq[:, 0].astype(np.float64), iq[:, 1].astype(np.float64)

def rms(i, q):
    return float(np.sqrt(np.mean(i ** 2 + q ** 2)))

def plot_window(i, q, start, win, fs, ax_i, ax_q, ax_env):
    seg_i, seg_q = i[start:start + win], q[start:start + win]
    t = np.arange(seg_i.size) / fs * 1e3
    ax_i.plot(t, seg_i, lw=0.5, color="tab:blue", label="I")
    ax_q.plot(t, seg_q, lw=0.5, color="tab:red", label="Q")
    ax_env.plot(t, np.hypot(seg_i, seg_q), lw=0.8, color="black", label="|I + jQ|")
    print(f"окно: offset {start}, {seg_i.size} отсчётов ({seg_i.size / fs * 1e3:.1f} мс), "
          f"RMS {rms(seg_i, seg_q):.2f}")

def plot_full_envelope(i, q, fs, ax):
    env = np.hypot(i, q)
    block = max(1, env.size // 200_000)
    usable = (env.size // block) * block
    env_ds = env[:usable].reshape(-1, block).mean(axis=1)
    t = (np.arange(env_ds.size) * block) / fs
    ax.plot(t, env_ds, lw=0.6, color="tab:green", label=f"огибающая (среднее по {block})")
    ax.set_xlabel("время, с")
    ax.legend(loc="upper right")

def main():
    args = parse_args()

    if not os.path.isfile(args.pcm):
        print(f"error: файл '{args.pcm}' не найден.", file=sys.stderr)
        print("Запустите ./build/pluto_link <tx|rx> <uri> и остановите Ctrl+C, "
              "чтобы .pcm записался.", file=sys.stderr)
        return 1

    i, q = load_iq(args.pcm)
    n = i.size
    if n == 0:
        print(f"error: '{args.pcm}' пуст.", file=sys.stderr)
        return 1

    print(f"загружено {n} отсчётов из {args.pcm} ({n * 4 / 1e6:.1f} МБ, {n / args.fs:.2f} с), "
          f"RMS {rms(i, q):.2f}")

    win = min(args.window, n)
    start = (n - win) // 2 if args.offset is None else min(max(args.offset, 0), n - win)

    rows = 4 if args.full_envelope else 3
    fig, axs = plt.subplots(rows, 1, figsize=(14, 3 * rows), sharex=False)
    axs[0].set_title(f"I — {args.pcm}")
    axs[1].set_title("Q")
    axs[2].set_title("Огибающая |I + jQ| (окно)")
    for ax in axs[:3]:
        ax.grid(True)
        ax.set_ylabel("амплитуда")
    axs[2].set_xlabel("время, мс")
    plot_window(i, q, start, win, args.fs, axs[0], axs[1], axs[2])
    for ax in axs[:3]:
        ax.legend(loc="upper right")

    if args.full_envelope:
        axs[3].set_title("Огибающая по всей записи")
        axs[3].grid(True)
        axs[3].set_ylabel("амплитуда")
        plot_full_envelope(i, q, args.fs, axs[3])

    fig.tight_layout()

    if args.save:
        os.makedirs(os.path.dirname(os.path.abspath(args.save)), exist_ok=True)
        fig.savefig(args.save, dpi=150)
        print(f"сохранено: {args.save}")
    else:
        plt.show()
    return 0

if __name__ == "__main__":
    sys.exit(main())
