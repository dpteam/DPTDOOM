# DPT.DOOM master — нативный directory-прототип

**Master написан на C99**, отдельный `dptdoom-master.exe` (Unix target:
`dptdoom-master`). Ни Python, ни .NET/Node/WebView2 для его запуска не нужны.
Python используется только в автоматических тестах. По уточнению владельца
будущий игровой сервер тоже должен быть нативным C/C++, не Python-сервисом.

По умолчанию это **каталог объявлений**, не игровой dedicated server и не новая
модель репликации. Есть opt-in `--relay` для двух исходящих игровых соединений
на том же порту: [RELAY.md](RELAY.md). По умолчанию relay OFF.
Текущая игра по-прежнему COOP на двоих, без late join.
Реализована публикация ожидающего хоста из игры и выбор объявления в Windows
HTML launcher. Это не late join: объявление снимается сразу после accept игрока.
`master-publish` и launcher test используют настоящие игровые процессы.

## Сборка и запуск

Master включён опцией `DPTDOOM_MASTER=ON` по умолчанию в корневом CMake. Он
собирается независимо от backend и не линкуется с движком/SDL/launcher.

```powershell
cmake --build --preset windows-sdl --target dptdoom-master
& ./build/windows-sdl/Release/dptdoom-master.exe --version
& ./build/windows-sdl/Release/dptdoom-master.exe
```

По умолчанию слушает **только `127.0.0.1:25135` TCP**, lease TTL 60 секунд.
Это осознанный безопасный дефолт: сервер не открывается наружу, пока оператор не
попросил об этом явно. Обратная сторона — удалённые клиенты получат отказ
соединения без объяснения, поэтому при старте на loopback мастер печатает
предупреждение с готовой подсказкой.

### Конфиг создаётся автоматически

Если передать `--config path`, которого ещё нет, мастер **создаст** файл с
подробными комментариями (все ключи закомментированы, то есть файл ничего не
меняет, пока его не отредактируют) и продолжит работу на дефолтах. Это убирает
ошибку «cannot open config» на первом запуске и даёт готовый шаблон для правки.

Опечатка в каталоге по-прежнему ошибка: `--config build/nosuchdir/x.conf` не
создаст ничего и завершится с кодом 2, чтобы нельзя было случайно получить
«настроенный» мастер, который молча игнорирует существующий конфиг в другом
месте.

Для LAN/Internet клиентов нужно слушать не только loopback:

```powershell
dptdoom-master.exe --bind 0.0.0.0 --port 25135
```

Либо конкретный интерфейс: `--bind 192.168.1.10`. Чтобы не повторять это при
каждом запуске, адрес можно закрепить в конфиге (`--config`):

```text
bind=0.0.0.0
```

Приоритет: `--bind` из командной строки **перекрывает** `bind=` из конфига, чтобы
общий конфиг не мог молча отменить явный аргумент. Некорректный адрес из любого
источника — ошибка запуска, а не тихий откат на loopback (иначе выглядело бы, что
сервер поднялся, хотя подключиться никто не может).

Аргументы: `--port 1..65535`, `--lease-seconds 3..300`, `--help`.
Короткий TTL полезен для тестов; heartbeat interval возвращается как TTL/3.
Ctrl+C завершает цикл. Закрытие/перезапуск master теряет все записи — базы нет.
Никакие firewall rules, автозапуск, службы или настройки роутера не создаются.
`--bind 0.0.0.0` означает «слушать на всех интерфейсах», но **не** делает сервер
доступным извне сам по себе: firewall/роутер/NAT по-прежнему могут блокировать,
и это не проверяется автоматически.

## Ban list

Мастер умеет отказывать по адресу источника. Файл задаётся ключом `banlist=` в
`--config`:

```text
# баним один адрес
203.0.113.7

# баним подсеть
192.168.10.0/24
```

- По строке на правило: точный IPv4 (означает `/32`) или `IPv4/prefix`.
- `#` — комментарий, пустые строки игнорируются.
- Необязательный срок: `203.0.113.7 until 2026-12-31T23:59:59Z` — временный бан.
  Время **только UTC с обязательной `Z`**; локальная зона не угадывается, потому
  что это меняло бы смысл правила от машины к машине. Просроченное правило
  перестаёт действовать само, файл трогать не нужно.
- **Сопоставление идёт только по TCP source**, никогда по advertised/listing
  адресу. Advertised адрес сообщает сам peer, поэтому бан по нему обходился бы
  сменой объявленного адреса, а также бил бы по невиновному третьему лицу.
- Некорректное правило (опечатка, `prefix > 32`, неканонический CIDR вроде
  `10.0.0.5/24`, битая дата, дубликат) — **ошибка запуска**. Частично
  применённый список хуже прежнего: он мог бы забанить не тех. Неканонический
  CIDR отклоняется, а не расширяется молча.
- Лимит: `DM_MAX_BANS` (64) правила; сверх лимита — ошибка, а не рост памяти.
  Просроченные правила вычищаются раз в минуту, чтобы таблица не заполнялась
  мёртвыми записями и не упиралась в лимит.
- Отказ логируется как `reason=banned-source` с адресом и портом источника.
- Файла нет — ничего не забанено (нормальное состояние, не ошибка).

### Перечитывание без перезапуска

Мастер проверяет файл раз в секунду (mtime + размер) и перечитывает его при
изменении. Работает в обе стороны: добавленное правило начинает банить, удалённое
— перестаёт.

Перечитывание идёт **в черновик**: при ошибке разбора прежний список остаётся в
силе, а в лог пишется `ban list reload failed; keeping the previous list`. Это
важно именно для «горячего» сценария: недописанный или испорченный файл не должен
ослаблять или расширять действующие баны.

### Allow list (приватный сервер)

Для приватного каталога есть обратный режим: пускать только перечисленных.

```text
allowlist=allow.txt
access-mode=allowlist
```

- Режим задаётся **явно** (`access-mode=open|allowlist`), а не выводится из
  наличия файла. Иначе забытый файл молча превратил бы открытый сервер в
  закрытый.
- Файл `allow.txt` — тот же формат, что `banlist` (IPv4 или CIDR, `#`
  комментарии), и то же сопоставление по TCP source.
- **Бан сильнее разрешения**: адрес, который и в allow, и в ban, получает отказ.
  Отказ должен всегда побеждать разрешение.
- Если `access-mode=allowlist` включён, но список пуст или не задан, мастер
  **отказывается стартовать**. Иначе он бы молча заблокировал всех, а оператор
  узнал бы об этом только по неудачным подключениям.
- Если файл есть, а режим `open`, мастер пишет, что список **не применяется** —
  чтобы не создавалось ложное впечатление ограничения доступа.
- Оба списка перечитываются на лету и так же атомарно: испорченный файл не
  ослабляет и не расширяет действующие правила. Для allow-списка это особенно
  важно: обрезанный файл иначе заблокировал бы всех, кого в нём больше нет.

**Границы, которые надо понимать:** бан по IP не защищает от смены адреса,
NAT-шеринга и relay, и не является ни аутентификацией, ни античитом. Allow-список
— грубый фильтр по адресу, не учётные записи.

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

### Диагностика подключения v2

Клиентский `game.log` теперь содержит endpoint master, UTC-время, operation/
attempt, локальный адрес/порт сокета, этап connect/send/receive-header/body,
системный Winsock/errno code, server status и прогресс sent/received/elapsed.
Системная ошибка сохраняется **до закрытия socket**, чтобы cleanup её не затёр.
`reason=deadline` на connect отличается от deadline ожидания ответа после
TCP-connect; `reply-magic-not-DPTM` явно указывает на чужой service/proxy response.
Никаких дампов ответа/lease secrets.

Master пишет process-local `cid`, peer source TCP port, byte progress, response
status и конкретные причины: неверное metadata field/header/version/length,
duplicate endpoint, per-IP/global capacity, source-IP change, wrong token,
unknown/expired lease, no-request TCP probe, truncated request, receive/send
deadline и OS socket error. `response-delivered-to-TCP` — передача в TCP stack,
не доказательство получения приложением peer. Подавление повторов сохранено.

В launcher данные DNS (домен → выбранный IPv4, native resolver error) сохраняются
в уникальный `launcher-network-<pid>-<ticks>.log` в каталоге данных; лимит около
1 MiB + один `.1` backup на запуск. При публикации DNS context пишется **перед
запуском дочерней игры** в начало session `game.log`, чтобы основного клиентского
лога хватало и без отдельного launcher network log. На ошибке browser подробность
и путь к логу видны в статусе.

Для разбора прислать полный клиентский `game.log` и master event log за ту же
попытку. Если игра не запустилась/DNS завис — ещё launcher-network log. В логах
есть IP/пути: отправлять адресно после просмотра. Отсутствие remote events и
connect timeout не дают права автоматически обвинить Firewall/NAT/VPN: это
граница наблюдений; логи показывают точную последнюю достигнутую стадию.

`accept timeout` отдельно объясняет, что второй игрок не подключился вовремя,
а directory без `--relay` не передаёт игровой трафик. Для ручного ожидания CLI `-net-timeout 120000`;
таймауты и правила Firewall автоматически не изменяются.

По просьбе владельца тестовый внешний адрес — `play.dartpower.xyz:25135`.
Для ручного проброса: `dptdoom-master.exe --bind 0.0.0.0 --port 25135`.
Пробрасывать **TCP**, не UDP. Это не игровой порт и не подтверждение внешней
доступности/DNS; её нужно отдельно проверить с другой машины/сети.
Launcher хранит `[Master] Endpoint=play.dartpower.xyz:25135`, `Publish=0|1`,
`SessionName` в своём `launcher.ini`. Endpoint редактируется в разделе «Серверы»:
numeric IPv4 **или DNS-домен** и порт; URL/пути/IPv6/Unicode host не принимаются.
Без явного refresh или публикации подключения к master нет.

## Hairpin NAT / masquerade: один публичный адрес дома и снаружи

При hairpin SNAT master может видеть source IP роутера вместо адреса хоста.
Добавлена **явная операторская policy** в необязательном текстовом конфиге:

```text
# Документационный пример: заменить обе стороны своими значениями!
address-map=192.168.50.0/24=203.0.113.10
```

```powershell
dptdoom-master.exe --bind 0.0.0.0 --port 25135 --config C:/local/master.conf
```

### Три режима публичного адреса

Для одного WAN у master и домашних игровых хостов можно обойтись без ручного
обновления IPv4. Все режимы включаются **только оператором**, по умолчанию `off`:

```text
public-address-mode=dns
public-host=games.example.org
hairpin-subnet=192.168.50.0/24
public-refresh-seconds=300
```

- `manual`: вместо `public-host` указать `public-ip=203.0.113.10` (пример, не
  реальный сервер). Никаких DNS/API запросов; адрес действует до перезапуска.
- `dns`: `public-host=...`, первый допустимый публичный IPv4 системного resolver.
  Split-DNS с private answer не принимается. HTTPS API не вызывается.
- `api`: `public-api=https://api.ipify.org/` или свой аналогичный plain-text
  endpoint. HTTPS/443, сертификаты проверяются штатным TLS, redirect OFF,
  максимум 64 bytes ответа, строго public IPv4; JSON/HTML не принимается.
  URL с credentials/query/fragment/custom port запрещён. Нет встроенного
  обязательного сервиса: API URL задаётся явно, запрос раскрывает провайдеру
  внешний egress IP master (или его proxy/VPN), это не IP произвольного клиента.

Для каждого режима нужна одна или несколько `hairpin-subnet` (суммарно со
static address-map максимум 8 непересекающихся диапазонов). Одновременно
public-host/public-ip/public-api не смешиваются; неверная конфигурация отклоняется.
`public-refresh-seconds` 30..3600, default 300. Сетевые запросы DNS/API идут в
одном worker вне accept loop. Через 5s попытка считается просроченной, поздний
результат отбрасывается. OS DNS/TLS может завершаться дольше: новый worker не
создаётся, пока старый не завершится; регистрация остальных sources продолжает
работать. Последний хороший IP живёт до трёх refresh intervals после успеха.
При его истечении mapped leases удаляются, новые mapped registrations получают
CAPACITY с логом discovery-not-ready; private/router IP не публикуется молча.
При смене IP старые mapped leases тоже удаляются — обычный heartbeat получает
UNAUTHORIZED, затем хост перепубликуется. Нет auto probes к игровому порту.

Windows использует native resolver/WinHTTP, без дополнительных DLL. Unix —
native resolver/pthreads, API доступен только если CMake нашёл libcurl; иначе
`api` даёт понятный startup error, а manual/dns продолжают поддерживаться.
Unix runtime этого нового пути пока не проверен. Системный HTTPS proxy/VPN
может изменить увиденный API egress IP; предпочитать DNS при известном домене.

Пустой/отсутствующий config не включает mapping. Файл читается только по
явному `--config`, неизвестные/неверные строки — отказ startup. Пример
`docs/master-example.conf` содержит **только комментарии**, без активных правил.
Пользовательских подсетей/IP в исходниках/defaults нет. Реальный operator config
хранить вне tracked source (например, в игнорируемом `build/`).

Для совпавшего source CIDR в LIST публикуется настроенный адрес, а game port
сохраняется. Для остальных sources — их реальный адрес, как раньше. Поэтому
хост из LAN может обращаться к master через свой внешний домен и получать ту
же публичную игровую карточку, что его друзья. **Клиент/launcher и directory
protocol v1 не меняются.** Домен master не подразумевает, что любой игровой
хост находится на том же IP: никакого автоматического присвоения master IP нет.

Адрес источника lease хранится отдельно от advertised address: heartbeat/remove
всё ещё требуют реальный source + secret. Mapping задаёт только оператор master,
клиент не может передать свой произвольный advertised IP. Дубликаты публичного
IP:game-port отвергаются, в том числе при конфликте mapped и обычного хостов.
Лимиты регистраций остаются по actual source, а не переписанному адресу.

Ограничения правил: максимум 8, numeric IPv4, canonical CIDR без overlap,
source только внутри RFC1918/loopback. Например `/32` позволяет ограничиться
одним SNAT-роутером вместо всей подсети. Доверять всем источникам из выбранного
диапазона — **решение администратора**, а не доказательство владения публичным
адресом. Частный source сам по себе ничего автоматически не разрешает.
Destination отбрасывает очевидные local/multicast addresses, но не проверяется
на реальную глобальную маршрутизируемость/владение: TEST-NET используется в tests.

Это **не настройка роутера, не relay и не NAT traversal**. Нужны рабочий hairpin
и проброс game TCP port на нужный игровой ПК. Если внешний порт отличается от
внутреннего, port mapping пока не поддерживается; использовать одинаковые
порты до отдельного расширения. В manual/static address-map при смене WAN IP
обновить config и перезапустить; DNS/API обновляются по интервалу. Изменения Firewall/
OpenWrt автоматически не выполняются. Все записи остаются UNVERIFIED.

Startup печатает активные правила, регистрация — actual peer и `advertised=...`
с `mapped=yes/no`, без имён/lease secrets. `master-address-map-model` и
`master-address-map` покрывают модель и настоящий EXE, default/no-config,
границы/overlap/config errors, публичный список и source-bound renew/remove.
`master-public-address-model` проверяет grammar/validation, режимы, истечение
last-good и смену адреса с удалением старых leases без обращения в Интернет.
Wire test дополнительно проверяет manual mode и fail-closed DNS localhost
(OS resolver only), пока основной directory остаётся доступным.

## Публикация и browser

В launcher: выбрать host, в «Серверы» включить «Публиковать мой ожидающий
COOP-хост», задать endpoint/name, сохранить и запустить. Публикация выключена по
умолчанию. LAN bind по-прежнему отдельное явное подтверждение; публикация
localhost-only хоста не делает его доступным другой машине.

CLI для numeric IPv4 master (домен асинхронно разрешает launcher):

```powershell
dptdoom.exe -iwad DOOM2.WAD -warp 1 -net-host 2342 -net-bind 0.0.0.0 `
  -master 127.0.0.1:25135 -session-name "DPT.DOOM COOP" -net-timeout 120000
```

Имя — printable ASCII 1..63. Хост публикуется **после listen**, пока ждёт peer;
metadata берётся из настоящих settings и тех же build/resource digest, что
handshake. Nonblocking query обновляется в accept loop. Heartbeat берёт interval
из lease response, а не предполагает TTL60. Ошибка master пишет предупреждение,
не прекращает ожидание direct peer. Retry не чаще 10 секунд. Публикация только
для `-net-host`; сочетания с SP/join отвергаются.

При accept/штатном shutdown снимается lease; cleanup имеет бюджет до 250ms на
pending register и до 250ms на remove. Если сеть недоступна/процесс убит, запись
исчезнет по TTL. Игра/протокол v2 не превращаются в master-dependent сессию.

В browser «Обновить» запрашивает bounded список в native worker. DNS не блокирует
UI; через 5s запрос считается просроченным, поздний результат отбрасывается.
Системный getaddrinfo может завершаться дольше: одновременно только один worker,
он не убивается через TerminateThread; direct/SP остаются доступны. Берётся
первый IPv4 DNS result, multi-address failover пока нет. Publication использует
разрешённый IPv4 до следующей смены endpoint/перезапуска launcher.

Выбор записи переносит IP/port/map/skill/rules в форму, **не запускает EXE** и не
скачивает ресурсы. Сохраняются выбранные EXE/IWAD/PWAD; содержимое и порядок
окончательно сверяет handshake. Карточки не утверждают совместимость сборки,
ping или port reachability. Истёкшую по snapshot TTL запись нельзя выбрать:
нужно refresh. Все строки выводятся textContent/JSON, не HTML.

Windows runtime проверен MSVC и MinGW, модель/loopback также MSVC ASan.
Unix socket/clock/entropy path предусмотрен, **Linux/macOS не запускались**.
CI build/test подключаются через CMake; наличие YAML не доказывает remote run.

## Поведение и пределы

- По умолчанию TCP source IPv4 — адрес записи; клиент передаёт только game port,
  **не host**. Явная операторская address-map policy может менять listing IP,
  не источник аутентификации (см. выше).
  Master не верит произвольным forwarded IP/домашним DNS/URL из metadata.
- При регистрации OS CSPRNG выдаёт случайные id (16 bytes) и secret (24 bytes).
  В Windows — BCryptGenRandom, в Unix — `/dev/urandom`; не rand()/timestamp.
  Если entropy недоступна, регистрация не принимается.
- Heartbeat/delete требуют id, secret и тот же source IP; stale/wrong credentials
  не оживляют запись. Secret не включается в list и не печатается в лог.
- Проверка TCP/source **не доказывает доступность game port** и не является
  аутентификацией человека/build. Поля name/build/resources/map — claims клиента.
  Все записи подразумевают `reachability=unverified`, `coop/waiting/1 of 2`.
  Host удаляет объявление, когда место занято; при аварийном завершении — TTL.
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
| 6 | 2 | operation: LIST=1, REGISTER=2, HEARTBEAT=3, REMOVE=4, RELAY_HOST=5, RELAY_JOIN=6 |
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
| 156 | 4 | flags: bit0=relay available, остальные 0; см. RELAY.md |

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
IP/домен master настраиваются, direct-connect независим. Relay теперь opt-in
(RELAY.md); NAT hole punching и игровой dedicated server — отдельные задачи.

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
`master-client` / `master-client-wire` — grammar/response bounds, hostile headers,
fragmentation, timeout/truncation против настоящего C query client.
`master-publish` (headless + IWAD) — настоящие engine/master/guest: metadata,
heartbeat > TTL, remove on join/timeout и direct при master outage.
`launcher-windows` — DNS endpoint, publish/list/select/escaping/remove/outage
через настоящий HTML bridge с игровым COOP.

На платформах без настроенных loopback aliases тест не создаёт их с root:
multi-source wire cases пропускаются с явным сообщением (модель проверяется),
остальные lifecycle/framing/rate сценарии выполняются. Capacity wire test
использует TTL 60s и pacing ниже global refill, не смешивая capacity и rate limit.

```powershell
ctest --preset headless -R '^master-'
ctest --preset windows-asan -R '^master-'
```
