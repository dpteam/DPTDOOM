# DPT.DOOM: план и состояние

## Этап 1 — основа форка и автоматические тесты

- [x] Имя пакета, версия, заголовок окна и Windows-ресурсы DPT.DOOM.
- [x] Копия пользовательской иконки в tracked assets, `.ico` нескольких размеров.
- [x] `refs/`, IWAD и результаты сборок исключены из Git.
- [x] CMake, Windows/MSVC и MinGW x64 Release, LTO и удаление мёртвого кода.
- [x] Headless без дисплея и SDL; лимит кадров, PPM и RGB FNV-1a.
- [x] Тесты версии, демо, корректного завершения и путей конфигов.
- [x] Исправлены срок жизни имени демо, выход из `I_Quit`, рекурсивный `I_Error`,
  пропуск части exit callbacks и отключённое upstream чтение/сохранение конфигов.
- [x] Оконный запуск Doom II MAP01 для владельца.

Проверено на Windows: MSVC 19.44 и MinGW GCC 16.1, Release x64.
Legacy VS solution тоже собирается с override `PlatformToolset=v143`.
Тесты используют локальный `refs/iwads/doom2.wad`. Shareware v1.8 дополнительно
доступен локально в `build/testdata/doom1.wad` (MD5
`5f4eb849b1af12887dec04a2a12e5e62`, для теста указывать gameversion `1.8`).
Linux/macOS пока **не проверены**.

## Этап 1.1 — SDL2 и звук

- [x] Windows MSVC x64 SDL2 2.32.10 + SDL2_mixer 2.8.2, reproducible fetch с SHA-256.
- [x] Звуковые эффекты и MIDI-музыка; владелец подтвердил звук через RDP.
- [x] Проверка SDL startup/render failures, software fallback, streaming texture,
  корректное закрытие с сохранением конфигов, иконка и масштабирование окна.
- [x] Ввод без 16-элементной очереди: F1/F12/backspace, фильтрация неизвестных
  keycodes, подавление repeat, общие modifiers, release при потере фокуса.
- [x] Пакетирование не более 32 key events за тик в 64-слотовую очередь движка;
  убран выход после первого key-up. Тест 400 событий проходит без потерь.
- [x] Инициализация/завершение аудиоподсистем с учётом ошибок и flags,
  остановка каналов до освобождения cache, защита от нулевой sample rate.
- [x] MUS/MIDI в памяти без общего временного doom.mid, удержание backing storage
  до освобождения песни; пользовательский Timidity config без temporary wrapper.
- [x] Dummy SDL input/audio unit tests без IWAD; engine smoke test с локальным IWAD.
- [x] Регрессионные Win32/headless тесты остаются зелёными.

Не проверены на этом этапе: SDL MinGW/Linux/macOS, аппаратный MIDI других машин,
OPL-синтез, мышь/геймпады. Дефолтный native Win32 backend остаётся без звука.

## Ближайшее

1. Профили ресурсов/игрока и простое UI прямого подключения как основа launcher;
   graceful startup/handshake failure UI, затем reconnect/re-host lifecycle.
2. Первый реальный GitHub Actions run после разрешённого push; проверить Linux/macOS.
3. Продолжить 64-битный/UB-аудит (не все legacy warnings устранены),
   расширить sanitizer coverage на SDL, другие IWAD/карты и платформы.

Предложения ассистента отделены от требований владельца в `docs/IDEAS.md`:
mod manifests, administration/mute/rate-limits, сетевой HUD/diagnostic reports,
доступность/ввод и расширенные save/demo форматы. Это не уже утверждённые фичи.

## Исправление BUG-001 — общий чат

- [x] Воспроизведён T-chat exit как false state desync после служебного chat byte.
- [x] Network checksum исключает только локально consumed chatchar; SP/save
  format и закреплённые game traces не изменены.
- [x] Cancel/reset destination/input/queue; hidden receive font; typed Space и
  приоритет Escape для чата, а не открытия меню.
- [x] `net-chat`: 210 тиков, две стороны, доставка/cancel/backspace/high-bit input.
- [x] Реальный T/Enter/Escape/пробел/Backspace test в двух SDL-окнах.
- [x] Полные проверки: 15/15 headless, 17/17 ASan, 18/18 SDL зелёные.

Реестр открытых/закрытых проблем — `docs/BUGS.md`. Team chat и voice — пока план.

## Этап 1.4 — data-free CI (подготовлен, remote run ожидается)

- [x] Workflow Windows/Linux/macOS headless, SDL2 и Windows ASan.
- [x] Startup/error smoke без IWAD; тесты timer/zone/input/audio не требуют игры.
- [x] Actions pinned SHA, read-only token, timeout/cancel, failure logs без build tree.
- [x] Workflow прошёл actionlint; Windows data-free build проверен с чистого дерева сборки.
- [x] Добавлены downloadable Windows SDL/headless build artifacts с инструкцией,
  commit ID, SHA-256 и соответствующим source ZIP; remote upload ещё не выполнен.
- [x] Исправлены X11 null display handling и неверная высота XImage (runtime не проверен).
- [ ] Выполнить workflow на GitHub и устранить реальные cross-platform failures.

WSL-инструменты по решению владельца не устанавливались. Linux/macOS пока не
считаются проверенными; см. `docs/CI.md`. Коммерческие IWAD в CI не передаются.

## Этап 1.3 — ASan и demo snapshots

- [x] Проверенная MSVC x64 ASan headless-конфигурация и локальный runtime DLL.
- [x] Zone allocator poisoning и probes, подтверждающие реальный детект ошибок.
- [x] Исправления tangent-table/post bounds, PNAMES, thinker traversal и
  dangling attacker для death-camera/HUD. Gameplay state baselines неизменны.
- [x] Длинный timer без преждевременного 32-bit multiply overflow, тесты epoch/wrap.
- [x] Сохранение attract-demo через настоящее меню/F2 и загрузка в single-player.
- [x] Интеграционный SDL Windows-тест F2/save/продолжение демо/F3/takeover/live-save.
- [x] Записано требование онлайн-snapshot с подтверждением disconnect при load.

См. `docs/SANITIZERS.md`. UBSan пока не проверен; не все ошибки/предупреждения
устранены. Оконные тесты имеют метку `interactive`.

## Этап 1.2 — сохранения и потиковая диагностика

- [x] Headless `-headless-tics`, `-headless-trace`, `-headless-save-tic`.
- [x] Версионированные archive/simulation projections, без адресов ASLR.
- [x] Два полных прогона каждого DEMO1/2/3, побайтовое сравнение трасс.
- [x] Закреплены эталоны конкретного Doom II IWAD; MSVC и GCC совпадают.
- [x] Checkpoint после 140 тиков, восстановление archive-проекции до первого
  продолженного тика, два одинаковых продолжения после загрузки.
- [x] Проверка успешной/неудачной замены слота, truncated/header/index/EOF ошибок.
- [x] Исправлены неинициализированные поля движущихся specials и unsafe EOF read.
- [x] Старый слот сохраняется при ошибке commit; ошибки записи/закрытия не теряются.
- [x] Дополнительно проверены три демо и save/load shareware Doom v1.8.

Формат, команды, цена диагностики и ограничения: `docs/STATE-TESTING.md`.
Это не полный snapshot сети и не обещание hardened-загрузки враждебных DSG.

## Этап 2 — кооператив с друзьями

### Этап 2.1 — direct coop v1 (экспериментальный Windows loopback)

- [x] Собственный DPT protocol v1, TCP lockstep, ровно два игрока, explicit IPv4.
  (Первоначальный этап; текущий протокол поднят до v2 для dmflags.)
- [x] По умолчанию bind только localhost, LAN bind только явным аргументом.
- [x] Handshake: fingerprint исходников/build версии, содержимое/порядок lumps,
  skill/map/monster flags/gameversion и противоположные player IDs.
- [x] Endian-safe codec, command bounds, reserved fields, номер тика, checksum,
  fragmented stream, EOF, disconnect и bounded timeout.
- [x] Два headless процесса: scripted movement/fire + monsters, 210 тиков с
  одинаковыми потиковыми трассами. Проверено MinGW и MSVC ASan на Windows.
- [x] Два SDL-окна: различный живой ввод/камеры, более 128 тиков, close/disconnect.
- [x] Смешанный Windows loopback MinGW host + MSVC ASan guest, 210 тиков.
- [x] Artifacts allowlist/source/dirty/overwrite checks в изолированном git fixture.
- [x] Регрессионные state-baselines сохранены без обновления.
- [ ] Реальная сессия двух машин/друзей, проверка latency/packet loss.
- [ ] Online snapshots и load-to-SP с предупреждением/подтверждением отключения.
- [x] Graceful disconnect-to-menu для активной игры; причина/подтверждение/SP reset.
- [ ] Reconnect/re-host из меню; startup/handshake failure recovery ещё не готов.

Команды и строгие ограничения: `docs/NETWORK.md`. Это не сетевой протокол
Chocolate Doom и не готовый публичный Internet server. UDP/prediction/NAT не
реализованы. Старый `FEATURE_MULTIPLAYER` остаётся выключен; новый путь включён
через CMake `DPTDOOM_NETWORK` и не использует заголовки как подмену реализации.

### Этап 2.2 — базовые DMFLAGS (протокол v2)

- [x] Идеи 1024-player server и DM/COOP-ботов записаны отдельно в `docs/IDEAS.md`.
- [x] `-dmflags 1`: не создавать в коопе multiplayer-only оружие, не трогая
  обычное оружие/патроны/ключи/дополнительных монстров.
- [x] `-dmflags 2`: проходить сквозь живых игроков, не через стены/монстров;
  выстрелы и урон не отключаются. Маска 3 включает оба флага.
- [x] Маска сверяется между peers и входит в checksum, неизвестные биты отвергаются.
- [x] Реальный temporary MAP01 test для всех четырёх масок + loopback с flags=3.
- [ ] Server config maxplayers и collision policy auto|block|pass (auto при configured max >4).
- [ ] DM-режим и DM-боты; текущая direct coop сессия всё ещё на двух игроков.

Не повышать MAXPLAYERS до 1024 ради заявления поддержки: нужна новая серверная
архитектура и нагрузочные тесты. См. подробности идей/этапов в `docs/IDEAS.md`.

### Этап 2.3 — завершение активной сессии

- [x] Peer close, receive/send failure, timeout, malformed/order/command/desync
  возвращают в menu на границе тика вместо I_Error завершения процесса.
- [x] F7 disconnect с cancel/confirm; закрытие окна по-прежнему закрывает приложение.
- [x] Сброс buffered ticcmd/local player mask/timebase, players/rules/chat/input/
  pause/fast-monster shared state; старый мир не обновляется после обрыва.
- [x] Бывший player2 начинает SP как player1, сохраняется/загружается без ghost player.
- [x] Headless recovery tests, SDL window tests с cancel/open-chat/paused/fast,
  отдельный SDL+ASan+dummy-audio teardown прогон.
- [x] Финальная регрессия: 16/16 MinGW headless, 18/18 MSVC headless ASan,
  19/19 MSVC SDL; single-player state baselines неизменны.
- [ ] Graceful startup/connect/handshake recovery, reconnect/re-host ещё отсутствуют.

Диагностический `-net-fail-fast` оставляет аварийный exit для packet regression tests.
Он не включён по умолчанию для обычной игры.

- Требование владельца: разрешить локальный snapshot во время онлайн-игры.
  Загрузка snapshot запускает single-player; в подключённой МП-сессии сначала
  показать подтверждение «Загрузка отключит тебя от сервера. Продолжить?».
  Отмена не должна менять соединение. Сохранение чужих игроков/мира и выбор
  локального игрока требуют отдельного формата/дизайна после реализации netcode.

- Выбрать и документировать протокол: сначала небольшой lockstep/ticcmd
  кооператив с прямым подключением; восстановление совместимого Chocolate Doom
  netcode сравнить с собственной реализацией. Не смешивать протоколы молча.
- Явная версия протокола, сверка IWAD/PWAD/настроек, endian-safe сериализация,
  строгие границы пакетов, таймауты и обнаружение рассинхронизации.
- Автотесты двух headless процессов на loopback, затем потеря/задержка пакетов,
  ручная проверка двух окон и реальная сессия с друзьями.
- Рассматривать client/server с prediction отдельно: это заметно более крупное
  изменение, чем LAN lockstep. Мастер-сервер не решает NAT автоматически.
- `coop-anything` — предложенный пользователем вариант для оценки, не установлен
  и не нужен для нынешнего headless. Не запускать скачанные скрипты без проверки.

## Этап 3 — мастер-сервер и лаунчеры

- После работающего игрового подключения: heartbeat, TTL, список серверов,
  challenge/проверка источника, лимиты и защита от UDP amplification.
- Изучить `refs/csDoomMaster`, но не копировать небезопасную сетевую обработку.
  В частности, в `bAddServer` есть обращение за пределами массива после цикла.
- Клиентский/серверный launcher с выбором IWAD, модов, адреса и параметров.
- Dedicated server — отдельная цель, не путать с графическим headless backend.
- Удобный launcher с профилями ресурсов, server browser с фильтрами/избранным/
  ping/version/модами, direct-connect fallback при недоступном master.
- Перед публичным browser/voice: администрирование, moderation/mute/rate-limits
  и понятная политика доверия/приватности. Подробности в `docs/IDEAS.md`.

## Общение и профили игрока (план)

- Общий текстовый чат как рабочая база (`T`), исправление BUG-001 и регрессия.
- Team chat после явного team model/DM/TDM: routing, channel indicator,
  история/scroll, mute, length/rate limits. Не считать текущий broadcast team chat.
- In-game voice: PTT по умолчанию, устройства/громкость/mute/deafen/индикаторы,
  отдельный от lockstep voice transport; без неявного доступа к микрофону.
- Профиль игрока, цветной безопасный nickname format, цвет персонажа/команды,
  cosmetic skins с preview/fallback и server policy. «Зомби/имп» сначала только
  визуальная оболочка с Doomguy hitbox, не иной класс/преимущество.

## Среднесрочно / долгосрочно

- PK3/ZIP с собственной интеграцией загрузчика, проверкой путей, размеров,
  коэффициента распаковки и лимитов; ресурсы и вложенные WAD, чёткий приоритет.
- DECORATE и совместимость с ZDoom: сначала конкретное подмножество;
  `refs/doom_ace-master` — материал для изучения, не готовая drop-in библиотека.
- Профилирование hot paths, сравнение Release/MinSizeRel, затем PGO.
  CRT startup не удалять, пока движок использует malloc/stdio/exit и др.
- Vulkan backend с сохранением software/headless paths; HD replacements и
  voxels как отдельные ресурсные/рендерные возможности. 3D floors требуют
  также world/map/physics/AI изменений, не только подключения Vulkan.

## Что не заявляется готовым

Полноценный Internet multiplayer, мастер-сервер DPT.DOOM, launcher, PK3 и
DECORATE ещё отсутствуют. Доступен только экспериментальный direct coop (protocol v2).
Первый этап не является реализацией всего ТЗ.
