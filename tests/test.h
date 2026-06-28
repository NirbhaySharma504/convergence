// test.h — tiny dependency-free test harness.
//
// Each test file defines test functions and a main() that RUN()s them.
#ifndef CONVERGENCE_TEST_H
#define CONVERGENCE_TEST_H

#include <stdio.h>
#include <stdlib.h>

#define RUN(t) do {                              \
    printf("  %-40s ", #t);                      \
    fflush(stdout);                              \
    t();                                         \
    printf("ok\n");                              \
} while (0)

#define CHECK(cond) do {                                                   \
    if (!(cond)) {                                                         \
        printf("FAIL\n    %s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
        exit(1);                                                           \
    }                                                                      \
} while (0)

#define CHECK_EQ(a, b) do {                                                       \
    long long _a = (long long)(a), _b = (long long)(b);                           \
    if (_a != _b) {                                                               \
        printf("FAIL\n    %s:%d: %s == %s  (%lld != %lld)\n",                     \
               __FILE__, __LINE__, #a, #b, _a, _b);                              \
        exit(1);                                                                  \
    }                                                                            \
} while (0)

#endif
