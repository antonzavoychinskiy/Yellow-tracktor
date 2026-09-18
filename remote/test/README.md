# Тесты чистой логики (без ESP-IDF, без стенда)

Каждый `test_*.c` — самостоятельная программа (свой `main()`), без
зависимости от Unity/PlatformIO test runner: один компилятор C99,
без сборочной системы. Так надёжнее всего, пока не выяснено, какая
именно версия ESP-IDF/component-manager будет закреплена (см.
`TODO(stand)` по проекту) — тестовый цикл логики не должен зависеть
от этого выбора.

Все тесты ниже реально прогнаны (MinGW gcc 6.3.0, Windows) и проходят
полностью — не только прослежены вручную.

## Сборка и запуск

Из соответствующего каталога `test/test_*`:

```sh
# test/test_state_machine
gcc -std=c11 -I ../../components/config/include \
    -I ../../components/state_machine/include \
    -I ../../components/mavlink_bridge/include \
    -I ../../components/keyswitch/include \
    test_state_machine.c ../../components/state_machine/state_machine_core.c \
    -o test_state_machine && ./test_state_machine

# test/test_control_loop
gcc -std=c11 -I ../../components/config/include \
    -I ../../components/control_loop/include \
    test_control_loop.c ../../components/control_loop/control_loop_core.c \
    -o test_control_loop && ./test_control_loop

# test/test_mission_ui
gcc -std=c11 -I ../../components/config/include \
    -I ../../components/mission_ui/include \
    test_mission_ui.c ../../components/mission_ui/mission_ui_core.c \
    -o test_mission_ui && ./test_mission_ui

# test/test_auto_sequence
gcc -std=c11 -I ../../components/config/include \
    -I ../../components/auto_sequence/include \
    -I ../../components/mavlink_bridge/include \
    test_auto_sequence.c ../../components/auto_sequence/auto_sequence_core.c \
    -o test_auto_sequence && ./test_auto_sequence

# test/test_buzzer
gcc -std=c11 -I ../../components/buzzer/include \
    test_buzzer.c ../../components/buzzer/buzzer_core.c \
    -o test_buzzer && ./test_buzzer
```

На Windows без `gcc` в PATH — поставить MSYS2/mingw-w64, либо собрать
тем же способом на WSL/Linux/macOS: `*_core.c` не содержит
ESP-IDF/FreeRTOS зависимостей, платформенно нейтрален.

## Что покрыто

- `test_state_machine` — FR-1.1, FR-1.2, FR-5/5.1, FR-6, FR-8.1, FR-9/9.1,
  FR-10.1, FR-35 (СЦ-6, СЦ-9, СЦ-11).
- `test_control_loop` — FR-13/14/15/16/17/18/19, FR-37.1 (частично,
  на уровне "невалидное показание -> нейтраль"; сама детекция серии
  отказов — в `joystick_adc_hal.c`, не тестируется на хосте, т.к. это ADC).
- `test_mission_ui` — FR-20.1/20.2/20.3/20.4, FR-21/22/22.1/23/24,
  FR-24.1, FR-25/26 (СЦ-1 шаги 1-4, СЦ-7).
- `test_auto_sequence` — FR-40/40.1/40.2/40.3/40.5/40.6 (СЦ-1 шаги 5-6,
  СЦ-12).
- `test_buzzer` — меандр 1/2 Гц зуммера подтверждения (FR-40.5/40.6),
  чистая функция `buzzer_square_on`.

## Что НЕ покрыто на хосте (требует стенда или как минимум прошивки)

- Драйверы периферии (`*_hal.c`) — ADC/SPI/GPIO/UART, ESP-IDF-специфика.
- Реальные тайминги MAVLink, watchdog, приоритеты FreeRTOS-задач
  (NFR-1..7).
- Сценарии СЦ-2, СЦ-3, СЦ-5, СЦ-8, СЦ-10 — требуют реального
  взаимодействия нескольких задач/аппаратуры одновременно.
- Открытые вопросы раздела 10 требований — по определению требуют
  стенда.
