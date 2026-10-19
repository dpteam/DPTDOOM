# DPT.DOOM direct + relay (experimental)

По запросу владельца добавлен альтернативный транспорт для COOP на двоих,
который **не требует входящего соединения к игровому ПК**: host и guest
подключаются исходящими TCP-соединениями к master, оператор которого включил
`--relay`. Используется тот же listening port master (например, 25135), без
новых firewall/router rules, UPnP или изменения OpenWrt из приложения.

Это **не исправление NAT, не dedicated simulation server и не произвольный TCP
proxy**. Relay связывает только конкретную зарегистрированную игровую пару.
Игра остаётся TCP lockstep, exactly 2; handshake/game rules/save behavior прежние.

## Использование

Master (relay выключен по умолчанию):

```powershell
dptdoom-master.exe --bind 0.0.0.0 --port 25135 --relay --log master.log
```

Можно сочетать с `--config` для ранее добавленных DNS/manual/API address policies.
Relay не зависит от правильности public game IP в объявлении: это direct
кандидат, а relay endpoint — адрес самого master, к которому уже подключается
launcher. Hairpin source IP роутера остаётся в diagnostic log и не подменяется.

В launcher публикация host добавляет `-net-relay`. Карточка становится
**DIRECT + RELAY** только после того, как master принял outgoing host channel.
Guest выбирает карточку, сохраняет выбранную session identity в памяти launcher
и запускает игру. Сперва пробует direct; при отказе/отсутствии TCP completion
за 2 секунды переключается на relay. Не требует вводить LAN IP. Ручное изменение
адреса/порта/режима или загрузка профиля сбрасывает выбранный relay context.
Session identity не сохраняется в profile INI и не пишется в лог.

CLI:

```text
host:  -net-host PORT -master MASTER_IPV4:PORT -net-relay
guest: -net-join DIRECT_IPV4:PORT -relay-master MASTER_IPV4:PORT -relay-session PUBLIC_ID_HEX32
```

Лаунчер разрешает DNS master в native worker, engine CLI пока numeric IPv4.
`-relay-session` — публичный session ID из свежего LIST, не secret host token.
Запуск plain direct без этих опций не зависит от master/relay.
После успешного TCP direct перехода на relay по ошибке game handshake нет:
нельзя тихо обходить mismatch/desync. При обрыве уже начатой игры reconnect/
переключение транспорта не реализованы; обычный EOF сохраняет survivor world.

Ожидание в launcher увеличено с 30 до **120 секунд** (это всё ещё bounded wait,
не постоянно слушающий игровой сервер). Прямой и relay пути логируют выбранный
транспорт. Во время startup окно показывает штатным HUD-шрифтом из IWAD:
WAITING FOR PLAYER / CONNECTING / TRYING RELAY, вместо пустого framebuffer.
Текст English ASCII: кириллицы в штатном HUD-font нет. Рендер startup не вызывает
HU_Init/I_InitGraphics дважды и не меняет simulation/RNG.

## Протокол и ограничения

Directory v1 расширен operations 5=RELAY_HOST, 6=RELAY_JOIN, не game v2.
LIST row offset156: uint32 flags, bit0=relay available. Прежние reserved bits
отвергаются. Старые browser builds могут отклонять такой LIST — для этой фичи
обновить оба клиента одной сборкой. Без `--relay` новый master отвергает attach.

- Host присылает существующие id16+secret24 (40 bytes) и должен совпасть с
  source IP lease. Master проверяет их до передачи владения socket relay.
- Guest присылает публичный id16; требуется уже подключённый waiting relay host.
  Повторный host/guest не может заменить участника. При pairing listing/lease
  потребляются атомарно, вторая гостевая попытка отклоняется.
- Обеим сторонам возвращается обычный 16-byte DPTM success header с нулевым телом,
  после него идут game handshake/tic bytes. До пары game data не принимаются.
  Master не открывает исходящие соединения по указанию клиента.
- Максимум **8 пар**, максимум **4 host-сессии на actual source IP**; waiting
  leases также ограничены directory limits. 4096-byte буфер на направление,
  read backpressure, до 64 KiB/s входящего traffic на сторону/секунду.
- Waiting channel зависит от lease и закрывается после remove/expiry; active
  relay больше не зависит от TTL. Idle timeout 120s, hard lifetime 6h.
- Clean EOF дочитывает накопленный буфер и передаёт half-close другой стороне;
  ошибки транспорта закрывают пару. Slow-reader не создаёт неограниченной очереди.
  Buffer/rate/session limits не означают Internet DoS-hardening.
- Логи: attach/pair/close, counters/duration/reason; secrets/id/payload не пишутся.
  Повторяющиеся relay events подчиняются ограничителю логов.

**Без TLS/encryption, peer account authentication, паролей и moderation.** Master
оператор может видеть трафик; публичный session ID позволяет первому гостю занять
место и даже сорвать handshake. Это как публичный direct accept, не приватное
приглашение. Несовместимый guest отклоняется игровым handshake, но слот может
быть потерян до перезапуска host. Не называть relay публично защищённым сервисом.
Использовать с доверенными тестировщиками и осознанным согласием оператора на
bandwidth/CPU; работа server процесса/сети нужна на всю relayed session.

## Проверки

- `master-relay`: настоящий native EXE, opt-in, wrong host auth, guest без host,
  duplicate attach, listing flag, single use, byte-exact transfer > buffer,
  half-close с очередью до EOF, очистка/disconnect/lease remove/capacity.
- `master-publish`: настоящие engine/master/guest. Direct-first с relay context,
  недоступный direct endpoint → relay → одинаковые потиковые traces, survivor
  после выхода guest, master outage direct fallback. Startup PPM не весь чёрный.
- `launcher-windows`: карта намеренно указывает TEST-NET direct IP; настоящий
  WebView2 выбирает её и запускает COOP через relay. Без внешних сетей в CI.

Проверено на Windows/MSVC/MinGW/ASan. Новому relay Unix пути нужен следующий CI
run; реальный двухмашинный hairpin/internet gameplay ещё проверить владельцу.
