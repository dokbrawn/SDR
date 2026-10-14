import numpy as np
import matplotlib.pyplot as plt

# Имя файла для отрисовки (по умолчанию тот, что создал режим TX)
name = "rx_from_840.pcm"

try:
    # Быстрое чтение всего файла через NumPy
    raw = np.fromfile(name, dtype=np.int16)
    
    # Проверка на четность (CS16 всегда парный)
    if len(raw) % 2 != 0:
        raw = raw[:-1]
        
    # Разделение на I и Q
    iq = raw.reshape(-1, 2)
    real = iq[:, 0].astype(np.float32)
    imag = iq[:, 1].astype(np.float32)
    
    n_samples = len(real)
    print(f"Loaded {n_samples} samples from {name}")
    
    if n_samples == 0:
        print("File is empty!")
        exit()

    # Берем окно из середины файла для чистого отображения (без переходных процессов старта/стопа)
    win_size = min(100000, n_samples)  # ~100 мс при 1 Msps
    start_idx = (n_samples - win_size) // 2
    
    seg_real = real[start_idx:start_idx+win_size]
    seg_imag = imag[start_idx:start_idx+win_size]
    
    fs = 1e6
    t = np.arange(win_size) / fs
    
    fig, axs = plt.subplots(3, 1, figsize=(14, 9), sharex=True)
    
    # График I
    axs[0].plot(t*1000, seg_real, color='blue', lw=0.5, label='I')
    axs[0].set_title('Real Component (I)')
    axs[0].set_ylabel('Amplitude')
    axs[0].grid(True)
    axs[0].legend(loc='upper right')
    
    # График Q
    axs[1].plot(t*1000, seg_imag, color='red', lw=0.5, label='Q')
    axs[1].set_title('Imaginary Component (Q)')
    axs[1].set_ylabel('Amplitude')
    axs[1].grid(True)
    axs[1].legend(loc='upper right')
    
    # График огибающей |I+jQ|
    env = np.sqrt(seg_real**2 + seg_imag**2)
    axs[2].plot(t*1000, env, color='black', lw=0.8, label='Envelope |I+jQ|')
    axs[2].set_title('Envelope - Rectangular Pulse')
    axs[2].set_xlabel('Time [ms]')
    axs[2].set_ylabel('Amplitude')
    axs[2].grid(True)
    axs[2].legend(loc='upper right')
    
    plt.tight_layout()
    plt.show()

except FileNotFoundError:
    print(f"Error: File '{name}' not found.")
    print("Make sure you ran the SDR binary and stopped it with Ctrl+C to save data.")
except Exception as e:
    print(f"An error occurred: {e}")
