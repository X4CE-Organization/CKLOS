#ifndef CKLOS_STDCKDINT_H
#define CKLOS_STDCKDINT_H

#define __STDC_VERSION_STDCKDINT_H__ 202311L

#define __cklos_ckd_add_signed(result, left, right) \
    __extension__ ({ \
        __typeof__(*(result)) cklos_l = (left), cklos_r = (right); \
        __typeof__(*(result)) cklos_v = (__typeof__(*(result)))(cklos_l + cklos_r); \
        *(result) = cklos_v; \
        (cklos_l > 0 && cklos_r > 0 && cklos_v < 0) \
        || (cklos_l < 0 && cklos_r < 0 && cklos_v >= 0); \
    })

#define __cklos_ckd_add_unsigned(result, left, right) \
    __extension__ ({ \
        __typeof__(*(result)) cklos_l = (left), cklos_r = (right); \
        __typeof__(*(result)) cklos_v = (__typeof__(*(result)))(cklos_l + cklos_r); \
        *(result) = cklos_v; \
        cklos_v < cklos_l; \
    })

#define __cklos_ckd_sub_signed(result, left, right) \
    __extension__ ({ \
        __typeof__(*(result)) cklos_l = (left), cklos_r = (right); \
        __typeof__(*(result)) cklos_v = (__typeof__(*(result)))(cklos_l - cklos_r); \
        *(result) = cklos_v; \
        (cklos_l >= 0 && cklos_r < 0 && cklos_v < 0) \
        || (cklos_l < 0 && cklos_r > 0 && cklos_v >= 0); \
    })

#define __cklos_ckd_sub_unsigned(result, left, right) \
    __extension__ ({ \
        __typeof__(*(result)) cklos_l = (left), cklos_r = (right); \
        __typeof__(*(result)) cklos_v = (__typeof__(*(result)))(cklos_l - cklos_r); \
        *(result) = cklos_v; \
        cklos_l < cklos_r; \
    })

#define __cklos_ckd_mul_signed(result, left, right) \
    __extension__ ({ \
        __typeof__(*(result)) cklos_l = (left), cklos_r = (right); \
        __typeof__(*(result)) cklos_v = (__typeof__(*(result)))(cklos_l * cklos_r); \
        *(result) = cklos_v; \
        cklos_l && cklos_v / cklos_l != cklos_r; \
    })

#define __cklos_ckd_mul_unsigned(result, left, right) \
    __extension__ ({ \
        __typeof__(*(result)) cklos_l = (left), cklos_r = (right); \
        __typeof__(*(result)) cklos_v = (__typeof__(*(result)))(cklos_l * cklos_r); \
        *(result) = cklos_v; \
        cklos_l && cklos_v / cklos_l != cklos_r; \
    })

#define ckd_add(result, left, right) \
    _Generic((*(result)), \
        signed char: __cklos_ckd_add_signed, short: __cklos_ckd_add_signed, \
        int: __cklos_ckd_add_signed, long: __cklos_ckd_add_signed, \
        long long: __cklos_ckd_add_signed, default: __cklos_ckd_add_unsigned) \
        (result, left, right)
#define ckd_sub(result, left, right) \
    _Generic((*(result)), \
        signed char: __cklos_ckd_sub_signed, short: __cklos_ckd_sub_signed, \
        int: __cklos_ckd_sub_signed, long: __cklos_ckd_sub_signed, \
        long long: __cklos_ckd_sub_signed, default: __cklos_ckd_sub_unsigned) \
        (result, left, right)
#define ckd_mul(result, left, right) \
    _Generic((*(result)), \
        signed char: __cklos_ckd_mul_signed, short: __cklos_ckd_mul_signed, \
        int: __cklos_ckd_mul_signed, long: __cklos_ckd_mul_signed, \
        long long: __cklos_ckd_mul_signed, default: __cklos_ckd_mul_unsigned) \
        (result, left, right)

#endif
