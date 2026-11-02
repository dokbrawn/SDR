import matplotlib
matplotlib.use('Agg')
import numpy as np
import matplotlib.pyplot as plt

INPUT_PCM = "rx_from_840.pcm"
SPS = 4

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

    magnitude = np.abs(filtered)

    plt.figure(figsize=(14, 5))
    plt.plot(magnitude[:1000], color='black', linewidth=1.5)
    plt.title('Амплитуда сигнала после свертки')
    plt.xlabel('Номер отсчета')
    plt.ylabel('|Сигнал|')
    plt.grid(True, linestyle='--', alpha=0.7)
    plt.tight_layout()
    plt.savefig('magnitude_after_convolution.png', dpi=150)
    print("График сохранен в magnitude_after_convolution.png")
