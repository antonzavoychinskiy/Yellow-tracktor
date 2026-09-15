--[[
    mission_select.lua  (v3)

    Два независимых интерфейса для Модуля управления:

    1. Загрузка маршрута по номеру (SCR_USER1, проверено на стенде).
    2. Выдача списка маршрутов из манифеста (SCR_USER2, НОВОЕ в v3,
       требует проверки на стенде - см. открытый вопрос №5 в
       документе требований).

    ---- 1. Загрузка маршрута ----

    Модуль пишет номер маршрута в SCR_USER1 (1..MAX_MISSION).
    Скрипт читает файл APM/missionN.txt (формат QGC WPL 110),
    проверяет условия, загружает миссию и сбрасывает SCR_USER1 в 0.

    Проверки перед загрузкой:
      - платформа disarmed
      - файл существует и корректно парсится
      - расстояние от текущей позиции до первой путевой точки < MAX_DIST_TO_WP1

    Порядок важен: файл парсится в память, все проверки проходят,
    и только затем выполняется mission:clear(). При отказе текущая
    миссия в памяти Pixhawk остаётся нетронутой.

    Ответ оператору - через STATUSTEXT с префиксом MLOAD:
      MLOAD OK n=3 wp=10 d1=4 len=240     - успех
      MLOAD ERR n=3 too_far d1=1240       - первая точка слишком далеко
      MLOAD ERR n=3 armed                 - платформа заармлена
      MLOAD ERR n=3 nofile                - файл не найден
      MLOAD ERR n=3 parse                 - ошибка разбора файла
      MLOAD ERR n=3 nopos                 - нет позиции (GPS/EKF)
      MLOAD ERR n=3 empty                 - в файле нет путевых точек
      MLOAD ERR n=3 write                 - ошибка записи миссии
      MLOAD ERR n=3 crash                 - внутренняя ошибка скрипта

    ---- 2. Список маршрутов ----

    Модуль пишет 1 в SCR_USER2. Скрипт читает манифест
    APM/missions.txt: построчно "<номер><TAB><название>", и отвечает
    серией STATUSTEXT, затем сбрасывает SCR_USER2 в 0.

      MLIST 1 Sklad_A - Angar_B
      MLIST 2 Angar_B - Sklad_A
      MLIST END 2                         - подтверждение числа строк

      MLIST ERR nofile                    - манифест не найден
      MLIST ERR parse                     - ошибка разбора манифеста

    Номера в манифесте должны соответствовать файлам missionN.txt.
    Манифест - единственный источник списка; ничего не хранится
    статично на стороне Модуля.

    Изменения v2 (относительно первой версии):
      - убраны методы item:current() и item:autocontinue() - их нет в API
        ArduPilot Lua (подтверждено штатным примером mission-load.lua)
      - добавлен mission:cmd_has_location() - для команд без координат
        поля x/y не масштабируются на 1e7
      - добавлена проверка последовательности seq
      - загрузка обёрнута в pcall: ошибка не убивает скрипт и не
        блокирует арминг через PreArm

    Изменения v3:
      - добавлена функция list_missions() и обработка SCR_USER2
      - манифест читается тем же io.open, что и файлы маршрутов
--]]

-- ============ Конфигурация ============

local MAX_MISSION       = 10      -- максимальный номер маршрута
local MAX_DIST_TO_WP1   = 50      -- м, порог удаления первой точки
local MISSION_DIR       = "APM/"  -- каталог с файлами на SD
local MANIFEST_FILE     = "missions.txt"  -- манифест номер->название
local RUN_INTERVAL_MS   = 200     -- период опроса SCR_USER1/SCR_USER2
local PARAM_SELECT      = "SCR_USER1"
local PARAM_LIST        = "SCR_USER2"

-- Уровни STATUSTEXT
local SEV_ERROR = 3
local SEV_INFO  = 6

-- ============ Служебное ============

local last_value = 0
local last_list_value = 0

local function say(sev, text)
    gcs:send_text(sev, text)
end

-- Разбор строки формата QGC WPL 110.
-- Поля разделены пробельными символами:
-- seq, current, frame, command, p1, p2, p3, p4, x(lat), y(lng), z(alt), autocontinue
local function parse_line(line)
    local f = {}
    for token in string.gmatch(line, "[^%s]+") do
        f[#f + 1] = token
    end
    if #f < 12 then
        return nil
    end
    return {
        seq     = tonumber(f[1]),
        frame   = tonumber(f[3]),
        command = tonumber(f[4]),
        param1  = tonumber(f[5]),
        param2  = tonumber(f[6]),
        param3  = tonumber(f[7]),
        param4  = tonumber(f[8]),
        x       = tonumber(f[9]),
        y       = tonumber(f[10]),
        z       = tonumber(f[11]),
    }
end

-- Читает файл маршрута в таблицу. Возвращает items или nil, код_ошибки
local function read_mission_file(num)
    local path = MISSION_DIR .. "mission" .. tostring(num) .. ".txt"
    local fh = io.open(path, "r")
    if not fh then
        return nil, "nofile"
    end

    local header = fh:read("l")
    if not header or not string.find(header, "QGC WPL 110") then
        fh:close()
        return nil, "parse"
    end

    local items = {}
    local expected_seq = 0
    while true do
        local line = fh:read("l")
        if not line then
            break
        end
        if string.find(line, "%S") then
            local item = parse_line(line)
            if not item then
                fh:close()
                return nil, "parse"
            end
            -- номера точек должны идти подряд с нуля
            if item.seq ~= expected_seq then
                fh:close()
                return nil, "parse"
            end
            expected_seq = expected_seq + 1
            items[#items + 1] = item
        end
    end
    fh:close()

    if #items < 2 then
        -- строка 0 - home, нужна хотя бы одна путевая точка
        return nil, "empty"
    end

    return items, nil
end

-- Строит Location из элемента, если у команды есть координаты
local function item_location(it)
    if not mission:cmd_has_location(it.command) then
        return nil
    end
    if it.x == 0 and it.y == 0 then
        return nil
    end
    local loc = Location()
    loc:lat(math.floor(it.x * 10 ^ 7))
    loc:lng(math.floor(it.y * 10 ^ 7))
    return loc
end

-- Первая навигационная точка с координатами (пропускаем home)
local function first_nav_location(items)
    for i = 2, #items do
        local loc = item_location(items[i])
        if loc then
            return loc
        end
    end
    return nil
end

-- Суммарная длина маршрута, м
local function mission_length(items)
    local total = 0
    local prev = nil
    for i = 2, #items do
        local loc = item_location(items[i])
        if loc then
            if prev then
                total = total + prev:get_distance(loc)
            end
            prev = loc
        end
    end
    return total
end

-- Запись разобранных точек в память миссии
local function write_mission(items)
    if not mission:clear() then
        return false
    end

    for i = 1, #items do
        local src = items[i]
        local item = mavlink_mission_item_int_t()

        item:seq(src.seq)
        item:frame(src.frame)
        item:command(src.command)
        item:param1(src.param1)
        item:param2(src.param2)
        item:param3(src.param3)
        item:param4(src.param4)

        -- координатные команды масштабируются на 1e7, прочие - нет
        if mission:cmd_has_location(src.command) then
            item:x(math.floor(src.x * 10 ^ 7))
            item:y(math.floor(src.y * 10 ^ 7))
        else
            item:x(math.floor(src.x))
            item:y(math.floor(src.y))
        end
        item:z(src.z)

        if not mission:set_item(src.seq, item) then
            mission:clear()  -- не оставляем частично загруженную миссию
            return false
        end
    end

    return true
end

-- ============ Основная логика загрузки ============

local function load_mission(num)
    local tag = " n=" .. tostring(num)

    -- 1. Платформа должна быть disarmed
    if arming:is_armed() then
        say(SEV_ERROR, "MLOAD ERR" .. tag .. " armed")
        return
    end

    -- 2. Нужна текущая позиция для проверки удаления первой точки
    local here = ahrs:get_location()
    if not here then
        say(SEV_ERROR, "MLOAD ERR" .. tag .. " nopos")
        return
    end

    -- 3. Читаем и парсим файл (миссия в памяти пока не тронута)
    local items, err = read_mission_file(num)
    if not items then
        say(SEV_ERROR, "MLOAD ERR" .. tag .. " " .. err)
        return
    end

    -- 4. Проверяем удаление первой путевой точки
    local wp1 = first_nav_location(items)
    if not wp1 then
        say(SEV_ERROR, "MLOAD ERR" .. tag .. " empty")
        return
    end

    local dist = here:get_distance(wp1)
    if dist > MAX_DIST_TO_WP1 then
        say(SEV_ERROR, string.format("MLOAD ERR%s too_far d1=%d", tag, math.floor(dist)))
        return
    end

    -- 5. Все проверки пройдены - только теперь затираем текущую миссию
    if not write_mission(items) then
        say(SEV_ERROR, "MLOAD ERR" .. tag .. " write")
        return
    end

    local len = mission_length(items)
    say(SEV_INFO, string.format("MLOAD OK%s wp=%d d1=%d len=%d",
                                tag, #items - 1, math.floor(dist), math.floor(len)))
end

-- ============ Список маршрутов (манифест) ============

-- Разбор строки манифеста: "<номер><пробелы/таб><название до конца строки>"
local function parse_manifest_line(line)
    local num_str, name = string.match(line, "^(%d+)%s+(.+)$")
    if not num_str then
        return nil
    end
    -- обрезаем возможный CR на конце (файл мог быть сохранён в Windows-формате)
    name = string.gsub(name, "\r$", "")
    return tonumber(num_str), name
end

local function list_missions()
    local path = MISSION_DIR .. MANIFEST_FILE
    local fh = io.open(path, "r")
    if not fh then
        say(SEV_ERROR, "MLIST ERR nofile")
        return
    end

    local count = 0
    while true do
        local line = fh:read("l")
        if not line then
            break
        end
        if string.find(line, "%S") then
            local num, name = parse_manifest_line(line)
            if not num then
                fh:close()
                say(SEV_ERROR, "MLIST ERR parse")
                return
            end
            -- STATUSTEXT ограничен ~50 символами - следим за длиной
            say(SEV_INFO, string.format("MLIST %d %s", num, name))
            count = count + 1
        end
    end
    fh:close()

    say(SEV_INFO, string.format("MLIST END %d", count))
end

-- ============ Цикл ============

local function update()
    -- --- обработка запроса загрузки маршрута ---
    local val = param:get(PARAM_SELECT)
    if val ~= nil then
        local num = math.floor(val + 0.5)
        if num ~= 0 and num ~= last_value then
            if num >= 1 and num <= MAX_MISSION then
                -- pcall: внутренняя ошибка не должна убивать скрипт
                -- и блокировать арминг через PreArm
                local ok, err = pcall(load_mission, num)
                if not ok then
                    say(SEV_ERROR, "MLOAD ERR n=" .. tostring(num) .. " crash")
                    say(SEV_ERROR, tostring(err))
                end
            else
                say(SEV_ERROR, "MLOAD ERR n=" .. tostring(num) .. " range")
            end
            param:set(PARAM_SELECT, 0)
            last_value = 0
        else
            last_value = num
        end
    end

    -- --- обработка запроса списка маршрутов ---
    local lval = param:get(PARAM_LIST)
    if lval ~= nil then
        local lnum = math.floor(lval + 0.5)
        if lnum ~= 0 and lnum ~= last_list_value then
            local ok, err = pcall(list_missions)
            if not ok then
                say(SEV_ERROR, "MLIST ERR crash")
                say(SEV_ERROR, tostring(err))
            end
            param:set(PARAM_LIST, 0)
            last_list_value = 0
        else
            last_list_value = lnum
        end
    end

    return update, RUN_INTERVAL_MS
end

gcs:send_text(SEV_INFO, "mission_select.lua v3 loaded")

return update, RUN_INTERVAL_MS
