
/* Arbitrary-width float helpers: IEEE-like 1|E|M layout in a 64-bit container (bits<=64) */
#include <stdint.h>
#include <math.h>

static int exp_bits(int bits) {
    if (bits == 8) return 4;
    if (bits == 16) return 5;
    if (bits == 32) return 8;
    if (bits == 64) return 11;
    if (bits == 128) return 15;
    int e = (bits * 3) / 16 + 2;
    if (e < 2) e = 2;
    if (e > bits - 2) e = bits - 2;
    return e;
}
static int mant_bits(int bits) { return bits - 1 - exp_bits(bits); }
static int bias_of(int bits) { int e = exp_bits(bits); return (1 << (e - 1)) - 1; }

/* Pack double into low `bits` of uint64 following 1|E|M */
uint64_t basalt_float_pack_n(double x, int64_t bits) {
    if (bits >= 64) {
        union { double d; uint64_t u; } v; v.d = x; return v.u;
    }
    int E = exp_bits((int)bits), M = mant_bits((int)bits), B = bias_of((int)bits);
    if (x == 0.0) return 0;
    int sign = x < 0; if (sign) x = -x;
    int exp; double frac = frexp(x, &exp); /* frac in [0.5,1) */
    /* convert to [1,2) mantissa */
    frac *= 2; exp -= 1;
    int e = exp + B;
    if (e <= 0) return (uint64_t)sign << (bits - 1); /* underflow → 0/min */
    if (e >= (1 << E) - 1) {
        /* overflow → inf */
        uint64_t u = ((uint64_t)sign << (bits - 1)) | ((uint64_t)((1 << E) - 1) << M);
        return u;
    }
    uint64_t mant = (uint64_t)((frac - 1.0) * ((double)(1ULL << M)));
    uint64_t u = ((uint64_t)sign << (bits - 1)) | ((uint64_t)e << M) | (mant & ((1ULL << M) - 1));
    return u;
}

double basalt_float_unpack_n(uint64_t u, int64_t bits) {
    if (bits >= 64) {
        union { double d; uint64_t u; } v; v.u = u; return v.d;
    }
    int E = exp_bits((int)bits), M = mant_bits((int)bits), B = bias_of((int)bits);
    int sign = (int)((u >> (bits - 1)) & 1);
    int e = (int)((u >> M) & ((1 << E) - 1));
    uint64_t mant = u & ((1ULL << M) - 1);
    if (e == 0) return sign ? -0.0 : 0.0;
    if (e == (1 << E) - 1) return sign ? -INFINITY : INFINITY;
    double frac = 1.0 + (double)mant / (double)(1ULL << M);
    double val = ldexp(frac, e - B);
    return sign ? -val : val;
}

double basalt_fadd_d(double a, double b) { return a + b; }
double basalt_fmul_d(double a, double b) { return a * b; }
