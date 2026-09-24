#pragma once

/*
 * Центральная конфигурация Модуля (NFR-11): все настраиваемые параметры
 * в одном месте, без переработки логики компонентов.
 *
 * Значения, для которых требования не задают число явно (например,
 * дедбенд, пороги таймаутов), выставлены ориентировочно и обязаны быть
 * выверены на реальном стенде (см. раздел 10 требований, открытые
 * вопросы) — помечено TODO(stand).
 */

#include <stdint.h>

/* ===================== Аппаратные интерфейсы (п. 3.2) ===================== */

/* UART — Pixhawk TELEM2 */
#define MODULE_UART_PORT_NUM        1
#define MODULE_UART_TX_GPIO         14
#define MODULE_UART_RX_GPIO         25
#define MODULE_UART_BAUD            115200

/* ADC — аналоговый джойстик (2 оси на потенциометрах), питание 3.3 В
 * без делителя напряжения (открытый вопрос №8 закрыт измерением).
 * GPIO36 подписан на плате «VP», GPIO39 — «VN»; обе линии на ADC1
 * (ADC2 на ESP32 не работает одновременно с Wi-Fi). Разводка платы
 * Модуля подтверждена на стенде 2026-09-18 (открытый вопрос №11). */
#define MODULE_JOYSTICK_X_GPIO      36
#define MODULE_JOYSTICK_Y_GPIO      39

/* Кнопка «мёртвая рука» — отдельная кнопка (контакт NC), электрически
 * не связана с джойстиком. Подтяжка вверх + активный низкий уровень
 * выбраны из требования безопасности (открытый вопрос №9): обрыв
 * провода или расстыковка разъёма оставляют вход подтянутым в 1, что
 * читается как «отпущена» (NFR-8), а не как «удержана». Полярность
 * подтверждена физическим тестом на стенде 2026-09-18 (отсоединение
 * разъёма во время удержания читается как «отпущена»). */
#define MODULE_DEAD_MAN_GPIO        13
#define MODULE_DEAD_MAN_ACTIVE_LOW  1

/* SPI — дисплей ST7789 */
#define MODULE_SPI_HOST             1   /* HSPI */
#define MODULE_DISPLAY_SCK_GPIO     18
#define MODULE_DISPLAY_MOSI_GPIO    23
#define MODULE_DISPLAY_RES_GPIO     16
#define MODULE_DISPLAY_DC_GPIO      17
#define MODULE_DISPLAY_CS_GPIO      5

/* Панель — 1.9", ST7789, физическая матрица 170x320 (портрет).
 * Подтверждено на стенде. Модуль работает в альбомной ориентации —
 * логическое разрешение для LVGL 320x170, поворот на 90° делается
 * аппаратно через esp_lcd (swap_xy + mirror), см. display_hal.c. */
#define MODULE_DISPLAY_WIDTH        320
#define MODULE_DISPLAY_HEIGHT       170

/* Физическое (портретное, до поворота) разрешение матрицы — нужно
 * display_hal.c отдельно, т.к. esp_lcd_new_panel_st7789 адресует
 * память панели в её "родных" координатах. */
#define MODULE_DISPLAY_PHYS_WIDTH   170
#define MODULE_DISPLAY_PHYS_HEIGHT  320

/* Поворот в альбомную ориентацию: swap_xy меняет местами x/y, mirror
 * задаёт направление считывания по каждой оси. Комбинация ниже —
 * наиболее часто работающая для этого семейства модулей 170x320;
 * TODO(stand): если картинка перевёрнута или зеркалирована — поменять
 * MIRROR_X/MIRROR_Y, это не требует пересборки логики, только этих
 * трёх констант. */
#define MODULE_DISPLAY_SWAP_XY      1
#define MODULE_DISPLAY_MIRROR_X     1
#define MODULE_DISPLAY_MIRROR_Y     0

/* Смещение видимого стекла относительно памяти контроллера. GRAM
 * ST7789 рассчитана на 240x320, физическая матрица модуля — 170x320,
 * т.е. видимые столбцы памяти это 35..204 ((240-170)/2 = 35).
 * Смещение применяется в драйвере ДО трансформации MADCTL, поэтому
 * после swap_xy (альбомная ориентация) физическая ось X становится
 * логической Y — смещение уходит именно в GAP_Y, а не в GAP_X.
 * Без него нижние 35 строк экрана никогда не перезаписываются и
 * показывают мусор GRAM (подтверждено на стенде: полоса шума снизу
 * занимала ~20% высоты = 35/170). */
#define MODULE_DISPLAY_GAP_X        0
#define MODULE_DISPLAY_GAP_Y        35

/* Частота SPI дисплея. 40 МГц штатно для короткой печатной дорожки;
 * на макетных проводах-джамперах даёт помехи/"снег" на экране —
 * пересмотреть в сторону повышения только после подтверждения
 * стабильной картинки на длительном прогоне и качественного монтажа
 * (короткие провода, общий GND). TODO(stand): подобрать по факту
 * монтажа — временно занижено для отладки на макетке. */
#define MODULE_DISPLAY_SPI_HZ       (5 * 1000 * 1000)

/* Энкодер — модуль EC11 на плате с кнопкой и ручкой (заменил HW-040).
 * Соответствие пинов: S1 = CLK, S2 = DT, KEY = SW. Питание модуля —
 * только 3.3 В. Если вращение идёт в обратную сторону — поменять
 * провода S1 и S2 местами. */
#define MODULE_ENCODER_CLK_GPIO     32   /* S1 */
#define MODULE_ENCODER_DT_GPIO      33   /* S2 */
#define MODULE_ENCODER_SW_GPIO      27   /* KEY, «OK» / «Загрузить» */

/* Кнопки */
#define MODULE_BUTTON_BACK_GPIO     19
#define MODULE_BUTTON_START_GPIO    4    /* «Пуск» */

/* Зуммер подтверждения пуска AUTO (FR-40.5..40.7). Активный зуммер
 * (готовый модуль со встроенным генератором тона) — GPIO включает его
 * напрямую HIGH/LOW, отдельного тона формировать не нужно. GPIO26
 * выбран как свободный, не strapping-пин. TODO(stand): если в итоге
 * ставится пассивный пьезоэлемент — заменить buzzer_hal.c на LEDC
 * (тон + gate по тем же MODULE_BUZZER_BEEP_HZ_*), сам GPIO и API не
 * меняются. */
#define MODULE_BUZZER_GPIO                 26

/* Ключ (трёхпозиционный, последовательный LOCAL-OFF-AUTO: между LOCAL и AUTO — всегда OFF) */
#define MODULE_KEY_LOCAL_GPIO       34   /* input-only, БЕЗ внутренней подтяжки */
#define MODULE_KEY_AUTO_GPIO        35   /* input-only, БЕЗ внутренней подтяжки */

/* GPIO34/35 на ESP32 не имеют внутренних подтягивающих резисторов —
 * внешние pull-down ОБЯЗАТЕЛЬНЫ в схеме (контакт замкнут = уровень
 * High). TODO(stand): сверить с фактической схемой; если контакты
 * разведены "в ноль", инвертировать здесь, а не в логике. */
#define MODULE_KEY_LOCAL_ACTIVE_HIGH 1
#define MODULE_KEY_AUTO_ACTIVE_HIGH  1

/* Кнопки/энкодер — GPIO с внутренней подтяжкой, стандартная схема
 * "кнопка на GND", активный уровень Low. TODO(stand): сверить. */
#define MODULE_BUTTON_ACTIVE_LOW     1
#define MODULE_ENCODER_SW_ACTIVE_LOW 1

/* ===================== MAVLink (раздел 3.4, 5.1) ===================== */

/* sysid Модуля должен совпадать со значением MAV_GCS_SYSID в Pixhawk
 * (FR-4). Значение по умолчанию — заглушка, обязана быть согласована
 * с настройкой автопилота перед натурными тестами. */
#define MODULE_MAVLINK_SYSID        200
#define MODULE_MAVLINK_COMPID       190
#define MODULE_MAVLINK_TARGET_SYSID 1    /* sysid Pixhawk по умолчанию в ArduPilot */
#define MODULE_MAVLINK_TARGET_COMPID 1

/* Потеря связи с Pixhawk (FR-38, FR-44): не получен HEARTBEAT дольше
 * этого времени — считаем связь потерянной. Стандартный HEARTBEAT
 * период ArduPilot — 1 Гц, берём запас x3. */
#define MODULE_MAVLINK_LINK_TIMEOUT_MS   3000

/* Период собственного HEARTBEAT Модуля. 1 Гц — стандарт MAVLink для
 * наземных станций; ArduPilot по нему регистрирует канал как активную
 * GCS (Модуль представляется MAV_TYPE_GCS с sysid = MAV_GCS_SYSID). */
#define MODULE_HEARTBEAT_PERIOD_MS       1000

/* ===================== Локальное ручное управление (5.3) ===================== */

#define MODULE_MANUAL_CONTROL_RATE_HZ      20     /* FR-12, NFR-4 */
#define MODULE_MANUAL_CONTROL_PERIOD_MS    (1000 / MODULE_MANUAL_CONTROL_RATE_HZ)

/* Центр каждой оси в сырых отсчётах ADC (12 бит, 0..4095). Подтверждено
 * на стенде 2026-09-18 (Приложение Б, п. 5): фактический механический
 * центр хода совпадает с серединой шкалы, подбор не потребовался. */
#define MODULE_JOYSTICK_ADC_CENTER_X       2048
#define MODULE_JOYSTICK_ADC_CENTER_Y       2048

/* Направление осей: 1 — инвертировать знак после центрирования.
 * TODO(stand): выставить так, чтобы «от себя» давало положительный
 * ход вперёд, а «вправо» — положительный поворот вправо. */
#define MODULE_JOYSTICK_X_INVERT           0
#define MODULE_JOYSTICK_Y_INVERT           0

/* Дедбенд джойстика (FR-18), в сырых отсчётах ADC после центрирования.
 * TODO(stand): уточнить по факту разброса показаний — у аналогового
 * джойстика шум ADC заметно больше, чем был у цифрового Nunchuk. */
#define MODULE_JOYSTICK_DEADBAND_COUNTS    128

/* Допуск «нейтрали» при входе в LOCAL (FR-17) — та же шкала. */
#define MODULE_JOYSTICK_NEUTRAL_TOLERANCE  MODULE_JOYSTICK_DEADBAND_COUNTS

/* Программное ограничение скорости в LOCAL (FR-16), % от полного хода. */
#define MODULE_LOCAL_SPEED_LIMIT_PERCENT   50

/* Номинальный полный ход оси джойстика после центрирования: ADC 12 бит
 * (0..4095) с центром на середине шкалы даёт ~±2048. TODO(stand):
 * скорректировать по фактически измеренным крайним положениям
 * (Приложение Б, п. 3) — реальный размах меньше полной шкалы ADC. */
#define MODULE_JOYSTICK_RAW_FULL_SCALE      2048

/* Ограничение скорости нарастания команды (FR-19): максимальное
 * приращение выхода (в единицах MANUAL_CONTROL, -1000..1000) за один
 * цикл отправки (MODULE_MANUAL_CONTROL_PERIOD_MS). TODO(stand). */
#define MODULE_MANUAL_CONTROL_SLEW_PER_TICK 100

/* ===================== Переходы по ключу (5.2) ===================== */

/* FR-5: порог путевой скорости, ниже которого считаем платформу
 * остановившейся, и таймаут ожидания остановки. TODO(stand). */
#define MODULE_STOP_SPEED_THRESHOLD_MPS    0.1f
#define MODULE_STOP_WAIT_TIMEOUT_MS        5000

/* Таймаут подтверждения дизарма/арминга по HEARTBEAT. */
#define MODULE_ARM_CONFIRM_TIMEOUT_MS      3000
#define MODULE_DISARM_CONFIRM_TIMEOUT_MS   3000

/* Таймаут подтверждения смены режима по HEARTBEAT (FR-40.3 и общий случай). */
#define MODULE_MODE_CONFIRM_TIMEOUT_MS     3000

/* FR-40.5: удержание «Пуск» в AUTO для подтверждения пуска — время
 * заполнения индикатора-полосы до срабатывания. */
#define MODULE_START_CONFIRM_HOLD_MS       3000

/* FR-40.6: обратный отсчёт после заполнения полосы, перед фактической
 * отправкой команды арминга (см. диаграмму 8А.3). */
#define MODULE_START_CONFIRM_COUNTDOWN_MS  5000

/* FR-40.7: частота писка зуммера на каждой из двух фаз подтверждения. */
#define MODULE_BUZZER_BEEP_HZ_HOLD         1
#define MODULE_BUZZER_BEEP_HZ_COUNTDOWN    2

/* ===================== Список и загрузка маршрутов (5.4) ===================== */

#define MODULE_PARAM_SELECT_ROUTE   "SCR_USER1"
#define MODULE_PARAM_REQUEST_LIST   "SCR_USER2"

/* FR-23: длительность удержания энкодера для запуска загрузки. */
#define MODULE_ENCODER_LONGPRESS_MS        2000

/* FR-20.3/20.4: повторы и таймаут запроса списка маршрутов. */
#define MODULE_MLIST_REQUEST_RETRIES       2
#define MODULE_MLIST_TIMEOUT_MS            2000

/* FR-25: таймаут ожидания MLOAD OK/ERR после PARAM_SET SCR_USER1. */
#define MODULE_MLOAD_TIMEOUT_MS            3000

/* Верхняя граница числа маршрутов для выделения памяти под список в UI.
 * Это НЕ бизнес-правило (список не кешируется дольше цикла OFF, FR-28) —
 * чисто предел ёмкости буфера на стороне Модуля. */
#define MODULE_MAX_ROUTES_UI               16

/* Ограничение длины названия маршрута в UI (см. открытый вопрос №7 —
 * STATUSTEXT ~50 символов на связку "MLIST <номер> <название>"). */
#define MODULE_ROUTE_NAME_MAX_LEN          40

/* ===================== Отказы и восстановление (раздел 6) ===================== */

/* FR-37.1: число подряд неудачных чтений ADC до объявления отказа
 * джойстика. Покрывает только ошибки драйвера; содержательный критерий
 * отказа аналогового джойстика (обрыв провода даёт формально валидный
 * отсчёт) в требованиях не определён — открытый вопрос №10. */
#define MODULE_JOYSTICK_FAIL_THRESHOLD      5

/* FR-39: антидребезг контактов ключа/кнопок, в мс. */
#define MODULE_DEBOUNCE_MS                  30

/* FR-39.1: защитный интервал вокруг нажатия энкодера, в течение
 * которого вращение игнорируется как паразитное. */
#define MODULE_ENCODER_PRESS_GUARD_MS       50

/* FR-36: период watchdog-задачи и таймаут детекции зависания цикла
 * реального времени. */
#define MODULE_WATCHDOG_TIMEOUT_MS          2000
#define MODULE_RT_LOOP_FEED_PERIOD_MS       50

/* ===================== Интерфейс оператора (5.6) ===================== */

/* FR-45: минимальное время показа результата операции. */
#define MODULE_RESULT_MESSAGE_MIN_MS        2000

/* ===================== Отладка (7.6) ===================== */

#ifndef MODULE_SYNTHETIC_MODE
#define MODULE_SYNTHETIC_MODE 0
#endif
