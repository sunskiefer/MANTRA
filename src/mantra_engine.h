// MANTRA engine: two layers x six effects (any order), 8 step/curve modulators,
// 24 slot mod matrix, master EQ / reverb / output. Plain C++17.
#pragma once
#include "mantra_params.h"
#include "mantra_fx.h"

namespace mx {

// ============================ REVERB (8 line FDN) =======================================
struct Reverb {
    static constexpr int N = 8;
    double sr = 48000; DelayLine line[N]; double len[N], base[N]; double g[N]; OnePole lp[N], hp[N];
    double modPh[N]; double wetGain = 0;
    double size = 0.5, decay = 0.5, bass = 0.5, treble = 0.5;
    void prepare(double sampleRate) {
        sr = sampleRate;
        static const double L[N] = {1117, 1327, 1559, 1787, 2053, 2371, 2693, 3023};
        for (int i = 0; i < N; ++i) { base[i] = L[i] * sr / 48000.0; line[i].init((int)(base[i] * 3.0) + 64); modPh[i] = i * 0.137; lp[i].reset(); hp[i].reset(); }
        update(0.5, 0.5, 0.5, 0.5);
    }
    void update(double sz, double dc, double bs, double tr) {
        size = sz; decay = dc; bass = bs; treble = tr;
        double rt60 = 0.3 + dc * dc * 12.0, sc = 0.5 + sz * 1.6;
        for (int i = 0; i < N; ++i) {
            len[i] = base[i] * sc;
            g[i] = std::pow(10.0, -3.0 * (len[i] / sr) / rt60);
            lp[i].setLP(1500.0 * std::pow(12.0, tr), sr);
            hp[i].setLP(bs < 0.5 ? 20.0 + (0.5 - bs) * 2.0 * 450.0 : 20.0, sr);
        }
    }
    inline void process(double inL, double inR, double& oL, double& oR) {
        double x = 0.5 * (inL + inR), v[N], sum = 0;
        for (int i = 0; i < N; ++i) {
            modPh[i] += (0.25 + 0.07 * i) / sr; modPh[i] -= std::floor(modPh[i]);
            double d = len[i] + 6.0 * std::sin(2 * kPi * modPh[i]);
            v[i] = line[i].read(d);
            double y = lp[i].lp(v[i]);
            if (bass < 0.5) y = hp[i].hp(y);
            v[i] = y * g[i];
            sum += v[i];
        }
        double mean = sum * (2.0 / N);
        oL = oR = 0;
        for (int i = 0; i < N; ++i) {
            double fbv = v[i] - mean;
            double inj = x * ((i & 1) ? -0.5 : 0.5) ;
            line[i].write((float)(std::tanh(fbv + inj)));
            if (i & 1) oR += v[i]; else oL += v[i];
        }
        oL *= 0.5; oR *= 0.5;
    }
};

// ============================ MODULATOR =================================================
struct Modulator {
    double phase = 0, out = 1, sm = 1;
    float steps[NUM_STEPS];
    Modulator() { for (auto& s : steps) s = 1.f; }
    // returns 0..1
    double evaluate(int mode, int nSteps, double shuffle, double gate, double tension, double shiftSteps, bool invert) {
        double ph = phase - std::floor(phase);
        double u = ph * nSteps + shiftSteps;
        u = std::fmod(u, (double)nSteps); if (u < 0) u += nSteps;
        double v;
        if (mode == 0) {
            int i = (int)std::floor(u); double fr = u - i;
            if (shuffle > 0.001) {
                int pair = i / 2; bool lastSingle = ((nSteps & 1) && i == nSteps - 1);
                if (!lastSingle) {
                    double sh = shuffle * 0.6, t = (u - pair * 2.0);
                    if (t < 1.0 + sh) { i = pair * 2; fr = t / (1.0 + sh); }
                    else { i = pair * 2 + 1; fr = (t - (1.0 + sh)) / (1.0 - sh); }
                }
            }
            i = std::min(i, nSteps - 1);
            v = steps[i];
            if (gate < 0.999 && fr > gate) v = 0.0;
        } else {
            int i = (int)std::floor(u); double t = u - i;
            double a = steps[i % nSteps], b = steps[(i + 1) % nSteps];
            double sm3 = t * t * (3.0 - 2.0 * t);
            double tt = t + tension * (sm3 - t);
            v = a + (b - a) * tt;
        }
        return invert ? 1.0 - v : v;
    }
};

// ============================ LAYER =====================================================
struct Layer {
    FilterFx fi; DistFx di; DelayFx de; LoFiFx lo; FlangerFx fl; GlitchFx gl;
    double mixS[6] = {0, 0, 0, 0, 0, 0}, actS[6] = {0, 0, 0, 0, 0, 0};
    double levelS = 0, gL = 1, gR = 1, gLs = 1, gRs = 1, tremS = 1;
    double mixT[6] = {1, 1, 1, 1, 1, 1}; bool bypT[6] = {false}; double actCoef = 0.01;
    int order[6] = {0, 1, 2, 3, 4, 5};
    double level = 0.8, trem = 1.0; bool bypass = false;
    void prepare(double sr) {
        fi.prepare(sr); di.prepare(sr); de.prepare(sr); lo.prepare(sr); fl.prepare(sr); gl.prepare(sr);
        actCoef = smoothCoef(8.0, sr);
    }
    inline void processEffect(int e, double& l, double& r) {
        switch (e) {
            case 0: fi.process(l, r); break;
            case 1: di.process(l, r); break;
            case 2: de.process(l, r); break;
            case 3: lo.process(l, r); break;
            case 4: fl.process(l, r); break;
            default: gl.process(l, r); break;
        }
    }
    inline void process(double& l, double& r) {
        for (int k = 0; k < 6; ++k) {
            int e = order[k];
            double target = bypT[e] ? 0.0 : mixT[e];
            mixS[e] += actCoef * (target - mixS[e]);
            if (mixS[e] < 1e-5 && target == 0.0) { continue; }
            double dl = l, dr = r, wl = l, wr = r;
            processEffect(e, wl, wr);
            double m = mixS[e];
            if (e == 2) { l = dl + wl * m; r = dr + wr * m; }          // delay: additive wet
            else { l = dl + (wl - dl) * m; r = dr + (wr - dr) * m; }
        }
        levelS += 0.002 * (level - levelS);
        gLs += 0.01 * (gL - gLs); gRs += 0.01 * (gR - gRs); tremS += 0.01 * (trem - tremS);
        double g = levelS * 1.25 * tremS;
        l *= g * gLs; r *= g * gRs;
    }
};

// ============================ ENGINE ====================================================
class Engine {
public:
    static constexpr int SUB = 16;
    double sr = 48000;

    Engine() { for (int i = 0; i < NUM_PARAMS; ++i) base[i] = eff[i] = PARAMS[i].def; }

    void prepare(double sampleRate) {
        sr = sampleRate;
        for (auto& l : layer) l.prepare(sr);
        rev.prepare(sr);
        for (auto& m : mods) { m.phase = 0; m.sm = 1; }
        outLS = outRS = 0;
        for (int c = 0; c < 2; ++c) { eqLow[c].reset(); eqMid[c].reset(); eqHigh[c].reset(); }
        controlDirty = true;
        updateControl(SUB);
    }
    void reset() { prepare(sr); }

    static double toNorm(const ParamInfo& p, double v) {
        double n = (v - p.lo) / (p.hi - p.lo);
        n = clampd(n, 0.0, 1.0);
        if (p.centre > -1e29) { double sk = std::log(0.5) / std::log((p.centre - p.lo) / (p.hi - p.lo)); n = std::pow(n, sk); }
        return n;
    }
    static double fromNorm(const ParamInfo& p, double n) {
        n = clampd(n, 0.0, 1.0);
        if (p.centre > -1e29) { double sk = std::log(0.5) / std::log((p.centre - p.lo) / (p.hi - p.lo)); n = std::pow(n, 1.0 / sk); }
        return p.lo + n * (p.hi - p.lo);
    }

    void setParam(int idx, double v) {
        if (idx < 0 || idx >= NUM_PARAMS) return;
        if (idx == G_StepWrite) {
            long code = (long)(v + 0.5); int val = (int)(code % 1001); long r = code / 1001;
            int st = (int)(r % NUM_STEPS), md = (int)(r / NUM_STEPS);
            if (md >= 0 && md < NUM_MODS) mods[md].steps[st] = (float)(val * 0.001);
            return;
        }
        base[idx] = v; controlDirty = true;
    }
    double getParam(int idx) const { return base[idx]; }
    void setStep(int mod, int step, double v) { mods[mod].steps[step] = (float)clampd(v, 0, 1); }
    // Host transport (Force / MPC OS): called once per audio block before process(). ppq < 0 = the host gave no position.
    // With host sync on and the transport playing, tempo-synced modulators lock their phase to the song position, so a
    // pattern always starts on the bar; the delay / flanger tempo follows the host tempo too.
    void setHost(double ppq, double bpm, bool playing) {
        hostValid = (ppq >= 0.0 && bpm > 1.0); hostBpm = bpm > 1.0 ? bpm : 0.0; hostPpq = ppq; hostPlaying = playing; blkPos = 0;
    }
    void setHostSync(bool on) { hostSync = on; }
    double modValue(int m) const { return mods[m].out; }

    // process stereo in place
    void process(float* L, float* R, int n) {
        int done = 0;
        while (done < n) {
            int len = std::min(SUB, n - done);
            updateControl(len);
            blkPos += len;
            for (int i = 0; i < len; ++i) {
                double l = L[done + i], r = R[done + i];
                if (!std::isfinite(l)) l = 0;
                if (!std::isfinite(r)) r = 0;
                processSample(l, r);
                L[done + i] = (float)l; R[done + i] = (float)r;
            }
            done += len;
        }
    }

private:
    double base[NUM_PARAMS], eff[NUM_PARAMS], accum[NUM_PARAMS];
    Layer layer[2]; Reverb rev; Modulator mods[NUM_MODS]; double modOut[NUM_MODS] = {0};
    Biquad eqLow[2], eqMid[2], eqHigh[2];
    double outLS = 0, outRS = 0, outVolS = 1, outMixS = 1, revMixS = 0, revInS = 1;
    bool controlDirty = true; bool masterEq = true, masterRev = true, masterOn = true, clip = true; bool parallel = false;
    double eqS[3] = {0, 0, 0}; double outGain = 1, outPan = 0, outMix = 1, revMix = 0, revIn = 1;
    double trem[3] = {1, 1, 1}, flip[3] = {0, 0, 0};
    double hostBpmUsed = 120, speedMul = 1;
    bool anyEq = false;
    bool hostSync = true, hostValid = false, hostPlaying = false; double hostBpm = 0, hostPpq = 0; int blkPos = 0;
    inline double bpmEff() const { return (hostSync && hostBpm > 1.0) ? hostBpm : std::max(20.0, base[G_Bpm]); }
    inline bool locked() const { return hostSync && hostValid && hostPlaying; }

    inline double E(int i) const { return eff[i]; }
    inline double LE(int l, int rel) const { return eff[LAYER_BASE[l] + rel]; }

    void updateModulation(int len) {
        const double bpm = bpmEff(); speedMul = base[G_Speed];
        const double ppqNow = hostPpq + (double)blkPos * bpm / 60.0 / sr; const bool lock = locked();
        // 1. advance & evaluate modulators
        for (int m = 0; m < NUM_MODS; ++m) {
            int b = MOD_BASE[m];
            Modulator& M = mods[m];
            int steps = (int)(base[b + MD_Steps] + 0.5); steps = std::max(2, std::min(NUM_STEPS, steps));
            bool sync = base[b + MD_Sync] > 0.5;
            double rate;                                     // pattern cycles per second
            if (sync) { int di = std::max(0, std::min(NUM_DIVS - 1, (int)(base[b + MD_Div] + 0.5))); rate = (bpm / 60.0) / DIV_BEATS[di]; }
            else rate = base[b + MD_Speed];
            rate *= speedMul;
            if (sync && lock) {                              // hard lock to the song position
                int di = std::max(0, std::min(NUM_DIVS - 1, (int)(base[b + MD_Div] + 0.5)));
                M.phase = ppqNow * speedMul / DIV_BEATS[di]; M.phase -= std::floor(M.phase);
            } else { M.phase += rate * len / sr; M.phase -= std::floor(M.phase); }
            double v = M.evaluate((int)(base[b + MD_Mode] + 0.5), steps, base[b + MD_Shuffle], base[b + MD_Gate],
                                  base[b + MD_Tension], base[b + MD_Shift], base[b + MD_Invert] > 0.5);
            double sm = base[b + MD_Smooth];
            if (sm > 0.001) {
                double stepDur = 1.0 / std::max(1e-3, rate * steps);
                double tau = sm * stepDur * 0.8 + 1e-4;
                M.sm += (1.0 - std::exp(-(double)len / sr / tau)) * (v - M.sm);
                v = M.sm;
            } else M.sm = v;
            M.out = v; modOut[m] = v;
        }
        // 2. matrix
        double* acc = accum;
        bool any = false;
        for (int s = 0; s < NUM_MATRIX; ++s) {
            int xb = MX_BASE + s * MX_SIZE;
            int src = (int)(base[xb + MXR_Src] + 0.5), dst = (int)(base[xb + MXR_Dst] + 0.5);
            if (src <= 0 || dst <= 0 || dst >= NUM_DESTS || base[xb + MXR_Byp] > 0.5) continue;
            if (!any) { std::fill(acc, acc + NUM_PARAMS, 0.0); any = true; }
            acc[DEST_PARAM[dst]] += base[xb + MXR_Depth] * modOut[src - 1];
        }
        for (int i = 0; i < NUM_PARAMS; ++i) {
            const ParamInfo& p = PARAMS[i];
            if (p.mod && any && acc[i] != 0.0) eff[i] = fromNorm(p, toNorm(p, base[i]) + acc[i]);
            else eff[i] = base[i];
        }
        // 3. tremolo / pan flip per destination
        for (int d = 0; d < 3; ++d) { trem[d] = 1.0; flip[d] = 0.0; }
        for (int m = 0; m < NUM_MODS; ++m) {
            int b = MOD_BASE[m];
            int td = (int)(base[b + MD_TremDest] + 0.5), fd = (int)(base[b + MD_FlipDest] + 0.5);
            if (td > 0) trem[td - 1] *= 1.0 - base[b + MD_TremDepth] * (1.0 - modOut[m]);
            if (fd > 0) flip[fd - 1] += base[b + MD_FlipDepth] * (modOut[m] * 2.0 - 1.0);
        }
    }

    void updateControl(int len) {
        updateModulation(len);
        parallel = base[G_Routing] > 0.5;
        for (int l = 0; l < 2; ++l) {
            Layer& L = layer[l];
            // effect order (defensive: guarantees a permutation)
            bool used[6] = {false}; int k = 0;
            for (int s = 0; s < 6; ++s) { int e = (int)(LE(l, L_Slot1 + s) + 0.5); e = std::max(0, std::min(5, e)); if (!used[e]) { used[e] = true; L.order[k++] = e; } }
            for (int e = 0; e < 6 && k < 6; ++e) if (!used[e]) L.order[k++] = e;
            L.mixT[0] = LE(l, L_FiMix); L.bypT[0] = LE(l, L_FiByp) > 0.5;
            L.mixT[1] = LE(l, L_DiMix); L.bypT[1] = LE(l, L_DiByp) > 0.5;
            L.mixT[2] = LE(l, L_DeMix); L.bypT[2] = LE(l, L_DeByp) > 0.5;
            L.mixT[3] = LE(l, L_LoMix); L.bypT[3] = LE(l, L_LoByp) > 0.5;
            L.mixT[4] = LE(l, L_FlMix); L.bypT[4] = LE(l, L_FlByp) > 0.5;
            L.mixT[5] = LE(l, L_GlMix); L.bypT[5] = LE(l, L_GlByp) > 0.5;
            L.fi.update((int)(LE(l, L_FiType) + 0.5), LE(l, L_FiCut), LE(l, L_FiRes), LE(l, L_FiLow), LE(l, L_FiHi));
            L.di.update((int)(LE(l, L_DiType) + 0.5), LE(l, L_DiDrive), LE(l, L_DiFuzz), LE(l, L_DiMono) > 0.5, LE(l, L_DiTone), LE(l, L_DiOut));
            double bpm = bpmEff();
            bool dsync = LE(l, L_DeSync) > 0.5;
            double msL = LE(l, L_DeL), msR = LE(l, L_DeR);
            if (dsync) {
                int a = std::max(0, std::min(NUM_DIVS - 1, (int)(LE(l, L_DeLS) + 0.5))), b = std::max(0, std::min(NUM_DIVS - 1, (int)(LE(l, L_DeRS) + 0.5)));
                msL = DIV_BEATS[a] * 60000.0 / bpm; msR = DIV_BEATS[b] * 60000.0 / bpm;
            }
            L.de.update(msL, msR, LE(l, L_DePing) > 0.5, LE(l, L_DeFb), LE(l, L_DeLP), LE(l, L_DeBP), LE(l, L_DeIn));
            L.lo.update(LE(l, L_LoBit), LE(l, L_LoRate), LE(l, L_LoNoise), LE(l, L_LoTone));
            double fr = LE(l, L_FlSpeed);
            if (LE(l, L_FlSync) > 0.5) { int di = std::max(0, std::min(NUM_DIVS - 1, (int)(LE(l, L_FlDiv) + 0.5))); fr = (bpm / 60.0) / DIV_BEATS[di]; }
            L.fl.update((int)(LE(l, L_FlMode) + 0.5), LE(l, L_FlDelay), LE(l, L_FlFb), LE(l, L_FlWidth), LE(l, L_FlTone), fr, LE(l, L_FlDepth));
            L.gl.update((int)(LE(l, L_GlMode) + 0.5), LE(l, L_GlSize), LE(l, L_GlScale), LE(l, L_GlRev) > 0.5, LE(l, L_GlSmooth), LE(l, L_GlStretch));
            L.level = LE(l, L_Level); L.bypass = LE(l, L_Bypass) > 0.5;
            L.trem = trem[1 + l] * trem[0] ;      // layer + global tremolo is applied here for simplicity
            double pan = clampd(LE(l, L_Pan) + flip[1 + l] + flip[0] * 0.0, -1.0, 1.0);
            L.gL = pan > 0 ? 1.0 - pan : 1.0; L.gR = pan < 0 ? 1.0 + pan : 1.0;
        }
        masterOn = E(G_MasterOn) > 0.5; masterEq = masterOn && E(G_EqOn) > 0.5; masterRev = masterOn && E(G_RvOn) > 0.5; clip = masterOn && E(G_OutClip) > 0.5;
        if (masterEq) {
            for (int c = 0; c < 2; ++c) {
                eqLow[c].setLowShelf(120.0, sr, E(G_EqBass)); eqMid[c].setPeak(E(G_EqFreq), sr, E(G_EqMid), 0.8); eqHigh[c].setHighShelf(7000.0, sr, E(G_EqTreble));
            }
        }
        rev.update(E(G_RvSize), E(G_RvDecay), E(G_RvBass), E(G_RvTreble));
        revMix = E(G_RvMix); revIn = E(G_RvIn);
        outGain = dbToGain(E(G_OutVol));
        double pan = clampd(E(G_OutPan) + flip[0], -1.0, 1.0);
        outPan = pan; outMix = E(G_OutMix);
        controlDirty = false;
    }

    inline void processSample(double& l, double& r) {
        const double dryL = l, dryR = r;
        double ol, orr;
        if (!parallel) {
            ol = l; orr = r;
            for (int i = 0; i < 2; ++i) if (!layer[i].bypass) layer[i].process(ol, orr);
        } else {
            double al = l, ar = r, bl = l, br = r;
            if (!layer[0].bypass) layer[0].process(al, ar); else { al = ar = 0; }
            if (!layer[1].bypass) layer[1].process(bl, br); else { bl = br = 0; }
            ol = al + bl; orr = ar + br;
        }
        if (masterOn) {
            if (masterEq) {
                ol = eqHigh[0].tick(eqMid[0].tick(eqLow[0].tick(ol)));
                orr = eqHigh[1].tick(eqMid[1].tick(eqLow[1].tick(orr)));
            }
            if (masterRev) {
                double rl, rr; rev.process(ol * revIn, orr * revIn, rl, rr);
                revMixS += 0.003 * (revMix - revMixS);
                ol += rl * revMixS * 1.6; orr += rr * revMixS * 1.6;
            }
            if (clip) { ol = softClip(ol); orr = softClip(orr); }
        }
        // output pan / volume / mix
        outLS += 0.01 * (outGain - outLS);
        double pl = outPan > 0 ? 1.0 - outPan : 1.0, pr = outPan < 0 ? 1.0 + outPan : 1.0;
        outMixS += 0.003 * (outMix - outMixS);
        double wl = ol * pl, wr = orr * pr;
        l = (dryL * (1.0 - outMixS) + wl * outMixS) * outLS;
        r = (dryR * (1.0 - outMixS) + wr * outMixS) * outLS;
        l = clampd(l, -16, 16); r = clampd(r, -16, 16);
    }
    static inline double softClip(double x) {
        if (x >= 1.5) return 1.0;
        if (x <= -1.5) return -1.0;
        return x - (4.0 / 27.0) * x * x * x;
    }
};

} // namespace mx
