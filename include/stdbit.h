#ifndef CKLOS_STDBIT_H
#define CKLOS_STDBIT_H

#define __STDC_VERSION_STDBIT_H__ 202311L

#define stdc_leading_zeros(value) \
    ((unsigned int)__builtin_clzll((unsigned long long)(value)))
#define stdc_leading_ones(value) \
    stdc_leading_zeros(~(unsigned long long)(value))
#define stdc_trailing_zeros(value) \
    ((unsigned int)__builtin_ctzll((unsigned long long)(value)))
#define stdc_trailing_ones(value) \
    stdc_trailing_zeros(~(unsigned long long)(value))
#define stdc_count_ones(value) \
    ((unsigned int)__builtin_popcountll((unsigned long long)(value)))
#define stdc_count_zeros(value) \
    stdc_count_ones(~(unsigned long long)(value))
#define stdc_has_single_bit(value) (stdc_count_ones(value) == 1)
#define stdc_bit_width(value) \
    (64U - stdc_leading_zeros((unsigned long long)(value)))
#define stdc_bit_floor(value) \
    ((value) ? (1ULL << (stdc_bit_width(value) - 1U)) : 0)
#define stdc_bit_ceil(value) \
    ((value) <= 1 ? 1ULL : (1ULL << (64U - stdc_leading_zeros((value) - 1U))))

#endif
