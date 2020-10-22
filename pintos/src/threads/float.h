#ifndef THREADS_PALLOC_H
#define THREADS_PALLOC_H


#include <stdbool.h>
#include <stdint.h>
typedef int64_t fixed_t;

#define SHIFT_AMOUNT 14
#define INT2FLOAT(n) ((fixed_t)(n << SHIFT_AMOUNT))
#define FLOAT2INTPART(x) (x >> SHIFT_AMOUNT)
#define FLOAT2INTNEAR(x) (x >= 0 ? ((x + (1 << (SHIFT_AMOUNT - 1))) >> SHIFT_AMOUNT) : ((x - (1 << SHIFT_AMOUNT)) >> SHIFT_AMOUNT))
#define FLOATADDFLOAT(x, y) (x + y)
#define FLOATSUBFLOAT(x, y) (x - y)
#define FLOATADDINT(x, n) (x + (n << SHIFT_AMOUNT))
#define FLOATSUBINT(x, n) (x - (n << SHIFT_AMOUNT))
#define FLOATMULFLOAT(x, y) ((((int64_t)x) * y) >> SHIFT_AMOUNT)
#define FLOATMULINT(x, n) (x * n)
#define FLOATDIVFLOAT(x, y) ((((int64_t)x) << SHIFT_AMOUNT) / y)
#define FLOATDIVINT(x, n) (x / n)
#endif /* threads/float.h */