import matplotlib
matplotlib.use('Agg')
import numpy as np
import matplotlib.pyplot as plt

INPUT_FILE = "rx_from_840.pcm"
SPS = 4
THRESHOLD_FACTOR = 0.5
MAX_POINTS = 10000

def read_cs16(filename):
    raw_data = np.fromfile(filename, dtype=np.int16)
    if len(raw_data) % 2 != 0:
        raw_data = raw_data[:-1]
    iq_data = raw_data.reshape(-1, 2)
    real = iq_data[:, 0].astype(np.float32)
    imag = iq_data[:, 1].astype(np.float32)
    return real + 1j * imag

def get_active_symbols(signal, sps, threshold_factor):
    envelope = np.abs(signal)
    peak_amp = np.max(envelope)
    threshold = peak_amp * threshold_factor
    active_mask = envelope > threshold
    symbols = []
    start_idx = sps // 2
    for i in range(start_idx, len(signal), sps):
        if active_mask[i]:
            symbols.append(signal[i])
    return np.array(symbols, dtype=np.complex64), peak_amp

if __name__ == "__main__":
    try:
        signal = read_cs16(INPUT_FILE)
        symbols, peak_amp = get_active_symbols(signal, SPS, THRESHOLD_FACTOR)

        if len(symbols) == 0:
            print("Ошибка: Активные символы не найдены.")
            exit()

        if len(symbols) > MAX_POINTS:
            indices = np.random.choice(len(symbols), MAX_POINTS, replace=False)
            symbols = symbols[indices]

        plt.figure(figsize=(8, 8))
        re = np.real(symbols)
        im = np.imag(symbols)

        plt.scatter(re, im, c='blue', alpha=0.4, s=10, edgecolors='none', label='Принятый сигнал')

        norm = 1.0 / np.sqrt(2.0)
        scale = peak_amp * 0.9
        ideal_re = [norm*scale, -norm*scale, -norm*scale, norm*scale]
        ideal_im = [norm*scale, norm*scale, -norm*scale, -norm*scale]

        plt.scatter(ideal_re, ideal_im, c='red', marker='x', s=200, linewidths=3, label='Идеальный QPSK')

        plt.axhline(y=0, color='k', linestyle='--', alpha=0.3)
        plt.axvline(x=0, color='k', linestyle='--', alpha=0.3)
        plt.grid(True, alpha=0.3)

        plt.title(f'Диаграмма созвездия (Только активные импульсы)\nФайл: {INPUT_FILE}')
        plt.xlabel('Синфазная составляющая (I)')
        plt.ylabel('Квадратурная составляющая (Q)')
        plt.legend()
        plt.axis('equal')

        plt.savefig('constellation_ru.png', dpi=150)
        print("График сохранен в constellation_ru.png")

    except FileNotFoundError:
        print(f"Ошибка: Файл '{INPUT_FILE}' не найден.")
    except Exception as e:
        print(f"Ошибка: {e}")
