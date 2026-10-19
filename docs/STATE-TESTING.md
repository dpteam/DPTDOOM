# Потиковые проверки и сохранения

## Запуск

Параметры доступны **только headless CMake-сборке**. В обычной Win32/SDL-сборке
нет потиковых вызовов диагностического кода.

```powershell
cmake --preset windows-headless -DDPTDOOM_TEST_IWAD="$PWD/refs/iwads/doom2.wad"
cmake --build --preset windows-headless
ctest --preset windows-headless
```

CTest запускает `state-smoke`: DEMO1/2/3 дважды, checkpoint/save/load,
повторное продолжение, замену существующего слота, ошибку замены и повреждённые
файлы. Каждый сценарий работает во временном каталоге, не в сохранениях игрока.
Python-скрипт можно запустить напрямую:

```powershell
python tests/state-smoke.py --exe build/headless/dptdoom-headless.exe --iwad refs/iwads/doom2.wad
```

## Аргументы диагностики

| Аргумент | Поведение |
|---|---|
| `-headless-tics N` | Успешный выход после N завершённых вызовов `G_Ticker`, независимо от кадров. |
| `-headless-trace path.txt` | Запись строки после каждого завершённого `G_Ticker` в состоянии `GS_LEVEL`. |
| `-headless-save-tic N` | **Перезаписывает слот 0** после указанного тика; только для тестов в отдельном рабочем каталоге. |

Если используются меню/паузы, вызов `G_Ticker` не обязательно продвигает
`leveltime`. N отсчитывается от начала текущего процесса, не от начала уровня.
С `-timedemo` счётчик в trace начинается с 1, `gametic` начинается с 0.
Окончание ограниченного `-headless-tics` теста — код 0, даже в `-timedemo`;
обычное достижение конца полного timedemo по-прежнему сообщает результат через
`I_Error`. `-headless-save-tic` вне игрового уровня завершается ошибкой.

Пример (создать отдельный рабочий каталог заранее):

```powershell
$exe = (Resolve-Path build/headless/dptdoom-headless.exe).Path
$iwad = (Resolve-Path refs/iwads/doom2.wad).Path
New-Item -ItemType Directory -Force build/checkpoint | Out-Null
Push-Location build/checkpoint
try {
  & $exe -iwad $iwad -timedemo demo1 -headless-tics 140 `
    -headless-save-tic 140 -headless-trace saved.txt
  & $exe -iwad $iwad -loadgame 0 -headless-tics 35 -headless-trace loaded.txt
} finally { Pop-Location }
```

Checkpoint вызывает тот же `G_DoSaveGame`, что и меню, но на точной границе
завершённого тика, без доставки команды через ввод. Меню сохранения тестом не
автоматизируется. Сообщение `Diagnostics: loaded ...` печатается **до** первого
возобновлённого тика: его archive-checksum должен совпадать с checkpoint.

В direct coop `-headless-save-tic` пока запрещён. Для net trace используется
тот же archive projection, но render-only ML_MAPPED исключён: разные игроки
видят разные линии. `-headless-input path.bin` подаёт scripted ticcmd (8-byte
codec) для теста обоих игроков; встроенный legacy consistency сохраняется.
См. `NETWORK.md`. Single-player baselines используют прежнюю схему без изменений.

## Формат `dptdoom-state-v1`

Строки: `tick gametic leveltime archive-fnv1a sim-fnv1a`. Все хеши FNV-1a 32-bit;
числа сериализуются little-endian, без padding нативных структур и адресов ASLR.
Checksum не криптографический и не предназначен для защиты сетевого протокола.

**archive-fnv1a-v1** — существующие save header/players/world/mobjs/specials/EOF:

- Описание пустое; игровые параметры и `leveltime` входят в header.
- Нативные адреса и mobj `validcount` заменены нулями.
- Кэш mobj `floorz`/`ceilingz` нормализован к границам subsector sector:
  ванильная загрузка пересчитывает их, а не использует сохранённые значения.
- Формат дискового сохранения не меняется. Нормализация действует только при
  подсчёте диагностического хеша.

**sim-fnv1a-v1** расширяет archive-хеш: `prndindex`, gamestate, respawn/fast/
nomonsters flags, исходные mobj floorz/ceilingz, target/tracer, полные fixed-point
высоты секторов/offsets sidedefs, sector soundtarget и активные кнопки.
Ссылки на mobj — порядковые номера в thinker list (не сетевые object IDs).

### Ограничения

- Это **проекция** состояния, не полный snapshot. Не покрывает все внутренние
  очереди, удаляемые thinkers, intermission/finale и часть legacy-specials;
  в частности ванильный archive пропускает fireflicker и некоторые stasis cases.
- RNG и target/tracer ванильный save не восстанавливает. Продолжение после
  загрузки сравнивается с повторной загрузкой, **не** с непрерывным демо.
- Совпадение checksum не гарантирует отсутствие коллизии, полную совместимость
  с DOS Doom или сетевую детерминированность на любых архитектурах.
- Диагностика может быть дорогой (включая линейный поиск mobj references).
  Не использовать её для измерения FPS и не переносить в игровой hot path.
- Валидация сохранений улучшена, но это не полный hardened parser произвольных
  враждебных DSG. Загружать только доверенные файлы с подходящим IWAD.

## Эталоны

`tests/state-baselines.json` привязан к SHA-256 конкретного IWAD. Хеш трассы —
SHA-256 ASCII-строк без комментария, с LF независимо от ОС. Файл содержит только
числа и хеши, не игровые ресурсы. Эталоны Doom II подтверждены независимо на
Windows x64 MSVC 19.44 и MinGW GCC 16.1 Release. Shareware v1.8 также проходит
проверки повторяемости/save-load, но его эталон пока не закреплён.

Если baseline меняется: сначала найти первый отличающийся тик и причину.
Не обновлять ожидаемый хеш автоматически ради зелёного теста. При изменении
семантики проекции вводить новую версию схемы и явно переснимать эталоны.

## Исправления, найденные проверками

- Поля платформ/дверей/полов/потолков, не используемые конкретным типом движения,
  сериализовались неинициализированными. Теперь структуры обнуляются при создании.
- Обрезанный DSG больше не возвращает неинициализированный байт и не продолжает
  разбор; версия сравнивается в пределах 16 байт, индексы ряда объектов проверяются.
- Ошибки записи/закрытия учитываются; старый слот не удаляется перед rename.
  При неудаче замены слот остаётся, завершённый `temp.dsg` доступен для восстановления.
