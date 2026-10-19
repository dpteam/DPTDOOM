# Экспериментальный прямой кооператив DPT.DOOM

Первый игровой прототип: **ровно два игрока**, TCP lockstep на 35 тиках/с.
Это отдельный DPT.DOOM protocol v2, **не** Chocolate Doom/csDoom protocol.
Не dedicated server, нет master server, prediction, rollback, NAT traversal,
reconnect, late join, аутентификации или шифрования. Только доверенная LAN/loopback.
Не выставлять порт в публичный Интернет как готовый защищённый сервер.

## Играть вдвоём

Оба используют одну сборку/Artifact и одинаковые IWAD/PWAD в одинаковом порядке.
Тестовая Windows-сборка со звуком: `build/windows-sdl/Release/dptdoom.exe`.

Хост (локально, для двух окон на одном компьютере):

```powershell
./dptdoom.exe -iwad C:/games/doom2.wad -warp 1 -net-host 2342
```

Второй игрок на том же компьютере:

```powershell
./dptdoom.exe -iwad C:/games/doom2.wad -warp 1 -net-join 127.0.0.1:2342
```

Для друга из LAN хост явно добавляет `-net-bind 0.0.0.0` (или свой LAN IPv4),
а друг подключается к **LAN_IP_ХОСТА:2342**. Windows Firewall может запросить
разрешение: предоставлять только осознанно для доверенной private-сети. Скрипты
форка не создают firewall rules, не включают UPnP и не пробрасывают роутер.
По умолчанию слушается **только 127.0.0.1**, не внешние интерфейсы.

Игровые параметры должны совпасть: например, оба `-warp 1 -skill 3`.
Игра стартует автоматически после подключения второго игрока и сверки handshake.
Путь к WAD может различаться; сверяется содержимое, порядок lumps и source fingerprint.
Адреса пока только numeric IPv4 (не DNS и не IPv6). `-net-timeout N` — таймаут
операции в ms (100..120000, default 10000), включая ожидание второго игрока.

## DMFLAGS (начальное подмножество правил)

`-dmflags N` — десятичная маска; **оба игрока указывают одинаковое значение**.
По умолчанию `0`: прежние vanilla multiplayer pickup/collision rules.
Это собственная нумерация DPT.DOOM, не ZDoom/Odamex-compatible bits.

| Бит | Значение | Правило |
|---|---:|---|
| COOP_NO_EXTRA_WEAPONS | 1 | В коопе не создавать weapon pickups, помеченные THINGS flag 16 (multiplayer-only). |
| PLAYERS_PASS_THROUGH | 2 | Разрешить живым телам игроков проходить сквозь друг друга, включая отсутствие player-to-player telefrag при teleport. |

Примеры: `-dmflags 1` — убрать дополнительные оружия; `-dmflags 2` — прохождение;
`-dmflags 3` — оба правила. Неизвестные биты/отрицательные/hex значения отвергаются.
Обычные оружия, патроны, ключи и extra coop monsters первым флагом не скрываются.
Отдельного DM-only бита в vanilla THINGS нет: фильтр касается только
multiplayer-marked pickups shotgun/SSG/chaingun/rocket launcher/plasma/BFG/chainsaw,
не weapon projectiles и не оружия, выпавшего из убитых монстров.

Прохождение — не общий `noclip`: стены/монстры остаются препятствиями. Выстрелы,
ракеты, splash damage и friendly-fire policy этим флагом не отключаются.
Он относится к двум активным живым player bodies, не к телам умерших игроков.
`maxplayers > 4` и auto collision policy пока отсутствуют: прототип фиксирован
на двух игроках. План для server config `auto|block|pass` — в `IDEAS.md`.

Правила задаются при запуске, не меняются локально на лету и сверяются в
handshake/state checksum. Ненулевой `-dmflags` вне direct coop запрещён, пока
нет расширенных save/demo formats: vanilla DSG не хранит эту маску.
Protocol v1 build и v2 build несовместимы; обоим тестировщикам нужен один artifact.

## Общий чат

`T` — открыть общий vanilla чат; Enter отправляет, Backspace редактирует,
Escape отменяет ввод. Пробелы поддерживаются. Сейчас это ASCII с ограниченным
HUD font, не Unicode/командные каналы. Team chat/voice/player custom names
находятся в плане (`IDEAS.md`). Содержимое чата не пишется в stdout по умолчанию;
только явный `-chat-debug` включает локальную диагностику (осторожно с приватностью).

BUG-001: первая версия direct coop ошибочно включала HUD-consumed chatchar в
state checksum и завершала игру при T. Исправление нормализует только это
transport/UI-поле, не отключая остальные проверки состояния. Чат сбрасывается
при новом уровне/получателе, Escape отменяет накопленный текст.

Меню не останавливает симуляцию в netgame. Закрытие/обрыв одного процесса,
таймаут или некорректный игровой пакет возвращают второго в титульное меню с
понятной причиной. После подтверждения сообщения можно начать SP или загрузить
обычный SP save. Сетевые игроки/rules/очереди/чат/пауза/fast state сбрасываются;
никакой тик из недополученной пары команд не исполняется.

**F7 / End Game** в direct coop предлагает подтверждение отключения. `N`/Escape
не меняют сессию; `Y` завершает её у обоих игроков и показывает меню. Закрытие
окна по-прежнему завершает локальный процесс нормально, не как разрыв в меню.
Reconnect/re-host/join из меню пока нет: для новой сетевой сессии перезапустить
оба процесса с CLI-параметрами. Автовосстановление прерванного мира не заявляется.

Ошибки аргументов/bind/connect/handshake **до старта сессии** пока завершают
процесс с ненулевым кодом; их UI recovery — следующий отдельный шаг.
`-net-fail-fast` сохраняет прежний диагностический ненулевой выход и для игровых
сетевых ошибок (используется тестом `net-errors`), не нужен для обычной игры.
На плохом канале TCP блокирует дальнейшие тики до доставки/таймаута: это
осознанное ограничение простого lockstep, не обещание плавного Internet gameplay.

Нельзя сочетать direct coop с demos/record/loadgame/turbo/deathmatch/timelimit/
дронами/ticdup. Сохранение онлайн и disconnect-to-SP load пока **не реализованы**:
меню сообщает это явно. Желаемое поведение записано в ROADMAP, сервер не откатывается.
Attract-demo snapshots в single-player остаются доступными.

## Протокол и сверка

- Фиксированный handshake 100 bytes: magic/version, SHA-1 fingerprint версии/
  исходников, SHA-1 имён/размеров/содержимого загруженных lumps в порядке загрузки,
  параметры игры, противоположный player index, player count 2 и `dmflags` (offset 96).
- Fingerprint исходников генерируется CMake из `.c/.h`, CRLF нормализуется к LF.
  Это проверка случайно разных builds, не remote attestation и не криптозащита.
- Фиксированный tic record 24 bytes: magic, номер тика, предыдущий state projection,
  8 bytes явно сериализованного Doom ticcmd, зарезервированные нули.
- Все multi-byte поля network byte order. Нативные структуры/указатели не передаются.
- Проверяются размер/порядок чтения, движение -50..50, special commands, reserved
  bytes, ожидаемый tic index, state mismatch, EOF и bounded timeout.
- Hash игры — нормализованный archive projection + gameplay RNG. Флаг ML_MAPPED,
  зависящий от локальной камеры, и consumed chat byte исключены. Это **не полный snapshot** всей
  симуляции и не защита от читов. Подробные ограничения archive — STATE-TESTING.md.
- TCP read/write поддерживают фрагментацию. После соединения NODELAY включён;
  главный поток ждёт peer. Оконный backend продолжает pump во время ожидания.

## Проверки

```powershell
ctest --test-dir build/headless -R 'net-' --output-on-failure
ctest --preset windows-asan -R 'net-' --output-on-failure
```

- `net-protocol`: самодостаточный byte-codec test без игровых данных (есть в CI).
- `net-loopback`: два реальных headless процесса, scripted movement/fire,
  монстры, 210 тиков и сравнение всего trace. Больше BACKUPTICS (128).
- `net-errors`: аргументы, несовпадение handshake, fragmented/truncated stream,
  silent peer, disconnect, неверный номер тика, checksum/command errors и dmflags mismatch.
- `net-recovery`: игровые peer close/timeout/order/desync/command failures без
  fail-fast возвращают в меню, продолжают headless loop и завершаются по лимиту
  тиков с кодом 0. Старый тест `net-errors` явно использует `-net-fail-fast`.
- `match-rules`: маска, weapon type/coop/DM filtering, живые игроки против
  monsters/projectiles/corpses (без игровых данных, включён в CI).
- `net-rules`: временный MAP01 overlay из локального Doom II IWAD, все четыре
  комбинации маски, реальные spawned pickups и player movement clipping.
  Временные игровые ресурсы не попадают в Git/artifacts; без MAP01 тест skip.
- `net-loopback-dmflags`: полные 210 тиков с маской 3 и сверкой трасс.
- `net-chat`: две стороны отправляют общий чат, cancel/backspace/high-bit input,
  210 тиков и совпадающие trace; сетевой GUI test также проверяет реальные T,
  пробел/Backspace/Enter/Escape и input widget при уменьшенном viewport.
- `net-windows`: два SDL-окна с различным движением/стрельбой/камерами,
  живое keyboard input, peer close → меню → бывший guest стартует SP/save/load;
  interactive test с локальным IWAD. `net-windows-local-end` проверяет F7 cancel/
  confirm, dmflags=3, fast monsters, paused/open chat teardown и тот же SP reset.
  Для ручного запуска скрипта с новыми правилами добавить `--dmflags 3`.

Дополнительно локально проверен SDL ASan-вариант с dummy аудио, `--audio` и
`--fast --local-end`. Это не проверка полного reconnect/online snapshot.

`-headless-input path.bin` — только тестовый headless input, последовательность
8-byte команд формата codec; после EOF — нулевая команда. Legacy consistency
производится движком, не берётся из input-файла. Не применять его как новый
формат записи пользовательских демо.

Проверки Windows loopback не заменяют настоящую сессию двух машин. Linux/macOS
и поведение на реальной задержке/потере пакетов пока не подтверждены. ASan
проверяется отдельно; milestones и фактические результаты — ROADMAP.md.
