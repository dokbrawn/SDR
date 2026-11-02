import numpy as np

INPUT_PCM = "rx_from_840.pcm"
OUTPUT_PCM = "tx_reconstructed.pcm"
SPS = 4
USE_QPSK = True
AMP_SCALE = 15000.0

def read_cs16(filename):
    raw_data = np.fromfile(filename, dtype=np.int16)
    if len(raw_data) % 2 != 0:
        raw_data = raw_data[:-1]
    iq_data = raw_data.reshape(-1, 2)
    real = iq_data[:, 0].astype(np.float32)
    imag = iq_data[:, 1].astype(np.float32)
    return real + 1j * imag

def demodulate_to_bits(signal, sps, use_qpsk):
    bits = []
    start_idx = sps // 2
    step = sps

    for i in range(start_idx, len(signal), step):
        sym = signal[i]
        mag = np.abs(sym)
        if mag < 1e-6:
            continue

        sym_norm = sym / mag

        if not use_qpsk:
            bit_val = 1 if sym_norm.real > 0 else 0
            bits.append(bit_val)
        else:
            if sym_norm.real >= 0 and sym_norm.imag >= 0:
                bits.extend([0, 0])
            elif sym_norm.real < 0 and sym_norm.imag >= 0:
                bits.extend([0, 1])
            elif sym_norm.real < 0 and sym_norm.imag < 0:
                bits.extend([1, 1])
            else:
                bits.extend([1, 0])

    return np.array(bits, dtype=np.uint8)

def modulate(bits, use_qpsk):
    symbols = []
    if not use_qpsk:
        for b in bits:
            val = 1.0 if b == 1 else -1.0
            symbols.append(complex(val, 0))
    else:
        norm = 1.0 / np.sqrt(2.0)
        for i in range(0, len(bits) - 1, 2):
            b1, b2 = bits[i], bits[i+1]

            if b1 == 0 and b2 == 0:      s = complex(norm, norm)
            elif b1 == 0 and b2 == 1:    s = complex(-norm, norm)
            elif b1 == 1 and b2 == 1:    s = complex(-norm, -norm)
            else:                        s = complex(norm, -norm)

            symbols.append(s)

    return np.array(symbols, dtype=np.complex64)

def upsample(symbols, sps):
    n_sym = len(symbols)
    upsampled = np.zeros(n_sym * sps, dtype=np.complex64)
    upsampled[::sps] = symbols
    return upsampled

def pulse_shaping_rect(signal, filter_len):
    kernel = np.ones(filter_len, dtype=np.complex64)
    result = np.convolve(signal, kernel, mode='full')
    return result

def save_cs16(filename, complex_data, scale):
    real = np.real(complex_data)
    imag = np.imag(complex_data)

    real_int16 = np.clip(real * scale, -32768, 32767).astype(np.int16)
    imag_int16 = np.clip(imag * scale, -32768, 32767).astype(np.int16)

    interleaved = np.empty(len(real_int16) * 2, dtype=np.int16)
    interleaved[0::2] = real_int16
    interleaved[1::2] = imag_int16

    with open(filename, 'wb') as f:
        f.write(interleaved.tobytes())

if __name__ == "__main__":
    rx_signal = read_cs16(INPUT_PCM)
    recovered_bits = demodulate_to_bits(rx_signal, SPS, USE_QPSK)

    if len(recovered_bits) == 0:
        print("Error: No bits recovered.")
        exit()

    tx_symbols = modulate(recovered_bits, USE_QPSK)
    tx_upsampled = upsample(tx_symbols, SPS)
    tx_final = pulse_shaping_rect(tx_upsampled, SPS)

    save_cs16(OUTPUT_PCM, tx_final, AMP_SCALE)
    print(f"Saved {len(tx_final)} samples to {OUTPUT_PCM}")
