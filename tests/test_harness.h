#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

/*
 * Desktop runs use assert() and print each test result. On Arduino, delegate
 * test execution to Unity so PlatformIO can report hardware test results.
 */
#ifdef ARDUINO
#define TEST_HARNESS_RUN_TEST(test) RUN_TEST(test)
#else
#define TEST_HARNESS_RUN_TEST(test) \
    do                              \
    {                               \
        printf("  %-52s", #test);   \
        fflush(stdout);             \
        test();                     \
        puts("OK");                 \
    } while (0)

#ifndef RUN_TEST
#define RUN_TEST(test) TEST_HARNESS_RUN_TEST(test)
#endif
#endif
