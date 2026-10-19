# DPT.DOOM master — нативный directory-прототип

**Master написан на C99**, отдельный `dptdoom-master.exe` (Unix target:
`dptdoom-master`). Ни Python, ни .NET/Node/WebView2 для его запуска не нужны.
Python используется только в автоматических тестах. По уточнению владельца
будущий игровой сервер тоже должен быть нативным C/C++, не Python-сервисом.

Это **каталог объявлений**, не игровой dedicated server, relay или новая модель
репликации. Текущая игра по-прежнему direct COOP на двоих, без late join.
**Автоматическая публикация из игры/launcher и server browser пока не подключены.**
Записи в тестах синтетические; их нельзя выдавать за опубликованные игровые сессии.

## Сборка и запуск

Master включён опцией `DPTDOOM_MASTER=ON` по умолчанию в корневом CMake. Он
собирается независимо от backend и не линкуется с движком/SDL/launcher.

```powershell
cmake --build --preset windows-sdl --target dptdoom-master
& ./build/windows-sdl/Release/dptdoom-master.exe --version
& ./build/windows-sdl/Release/dptdoom-master.exe
```

По умолчанию слушает **только `127.0.0.1:25135` TCP**, lease TTL 60 секунд.
Для осознанной LAN-проверки можно задать `--bind 192.168.1.10` либо `0.0.0.0`.
Аргументы: `--port 1..65535`, `--lease-seconds 3..300`, `--help`.
Короткий TTL полезен для тестов; heartbeat interval возвращается как TTL/3.
Ctrl+C завершает цикл. Закрытие/перезапуск master теряет все записи — базы нет.
Никакие firewall rules, автозапуск, службы или настройки роутера не создаются.

## Консоль и файловые события

Master печатает UTC-время и события в stdout **и** `dptdoom-master.log` в рабочем
каталоге: startup/stop, connect/list/register/heartbeat/remove/expire,
invalid/auth failures/limits/deadlines/transport errors. `--log path` меняет путь;
папка должна существовать. Ошибка открытия файла — понятный отказ startup, а не
незаметная работа без лога. `--version`/`--help` файлов не создают.

По умолчанию файл ограничен 1 MiB, один backup `<log>.1`;
`--log-max-bytes 4096..16777216` меняет лимит. Ротация удаляет только предыдущий
backup с этим точным именем, не каталоги. При ошибке записи/ротации файловый sink
отключается с предупреждением, консоль остаётся. Не направлять несколько master
процессов в один log path. Медленный диск/перенаправленный stdout может задержать
event loop: это локальная диагностика, не асинхронный production log collector.

На каждый тип события — максимум 4 подробные строки за 5 секунд, далее одна
сводка `suppressed=N`. Client name/packet bytes/public id/lease secret не
логируются. IP/порт — сетевые данные, учитывать их при публикации логов.

По просьбе владельца тестовый внешний адрес — `play.dartpower.xyz:25135`.
Для ручного проброса: `dptdoom-master.exe --bind 0.0.0.0 --port 25135`.
Пробрасывать **TCP**, не UDP. Это не игровой порт и не подтверждение внешней
доступности/DNS; её нужно отдельно проверить с другой машины/сети.
Launcher при сохранении профиля создаёт `[Master] Endpoint=play.dartpower.xyz:25135`
в своём `launcher.ini`, не заменяя пользовательский адрес. Пока это сохранённый
default для следующей browser-интеграции, не активное подключение к master.

Windows runtime проверен MSVC и MinGW, модель/loopback также MSVC ASan.
Unix socket/clock/entropy path предусмотрен, **Linux/macOS не запускались**.
CI build/test подключаются через CMake; наличие YAML не доказывает remote run.

## Поведение и пределы

- TCP source IPv4 — адрес записи; клиент передаёт только game port, **не host**.
  Master не верит произвольным forwarded IP/домашним DNS/URL из metadata.
- При регистрации OS CSPRNG выдаёт случайные id (16 bytes) и secret (24 bytes).
  В Windows — BCryptGenRandom, в Unix — `/dev/urandom`; не rand()/timestamp.
  Если entropy недоступна, регистрация не принимается.
- Heartbeat/delete требуют id, secret и тот же source IP; stale/wrong credentials
  не оживляют запись. Secret не включается в list и не печатается в лог.
- Проверка TCP/source **не доказывает доступность game port** и не является
  аутентификацией человека/build. Поля name/build/resources/map — claims клиента.
  Все записи подразумевают `reachability=unverified`, `coop/waiting/1 of 2`.
  Host обязан удалить объявление, когда место занято; эта интеграция ещё впереди.
- TTL идёт по монотонным часам, не по количеству idle-итераций. Prune выполняется
  и без запросов, поэтому поток трафика не замораживает старение записей.
- Максимум 128 записей, 4 с одного IP; дубликат IP/game-port отвергается.
- Одновременно 32 соединения, максимум 8 с одного IP, фиксированные буферы,
  deadline 3 секунды на весь приём/отправку, nonblocking `select`.
- Rate buckets: на source IP burst 24 / refill 4 соединения в секунду;
  глобально burst 128 / refill 64 в секунду; таблица IP ограничена 256.
  Превышение connection/rate limits закрывает соединение без ответа. Это
  ограничивает ресурсы, но **не обещает защиту от DoS/ботнета**.
- Пакеты UDP отсутствуют: сервис не служит UDP amplification reflector.
  Никаких исходящих probes по заданным клиентом адресам/портам пока нет.

## Протокол directory v1

Отдельный протокол, **не direct game v2 и не csDoom/Chocolate**. Все целые —
big-endian, значения serialized по offsets; native struct/padding не передаются.
Один request/response на TCP connection, затем close; без pipelining/keepalive.
Неполный запрос никогда не создаёт/не обновляет запись. Заголовок сначала
проверяется целиком, произвольная длина тела не выделяет память.

16-byte header:

| Offset | Bytes | Поле |
|---|---:|---|
| 0 | 4 | ASCII `DPTM` |
| 4 | 2 | directory version = 1 |
| 6 | 2 | operation: LIST=1, REGISTER=2, HEARTBEAT=3, REMOVE=4 |
| 8 | 4 | body length |
| 12 | 2 | status: в request 0; response 0=OK, 1=bad request, 2=unauthorized, 3=conflict, 4=capacity |
| 14 | 2 | reserved = 0 |

LIST request: пустое тело. Response: uint32 count, затем count × 160-byte rows.
Максимальное тело 20484 bytes. Каждая row:

| Offset | Bytes | Поле |
|---|---:|---|
| 0 | 16 | public session id |
| 16 | 4 | source IPv4 в network byte order |
| 20 | 4 | оставшийся TTL в секундах (округлён вниз) |
| 24 | 132 | registration info (ниже) |
| 156 | 4 | reserved = 0 |

REGISTER request: 132-byte info:

| Offset | Bytes | Поле |
|---|---:|---|
| 0 | 64 | name: 1..63 printable ASCII, NUL + zero padding, без пробелов по краям |
| 64 | 20 | game handshake build SHA-1 (не raw CMake SHA-256 string) |
| 84 | 20 | game resource SHA-1, в том же порядке, что handshake |
| 104 | 12 | reserved = 0 |
| 116 | 8 | `MAP01`..`MAP32` или `E1M1`..`E4M9`, NUL + zero padding |
| 124 | 2 | game TCP port 1..65535 |
| 126 | 1 | skill 1..5 |
| 127 | 1 | bits 0..1 = dmflags 0..3, bit 2 = nomonsters, bit 3 = fast, bit 4 = respawn |
| 128 | 1 | direct game protocol = 2 |
| 129 | 3 | reserved = 0 |

REGISTER success: 48 bytes: id16 + secret24 + uint32 lease seconds + uint32
recommended heartbeat interval. HEARTBEAT/REMOVE request: id16 + secret24
(40 bytes). Success: uint32 lease seconds (REMOVE — 0). Error response body
пустое. Неизвестные bits/version/op/reserved/length/name/map отвергаются.

ASCII name — явное ограничение v1; кириллицу нельзя тихо кодировать системной
ANSI-страницей. Для Unicode нужен согласованный UTF-8 extension/version.

## Что ещё нужно перед публичным использованием

**Не публиковать этот прототип как Internet-ready master.** Нет TLS, account
auth/moderation, distributed abuse control, port ownership challenge/proof,
доверенных game status queries, pagination/version negotiation или persistent DB.
Source+secret защищают lease от простого чужого update, но не от прослушивания
cleartext TCP, злонамеренных регистраций с собственного IP и ложных metadata.

Нельзя проверять нынешний direct-host произвольным TCP connect: это может занять
его единственный accept вместо игрока. Для reachability нужен отдельный
безопасный query/challenge путь в игре, затем фильтрация/явный статус в browser.
IP/домен master в launcher будут настраиваемыми, direct-connect останется
независимым. NAT traversal/relay и игровой dedicated server — отдельные задачи.

Старый `refs/csDoomMaster` изучен, код не скопирован: ошибки `bAddServer` после
выхода index за массив, доверие входящим UDP registrations/remote list и
небезопасные string-to-address routines не переносятся. Новая реализация написана
заново; наличие refs само по себе не подтверждает право включать старый код.

## Автотесты

`master-model` (C, без сети): validation/header bounds, capacity, TTL,
source+secret auth, renew/delete; полный bounded list.
`master-loopback` (Python-клиент к **настоящему native EXE**, не Python-server):
fragmented TCP, lifecycle/entropy/secrets, wrong-source token, TTL/heartbeat,
malformed headers/info, slow/truncated peers, rate/capacity и startup/bind errors.
Также проверяются 32 одновременных partial peers (8/IP), восстановление слотов
после deadline и реальная выдача максимального списка из 128 записей.
Временный loopback port и рабочий каталог; ни IWAD, ни окно не нужны.
`master-log` отдельно проверяет ограничение повторов, размер/backup rotation
и отказ при недоступном пути/ошибке ротации.

На платформах без настроенных loopback aliases тест не создаёт их с root:
multi-source wire cases пропускаются с явным сообщением (модель проверяется),
остальные lifecycle/framing/rate сценарии выполняются. Capacity wire test
использует TTL 60s и pacing ниже global refill, не смешивая capacity и rate limit.

```powershell
ctest --preset headless -R '^master-'
ctest --preset windows-asan -R '^master-'
```
