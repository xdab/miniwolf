#!/usr/bin/env python3
"""Theoretical PER vs C/N for the Bell202-over-FM link, for overlaying the
measured sweeps.

Model chain (formulas cited from Rice's click-noise work and standard FM
texts; the click-induced bit-flip model is an approximation):

  1. Above-threshold FM: parabolic discriminator noise PSD f^2*N0/(2C);
     audio SNR in W = baud Hz: SNR_A = 3*beta^2*(B/W)*rho, beta = dF/W.
  2. Rice click rate for the limiter-discriminator (rectangular IF,
     modulation term included):
     N_t = 0.5*erfc(sqrt(rho))*(B/sqrt(3) + 2*dF/pi)  [clicks/s].
     Click power in 0..W: P_c = 2*N_t*W; Gaussian term P_G = W^3/(6*rho*B).
  3. Noncoherent FSK over f^2-weighted noise: both branches share the same
     exponent, so Pb_gauss = 0.5*exp(-gamma_h/2) with the harmonic-mean
     gamma_h = rho*(B/R)*(dF/f_rms)^2, f_rms^2 = (f_mark^2+f_space^2)/2
     (the 2200 Hz tone is the weak one).
  4. Click-induced flips: a click impulse of area 1 vs signal envelope
     m = dF*T/2 flips a tone decision with kappa = acos(m/2)/pi;
     P_click = 0.5*(1-exp(-2*kappa*N_t*T)); line error
     p = Pb_gauss*(1-P_click) + (1-Pb_gauss)*P_click.
  5. PER = 1-(1-p)^N_line over the frame's NRZI line bits (computed from
     tools/frame.py with the standard trial framing).

Known simplifications, each worth ~1 dB or less unless noted: tone
non-orthogonality (h=0.83) ignored, about -0.9 dB; real filters and timing
recovery, 1-2 dB implementation loss; the click picture is valid for
C/N >~ 3-4 dB only. The stdout table is ber.py-compatible (seed=0), so
ber_combine.py overlays it on measured logs directly.
"""

import argparse
import math
import os
import sys

import numpy as np

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import frame as frame_mod  # noqa: E402


def line_bits(args) -> int:
    """NRZI line bits of the standard trial frame (bit stuffing included)."""
    rng = np.random.default_rng(0)
    chars = np.frombuffer(
        ("abcdefghijklmnopqrstuvwxyz").encode("ascii"), dtype=np.uint8)
    text = bytes(rng.choice(chars, args.text_len)).decode("ascii")
    frame = frame_mod.ax25_frame(args.dest, args.source, [], text)
    return len(frame_mod.hdlc_bits(frame, args.head_flags, args.tail_flags))


def theory_row(cnr_db: float, a) -> dict:
    """All model quantities at one C/N point."""
    rho = 10.0 ** (cnr_db / 10.0)
    w = a.baud                     # audio noise bandwidth W
    b = a.if_bw                    # IF noise bandwidth
    r = a.baud                     # bit rate
    t_bit = 1.0 / r
    d_f = a.deviation * a.amplitude  # effective peak deviation per tone
    beta = d_f / w
    f_rms_sq = (a.mark ** 2 + a.space ** 2) / 2.0

    snr_a = 3.0 * beta ** 2 * (b / w) * rho          # above-threshold, linear
    n_t = 0.5 * math.erfc(math.sqrt(rho)) * (b / math.sqrt(3.0) + 2.0 * d_f / math.pi)
    p_g = w ** 3 / (6.0 * rho * b)
    p_c = 2.0 * n_t * w
    snr_click = (d_f ** 2 / 2.0) / (p_g + p_c)

    gamma_h = rho * (b / r) * (d_f ** 2 / f_rms_sq)  # harmonic-mean Eb/N0
    pb_gauss = 0.5 * math.exp(-gamma_h / 2.0)

    m = d_f * t_bit / 2.0
    kappa = math.acos(min(m / 2.0, 1.0)) / math.pi
    p_click = 0.5 * (1.0 - math.exp(-2.0 * kappa * n_t * t_bit))
    p_line = pb_gauss * (1.0 - p_click) + (1.0 - pb_gauss) * p_click

    per = 1.0 - (1.0 - p_line) ** a.line_bits
    return {
        "cnr": cnr_db, "per": per, "p_line": p_line,
        "snr_a": snr_a, "snr_click": snr_click, "gamma_h": gamma_h,
        "n_t": n_t, "kappa": kappa, "p_click": p_click,
        "pb_gauss": pb_gauss,
    }


def main() -> None:
    p = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "examples:\n"
            "  python3 tools/ber_theory.py --cnr-min 0 --cnr-max 16 | tee theory.log\n"
            "  python3 tools/ber_combine.py race.log theory.log --out overlay.png\n"
            "  python3 tools/ber_combine.py race.log theory.log --ebn0 --out ebn0.png"
        ))
    # ber.py-compatible grid knobs
    p.add_argument("--cnr-min", type=float, default=0.0)
    p.add_argument("--cnr-max", type=float, default=16.0)
    p.add_argument("--cnr-step", type=float, default=1.0)
    # the link, matching the channel model's constants
    p.add_argument("--if-bw", type=float, default=25000.0, dest="if_bw",
                   help="IF noise bandwidth the C/N references (Hz)")
    p.add_argument("--baud", type=float, default=1200.0)
    p.add_argument("--mark", type=float, default=1200.0)
    p.add_argument("--space", type=float, default=2200.0)
    p.add_argument("--deviation", type=float, default=3000.0,
                   help="channel FM deviation parameter (Hz); effective peak "
                        "deviation = deviation * amplitude")
    p.add_argument("--amplitude", type=float, default=0.5,
                   help="AFSK tone amplitude fed to the FM modulator")
    # framing, matching ber.py's trial
    p.add_argument("--text-len", type=int, default=30, dest="text_len")
    p.add_argument("--head-flags", type=int, default=16, dest="head_flags")
    p.add_argument("--tail-flags", type=int, default=4, dest="tail_flags")
    p.add_argument("--dest", default="APZMWL")
    p.add_argument("--source", default="N0CALL")
    p.add_argument("--rate", type=float, default=48000.0)
    p.add_argument("--ebn0", action="store_true",
                   help="plot the x-axis as Eb/N0 = C/N + 10*log10(if-bw/baud)")
    p.add_argument("--out", default=None, help="output PNG path (table always prints)")
    args = p.parse_args()

    args.line_bits = line_bits(args)

    cnrs = np.arange(args.cnr_min, args.cnr_max + 1e-9, args.cnr_step)
    rows = [theory_row(float(c), args) for c in cnrs]

    # stdout: ber.py-compatible table (seed=0), consumable by ber_combine.py
    trials = 1000
    print(f"PER vs C/N  audio {args.rate:g} Hz  text {args.text_len} chars  "
          f"{trials} trials/point  seed=0  "
          f"arms=theory dF={args.deviation * args.amplitude:g} "
          f"B={args.if_bw:g} Nline={args.line_bits}")
    print(" C/N [dB]        PER      ok  wrong  none  extra")
    for row in rows:
        ok = int(round((1.0 - row["per"]) * trials))
        print(f"{row['cnr']:7.1f}   {row['per']:.3e}   {ok:4d}     0     "
              f"{trials - ok:4d}      0")

    # stderr: the model's guts, for reading the regimes
    print(f"\nline bits {args.line_bits}, peak deviation "
          f"{args.deviation * args.amplitude:g} Hz (beta "
          f"{args.deviation * args.amplitude / args.baud:.2f})", file=sys.stderr)
    print(f"  {'C/N':>6} {'SNR_A dB':>9} {'+clicks':>9} {'gamma_h dB':>10} "
          f"{'N_t /s':>9} {'kappa':>6} {'p_click':>8} {'p_line':>9}",
          file=sys.stderr)
    for row in rows:
        print(f"  {row['cnr']:6g} "
              f"{10 * math.log10(row['snr_a']):9.1f} "
              f"{10 * math.log10(row['snr_click']):9.1f} "
              f"{10 * math.log10(row['gamma_h']):10.1f} "
              f"{row['n_t']:9.3g} {row['kappa']:6.3f} "
              f"{row['p_click']:8.3g} {row['p_line']:9.3g}", file=sys.stderr)

    if not args.out:
        return

    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    if args.ebn0:
        off = 10.0 * np.log10(args.if_bw / args.baud)
        xs = cnrs + off
        xlabel = f"Eb/N0, noise in the {args.if_bw:g} Hz IF [dB]"
        note = f" (x-axis: Eb/N0 = C/N + {off:.1f} dB)"
    else:
        xs = cnrs
        xlabel = "C/N at the IF output [dB]"
        note = ""

    fig, ax = plt.subplots(figsize=(9, 6), constrained_layout=True)
    y = np.where(np.array([r["per"] for r in rows]) > 0,
                 [r["per"] for r in rows], np.nan)
    ax.semilogy(xs, y, "o-", label="theory: clicks + gaussian (Rice model)")
    ax.set_xlabel(xlabel)
    ax.set_ylabel("packet error rate")
    fig.suptitle("theoretical PER, Bell202 AFSK over FM", fontsize=13)
    ax.set_title(f"peak dev {args.deviation * args.amplitude:g} Hz, "
                 f"IF {args.if_bw:g} Hz, {args.line_bits} line bits{note}",
                 fontsize=9)
    ax.grid(True, which="both", alpha=0.3)
    ax.legend()
    fig.savefig(args.out, dpi=150)
    print(f"wrote {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
