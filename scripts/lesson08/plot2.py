import matplotlib
matplotlib.use('Agg')
import numpy as np
import matplotlib.pyplot as plt

INPUT_PCM = "rx_from_840.pcm"
SPS = 4
NUM_DIGITS = 9

def read_cs16(filename):
    raw = np.fromfile(filename, dtype=np.int16)
    if len(raw) % 2 != 0:
        raw = raw[:-1]
    iq = raw.reshape(-1, 2)
    return iq[:, 0].astype(np.float32) + 1j * iq[:, 1].astype(np.float32)

def convolve_rect(signal, length):
    kernel = np.ones(length, dtype=np.complex64)
    return np.convolve(signal, kernel, mode='same')

if __name__ == "__main__":
    signal = read_cs16(INPUT_PCM)
    filtered = convolve_rect(signal, SPS)

    start_idx = SPS // 2
    symbols = filtered[start_idx::SPS]

    mag_mean = np.mean(np.abs(symbols[np.abs(symbols) > 10]))
    norm_symbols = symbols / mag_mean if mag_mean > 0 else symbols

    fig, axs = plt.subplots(3, 3, figsize=(15, 15))
    axs = axs.flatten()

    qpsk_ref = [
        (1/np.sqrt(2), 1/np.sqrt(2)),
        (-1/np.sqrt(2), 1/np.sqrt(2)),
        (-1/np.sqrt(2), -1/np.sqrt(2)),
        (1/np.sqrt(2), -1/np.sqrt(2))
    ]

    for i in range(NUM_DIGITS):
        idx = i
        if idx < len(norm_symbols):
            s = norm_symbols[idx]
            axs[i].scatter(s.real, s.imag, c='red', s=100, zorder=5)

        for x, y in qpsk_ref:
            axs[i].plot(x, y, 'b+', markersize=20, markeredgewidth=2)

        axs[i].axhline(y=0, color='grey', linewidth=0.5)
        axs[i].axvline(x=0, color='grey', linewidth=0.5)
        axs[i].grid(True, linestyle='--', alpha=0.6)
        axs[i].set_title(f'Цифра {i+1}', fontsize=14)
        axs[i].set_aspect('equal', adjustable='box')

    plt.suptitle('Диаграммы созвездия для цифр 1-9 после свертки', fontsize=16)
    plt.tight_layout(rect=[0, 0, 1, 0.96])
    plt.savefig('constellations_1_to_9.png', dpi=150)
    print("График сохранен в constellations_1_to_9.png")
