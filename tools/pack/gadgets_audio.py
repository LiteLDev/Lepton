"""Original audio for U23 gadgets (ambience discs, Endermosh, tiny potato name sounds).

Everything is synthesised from scratch here (noise, oscillators, envelopes, simple filters); no
recorded or third-party samples are used. Run through tools/pack/gadgets.ps1, which encodes the
WAV files written here to Ogg Vorbis with ffmpeg. Deterministic: fixed seeds.

    python -I tools/pack/gadgets_audio.py <out_dir>
"""

import math
import random
import struct
import sys
import wave

RATE = 22050
TAU = 2 * math.pi
LOOP = 24.0  # seconds, every ambience loop


def write_wav(path, samples):
    peak = max(1e-9, max(abs(s) for s in samples))
    gain = 0.89 / peak
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s * gain)) * 32767)) for s in samples))


def loopify(samples, seconds, fade=2.0):
    """Makes `samples` (seconds + fade long) loop seamlessly: the tail is cross-faded into the head."""
    n = int(seconds * RATE)
    f = int(fade * RATE)
    out = samples[:n]
    for i in range(f):
        t = i / f
        out[i] = samples[n + i] * (1 - t) + samples[i] * t
    return out


class OnePole:
    def __init__(self, cutoff):
        self.set(cutoff)
        self.y = 0.0

    def set(self, cutoff):
        self.a = 1 - math.exp(-TAU * cutoff / RATE)

    def lp(self, x):
        self.y += self.a * (x - self.y)
        return self.y


class Biquad:
    """RBJ band-pass (constant peak gain)."""

    def __init__(self, freq, q):
        self.x1 = self.x2 = self.y1 = self.y2 = 0.0
        self.set(freq, q)

    def set(self, freq, q):
        w = TAU * freq / RATE
        alpha = math.sin(w) / (2 * q)
        a0 = 1 + alpha
        self.b0 = alpha / a0
        self.b2 = -alpha / a0
        self.a1 = -2 * math.cos(w) / a0
        self.a2 = (1 - alpha) / a0

    def run(self, x):
        y = self.b0 * x + self.b2 * self.x2 - self.a1 * self.y1 - self.a2 * self.y2
        self.x2, self.x1 = self.x1, x
        self.y2, self.y1 = self.y1, y
        return y


def brown(rng, n, leak=0.995):
    y = 0.0
    out = [0.0] * n
    for i in range(n):
        y = y * leak + rng.uniform(-1, 1) * 0.06
        out[i] = y
    return out


def add(dst, src, at, gain=1.0):
    for i, s in enumerate(src):
        j = at + i
        if j >= len(dst):
            break
        dst[j] += s * gain


def total(seconds):
    return int((seconds + 2.0) * RATE)


# ---- ambience ---------------------------------------------------------------------------------


def amb_rain():
    rng = random.Random(11)
    n = total(LOOP)
    lp1, lp2 = OnePole(2600), OnePole(500)
    out = [0.0] * n
    for i in range(n):
        w = rng.uniform(-1, 1)
        hi = lp1.lp(w) - lp2.lp(w)  # band of hiss
        out[i] = hi * 0.6
    for _ in range(int(n / RATE * 140)):  # drops on leaves and roofs
        at = rng.randrange(n)
        f = rng.uniform(1500, 4500)
        ln = int(RATE * rng.uniform(0.004, 0.015))
        amp = rng.uniform(0.05, 0.25)
        add(out, [math.sin(TAU * f * k / RATE) * math.exp(-k / (ln / 4)) * amp for k in range(ln)], at)
    rumble = brown(rng, n)
    for i in range(n):
        out[i] += rumble[i] * 0.5
    return loopify(out, LOOP)


def amb_drips():
    rng = random.Random(12)
    n = total(LOOP)
    out = [0.0] * n
    room = brown(rng, n, 0.999)
    for i in range(n):
        out[i] = room[i] * 0.05
    t = 0.3
    while t < LOOP + 1.5:
        f0 = rng.uniform(900, 1800)
        ln = int(RATE * 0.18)
        amp = rng.uniform(0.3, 0.8)
        ph = 0.0
        drop = []
        for k in range(ln):
            f = f0 * (1 + 0.9 * math.exp(-k / (RATE * 0.012)))
            ph += TAU * f / RATE
            drop.append(math.sin(ph) * math.exp(-k / (RATE * 0.035)) * amp)
        add(out, drop, int(t * RATE))
        t += rng.uniform(0.35, 1.6)
    # small cave echo
    d = int(RATE * 0.13)
    for i in range(d, n):
        out[i] += out[i - d] * 0.35
    return loopify(out, LOOP)


def amb_ocean():
    rng = random.Random(13)
    n = total(LOOP)
    lp = OnePole(900)
    hp = OnePole(120)
    out = [0.0] * n
    for i in range(n):
        t = i / RATE
        swell = 0.25 + 0.75 * max(0.0, math.sin(TAU * t / 8.0)) ** 2  # 3 waves per loop
        lp.set(300 + 1500 * swell)
        w = rng.uniform(-1, 1)
        x = lp.lp(w)
        out[i] = (x - hp.lp(x)) * swell
    return loopify(out, LOOP)


def amb_wind():
    rng = random.Random(14)
    n = total(LOOP)
    bp = Biquad(400, 1.2)
    bp2 = Biquad(900, 4.0)
    out = [0.0] * n
    for i in range(n):
        t = i / RATE
        gust = 0.5 + 0.3 * math.sin(TAU * t / 12.0) + 0.2 * math.sin(TAU * t / 4.0 + 1.3)
        if i % 64 == 0:
            bp.set(250 + 500 * gust, 1.0)
            bp2.set(700 + 900 * gust, 6.0)
        w = rng.uniform(-1, 1)
        out[i] = (bp.run(w) * 0.8 + bp2.run(w) * 0.5) * (0.3 + gust)
    return loopify(out, LOOP)


def amb_fire():
    rng = random.Random(15)
    n = total(LOOP)
    out = brown(rng, n, 0.997)
    lp = OnePole(400)
    for i in range(n):
        out[i] = lp.lp(out[i]) * 1.5
    for _ in range(int(LOOP * 9)):  # crackles
        at = rng.randrange(n)
        ln = int(RATE * rng.uniform(0.002, 0.02))
        amp = rng.uniform(0.1, 0.6)
        add(out, [rng.uniform(-1, 1) * amp * (1 - k / ln) for k in range(ln)], at)
    for _ in range(int(LOOP * 1.5)):  # pops
        at = rng.randrange(n)
        ln = int(RATE * 0.04)
        f = rng.uniform(200, 600)
        add(out, [math.sin(TAU * f * k / RATE) * math.exp(-k / (RATE * 0.006)) * 0.6 for k in range(ln)], at)
    return loopify(out, LOOP)


def amb_clock():
    rng = random.Random(16)
    n = int(LOOP * RATE)
    out = [0.0] * n
    for s in range(int(LOOP)):
        f = 2400 if s % 2 == 0 else 1900  # tick, tock
        ln = int(RATE * 0.05)
        click = []
        for k in range(ln):
            e = math.exp(-k / (RATE * 0.004))
            click.append((math.sin(TAU * f * k / RATE) * 0.7 + rng.uniform(-1, 1) * 0.3) * e)
        add(out, click, int(s * RATE))
        # wooden case resonance
        add(out, [math.sin(TAU * 310 * k / RATE) * math.exp(-k / (RATE * 0.03)) * 0.25 for k in range(ln * 3)], int(s * RATE))
    room = brown(rng, n, 0.999)
    for i in range(n):
        out[i] += room[i] * 0.01
    return out  # exactly periodic: no cross-fade needed


def amb_crickets():
    rng = random.Random(17)
    n = total(LOOP)
    out = [0.0] * n
    night = brown(rng, n, 0.998)
    for i in range(n):
        out[i] = night[i] * 0.05
    for voice in range(5):
        f = rng.uniform(3800, 5200)
        pan_gain = rng.uniform(0.25, 0.7)
        period = rng.uniform(0.7, 1.3)
        t = rng.uniform(0, period)
        while t < LOOP + 2:
            pulses = rng.choice((2, 3, 3, 4))
            for p in range(pulses):
                at = int((t + p * 0.035) * RATE)
                ln = int(RATE * 0.025)
                add(out, [math.sin(TAU * f * k / RATE) * math.sin(math.pi * k / ln) * pan_gain for k in range(ln)], at)
            t += period * rng.uniform(0.9, 1.1)
    return loopify(out, LOOP)


def amb_chatter():
    rng = random.Random(18)
    n = total(LOOP)
    out = [0.0] * n
    # a crowd: many babbling "voices", each a buzz through two moving formants with syllable envelopes
    for v in range(14):
        f0 = rng.uniform(95, 240)
        g = rng.uniform(0.15, 0.4)
        f1, f2 = Biquad(600, 5), Biquad(1500, 7)
        ph = 0.0
        syl = 0.0
        syl_rate = rng.uniform(3.5, 6.0)
        talk = rng.random() < 0.6
        next_switch = rng.uniform(0.5, 3.0)
        for i in range(n):
            t = i / RATE
            if t > next_switch:
                talk = not talk
                next_switch = t + (rng.uniform(1.0, 4.0) if talk else rng.uniform(0.3, 2.0))
            if i % 256 == 0:
                f1.set(rng.uniform(300, 850), 5)
                f2.set(rng.uniform(900, 2300), 7)
            syl += syl_rate / RATE
            env = max(0.0, math.sin(TAU * syl)) ** 2 if talk else 0.0
            if env == 0.0:
                continue
            ph += f0 * (1 + 0.05 * math.sin(TAU * 0.7 * t + v)) / RATE
            buzz = (ph % 1.0) * 2 - 1
            out[i] += (f1.run(buzz) + 0.6 * f2.run(buzz)) * env * g
    room = brown(rng, n, 0.998)
    for i in range(n):
        out[i] += room[i] * 0.15
    return loopify(out, LOOP)


# ---- Endermosh (original piece written for Lepton) --------------------------------------------

BPM = 128
BEAT = 60.0 / BPM
NOTE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def midi(name):
    acc = 1 if "#" in name else (-1 if "b" in name[1:] else 0)
    octave = int(name[-1])
    return 12 * (octave + 1) + NOTE[name[0]] + acc


def hz(m):
    return 440.0 * 2 ** ((m - 69) / 12)


def endermosh():
    seconds = 189.4
    n = int(seconds * RATE)
    out = [0.0] * n
    rng = random.Random(23)
    # chord roots per bar (E minor, a slow, eerie progression): Em C G D / Em Am B7 Em
    prog = [("E2", [0, 3, 7]), ("C2", [0, 4, 7]), ("G2", [0, 4, 7]), ("D2", [0, 4, 7]),
            ("E2", [0, 3, 7]), ("A2", [0, 3, 7]), ("B1", [0, 4, 7, 10]), ("E2", [0, 3, 7])]
    bar = 4 * BEAT
    bars = int(seconds / bar)

    def section(b):
        if b < 8:
            return "intro"
        if b < 40:
            return "a"
        if b < 56:
            return "break"
        if b < 92:
            return "b"
        return "outro"

    for b in range(bars):
        sec = section(b)
        root_name, chord = prog[b % len(prog)]
        root = midi(root_name)
        t0 = b * bar
        # pad: detuned saws, slow attack
        if sec != "break" or True:
            amp = {"intro": 0.10, "a": 0.08, "break": 0.12, "b": 0.07, "outro": 0.09}[sec]
            for iv in chord:
                for det in (-0.08, 0.08):
                    f = hz(root + 24 + iv + det)
                    ln = int(bar * RATE)
                    start = int(t0 * RATE)
                    ph = rng.random()
                    for k in range(ln):
                        if start + k >= n:
                            break
                        e = min(1.0, k / (RATE * 0.6)) * min(1.0, (ln - k) / (RATE * 0.4))
                        ph += f / RATE
                        out[start + k] += ((ph % 1.0) * 2 - 1) * e * amp * 0.5
        # bass: eighth notes on the root
        if sec in ("a", "b", "outro"):
            for e8 in range(8):
                f = hz(root + (12 if e8 % 4 == 3 else 0))
                start = int((t0 + e8 * BEAT / 2) * RATE)
                ln = int(BEAT / 2 * RATE * 0.9)
                for k in range(ln):
                    if start + k >= n:
                        break
                    env = math.exp(-k / (RATE * 0.12))
                    s = math.sin(TAU * f * k / RATE)
                    out[start + k] += (s + 0.3 * math.copysign(1, s)) * env * 0.22
        # arpeggio: sixteenth notes, square wave, two octaves up
        if sec in ("a", "b"):
            pattern = [0, 1, 2, 1, 2, 3 % len(chord), 2, 1] * 2
            for s16 in range(16):
                iv = chord[pattern[s16] % len(chord)] + (12 if (s16 // 4) % 2 and sec == "b" else 0)
                f = hz(root + 36 + iv)
                start = int((t0 + s16 * BEAT / 4) * RATE)
                ln = int(BEAT / 4 * RATE)
                for k in range(ln):
                    if start + k >= n:
                        break
                    env = math.exp(-k / (RATE * 0.05))
                    sq = 1.0 if (f * k / RATE) % 1.0 < 0.5 else -1.0
                    out[start + k] += sq * env * 0.06
        # lead melody in "b": a slow call over the chords
        if sec == "b":
            melody = [7, 10, 12, 10, 7, 5, 3, 5]
            for q in range(4):
                if (b + q) % 2:
                    continue
                f = hz(root + 24 + melody[(b * 2 + q) % len(melody)])
                start = int((t0 + q * BEAT) * RATE)
                ln = int(BEAT * 2 * RATE)
                ph = 0.0
                for k in range(ln):
                    if start + k >= n:
                        break
                    vib = 1 + 0.006 * math.sin(TAU * 5.5 * k / RATE)
                    ph += f * vib / RATE
                    env = min(1.0, k / (RATE * 0.03)) * math.exp(-k / (RATE * 0.9))
                    tri = 4 * abs((ph % 1.0) - 0.5) - 1
                    out[start + k] += tri * env * 0.12
        # drums
        if sec in ("a", "b"):
            for beat in range(4):
                start = int((t0 + beat * BEAT) * RATE)
                ln = int(RATE * 0.25)
                ph = 0.0
                for k in range(ln):  # kick
                    if start + k >= n:
                        break
                    f = 50 + 120 * math.exp(-k / (RATE * 0.03))
                    ph += f / RATE
                    out[start + k] += math.sin(TAU * ph) * math.exp(-k / (RATE * 0.09)) * 0.5
                if beat % 2 == 1:  # snare
                    for k in range(int(RATE * 0.18)):
                        if start + k >= n:
                            break
                        out[start + k] += (rng.uniform(-1, 1) * 0.7 + math.sin(TAU * 190 * k / RATE) * 0.3) * math.exp(-k / (RATE * 0.05)) * 0.25
            for h in range(8):  # closed hats
                start = int((t0 + h * BEAT / 2 + BEAT / 4) * RATE)
                for k in range(int(RATE * 0.04)):
                    if start + k >= n:
                        break
                    out[start + k] += rng.uniform(-1, 1) * math.exp(-k / (RATE * 0.008)) * 0.08
    # global fade in/out and a simple echo for space
    d = int(RATE * BEAT * 0.75)
    for i in range(d, n):
        out[i] += out[i - d] * 0.25
    for i in range(n):
        t = i / RATE
        out[i] *= min(1.0, t / 4.0) * min(1.0, (seconds - t) / 6.0)
    return out


# ---- tiny potato name sounds (original jingles) ------------------------------------------------


def jingle(notes, wave_kind="square", length=0.12, gain=0.4):
    out = []
    for m in notes:
        f = hz(m)
        ln = int(RATE * length)
        for k in range(ln):
            env = min(1.0, k / 60) * math.exp(-k / (RATE * length * 0.6))
            p = (f * k / RATE) % 1.0
            if wave_kind == "square":
                s = 1.0 if p < 0.5 else -1.0
            elif wave_kind == "tri":
                s = 4 * abs(p - 0.5) - 1
            else:
                s = math.sin(TAU * p)
            out.append(s * env * gain)
    return out


def potato_do_it():
    return jingle([midi("C5"), midi("E5"), midi("G5"), midi("C6")], "square", 0.09) + jingle([midi("C6")], "square", 0.35)


def potato_soda():
    rng = random.Random(31)
    n = int(RATE * 0.9)
    out = [0.0] * n
    for _ in range(90):  # fizz
        at = rng.randrange(n - 400)
        f = rng.uniform(2000, 6000)
        add(out, [math.sin(TAU * f * k / RATE) * math.exp(-k / 40) * 0.15 for k in range(300)], at)
    boing = []
    ph = 0.0
    for k in range(int(RATE * 0.4)):
        f = 300 + 500 * math.exp(-k / (RATE * 0.1))
        ph += f / RATE
        boing.append(math.sin(TAU * ph) * math.exp(-k / (RATE * 0.15)) * 0.5)
    add(out, boing, 0)
    return out


def potato_yung():
    return jingle([midi("G4"), midi("B4"), midi("D5"), midi("B4"), midi("G5")], "tri", 0.11, 0.5)


SOUNDS = {
    "ambient_drips": amb_drips,
    "ambient_ocean": amb_ocean,
    "ambient_rain": amb_rain,
    "ambient_wind": amb_wind,
    "ambient_fire": amb_fire,
    "ambient_clock": amb_clock,
    "ambient_crickets": amb_crickets,
    "ambient_chatter": amb_chatter,
    "endermosh": endermosh,
    "potato_do_it": potato_do_it,
    "potato_soda": potato_soda,
    "potato_yung": potato_yung,
}


def main():
    out_dir = sys.argv[1]
    only = set(sys.argv[2:])
    for name, fn in SOUNDS.items():
        if only and name not in only:
            continue
        samples = fn()
        write_wav(f"{out_dir}/{name}.wav", samples)
        print(f"{name}: {len(samples) / RATE:.1f} s")


if __name__ == "__main__":
    main()
