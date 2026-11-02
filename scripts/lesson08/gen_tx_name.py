import numpy as np

NAME = "TIMUR"
SPS = 4
USE_QPSK = True
AMP_SCALE = 15000.0
OUTPUT_FILE = "tx_name.pcm"

def name_to_bits(name):
    bits = []
    for ch in name:
        val = ord(ch)
        for i in range(7, -1, -1):
            bits.append((val >> i) & 1)
    return np.array(bits, dtype=np.uint8)

def modulate_qpsk(bits):
    if len(bits) % 2 != 0:
        bits = bits[:-1]

    norm = 1.0 / np.sqrt(2.0)
    symbols = []

    for i in range(0, len(bits), 2):
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
    return np.convolve(signal, kernel, mode='full')

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

    print(f"[SAVE] Saved {len(complex_data)} complex samples to '{filename}'")

if __name__ == "__main__":
    print(f"=== Generating TX signal from name: '{NAME}' ===")

    bits = name_to_bits(NAME)
    print(f"[1/4] Name converted to {len(bits)} bits")

    symbols = modulate_qpsk(bits)
    print(f"[2/4] Modulated to {len(symbols)} QPSK symbols")

    upsampled = upsample(symbols, SPS)
    print(f"[3/4] Upsampled x{SPS} -> {len(upsampled)} samples")

    tx_signal = pulse_shaping_rect(upsampled, SPS)
    print(f"[4/4] Pulse shaped -> {len(tx_signal)} samples")

    save_cs16(OUTPUT_FILE, tx_signal, AMP_SCALE)
    print("\nDone. File ready for C++ transmitter.")
