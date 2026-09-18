/*
 * Тесты чистого ядра зуммера (buzzer_core.c). Сборка:
 *   gcc -std=c11 -I ../../components/buzzer/include \
 *       test_buzzer.c ../../components/buzzer/buzzer_core.c \
 *       -o test_buzzer && ./test_buzzer
 */

#include "../test_common/test_framework.h"
#include "buzzer.h"

/* freq_hz == 0 -> всегда тишина, независимо от времени. */
static void test_zero_freq_always_off(void)
{
    TEST_CASE("zero_freq_always_off");
    TEST_ASSERT(!buzzer_square_on(0, 0));
    TEST_ASSERT(!buzzer_square_on(12345, 0));
}

/* FR-40.5: 1 Гц -> период 1000 мс, меандр 50%/50%. */
static void test_1hz_duty_cycle(void)
{
    TEST_CASE("1hz_duty_cycle");
    TEST_ASSERT(buzzer_square_on(0, 1));
    TEST_ASSERT(buzzer_square_on(499, 1));
    TEST_ASSERT(!buzzer_square_on(500, 1));
    TEST_ASSERT(!buzzer_square_on(999, 1));
    TEST_ASSERT(buzzer_square_on(1000, 1)); /* второй период — то же самое */
}

/* FR-40.6: 2 Гц -> период 500 мс, вдвое чаще, чем 1 Гц. */
static void test_2hz_duty_cycle(void)
{
    TEST_CASE("2hz_duty_cycle");
    TEST_ASSERT(buzzer_square_on(0, 2));
    TEST_ASSERT(buzzer_square_on(249, 2));
    TEST_ASSERT(!buzzer_square_on(250, 2));
    TEST_ASSERT(!buzzer_square_on(499, 2));
    TEST_ASSERT(buzzer_square_on(500, 2));
}

int main(void)
{
    test_zero_freq_always_off();
    test_1hz_duty_cycle();
    test_2hz_duty_cycle();
    TEST_SUMMARY();
}
