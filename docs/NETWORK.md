# Экспериментальный прямой кооператив DPT.DOOM

Первый игровой прототип: **ровно два игрока**, TCP lockstep на 35 тиках/с.
Это отдельный DPT.DOOM protocol v1, **не** Chocolate Doom/csDoom protocol.
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

Меню не останавливает симуляцию в netgame. Закрытие/обрыв одного процесса
завершает второго понятной сетевой ошибкой; возврата в меню/reconnect пока нет.
На плохом канале TCP блокирует дальнейшие тики до доставки/таймаута: это
осознанное ограничение простого lockstep, не обещание плавного Internet gameplay.

Нельзя сочетать direct coop с demos/record/loadgame/turbo/deathmatch/timelimit/
дронами/ticdup. Сохранение онлайн и disconnect-to-SP load пока **не реализованы**:
меню сообщает это явно. Желаемое поведение записано в ROADMAP, сервер не откатывается.
Attract-demo snapshots в single-player остаются доступными.

## Протокол и сверка

- Фиксированный handshake 96 bytes: magic/version, SHA-1 fingerprint версии/
  исходников, SHA-1 имён/размеров/содержимого загруженных lumps в порядке загрузки,
  параметры игры, противоположный player index, player count 2.
- Fingerprint исходников генерируется CMake из `.c/.h`, CRLF нормализуется к LF.
  Это проверка случайно разных builds, не remote attestation и не криптозащита.
- Фиксированный tic record 24 bytes: magic, номер тика, предыдущий state projection,
  8 bytes явно сериализованного Doom ticcmd, зарезервированные нули.
- Все multi-byte поля network byte order. Нативные структуры/указатели не передаются.
- Проверяются размер/порядок чтения, движение -50..50, special commands, reserved
  bytes, ожидаемый tic index, state mismatch, EOF и bounded timeout.
- Hash игры — нормализованный archive projection + gameplay RNG. Флаг ML_MAPPED,
  зависящий от локальной камеры, исключён. Это **не полный snapshot** всей
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
  silent peer, disconnect, неверный номер тика, checksum/command errors.
- `net-windows`: два SDL-окна с различным движением/стрельбой/камерами,
  живое keyboard input и close/disconnect; interactive test с локальным IWAD.

`-headless-input path.bin` — только тестовый headless input, последовательность
8-byte команд формата codec; после EOF — нулевая команда. Legacy consistency
производится движком, не берётся из input-файла. Не применять его как новый
формат записи пользовательских демо.

Проверки Windows loopback не заменяют настоящую сессию двух машин. Linux/macOS
и поведение на реальной задержке/потере пакетов пока не подтверждены. ASan
проверяется отдельно; milestones и фактические результаты — ROADMAP.md.
