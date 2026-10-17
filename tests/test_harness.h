#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

/*
 * Test harness macro:
 * Prints the test name with fixed padding, flushes stdout, executes the test function,
 * and prints "OK" if all assertions pass. A failed assert() aborts automatically.
 */
#define RUN_TEST(test)            \
    do                            \
    {                             \
        printf("  %-52s", #test); \
        fflush(stdout);           \
        test();                   \
        puts("OK");               \
    } while (0)
