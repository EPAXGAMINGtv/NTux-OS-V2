#include <locale.h>

static struct lconv g_lconv = {
    .decimal_point = ".",
    .thousands_sep = "",
    .grouping = "",
    .int_curr_symbol = "",
    .currency_symbol = "",
    .mon_decimal_point = "",
    .mon_thousands_sep = "",
    .mon_grouping = "",
    .positive_sign = "",
    .negative_sign = "",
    .int_frac_digits = 0,
    .frac_digits = 0,
    .p_cs_precedes = 0,
    .p_sep_by_space = 0,
    .n_cs_precedes = 0,
    .n_sep_by_space = 0,
    .p_sign_posn = 0,
    .n_sign_posn = 0
};

char *setlocale(int category, const char *locale) {
    (void)category;
    (void)locale;
    return "C";
}

struct lconv *localeconv(void) {
    return &g_lconv;
}
