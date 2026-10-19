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

1. Прогнать новый publication/browser на GitHub и двух машинах; затем улучшать
   reachability/compatibility UI. Player profiles пока план. Graceful engine
   startup/handshake recovery, reconnect/re-host lifecycle — отдельный этап.
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
  commit ID, SHA-256 и соответствующим source ZIP; появились успешные remote
  runs (6061a13/run #11), текущие изменения требуют нового прогона.
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

- [x] Исходный этап: transport/game packet errors не закрывают процесс, а
  восстанавливают управление меню на границе тика (обычный peer leave уточнён ниже).
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

### Этап 2.4 — оставшийся игрок продолжает карту

- [x] По уточнению владельца: F7 возвращает только выходящего в меню, напарник
  продолжает тот же мир; закрытие peer/connection loss/empty timeout также не
  выбрасывают оставшегося. Работает для обоих слотов, не только первоначального хоста.
- [x] Карта/leveltime/позиция/инвентарь/монстры/dmflags не сбрасываются;
  отсутствующий игрок исключается из маски/команд, его body — неблокирующий corpse.
- [x] Снимается пауза, показывается локальное сообщение о выходе напарника.
- [x] Malformed/truncated tic, invalid command и desync остаются error → menu,
  не подменяются успешным выходом и не исполняют частичный/сомнительный ввод.
- [x] Headless host/guest survivor 70→140 тиков; ASan; SDL окно закрытие/F7
  обоих игроков, продолжение, собственный F7 и последующая SP/save/load.
- [x] Регрессия: 17/17 headless, 19/19 headless ASan на полном прогоне,
  20/20 SDL; усиленный survivor и сетевой ASan subset перепроверены после
  добавления boundary-report. SDL ASan+dummy audio guest-F7 teardown тоже зелёный.
- [ ] Сохранение local-only coop world, late join/reconnect ещё не реализованы.

Это не превращение в SP и не постоянно слушающий dedicated server. Сетевой
partition может породить две отдельные локальные ветки — их merge/resync нет.

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

### Этап 3.1 — Windows HTML launcher

- [x] Локальный HTML/CSS/JS встроен в EXE, WebView2 + native CreateProcessW host;
  прежний Win32-controls UI заменён по просьбе владельца.
- [x] ANOM-style палитры/градиенты/объёмные controls без заимствования компоновки;
  light/dark switch с persistent choice в выбранном WebView data directory.
- [x] Три профиля, EXE/IWAD/PWAD chooser/order, map/skill/monster/rules,
  command preview, single/host/join, LAN warning, session log/exit status.
- [x] `portable.txt` → `user/` рядом с launcher, без silent AppData fallback;
  copy с подтверждением/no-overwrite, browser cache тоже внутри user.
- [x] Постоянные config/saves по profileN, unique logs; lock занятого game slot.
- [x] Pinned SDK fetch + SHA-256, CMake install/package Loader DLL/LICENSE/NOTICE.
- [x] Model data-free tests и реальный Windows/WebView2 SP/host/join UI test.
- [x] Windows regression: 22/22 SDL, 18/18 MinGW headless, 20/20 MSVC headless
  ASan; workflow actionlint зелёный. Remote run #7/21aae1e прошёл.
- [x] Browser/master endpoint IP/domain — см. этап 3.2 ниже.
- [ ] Player appearance profiles, Unicode engine paths, drag/drop/search,
  launcher для Linux/macOS.

См. `docs/LAUNCHER.md`. Это playtest, не installer/release. WebView2 Runtime
требуется отдельно, сам launcher ничего не устанавливает.

### Дальнейший master/server этап

### Этап 3.2 — нативное directory-ядро

- [x] По уточнению владельца — C99 standalone `dptdoom-master`, без Python runtime.
- [x] Directory protocol v1/TCP, register/list/heartbeat/remove, OS random lease
  secrets + source IPv4, монотонный TTL, фиксированные лимиты/буферы/deadlines.
- [x] Data-free C model + real native EXE loopback tests; MSVC/MinGW Windows.
- [x] Regression Windows: 20/20 MinGW headless, 22/22 MSVC headless ASan,
  18/18 SDL без interactive; игровой engine/handshake не менялись.
- [x] По просьбе владельца master default TCP port 25135, клиентский default
  `play.dartpower.xyz:25135` в launcher.ini при сохранении; custom не затирается.
  Тестовый bind 0.0.0.0:25135 локально ответил; внешний port forwarding/DNS не проверены.
- [x] Opt-in публикация waiting-host из игры/launcher, heartbeat, снятие при
  accept/shutdown; реальный engine/master/guest test и outage fallback.
- [x] По просьбе владельца: Debug-only AllocConsole игрового клиента + file log;
  Release не открывает диагностическую консоль. Master — console + file events
  (startup/registration/heartbeat/expiry/errors), без токенов и packet spam.
- [x] Debug tee сохраняет inherited stdout/stderr/session log, tests для file/pipe,
  normal exit flush; master rotation/suppression/secrets tests.
- [x] Полный локальный прогон: 23/23 MinGW headless, 25/25 MSVC headless ASan;
  27/27 MSVC SDL; настоящий Debug SDL windows-smoke и launcher SP/COOP suite
  тоже прошли. Simulation baselines не обновлялись.
- [x] CI run #9: Windows jobs успешны; Linux/macOS master-loopback падал.
  Исправлены предпосылки теста (loopback aliases, capacity pacing/TTL), добавлены
  case diagnostics; новый run #11/6061a13 success, подтверждён API и владельцем.
- [x] Browser в launcher, выбор IP/домена/порта master, refresh/select и
  unverified metadata; join с переносом map/rules, ресурсы проверяет handshake.
- [x] Regression: 29/29 Windows MSVC SDL, 26/26 MinGW headless,
  28/28 MSVC headless ASan. Новые client-wire и actual game publication tests.
- [x] После remote test владельца добавлена network diagnostic v2: client
  endpoint/local socket/stage/OS error/status/bytes/time, launcher DNS context,
  master cid/source port/specific reject/progress reasons без secrets.
  Refusal/timeout/truncated/wrong-service/rejected cases покрыты tests, полные
  локальные 29/26/28 suites повторно зелёные. Реальная внешняя причина ещё не установлена.
- [ ] Browser compatibility badges/ping/filter/favorites; сейчас не обещать
  совпадение build/resources до handshake и game-port reachability.
- [ ] Отдельный безопасный game-query/challenge для port reachability; текущий
  single accept нельзя занимать проверочным подключением master.

Протокол/запуск/ограничения — `docs/MASTER.md`. Это directory, **не игровой server**,
не Internet-ready сервис. Native browser/publication связка проверяется локально.

### Последующие задачи master/server

- [ ] P2P PWAD (PK3 после загрузчика), без коммерческих IWAD; allowlist точных
  hashes для Freedoom 1/2 и shareware DOOM1.WAD, согласие/права/лимиты/safe cache.

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

- [ ] DeHackEd: `.deh` + `DEHACKED` lump, порядок patches, parsing bounds,
  vanilla regression и сверка модификаций в сети. FEATURE_DEHACKED сейчас OFF.
- [ ] Аудит DOOM ACE `engine/dehacked.*`; альтернативно игровой csDoom, если
  конкретная реализация проще/подходит. Проверить licenses/provenance и зависимости;
  локальный csDoomMaster — не игровой порт и не источник DeHackEd/MBF.
- [x] Владелец добавил игровые `csDoomNew` / `csDoomOld07` в refs. Найдены
  DeHackEd/BEX, Boom-элементы и MBF skies; первичный source/license inventory —
  `docs/REFERENCE-SOURCES.md`. Ничего не перенесено/не собрано из refs.
- [ ] MBF-совместимые карты/моды (medium по просьбе владельца); оценить BEX/Boom
  как промежуточные этапы, MBF21 отдельно. Сначала compatibility matrix/tests,
  не выдавать загрузку карты за корректную реализацию её mechanics.

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

README showcase обновлён по просьбе владельца: WebView screenshots обеих тем и
три headless demo framebuffer PNG, без desktop/window borders/личных путей.
Воспроизведение — `tools/capture-showcase.py`; raw captures только в build/.

Полноценный Internet multiplayer, игровой dedicated server, P2P downloads,
PK3 и DECORATE ещё отсутствуют. Windows HTML launcher/browser и opt-in
master publication есть; игра — экспериментальный direct coop (protocol v2).
Первый этап не является реализацией всего ТЗ.
