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
  Server browser: native TCP query worker, IP/domain endpoint, explicit refresh,
  select→form (не auto launch), unverified rows. MinGW: DPTDOOM_LAUNCHER=OFF.
- Master и будущий игровой сервер по требованию владельца — native C/C++, не
  Python-сервисы. `master/` → `dptdoom-master`, опция DPTDOOM_MASTER default ON;
  протокол/limits — `docs/MASTER.md`. Windows MSVC/MinGW tests: master-model,
  master-loopback (Python только клиент теста). Не называть directory игровым
  server: публикация waiting host opt-in `-master IPv4:port -session-name ASCII`,
  heartbeat/accept-remove; query-reachability/TLS/auth нет. Shared bounded client
  `master/client.*`; тесты master-client(-wire), master-publish с IWAD, UI browser.
  Default master TCP port 25135; launcher.ini [Master] Endpoint по умолчанию
  play.dartpower.xyz:25135 (не перезаписывать пользовательский адрес).
  По просьбе владельца допустим тестовый --bind 0.0.0.0:25135, но не создавать
  firewall/router rules и не заявлять внешнюю доступность без проверки.
- Hairpin/SNAT: optional master --config с address-map=private-CIDR=public-IPv4,
  до 8 canonical nonoverlapping RFC1918/loopback ranges. По умолчанию OFF;
  actual source сохраняется для auth/limits, list IP отдельно, game port прежний.
  Owner IP не хардкодить/не коммитить; local config только build/ или вне repo.
  Пример docs/master-example.conf inert; tests master-address-map(-model).
- Public address modes: public-address-mode=manual|dns|api + hairpin-subnet,
  соответственно public-ip/public-host/public-api; default OFF. DNS/HTTPS worker,
  refresh 300s, last-good 3 intervals, смена/истечение удаляет mapped leases.
  Windows WinHTTP, Unix optional libcurl + pthreads; API только opt-in HTTPS,
  no redirects/TLS bypass. Owner config только build/, no owner-IP hardcode.
- Debug graphical Windows client: AllocConsole + stdout/stderr tee/file,
  `dpt_debug_console.c`; Release/RelWithDebInfo/headless без него. --version тихий.
  debug-console test interactive. Master `--log`, `--log-max-bytes`: 1MiB + .1,
  event suppression 4/type/5s; токены/packet strings не логировать.
- CI run #11/6061a13 подтверждён success GitHub API/владельцем: data-free
  Windows/Linux/macOS SDL/headless + Windows ASan; ручная игра Unix не проверена.
  Новые publication/browser изменения требуют следующего remote run.
- Master diagnostic v2: game.log содержит endpoint/DNS context/stage/OS error/
  sent/received/status/UTC; master log — cid/source TCP port/reject reason/bytes.
  Launcher DNS failures: launcher-network-<pid>-<ticks>.log в data dir.
  Не логировать raw packet/auth/name; Firewall/NAT/VPN не менять автоматически
  и не выдавать connect timeout за доказанную причину сетевой неисправности.
- Relay разрешён владельцем: master --relay opt-in, 8 пар/4 на source, 4KiB
  buffers/side, 64KiB/s/side, idle120s/lifetime6h. Operations5/6 и LIST row156
  bit0; старые browser builds могут отклонить flag, обновлять обоих peers.
  Native master/relay.c, docs/RELAY.md; not arbitrary proxy/TLS/auth. Host lease
  source+secret, guest public session-id, consumed listing; direct first then
  2s fallback. Tests master-relay/master-publish/UI include unavailable direct,
  traces/survivor. Launcher wait120s; startup dpt_connect_screen.* uses HUD font.
- P2P — план: PWAD, PK3 после загрузчика, без commercial IWAD; точный hash
  allowlist только Freedoom 1/2 + shareware DOOM1.WAD (не full Doom I).
- Звук/музыка работают в проверенном Windows SDL-бэкенде; нативная
  Win32/headless сборка без звука. Mouse/gamepad/OPL пока не реализованы.
- DECORATE, PK3 и загрузка вложенных WAD пока не реализованы.
- Boom/MBF maps: реализован sky transfer (linedef 271/272, per-sector
  `sector->sky` вне savegame, visplane учитывает текстуру), весь диапазон
  generalized-дверей `0x3c00..0x3fff`, locked doors `0x3800..0x3bff` (все
  8 lock-значений и режим `sk=ck`, ключи проверяются до создания thinker-а),
  generalized crushers `0x2f80..0x2fff`
  и stairs `0x3000..0x33ff`, подмножество lifts `0x3400..0x37ff`, плюс
  подмножества generalized floors `0x6000..0x7fff` и ceilings
  `0x4000..0x5fff`; push-триггеры P1/PR работают для всех семейств и действуют
  на сектор за линией; generalized sector specials (base/damage/secret, без
  friction/push). Декодеры в `dpt_gendoor.c`/`dpt_genfloor.c`/
  `dpt_genceiling.c`/`dpt_gencrusher.c`/`dpt_genstairs.c`/`dpt_genlift.c`/
  `dpt_genlocked.c`/`dpt_secspecial.c`,
  data-free тесты `gendoor`/`genfloor`/`genceiling`/`gencrusher`/`genstairs`/
  `genlift`/`genlocked`/`secspecial`). Остальные Boom/MBF specials (часть
  lift-целей, часть floor/ceiling-целей, friction/push) не реализованы и
  попадают в отчёт
  `Map compatibility:`; не выдавать загрузку карты за совместимость.
  Детали/тесты — `docs/MAPS.md`, `map-sky` и data-free тесты.
- DeHackEd subset: deh_classic.c, DPT_DEH_CLASSIC, **Misc + Ammo + bounded Text + Weapon frames + Thing numeric/flags/states/sounds + Frame graph/visual/psprite** classic
  v3.0/Doom19/format6; FEATURE_DEHACKED остаётся OFF. -deh files first, explicit
  -dehlump IWAD/PWAD directory order second; atomic per source, strict rejects.
  Raw bytes/order/subset digest в ResourceDigest; patched saves используют
  marker DPTDEH1 + digest, patched demos — версию 120 с тем же manifest;
  vanilla DSG/.lmp байт-совместимы. Версия DPT-deh-levelnames-v19; Ammo max/per,
  Weapon Ammo type только 0..3, arbitrary frame/noammo/overflow tricks отклонять.
  Tests deh-misc/deh-engine, docs/DEHACKED.md.
  Classic Sound/Sprite/Pointer осознанно не реализуются: это DOS-EXE offsets и
  version-bound function pointers (см. DEHACKED-MBF-AUDIT.md). Cheat — отдельный
  сетевой класс. BEX [STRINGS]/[PARS]/[CODEPTR] реализованы bounded;
  [CODEPTR] — allowlist 74 actions + проверка класса acp1/acp2, общая
  X-макрос-таблица deh_actions.h (unit test — stub). Самоссылающееся AI-действие
  ограничено 64 уровнями P_SetMobjState вместо stack overflow.
  Thing 1-based, optional (label); ID #/Bits/state/sound references bounded; health/reaction/pain/speed/
  radius/height/mass/damage bounded; Frame Duration1..32767, S_NULL и SARG fast range запрещены,
  bounded Text whitelist; durations only staged/reset; bounded Next frame and Weapon frame references only,
  Frame Sprite number 0..NUMSPRITES-1, subnumber0..28|32768 (fullbright), staged/reset.
  R_InitSprites validates visually patched states against loaded frame counts
  before simulation; missing resources fail startup, no fallback.
  Frame Unknown 1/2 = misc1/misc2 only for weapon states, -128..128 map units,
  applied via multiplication (no negative-shift UB); psprite sx/sy tested.
  Misc Monsters Infight 0..1 через общий staged/reset path, patch report/digest;
  боевой сценарий двух видов монстров автоматически не покрыт.
  Actions не менять. Full Thing flags/
  states/sounds, остальные Frame/Text/BEX/MBF не готовы.
  Base/staged mobjinfo static, reset restores original. Tests actual barrel spawn;
  refs code не переносился.
- Upstream PR и решения по ним фиксируются в `docs/UPSTREAM.md`.
- Игровые csDoom references теперь доступны: `refs/csDoomNew`, `csDoomOld07`,
  плюс отдельный `csDoomMaster`. Source/license карта — `docs/REFERENCE-SOURCES.md`.
  New client/license.txt — MIDAS, server/Licence — NASM, не общая GPL-лицензия.
  DeHackEd/BEX найден, full MBF/MBF21 не подтверждены; refs binaries не запускать.
- Идеи/цели: `docs/IDEAS.md`; реестр багов: `docs/BUGS.md`. Владелец сообщил
  T-chat exit (BUG-001): network checksum должен исключать HUD-consumed chatchar,
  а не отключать чат. `net-chat` и `net-windows` проверяют delivery/cancel/edit.
- Team chat/voice, player profiles/colors/skins, launcher/browser/master и
  Vulkan/3D floors/HD/voxels — план, не готовые фичи. Не включать микрофон без
  явного согласия; Vulkan сам по себе не добавляет world/physics 3D floors.
