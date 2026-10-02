/* Integer limits for the freestanding GCC build (no host libc headers). */
#ifndef ESCHATON_FREESTANDING_LIMITS_H
#define ESCHATON_FREESTANDING_LIMITS_H
#define CHAR_BIT __CHAR_BIT__
#define INT_MAX __INT_MAX__
#define INT_MIN (-INT_MAX - 1)
#define UINT_MAX (INT_MAX * 2U + 1U)
#define LONG_MAX __LONG_MAX__
#define LONG_MIN (-LONG_MAX - 1L)
#define ULONG_MAX (LONG_MAX * 2UL + 1UL)
#endif
