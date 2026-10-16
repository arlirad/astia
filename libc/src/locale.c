#include <locale.h>

static struct lconv c_locale = {
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
    .int_frac_digits = (char) 127,
    .frac_digits = (char) 127,
    .p_cs_precedes = (char) 127,
    .p_sep_by_space = (char) 127,
    .n_cs_precedes = (char) 127,
    .n_sep_by_space = (char) 127,
    .p_sign_posn = (char) 127,
    .n_sign_posn = (char) 127,
    .int_p_cs_precedes = (char) 127,
    .int_p_sep_by_space = (char) 127,
    .int_n_cs_precedes = (char) 127,
    .int_n_sep_by_space = (char) 127,
    .int_p_sign_posn = (char) 127,
    .int_n_sign_posn = (char) 127
};

char* setlocale(int category, const char* locale)
{
    if (category != LC_ALL &&
        (category < LC_COLLATE || category > LC_TIME))
        return NULL;
    if (locale == NULL || locale[0] == '\0' ||
        (locale[0] == 'C' && locale[1] == '\0') ||
        (locale[0] == 'P' && locale[1] == 'O' && locale[2] == 'S' &&
         locale[3] == 'I' && locale[4] == 'X' && locale[5] == '\0'))
        return "C";
    return NULL;
}

struct lconv* localeconv(void)
{
    return &c_locale;
}
