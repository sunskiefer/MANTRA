// MANTRA effects. Plain C++17.
#pragma once
#include "mantra_dsp.h"

namespace mx {

// ============================ FILTER ====================================================
struct FilterFx {
    double sr = 48000;
    int type = 0;
    // parameters (set at control rate)
    double cutoff = 2000, res = 0.25, lowCut = 20, hiCut = 20000;
    // state
    Svf s[2][4]; OnePole op[2]; Chamberlin ch[2][2];
    double apZ[2][8] = {{0}}; double apA = 0, fbS[2] = {0, 0};
    double rmPhase = 0, rmInc = 0;
    Svf form[2][3]; Svf oct[2][3];
    Svf lc[2][1], hc[2][1];
    // pitch shifter
    DelayLine ps[2]; double psPhase = 0, psInc = 0, psRatio = 1, psWin = 1;
    double formantW[3] = {1, 0.6, 0.35};
    bool useLow = false, useHi = false; double makeup = 1.0;
    Rng rng;

    void prepare(double sampleRate) {
        sr = sampleRate;
        for (auto& c : s) for (auto& f : c) f.reset();
        for (auto& c : ch) for (auto& f : c) f.reset();
        for (int c = 0; c < 2; ++c) { op[c].reset(); ps[c].init((int)(sr * 0.25)); for (double& z : apZ[c]) z = 0; fbS[c] = 0; }
    }
    static double norm(double hz) { return clampd(std::log(hz / 20.0) / std::log(1000.0), 0.0, 1.0); }

    void update(int t, double cut, double rs, double lo, double hi) {
        type = t; cutoff = cut; res = clampd(rs, 0, 1); lowCut = lo; hiCut = hi;
        makeup = 1.0;
        if (t < 12 && ((t % 6) / 2) == 1) makeup = 2.0;
        else if (t == 14 || t == 15) makeup = 2.0;
        else if (t == 22) makeup = 1.5;
        else if (t == 23) makeup = 2.5;
        useLow = lo > 21.0; useHi = hi < 19000.0;
        double k = 2.0 * std::pow(0.02, res);              // 2 (no resonance) .. 0.04
        if (t < 12) {                                       // Clean / Dark
            bool is42 = (t % 2) == 1;
            double kk = is42 ? 1.2 : k;
            for (int c = 0; c < 2; ++c) {
                s[c][0].set(cut, sr, is42 ? k : kk);
                for (int i = 1; i < 3; ++i) s[c][i].set(cut, sr, 1.2);
                op[c].setLP(cut, sr);
            }
        } else if (t < 18) {                                // Legacy
            double q = 2.0 * std::pow(0.03, res);
            for (int c = 0; c < 2; ++c) { ch[c][0].set(cut, sr, q); ch[c][1].set(cut, sr, 1.0 + 0.5 * q); }
        } else if (t < 20) {                                // phasers
            double w = std::tan(kPi * clampd(cut, 20, sr * 0.4) / sr);
            apA = (w - 1.0) / (w + 1.0);
        } else if (t == 20) {                               // ringmod
            rmInc = clampd(cut, 1, sr * 0.45) / sr;
        } else if (t == 21) {                               // pitchshift
            double semis = (norm(cut) - 0.5) * 48.0;
            psRatio = std::pow(2.0, semis / 12.0);
            psWin = sr * (0.03 + 0.07 * (1.0 - res));
            psInc = (1.0 - psRatio) / psWin;
        } else if (t == 22) {                               // octave band
            double q = 0.6 + res * 0.4;                     // k
            double kk = 2.0 * std::pow(0.05, q);
            for (int c = 0; c < 2; ++c) {
                oct[c][0].set(cut * 0.5, sr, kk); oct[c][1].set(cut, sr, kk); oct[c][2].set(cut * 2.0, sr, kk);
            }
        } else {                                            // vowel
            static const double F[5][3] = {{800, 1150, 2900}, {400, 1600, 2700}, {350, 1700, 2700}, {450, 800, 2830}, {325, 700, 2700}};
            double pos = norm(cut) * 4.0; int i = std::min(3, (int)pos); double fr = pos - i;
            double kk = 2.0 * std::pow(0.02, 0.45 + 0.5 * res) ;
            for (int c = 0; c < 2; ++c)
                for (int j = 0; j < 3; ++j) form[c][j].set(F[i][j] * (1 - fr) + F[i + 1][j] * fr, sr, kk);
        }
        for (int c = 0; c < 2; ++c) {
            if (useLow) lc[c][0].set(lo, sr, 1.4142);
            if (useHi) hc[c][0].set(hi, sr, 1.4142);
        }
    }

    inline double run(int c, double x) {
        double y = x;
        const int t = type;
        if (t < 12) {
            const bool dark = t >= 6;
            const int m = (t % 6) / 2;                      // 0 LP 1 BP 2 HP
            const bool is42 = (t % 2) == 1;
            const double k0 = s[c][0].k;
            if (!is42) {
                s[c][0].tick(x, dark);
                y = m == 0 ? s[c][0].lp : (m == 1 ? s[c][0].bp * k0 : s[c][0].hp);
            } else {
                double v = x;
                for (int i = 0; i < 3; ++i) {
                    s[c][i].tick(v, dark);
                    v = m == 0 ? s[c][i].lp : (m == 1 ? s[c][i].bp * s[c][i].k : s[c][i].hp);
                }
                double o = op[c].lp(v);                     // 4th pole (6 dB)
                y = m == 0 ? o : (m == 1 ? v : v - o);
                if (m == 1) y = v;
            }
            if (dark) y = std::tanh(y * 1.2) / 1.2;
        } else if (t < 18) {
            int m = ((t - 12) / 2), pol24 = (t - 12) % 2;
            ch[c][0].tick(x);
            double o = m == 0 ? ch[c][0].low : (m == 1 ? ch[c][0].band * ch[c][0].q : ch[c][0].high);
            if (pol24) {
                ch[c][1].tick(o);
                o = m == 0 ? ch[c][1].low : (m == 1 ? ch[c][1].band * ch[c][1].q : ch[c][1].high);
            }
            y = o;
        } else if (t < 20) {
            int n = t == 18 ? 4 : 8;
            double v = x + fbS[c] * res * 0.9;
            for (int i = 0; i < n; ++i) { double o = apA * v + apZ[c][i]; apZ[c][i] = v - apA * o; apZ[c][i] = fixDenorm(apZ[c][i]); v = o; }
            fbS[c] = std::tanh(v);
            y = 0.5 * (x + v);
        } else if (t == 20) {
            double ph = rmPhase; if (c == 1) ph = rmPhase;  // same carrier both sides
            double car = std::sin(2 * kPi * ph);
            car = (1.0 - res) * car + res * std::tanh(6.0 * car);
            y = x * car;
        } else if (t == 21) {
            ps[c].write((float)x);
            double d1 = psPhase * psWin, d2 = std::fmod(psPhase + 0.5, 1.0) * psWin;
            double w1 = std::sin(kPi * psPhase), w2 = std::sin(kPi * std::fmod(psPhase + 0.5, 1.0));
            y = ps[c].read(d1 + 1.0) * w1 * w1 + ps[c].read(d2 + 1.0) * w2 * w2;
        } else if (t == 22) {
            y = 0.0;
            static const double W[3] = {0.7, 1.0, 0.7};
            for (int i = 0; i < 3; ++i) { oct[c][i].tick(x, false); y += oct[c][i].bp * oct[c][i].k * W[i]; }
            y *= 0.8;
        } else {
            y = 0.0;
            for (int i = 0; i < 3; ++i) { form[c][i].tick(x, false); y += form[c][i].bp * form[c][i].k * formantW[i]; }
        }
        y *= makeup;
        if (useLow) { lc[c][0].tick(y, false); y = lc[c][0].hp; }
        if (useHi) { hc[c][0].tick(y, false); y = hc[c][0].lp; }
        return y;
    }
    inline void process(double& l, double& r) {
        double yl = run(0, l), yr = run(1, r);
        if (type == 20) { rmPhase += rmInc; rmPhase -= std::floor(rmPhase); }
        if (type == 21) { psPhase += psInc; psPhase -= std::floor(psPhase); }
        l = yl; r = yr;
    }
};

// ============================ DISTORTION ================================================
struct DistFx {
    double sr = 48000;
    int type = 0; double pre = 1, comp = 1, fuzz = 0, outG = 1, tone = 0.5; bool mono = false;
    Biquad upLP[2], dnLP[2]; OnePole dc[2], toneLP[2];
    void prepare(double sampleRate) {
        sr = sampleRate;
        for (int c = 0; c < 2; ++c) { upLP[c].setLP(sr * 0.45, sr * 2, 0.7071); dnLP[c].setLP(sr * 0.45, sr * 2, 0.7071); upLP[c].reset(); dnLP[c].reset(); dc[c].setLP(20, sr); dc[c].reset(); toneLP[c].setLP(900, sr); toneLP[c].reset(); }
    }
    void update(int t, double drive, double fz, bool mn, double tn, double outDb) {
        type = t; pre = dbToGain(drive * 42.0); comp = 1.0 / std::sqrt(std::max(1.0, pre)); fuzz = fz; mono = mn; tone = tn; outG = dbToGain(outDb);
    }
    inline double shape(double x) const {
        x += fuzz * 0.35;
        x *= 1.0 + fuzz * 3.0;
        switch (type) {
            case 0: return std::tanh(x);
            case 1: return clampd(x, -1.0, 1.0) * (1.0 - 0.1 * std::tanh(x * 0.3)) ;
            case 2: return std::asin(std::sin(x * 0.5 * kPi)) * (2.0 / kPi) ;
            default: return x >= 0 ? (1.0 - std::exp(-x)) : -0.9 * (1.0 - std::exp(0.85 * x));
        }
    }
    inline void process(double& l, double& r) {
        if (mono) { l = r = 0.5 * (l + r); }
        double in[2] = {l, r}, out[2];
        for (int c = 0; c < 2; ++c) {
            double x = in[c] * pre;
            double a = upLP[c].tick(2.0 * x);               // zero-stuffed sample
            double b = upLP[c].tick(0.0);
            double ya = shape(a), yb = shape(b);
            dnLP[c].tick(ya);
            double y = dnLP[c].tick(yb);
            y = y - dc[c].lp(y);                            // remove bias
            double lp = toneLP[c].lp(y), tt = (tone - 0.5) * 2.0;
            y = lp * (1.0 - tt * 0.92) + (y - lp) * (1.0 + tt * 0.92);
            out[c] = y * outG * comp;
        }
        l = out[0]; r = out[1];
    }
};

// ============================ DELAY =====================================================
struct DelayFx {
    double sr = 48000;
    DelayLine dl[2];
    double tL = 12000, tR = 12000, tgtL = 12000, tgtR = 12000, tCoef = 0.002;
    double fb = 0.4, inG = 1; bool ping = false;
    OnePole lp[2], hp[2]; bool useBp = false;
    void prepare(double sampleRate) {
        sr = sampleRate; dl[0].init((int)(sr * 2.6)); dl[1].init((int)(sr * 2.6));
        tCoef = smoothCoef(40.0, sr); for (int c = 0; c < 2; ++c) { lp[c].reset(); hp[c].reset(); }
    }
    void update(double timeLms, double timeRms, bool pp, double feedback, double lpHz, double bp, double in) {
        tgtL = clampd(timeLms * 0.001 * sr, 8.0, sr * 2.5); tgtR = clampd(timeRms * 0.001 * sr, 8.0, sr * 2.5);
        ping = pp; fb = feedback * 0.98; inG = in;
        useBp = bp > 0.001;
        double lpF = lpHz, hpF = 20.0;
        if (useBp) { hpF = 80.0 * std::pow(50.0, bp); lpF = std::min(lpHz, hpF * 7.0); }
        for (int c = 0; c < 2; ++c) { lp[c].setLP(lpF, sr); hp[c].setLP(hpF, sr); }
    }
    inline void process(double& l, double& r) {
        tL += tCoef * (tgtL - tL); tR += tCoef * (tgtR - tR);
        double wl = dl[0].read(tL), wr = dl[1].read(tR);
        double il = l * inG, ir = r * inG;
        double fl = wl, fr = wr;
        for (int pass = 0; pass < 1; ++pass) {
            fl = lp[0].lp(fl); fr = lp[1].lp(fr);
            if (useBp) { fl = hp[0].hp(fl); fr = hp[1].hp(fr); }
        }
        fl = std::tanh(fl * fb); fr = std::tanh(fr * fb);
        if (ping) { dl[0].write((float)(0.5 * (il + ir) + fr)); dl[1].write((float)(fl)); }
        else { dl[0].write((float)(il + fl)); dl[1].write((float)(ir + fr)); }
        l = wl; r = wr;
    }
};

// ============================ LO-FI =====================================================
struct LoFiFx {
    double sr = 48000, bits = 16, factor = 1, acc = 0, noise = 0, hold[2] = {0, 0};
    bool useTone = false; OnePole tone[2]; Rng rng; double crackle = 0;
    void prepare(double sampleRate) { sr = sampleRate; for (auto& t : tone) t.reset(); }
    void update(double bit, double rate, double nz, double tn) {
        bits = bit; factor = 1.0 + rate * rate * 63.0; noise = nz;
        useTone = tn < 0.995; for (auto& t : tone) t.setLP(800.0 * std::pow(25.0, tn), sr);
    }
    inline void process(double& l, double& r) {
        acc += 1.0 / factor;
        if (acc >= 1.0) { acc -= 1.0; hold[0] = l; hold[1] = r; }
        double o[2] = {hold[0], hold[1]};
        for (int c = 0; c < 2; ++c) {
            double x = o[c];
            double n = noise * noise * 0.22 * rng.bi();
            if (noise > 0.001 && rng.uni() < noise * noise * noise * 0.0008) n += rng.bi() * 0.7 * noise;
            x += n;
            if (bits < 15.99) { double q = std::pow(2.0, bits - 1.0); x = std::floor(x * q + 0.5) / q; }
            if (useTone) x = tone[c].lp(x);
            o[c] = x;
        }
        l = o[0]; r = o[1];
    }
};

// ============================ FLANGER / PHASER ==========================================
struct FlangerFx {
    double sr = 48000;
    DelayLine dl[2]; int mode = 0; double delayMs = 2, fb = 0.4, width = 0.5, depth = 0.6, rate = 0.5;
    double phase = 0; OnePole tone[2]; double fbS[2] = {0, 0};
    double apZ[2][6] = {{0}}; double apA[2] = {0, 0};
    void prepare(double sampleRate) { sr = sampleRate; dl[0].init((int)(sr * 0.04)); dl[1].init((int)(sr * 0.04)); for (auto& t : tone) t.reset(); }
    void update(int m, double dMs, double f, double w, double tn, double rateHz, double dp) {
        mode = m; delayMs = dMs; fb = f; width = w; depth = dp; rate = rateHz;
        for (auto& t : tone) t.setLP(800.0 * std::pow(22.0, tn), sr);
    }
    inline void process(double& l, double& r) {
        phase += rate / sr; phase -= std::floor(phase);
        double in[2] = {l, r}, out[2];
        for (int c = 0; c < 2; ++c) {
            double ph = phase + (c ? width * 0.5 : 0.0);
            double lfo = std::sin(2 * kPi * ph);
            if (mode == 0) {
                double d = clampd(delayMs * std::pow(2.0, depth * lfo * 2.0), 0.05, 15.0) * 0.001 * sr;
                double w = dl[c].read(d);
                double f = tone[c].lp(w) * fb;
                f = std::tanh(f);
                dl[c].write((float)(in[c] + f));
                out[c] = w;
            } else {
                double fc = clampd(2000.0 / delayMs * std::pow(2.0, depth * lfo * 2.0), 40.0, sr * 0.4);
                double wv = std::tan(kPi * fc / sr), a = (wv - 1.0) / (wv + 1.0);
                double v = in[c] + fbS[c] * fb * 0.9;
                for (int i = 0; i < 6; ++i) { double o = a * v + apZ[c][i]; apZ[c][i] = fixDenorm(v - a * o); v = o; }
                fbS[c] = tone[c].lp(std::tanh(v));
                out[c] = v;
            }
        }
        l = out[0]; r = out[1];
    }
};

// ============================ GLITCH ====================================================
struct GlitchFx {
    double sr = 48000; DelayLine buf[2]; int mode = 0; bool rev = false;
    double sizeMs = 120, scale = 1, smooth = 0.3, stretch = 1;
    double pos = 0, len = 4000, start = 0; bool frozen = false, havePrev = false; int prevMode = 0; double w = 0;
    void prepare(double sampleRate) { sr = sampleRate; buf[0].init((int)(sr * 5.5)); buf[1].init((int)(sr * 5.5)); pos = 0; frozen = false; }
    void update(int m, double size, double sc, bool rv, double sm, double st) {
        mode = m; sizeMs = size; scale = sc; rev = rv; smooth = sm; stretch = st;
    }
    inline void capture() {
        len = clampd(sizeMs * scale * 0.001 * sr, 64.0, sr * 2.0);
        start = (double)buf[0].w - len;
        pos = 0;
    }
    inline void process(double& l, double& r) {
        if (mode == 1 && prevMode == 0) { capture(); frozen = true; }
        if (mode == 0 && prevMode == 1) { frozen = false; capture(); }
        prevMode = mode;
        if (!frozen) { buf[0].write((float)l); buf[1].write((float)r); }
        if (len < 64.0) capture();
        double p = rev ? (len - 1.0 - pos) : pos;
        double rp = start + p;
        double fade = smooth * 0.5 * len;
        double env = 1.0;
        if (fade > 2.0) env = std::min(1.0, std::min(pos / fade, (len - pos) / fade));
        double ol = buf[0].readAbs(rp) * env, orr = buf[1].readAbs(rp) * env;
        pos += stretch;
        if (pos >= len) { pos -= len; if (mode == 0) capture(); }
        l = ol; r = orr;
    }
};

} // namespace mx
