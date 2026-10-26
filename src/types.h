#ifndef CKLOS_TYPES_H
#define CKLOS_TYPES_H

typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef unsigned short uint16_t;
typedef signed short int16_t;
typedef unsigned int uint32_t;
typedef signed int int32_t;
typedef unsigned long uint64_t;
typedef signed long int64_t;
typedef unsigned long size_t;
typedef unsigned long uintptr_t;
#define NULL ((void *)0)

#ifndef __bool_true_false_are_defined
typedef _Bool bool;
#define true 1
#define false 0
#define __bool_true_false_are_defined 1
#endif

#endif
