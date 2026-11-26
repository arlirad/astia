#include <math.h>

double fabs(double value) {
    return __builtin_fabs(value);
}

double floor(double value) {
    return __builtin_floor(value);
}

double ceil(double value) {
    return __builtin_ceil(value);
}

double sqrt(double value) {
    return __builtin_sqrt(value);
}

double pow(double base, double exponent) {
    return __builtin_pow(base, exponent);
}

double sin(double value) {
    return __builtin_sin(value);
}

double cos(double value) {
    return __builtin_cos(value);
}

double tan(double value) {
    return __builtin_tan(value);
}

double log(double value) {
    return __builtin_log(value);
}

double exp(double value) {
    return __builtin_exp(value);
}

double fmod(double numerator, double denominator) {
    return __builtin_fmod(numerator, denominator);
}

double ldexp(double value, int exponent) {
    return __builtin_ldexp(value, exponent);
}
