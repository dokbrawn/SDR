import numpy as np
import matplotlib.pyplot as plt

name = "/home/dok_brwn/pluto_lesson/rx_from_800.pcm"

try:
    raw_data = np.fromfile(name, dtype=np.int16)

    if len(raw_data) % 2 != 0:
        raw_data = raw_data[:-1]

    iq_data = raw_data.reshape(-1, 2)

    real = iq_data[:, 0].astype(np.float32)
    imag = iq_data[:, 1].astype(np.float32)

    n_samples = len(real)
    print(f"Загружено {n_samples} cэмплов")

    z = real + 1j * imag
    magnitude = np.abs(z)

    win_size = min(100000, n_samples)
    start_idx = (n_samples - win_size) // 2

    seg_real = real[start_idx:start_idx+win_size]
    seg_imag = imag[start_idx:start_idx+win_size]
    seg_mag = magnitude[start_idx:start_idx+win_size]
    count = np.arange(win_size)

    plt.figure(1, figsize=(10, 6))
    plt.plot(count, seg_imag, color='red', lw=0.5, label='Q (Мнимая часть / Фаза)')
    plt.plot(count, seg_real, color='blue', lw=0.5, label='I (Действительная часть / Синфазная)')

    plt.plot(count, seg_mag, color='black', lw=1.5, label='Модуль (Амплитуда сигнала)')

    plt.title('Данные RX')
    plt.xlabel('Номер отсчета')
    plt.ylabel('Амплитуда')
    plt.legend()
    plt.grid(True)
    plt.show()

except FileNotFoundError:
    print(f"File '{name}' not found")
except Exception as e:
    print(f"Error: {e}")
