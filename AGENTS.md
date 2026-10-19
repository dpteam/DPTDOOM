# DPT.DOOM: правила работы

## Требования владельца

- Основное ТЗ — `PROMPT.md`; прогресс и очередь — `docs/ROADMAP.md`.
- Общаться по-русски. Публичное имя — **DPT.DOOM**, бинарник — `dptdoom`.
- Основная среда разработки/проверки — Windows. Сохранять переносимость C-кода;
  не выдавать сборку Linux/macOS за проверенную, пока она не проверена.
- Частые тесты выполнять headless. После средних/крупных изменений иногда
  запускать оконную игру и оставлять открытой, чтобы владелец видел результат.
  Не открывать окно после каждой мелкой правки.
- Пользоваться Git: небольшие логические коммиты после проверок. Не пушить
  без отдельной команды. Не откатывать чужие изменения.

## Данные и лицензии

- `refs/` — локальные материалы, весь каталог игнорируется Git.
- Тестовый лицензионный IWAD владельца: `refs/iwads/doom2.wad`.
  Не коммитить IWAD, извлечённые игровые ресурсы, сохранения или тестовые снимки.
  Исключение по явной просьбе владельца: выбранные showcase PNG в `docs/images/`
  для README; получать из framebuffer/WebView, без рамок/рабочего стола/личных
  путей. Генерация — `tools/capture-showcase.py`, временные captures в build/.
- Иконка из `refs/DPTDOOM_ICON.png` скопирована в `resources/`.
  Обновление: `python tools/import-icon.py` (нужен Pillow).
- Сохранять GPL и copyright исходных авторов. Наличие кода в `refs/` не является
  подтверждением лицензии на его включение: сначала проверить происхождение.

## Сборка и тесты

Основная поддерживаемая система сборки — корневой CMake. Старые Makefile и
VS solution сохранены для совместимости; имена `doomgeneric_*` и API `DG_*`
пока сохраняются ради понятного сравнения с upstream.

```powershell
cmake --preset windows -DDPTDOOM_TEST_IWAD="$PWD/refs/iwads/doom2.wad"
cmake --build --preset windows
ctest --preset windows

cmake --preset windows-headless -DDPTDOOM_TEST_IWAD="$PWD/refs/iwads/doom2.wad"
cmake --build --preset windows-headless
ctest --preset windows-headless
```

- В текущей машине также проверены `build/msvc`, `build/mingw`, `build/headless`
  (MinGW), `build/headless-msvc`. Это локальные build trees, не исходники.
- Windows SDL + звук: `powershell -NoProfile -File tools/fetch-sdl.ps1`, затем
  `cmake --preset windows-sdl`, build/test с тем же preset. Зависимости лежат
  только в `build/deps`, официальный ZIP проверяется SHA-256. Полные инструкции
  и ограничения — `docs/TESTING.md`. Владелец подтвердил звук через RDP.
- `ctest --preset windows-sdl -LE interactive` не открывает видимые окна:
  input/audio tests и sdl-engine используют dummy SDL drivers. Они не доказывают
  работу аппаратного MIDI; для него нужна ручная оконная проверка.
- Тесты создают временные рабочие каталоги, не меняя конфиги игрока.
- Headless — бэкенд тестирования, **не** dedicated multiplayer server.
- Ванильный `-timedemo` сообщает результат через `I_Error` и возвращает ненулевой
  код. Тест обязан проверить сообщение `timed ... gametics`, не только exit code.
- Не включать `fast-math`, `-march=native`, обход CRT или упаковщик EXE без
  отдельного обоснования, сравнительных измерений и regression tests.
- RGB checksum — только проверка повторяемости изображения в текущем движке,
  не доказательство полной детерминированности симуляции/сети.
- Потиковые projections и save/load: `docs/STATE-TESTING.md`, тест `state-smoke`.
  Эталоны `tests/state-baselines.json` привязаны к SHA-256 IWAD и версии схемы;
  не обновлять их молча при несовпадении. MSVC/GCC Doom II traces совпадают.
- `-headless-save-tic N` **перезаписывает слот 0**: только временный рабочий
  каталог. Ванильный DSG не хранит RNG/target/tracer; не сравнивать продолжение
  после load с непрерывным демо как будто это полный snapshot.

## Архитектурные ограничения

- `windows-asan` preset — MSVC x64 headless RelWithDebInfo. Ограничения и команды:
  `docs/SANITIZERS.md`. Runtime DLL копируется рядом с EXE. Сначала проверить
  `--version`; не множить missing-DLL диалоги при поломанной среде.
- ASan zone probes намеренно падают только в дочерних тестовых процессах;
  wrapper требует настоящий ASan report. UBSan/Linux/macOS пока не проверены.
- Сохранение во время attract-demo — **желаемая фича владельца**, не баг для
  удаления: F2/menu сохраняет, playback продолжается, load передаёт управление
  single-player. Тест `demo-snapshot` открывает окно (метка `interactive`).
- Будущая сеть: разрешить online snapshot; load только в SP с подтверждением
  отключения от сервера. В текущем direct coop online save/load заблокированы
  до отдельной реализации; не изображать их рабочими заглушками.
- CI подготовлен в `.github/workflows/ci.yml`, прошёл actionlint, но remote run
  ещё не выполнен. См. `docs/CI.md`: не называть Linux/macOS проверенными до
  реального успешного запуска. Коммерческие IWAD в CI не передавать.
- Владелец отказался от установки build tools/SDL в локальный WSL Debian на
  текущем этапе; не устанавливать их без нового разрешения.

- Старый `FEATURE_MULTIPLAYER` выключен. Новый `dpt_net.c` включён CMake option
  `DPTDOOM_NETWORK`: TCP lockstep ровно на 2 игроков, Windows loopback проверен
  headless/ASan/SDL. См. `docs/NETWORK.md`; не заявлять публичную Internet-ready
  сеть, NAT, reconnect или совместимость с Chocolate/csDoom. В `refs/csDoomMaster`
  — только старый мастер-сервер, не используемый новым прототипом.
- Обычный peer leave/transport loss/empty timeout теперь оставляет напарника в
  текущем мире (оба слота), local coop session/rules сохраняются, late join нет.
  Собственный F7 с cancel/confirm возвращает только выходящего в меню; собственный
  F7 оставшегося тоже заканчивает локальное продолжение. Malformed/truncated tic,
  invalid cmd/desync → меню, не считать их обычным leave. `-net-fail-fast` для tests.
  Перед SP после собственного выхода сбрасываются loop/player/rules/chat/pause/fast states.
  Startup/handshake errors пока fail-fast; reconnect/re-host из меню не реализованы.
- Direct protocol теперь v2/100-byte handshake с `dmflags` (0..3). Bit1=не
  создавать multiplayer-only coop weapon pickups, bit2=player body pass-through.
  Это не ZDoom numbering. Ненулевые flags только в direct coop, не писать их
  молча в vanilla DSG/demo. См. `docs/NETWORK.md`, `match-rules`/`net-rules` tests.
- 1024 игрока, DM/COOP progression bots и auto collision при configured max>4
  — **идеи/цели**, не готовые функции. См. `docs/IDEAS.md`; MAXPLAYERS не увеличен,
  серверного maxplayers config и ботов сейчас нет.
- Artifacts: `.github/workflows/ci.yml` + `tools/package-build.py`. Windows SDL
  build использует static CRT. Пакеты только из allowlist CMake install и
  `git archive HEAD`; source должен совпадать с HEAD, не паковать dirty builds.
  Включать commit/source/licenses, не включать IWAD/config/save/screenshots.
- Native Windows launcher в `launcher/`, документация `docs/LAUNCHER.md`.
  HTML/CSS встроены в EXE; MSVC C++/WRL host + WebView2 Runtime, engine/model — C.
  SDK: `tools/fetch-webview2.ps1` (pinned NuGet + SHA-256, без установки runtime).
  CreateProcessW без shell; 3 профиля LocalAppData или `portable.txt` → `user/`
  рядом с EXE. Game CWD постоянный `game/profileN`, logs — `sessions/`.
  `launcher-model` — data-free; `launcher-windows` — interactive + IWAD/Runtime.
  Server browser/master ещё не готов; MinGW engine: DPTDOOM_LAUNCHER=OFF.
- Master и будущий игровой сервер по требованию владельца — native C/C++, не
  Python-сервисы. `master/` → `dptdoom-master`, опция DPTDOOM_MASTER default ON;
  протокол/limits — `docs/MASTER.md`. Windows MSVC/MinGW tests: master-model,
  master-loopback (Python только клиент теста). Не называть directory игровым
  server: публикации игры/browser/query-reachability пока нет, TLS/auth тоже нет.
  Default master TCP port 25135; launcher.ini [Master] Endpoint по умолчанию
  play.dartpower.xyz:25135 (не перезаписывать пользовательский адрес).
  По просьбе владельца допустим тестовый --bind 0.0.0.0:25135, но не создавать
  firewall/router rules и не заявлять внешнюю доступность без проверки.
- Звук/музыка работают в проверенном Windows SDL-бэкенде; нативная
  Win32/headless сборка без звука. Mouse/gamepad/OPL пока не реализованы.
- DECORATE, PK3 и загрузка вложенных WAD пока не реализованы.
- Upstream PR и решения по ним фиксируются в `docs/UPSTREAM.md`.
- Идеи/цели: `docs/IDEAS.md`; реестр багов: `docs/BUGS.md`. Владелец сообщил
  T-chat exit (BUG-001): network checksum должен исключать HUD-consumed chatchar,
  а не отключать чат. `net-chat` и `net-windows` проверяют delivery/cancel/edit.
- Team chat/voice, player profiles/colors/skins, launcher/browser/master и
  Vulkan/3D floors/HD/voxels — план, не готовые фичи. Не включать микрофон без
  явного согласия; Vulkan сам по себе не добавляет world/physics 3D floors.
