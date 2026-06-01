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

double sin(double x) { (void)x; return 0.0; }
double cos(double x) { (void)x; return 1.0; }
double tan(double x) { (void)x; return 0.0; }
double atan(double x) { (void)x; return 0.0; }
double atan2(double y, double x) { (void)y; (void)x; return 0.0; }
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
