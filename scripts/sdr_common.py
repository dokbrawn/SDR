import numpy as np

SPS = 4
AMP_SCALE = 15000.0
SILENCE_SAMPLES = 2048
PREABLE_LEN = 16
PREAMBLE_SEED = 7

NORM = 1.0 / np.sqrt(2.0)

BITS_TO_SYM = {
    (0, 0): complex(NORM, NORM),
    (0, 1): complex(-NORM, NORM),
    (1, 1): complex(-NORM, -NORM),
    (1, 0): complex(NORM, -NORM),
}

def preamble_symbols():
    rng = np.random.default_rng(PREAMBLE_SEED)
    syms = []
    for _ in range(PREABLE_LEN):
        b1, b2 = int(rng.integers(0, 2)), int(rng.integers(0, 2))
        syms.append(BITS_TO_SYM[(b1, b2)])
    return np.array(syms, dtype=np.complex64)

def name_to_bits(text):
    bits = []
    for ch in text:
        val = ord(ch)
        for i in range(7, -1, -1):
            bits.append((val >> i) & 1)
    return np.array(bits, dtype=np.uint8)

def bits_to_name(bits):
    out = []
    for i in range(0, len(bits) - 7, 8):
        byte = 0
        for b in bits[i:i + 8]:
            byte = (byte << 1) | int(b)
        out.append(chr(byte) if 32 <= byte < 127 else ".")
    return "".join(out)

def modulate_qpsk(bits):
    bits = np.asarray(bits, dtype=np.uint8)
    if len(bits) % 2:
        bits = bits[:-1]
    syms = [BITS_TO_SYM[(int(bits[i]), int(bits[i + 1]))] for i in range(0, len(bits), 2)]
    return np.array(syms, dtype=np.complex64)

def demodulate_qpsk(symbols):
    bits = []
    for s in symbols:
        re_ok, im_ok = s.real >= 0, s.imag >= 0
        if re_ok and im_ok:
            bits += [0, 0]
        elif not re_ok and im_ok:
            bits += [0, 1]
        elif not re_ok and not im_ok:
            bits += [1, 1]
        else:
            bits += [1, 0]
    return np.array(bits, dtype=np.uint8)

def upsample(symbols, sps=SPS):
    up = np.zeros(len(symbols) * sps, dtype=np.complex64)
    up[::sps] = symbols
    return up

def pulse_shape_rect(signal, sps=SPS):
    return np.convolve(signal, np.ones(sps, dtype=np.complex64), mode="full")

def matched_filter_rect(signal, sps=SPS):
    return np.convolve(signal, np.ones(sps, dtype=np.complex64), mode="full")

def save_cs16(filename, complex_data, scale=AMP_SCALE):
    re_i = np.clip(np.real(complex_data) * scale, -32768, 32767).astype(np.int16)
    im_i = np.clip(np.imag(complex_data) * scale, -32768, 32767).astype(np.int16)
    inter = np.empty(re_i.size * 2, dtype=np.int16)
    inter[0::2] = re_i
    inter[1::2] = im_i
    with open(filename, "wb") as f:
        f.write(inter.tobytes())
    print(f"[SAVE] {filename}: {complex_data.size} комплексных отсчётов")

def read_cs16(filename):
    raw = np.fromfile(filename, dtype=np.int16)
    if raw.size % 2:
        raw = raw[:-1]
    iq = raw.reshape(-1, 2)
    return iq[:, 0].astype(np.float32) + 1j * iq[:, 1].astype(np.float32)
