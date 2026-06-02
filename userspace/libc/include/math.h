#ifndef USER_MATH_H
#define USER_MATH_H

#define INFINITY (1.0/0.0)
#define NAN (0.0/0.0)

double fabs(double x);
float fabsf(float x);
double sqrt(double x);
float sqrtf(float x);
int isnan(double x);
int isinf(double x);
double log10(double x);

double sin(double x);
double cos(double x);
double tan(double x);
double atan(double x);
double atan2(double y, double x);
double floor(double x);
double ceil(double x);
double pow(double x, double y);
double fmod(double x, double y);
double frexp(double x, int *exp);
double ldexp(double x, int exp);
long double ldexpl(long double x, int exp);

#endif
