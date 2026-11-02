import argparse

import numpy as np

import sdr_common as cm

def build_frame(text, sps):
    syms = np.concatenate([cm.preamble_symbols(), cm.modulate_qpsk(cm.name_to_bits(text))])
    x = cm.pulse_shape_rect(cm.upsample(syms, sps), sps)
    frame = np.concatenate([
        np.zeros(cm.SILENCE_SAMPLES, dtype=np.complex64),
        x,
    ]).astype(np.complex64)
    pre_n = cm.PREABLE_LEN * sps + sps - 1
    return frame, pre_n, x.size - pre_n

def main():
    p = argparse.ArgumentParser(description="Текст -> биты -> Gray-QPSK -> upsampling -> "
                                             "прямоугольный импульс -> CS16 для pluto_link tx")
    p.add_argument("--name", default="TIMUR", help="текст для передачи (ASCII)")
    p.add_argument("--sps", type=int, default=cm.SPS, help="отсчётов на символ")
    p.add_argument("--scale", type=float, default=cm.AMP_SCALE, help="масштаб в int16")
    p.add_argument("--out", default="tx_name.pcm", help="имя выходного файла")
    args = p.parse_args()

    bits = cm.name_to_bits(args.name)
    symbols = cm.modulate_qpsk(bits)
    frame, pre_n, data_n = build_frame(args.name, args.sps)

    print(f"[1/4] текст '{args.name}' -> {bits.size} бит")
    print(f"[2/4] QPSK-символов: {symbols.size} (+{cm.PREABLE_LEN} преамбулы)")
    print(f"[3/4] upsampling x{args.sps} + прямоугольный импульс")
    print(f"[4/4] кадр: {frame.size} отсчётов "
          f"(тишина {cm.SILENCE_SAMPLES}, преамбула {pre_n}, данные {data_n}), "
          f"{frame.size / 1e6 * 1000:.2f} мс при Fs=1 МГц")

    peak = np.max(np.abs(frame))
    if peak * args.scale > 32767:
        print(f"WARNING: пик {peak:.3f} * scale {args.scale} = {peak * args.scale:.0f} > 32767, "
              f"будет клиппинг; уменьшите --scale")

    cm.save_cs16(args.out, frame, args.scale)

if __name__ == "__main__":
    main()
