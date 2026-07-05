#include <math.h>
#include <stdint.h>

double fabs(double x) { return x < 0.0 ? -x : x; }
float fabsf(float x) { return x < 0.0f ? -x : x; }

double sqrt(double x) {
    if (x <= 0.0) return 0.0;
    double g = x > 1.0 ? x : 1.0;
    for (int i = 0; i < 16; ++i) g = 0.5 * (g + x / g);
    return g;
}
float sqrtf(float x) { return (float)sqrt((double)x); }

#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define M_PI_4 0.78539816339744830962

/* Lookup table for sin on [0, π/2] with 4096 entries (interpolated) */
#define SIN_TABLE_BITS 12
#define SIN_TABLE_SIZE (1 << SIN_TABLE_BITS)
#define SIN_TABLE_MASK (SIN_TABLE_SIZE - 1)

static double g_sin_table[SIN_TABLE_SIZE + 1];
static uint8_t g_sin_table_init = 0;

static void sin_table_init(void) {
    if (g_sin_table_init) return;
    for (int i = 0; i <= SIN_TABLE_SIZE; ++i) {
        double a = (double)i * (M_PI_2 / (double)SIN_TABLE_SIZE);
        double x = a;
        double x3 = x * x * x;
        double x5 = x3 * x * x;
        double x7 = x5 * x * x;
        double x9 = x7 * x * x;
        g_sin_table[i] = x - x3 / 6.0 + x5 / 120.0 - x7 / 5040.0 + x9 / 362880.0;
    }
    g_sin_table_init = 1;
}

static double sin_lookup(double x) {
    sin_table_init();
    if (x < 0.0) return -sin_lookup(-x);
    if (x > M_PI_2) {
        if (x <= M_PI) return sin_lookup(M_PI - x);
        if (x <= 2.0 * M_PI) return -sin_lookup(x - M_PI);
        x -= (double)((int)(x / (2.0 * M_PI))) * (2.0 * M_PI);
        if (x < 0.0) x += 2.0 * M_PI;
        if (x <= M_PI) {
            if (x <= M_PI_2) return sin_lookup(x);
            return sin_lookup(M_PI - x);
        }
        return -sin_lookup(x - M_PI);
    }
    double idx = x * (double)SIN_TABLE_SIZE / M_PI_2;
    int i = (int)idx;
    if (i < 0) i = 0;
    if (i > SIN_TABLE_MASK) i = SIN_TABLE_MASK;
    double frac = idx - (double)i;
    return g_sin_table[i] + frac * (g_sin_table[i + 1] - g_sin_table[i]);
}

double sin(double x) {
    return sin_lookup(x);
}

double cos(double x) {
    return sin_lookup(x + M_PI_2);
}

double tan(double x) {
    double s = sin_lookup(x);
    double c = sin_lookup(x + M_PI_2);
    if (c == 0.0) c = 1e-300;
    return s / c;
}

double atan(double x) {
    if (x < 0.0) return -atan(-x);
    if (x > 1.0) return M_PI_2 - atan(1.0 / x);
    double x2 = x * x;
    double x3 = x2 * x;
    double x5 = x3 * x2;
    double x7 = x5 * x2;
    return x - x3 / 3.0 + x5 / 5.0 - x7 / 7.0;
}

double atan2(double y, double x) {
    if (x == 0.0 && y == 0.0) return 0.0;
    if (x > 0.0) return atan(y / x);
    if (x < 0.0) {
        if (y >= 0.0) return atan(y / x) + M_PI;
        return atan(y / x) - M_PI;
    }
    if (y > 0.0) return M_PI_2;
    if (y < 0.0) return -M_PI_2;
    return 0.0;
}

double floor(double x) {
    long long i = (long long)x;
    if (x < 0.0 && x != (double)i) return (double)(i - 1);
    return (double)i;
}

double ceil(double x) {
    long long i = (long long)x;
    if (x > 0.0 && x != (double)i) return (double)(i + 1);
    return (double)i;
}

double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    long long yi = (long long)y;
    if (y == (double)yi) {
        int neg = 0;
        if (yi < 0) {
            neg = 1;
            yi = -yi;
        }
        double res = 1.0;
        double base = x;
        while (yi) {
            if (yi & 1LL) res *= base;
            base *= base;
            yi >>= 1LL;
        }
        return neg ? (1.0 / res) : res;
    }
    return 0.0;
}

double fmod(double x, double y) {
    if (y == 0.0) return 0.0;
    double q = x / y;
    double t = q >= 0.0 ? floor(q) : ceil(q);
    return x - (t * y);
}

double frexp(double x, int *exp) {
    union {
        double d;
        uint64_t u;
    } v = { x };
    int e = (int)((v.u >> 52) & 0x7FF);
    if (e == 0) {
        if (x == 0.0) {
            if (exp) *exp = 0;
            return 0.0;
        }
        v.d *= (double)(1ULL << 52);
        e = (int)((v.u >> 52) & 0x7FF);
        e -= 52;
    }
    if (exp) *exp = e - 1022;
    v.u = (v.u & ((1ULL << 52) - 1)) | ((uint64_t)1022 << 52);
    return v.d;
}

double ldexp(double x, int exp) {
    union {
        double d;
        uint64_t u;
    } v = { x };
    int e = (int)((v.u >> 52) & 0x7FF);
    if (e == 0) {
        if (x == 0.0) return 0.0;
        v.d *= (double)(1ULL << 52);
        e = (int)((v.u >> 52) & 0x7FF);
        e -= 52;
    }
    e += exp;
    if (e <= 0) return 0.0;
    if (e >= 0x7FF) return x > 0.0 ? (1.0 / 0.0) : (-1.0 / 0.0);
    v.u = (v.u & ((1ULL << 52) - 1)) | ((uint64_t)e << 52);
    return v.d;
}

long double ldexpl(long double x, int exp) {
    return (long double)ldexp((double)x, exp);
}

int isnan(double x) {
    union {
        double d;
        uint64_t u;
    } v = { x };
    return ((v.u >> 52) & 0x7FF) == 0x7FF && (v.u & ((1ULL << 52) - 1)) != 0;
}

int isinf(double x) {
    union {
        double d;
        uint64_t u;
    } v = { x };
    return ((v.u >> 52) & 0x7FF) == 0x7FF && (v.u & ((1ULL << 52) - 1)) == 0;
}

double log10(double x) {
    if (x <= 0.0) return -(1.0 / 0.0);
    int exp10 = 0;
    while (x >= 10.0) {
        x /= 10.0;
        exp10++;
        if (exp10 > 308) break;
    }
    while (x < 1.0) {
        x *= 10.0;
        exp10--;
        if (exp10 < -308) break;
    }
    return (double)exp10;
}
