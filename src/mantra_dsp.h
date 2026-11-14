// MANTRA DSP building blocks. Plain C++17, no HISE / JUCE dependency.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <vector>

namespace mx {

constexpr double kPi = 3.14159265358979323846;

inline double clampd(double x, double a, double b) { return x < a ? a : (x > b ? b : x); }
inline double dbToGain(double db) { return std::pow(10.0, db / 20.0); }
inline double fixDenorm(double x) { return (std::fabs(x) < 1e-20) ? 0.0 : x; }

struct Rng {
    uint32_t s = 0x1234567u;
    inline uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline double uni() { return (next() >> 8) * (1.0 / 16777216.0); }          // 0..1
    inline double bi() { return uni() * 2.0 - 1.0; }                             // -1..1
};

struct OnePole {
    double a = 0, z = 0;
    void setLP(double fc, double sr) { a = 1.0 - std::exp(-2.0 * kPi * clampd(fc, 5.0, sr * 0.45) / sr); }
    inline double lp(double x) { z += a * (x - z); z = fixDenorm(z); return z; }
    inline double hp(double x) { return x - lp(x); }
    void reset() { z = 0; }
};

// Zavalishin TPT state variable filter, optional soft saturation in the integrators ("Dark")
struct Svf {
    double g = 0.1, k = 1.0, a1 = 0, s1 = 0, s2 = 0;
    double lp = 0, bp = 0, hp = 0;
    void set(double fc, double sr, double kk) {
        g = std::tan(kPi * clampd(fc, 10.0, sr * 0.45) / sr);
        k = kk;
        a1 = 1.0 / (1.0 + g * (g + k));
    }
    inline void tick(double x, bool sat) {
        hp = (x - (k + g) * s1 - s2) * a1;
        bp = g * hp + s1;
        lp = g * bp + s2;
        double bpS = sat ? std::tanh(bp) : bp;
        s1 = g * hp + bpS;
        s2 = g * bp + lp;
        s1 = fixDenorm(s1); s2 = fixDenorm(s2);
        if (sat) { s1 = clampd(s1, -8, 8); }
    }
    void reset() { s1 = s2 = 0; lp = bp = hp = 0; }
};

// Chamberlin SVF ("Legacy" filters)
struct Chamberlin {
    double f = 0.1, q = 1.0, low = 0, band = 0, high = 0;
    void set(double fc, double sr, double qq) {
        f = 2.0 * std::sin(kPi * clampd(fc, 10.0, sr / 6.0) / sr);
        q = qq;
    }
    inline void tick(double x) {
        low += f * band;
        high = x - low - q * band;
        band += f * high;
        low = fixDenorm(low); band = fixDenorm(band);
        if (std::fabs(band) > 50.0) { low = band = 0; }
    }
    void reset() { low = band = high = 0; }
};

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline double tick(double x) {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        z1 = fixDenorm(z1); z2 = fixDenorm(z2);
        return y;
    }
    void reset() { z1 = z2 = 0; }
    void setLP(double fc, double sr, double q) {
        double w = 2 * kPi * clampd(fc, 10, sr * 0.49) / sr, c = std::cos(w), s = std::sin(w), al = s / (2 * q);
        double a0 = 1 + al;
        b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    void setLowShelf(double fc, double sr, double gainDb) {
        double A = std::pow(10.0, gainDb / 40.0), w = 2 * kPi * clampd(fc, 10, sr * 0.49) / sr;
        double c = std::cos(w), s = std::sin(w), beta = 2 * std::sqrt(A) * (s / 2) * std::sqrt(2.0);
        double a0 = (A + 1) + (A - 1) * c + beta;
        b0 = A * ((A + 1) - (A - 1) * c + beta) / a0;
        b1 = 2 * A * ((A - 1) - (A + 1) * c) / a0;
        b2 = A * ((A + 1) - (A - 1) * c - beta) / a0;
        a1 = -2 * ((A - 1) + (A + 1) * c) / a0;
        a2 = ((A + 1) + (A - 1) * c - beta) / a0;
    }
    void setHighShelf(double fc, double sr, double gainDb) {
        double A = std::pow(10.0, gainDb / 40.0), w = 2 * kPi * clampd(fc, 10, sr * 0.49) / sr;
        double c = std::cos(w), s = std::sin(w), beta = 2 * std::sqrt(A) * (s / 2) * std::sqrt(2.0);
        double a0 = (A + 1) - (A - 1) * c + beta;
        b0 = A * ((A + 1) + (A - 1) * c + beta) / a0;
        b1 = -2 * A * ((A - 1) + (A + 1) * c) / a0;
        b2 = A * ((A + 1) + (A - 1) * c - beta) / a0;
        a1 = 2 * ((A - 1) - (A + 1) * c) / a0;
        a2 = ((A + 1) - (A - 1) * c - beta) / a0;
    }
    void setPeak(double fc, double sr, double gainDb, double q) {
        double A = std::pow(10.0, gainDb / 40.0), w = 2 * kPi * clampd(fc, 10, sr * 0.49) / sr;
        double c = std::cos(w), s = std::sin(w), al = s / (2 * q);
        double a0 = 1 + al / A;
        b0 = (1 + al * A) / a0; b1 = -2 * c / a0; b2 = (1 - al * A) / a0;
        a1 = -2 * c / a0; a2 = (1 - al / A) / a0;
    }
};

// delay line, power of two length, 4 point hermite read
struct DelayLine {
    std::vector<float> buf; int mask = 0, w = 0;
    void init(int minLen) {
        int n = 16; while (n < minLen) n <<= 1;
        buf.assign((size_t)n, 0.f); mask = n - 1; w = 0;
    }
    void clear() { std::fill(buf.begin(), buf.end(), 0.f); }
    inline void write(float x) { buf[(size_t)w] = x; w = (w + 1) & mask; }
    // delay in samples (>=1)
    inline float read(double d) const {
        double rp = (double)w - 1.0 - d;
        double fl = std::floor(rp);
        int i = (int)fl; float t = (float)(rp - fl);
        float y0 = buf[(size_t)((i - 1) & mask)], y1 = buf[(size_t)(i & mask)],
              y2 = buf[(size_t)((i + 1) & mask)], y3 = buf[(size_t)((i + 2) & mask)];
        float c0 = y1, c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3,
              c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }
    inline float readAbs(double p) const {   // absolute position in circular buffer
        double fl = std::floor(p); int i = (int)fl; float t = (float)(p - fl);
        float y0 = buf[(size_t)((i - 1) & mask)], y1 = buf[(size_t)(i & mask)],
              y2 = buf[(size_t)((i + 1) & mask)], y3 = buf[(size_t)((i + 2) & mask)];
        float c0 = y1, c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3,
              c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }
};

inline double smoothCoef(double ms, double sr) { return 1.0 - std::exp(-1.0 / (0.001 * ms * sr)); }

inline double softSat(double x) { return std::tanh(x); }

} // namespace mx
