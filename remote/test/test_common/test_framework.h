#pragma once

/*
 * Минимальный host-фреймворк тестирования — без внешних зависимостей
 * (Unity и т.п.), чтобы тесты можно было собрать одной командой любым
 * компилятором C99, без PlatformIO/CMake. См. test/README.md.
 */

#include <stdio.h>

static int g_test_failures = 0;
static int g_test_count = 0;
static const char *g_current_test = "";

#define TEST_CASE(name) g_current_test = name;

#define TEST_ASSERT(cond)                                                        \
    do {                                                                         \
        g_test_count++;                                                          \
        if (!(cond)) {                                                           \
            g_test_failures++;                                                   \
            printf("FAIL [%s] %s:%d: %s\n", g_current_test, __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

#define TEST_ASSERT_EQ(a, b) TEST_ASSERT((a) == (b))

#define TEST_SUMMARY()                                                           \
    do {                                                                         \
        printf("%d/%d assertions passed (%s)\n", g_test_count - g_test_failures, \
               g_test_count, g_test_failures == 0 ? "OK" : "FAILED");            \
        return g_test_failures ? 1 : 0;                                          \
    } while (0)
