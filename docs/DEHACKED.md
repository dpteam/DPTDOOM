# DeHackEd: ограниченный Misc / Ammo / Text / Weapon-frames / Thing / Frame graph срез

Это **не полная поддержка DeHackEd/BEX**, Boom, MBF или MBF21. Реализован
отдельный `DPT-deh-savemanifest-v14` parser на C, написанный для DPT.DOOM. Из ACE/csDoom
ничего не скопировано; их parser/gameplay код остаётся материалом для дальнейшего
аудита. Полный upstream `FEATURE_DEHACKED` по-прежнему OFF.

## Что работает

Внешние plain-text patches через `-deh FILE [FILE ...]`, несколько `-deh` групп
в порядке командной строки. Встроенные lumps с именем `DEHACKED` — только при
явном **`-dehlump`**. Без этого флага lump не применяется (хотя WAD bytes всё равно
участвуют в существующем resource digest). Automatic Freedoom/HACX patch loading
не объявляется готовым: их patches могут содержать неподдерживаемые секции.

Пока принимается classic signature v3.0, `Doom version = 19`, `Patch format = 6`
и секции **`Misc 0`**, `Ammo 0..3`, `Weapon 0..8`, `Thing 1..NUMMOBJTYPES`,
`Frame 1..NUMSTATES-1` (с ограничениями ниже), не выдуманная `[Misc]`
и не BEX `[STRINGS]`.

```text
Patch File for DeHackEd v3.0
# Example created for testing; not an extracted game resource.
Doom version = 19
Patch format = 6

Misc 0
Initial Health = 125
Initial Bullets = 30
BFG Cells/Shot = 20
```

```powershell
dptdoom.exe -iwad C:/Games/DOOM2.WAD -deh C:/Mods/example.deh -warp 1
dptdoom.exe -iwad C:/Games/DOOM2.WAD -file C:/Mods/example.wad -dehlump -warp 1
```

Настройки игрока/монстров остаются vanilla без patch options. Нет скачивания
patches или их поиска на диске; launcher пока не имеет `.deh` chooser и не
включает `-dehlump` автоматически. Для этого среза использовать engine CLI.

Порядок **явно ограниченного DPT среза**, не обещание upstream load order:
1. Все внешние `-deh` файлы до PWAD loading.
2. При `-dehlump` — все `DEHACKED` lumps из IWAD/PWAD в порядке WAD/lump directory.
Позднее присваивание того же поля побеждает. Соответственно explicit lump может
переопределить внешний patch. Имена файлов и абсолютные пути не влияют на digest.

## Поля и защитные пределы

| Поле | Диапазон |
|---|---:|
| Initial Health | 1..10000 |
| Initial Bullets | 0..200; не больше итогового Max ammo bullets |
| Max Health, Max Soulsphere, Megasphere Health, God Mode Health | 1..10000 |
| Max Armor, Soulsphere Health, IDFA Armor, IDKFA Armor | 0..10000 |
| Green Armor Class, Blue Armor Class, IDFA Armor Class, IDKFA Armor Class | 1..2 |
| BFG Cells/Shot | 1..1000 |
| Monsters Infight | 0..1; 1 разрешает урон между монстрами одного вида |

`Ammo N`: индексы 0=bullets, 1=shells, 2=cells, 3=rockets. `Max ammo` —
1..10000 (базовая ёмкость; backpack штатно удваивает), `Per ammo` — 0..1000
(количество в обычной порции; коэффициенты pickup/skill остаются штатными).
Понижение bullet capacity ниже Initial Bullets отклоняет весь источник.

`Text` принимает classic `Text old-length new-length` только для заранее
зарегистрированных vanilla messages. Разрешены pickup/save/chat strings из
текущего whitelist; старые bytes должны совпасть полностью. Новый payload —
до 2047 bytes, printable ASCII с `\\n`/`\\r`/`\\t`; `%`, NUL, другие control и
non-ASCII bytes запрещены. `[STRINGS]`, неизвестные old strings и format strings
не поддерживаются.

```text
Text 19 18
Picked up the armor.DPT armor message!
```

Payload — ровно `old-length` bytes, затем `new-length` bytes; line endings внутри
payload не нормализуются. Замена применяется через DPT `DEH_String`; rollback и
reset возвращают исходные строки.

`Weapon N`: индексы 0=fist, 1=pistol, 2=shotgun, 3=chaingun, 4=rocket launcher,
5=plasma, 6=BFG, 7=chainsaw, 8=super shotgun. Поддержаны `Ammo type = 0..3`
и пять ссылок на существующие weapon states:
Тип 5/noammo и исторические overflow в maxammo **не поддерживаются**: некоторые
штатные shooting actions обращаются по этому типу к массивам. Исходные vanilla
fist/chainsaw остаются am_noammo; patch не может назначить этот опасный тип другому
оружию. `Deselect frame`, `Select frame`, `Bobbing frame`, `Shooting frame`,
`Firing frame` поддерживаются для существующих vanilla weapon states. Индексы
нулевые; `S_NULL` разрешён только для `Firing frame`. Mobj states (например
`S_BLOOD1`), выход за таблицу и infinite states отклоняются. Изменённые в patch
переходы не могут пересечь weapon/non-weapon state domain.

```text
Ammo 0
Max ammo = 400
Per ammo = 15

Weapon 1
Ammo type = 1
Deselect frame = 11
Select frame = 12
Bobbing frame = 10
Shooting frame = 15
Firing frame = 17
```

Это продолжение того же classic файла с header, не самостоятельный headerless
patch. Пистолет в примере использует shells; если их нет, он не сжигает bullets.
Parser делает staged copies всех поддерживаемых таблиц: неверное поле в конце
источника откатывает также Misc/Ammo/Weapon assignments этого источника,
включая frame references.

### Thing — только числовые свойства существующих типов

Нумерация **с единицы**, в отличие от Ammo/Weapon. `Thing 1` — player,
`Thing 2` — trooper, `Thing 31` — barrel. Это индекс исходной таблицы типов,
**не DoomEd/map thing number** (у barrel map number 2035). Допускается classic
описание в скобках, например `Thing 2 (Trooper)`: оно не используется как
actor/class name, не исполняется и не попадает в digest отдельно от raw source.

| Thing field | Поддерживаемый диапазон / смысл |
|---|---|
| Hit points | 1..10000, здоровье mobj при spawn |
| Reaction time | 0..10000, исходные тики реакции |
| Pain chance | 0..255 |
| Width | 1..8388608, **радиус** в 16.16 fixed-point, максимум 128 map units |
| Height | 1..16777216, 16.16 fixed-point, максимум 256 map units |
| Speed | 1..128 для типов с исходной whole-unit speed; 1..4194304 для исходной fixed-point speed |
| Mass | 1..1000000; ноль запрещён, thrust делит на массу |
| Missile damage | 0..100, info damage для missile/skull столкновений, **не** глобальная замена hitscan/action damage |
| ID # | -1..32767, DoomEd number; duplicate non-negative IDs запрещены |
| Bits | bounded vanilla mobj flags; неизвестные/reserved bits запрещены |
| Initial/First moving/Injury frame | существующий non-weapon state или `S_NULL` |
| Close/Far attack frame | существующий non-weapon state или `S_NULL` |
| Death/Exploding/Respawn frame | существующий non-weapon state или `S_NULL` |
| Alert/Attack/Pain/Death/Action sound | существующий vanilla `sfx` ID `0..NUMSFX-1` |

Категория Speed определяется исходной таблицей (исходная speed ≥65536 —
fixed-point). Категория остаётся исходной даже при правке Bits/state references;
совместимость любых сочетаний поведения с новой скоростью не гарантируется.
Ноль отклоняется: некоторые missile/charge paths делят на speed. Это намеренно
узкий subset, не обещание корректности любых сочетаний свойств любого мода.
Nightmare/`-fast` штатно переопределяет скорости некоторых снарядов; в этом
срезе соответствующие compatibility semantics не изменены.

```text
Thing 31 (Barrel)
Hit points = 123
Width = 1572864
Height = 3932160
Mass = 250
```

После такого patch barrel при spawn получает health123/radius24/height60.
`ID #` меняет DoomEd number, а `Bits` — только разрешённую vanilla flag mask;
это влияет на последующие map spawns и collision/pickup/kill semantics. Duplicate
non-negative IDs отклоняются, чтобы DoomEd lookup не стал неоднозначным.
State fields меняют только существующие non-weapon state references; `S_NULL`
разрешён как конечное состояние, weapon-state pool и выход за таблицу запрещены.
Sound fields меняют только ID существующей vanilla sound table; новые lumps,
произвольные resources и action pointers через Thing не разрешены.
`Thing 1 Hit points` меняет mobjinfo player, но стартовое `player.health` по-прежнему
задаёт `Misc 0 Initial Health`, как в существующем engine path. Изменения `Bits`,
map ID, sound и state references staged атомарно; action pointers не меняются.
Типы не добавляются. Таблица Thing также
staged: неверное поле в конце patch не меняет ни Things, ни Misc/Ammo/Weapon,
ни digest/count. Reset восстанавливает все исходные таблицы.

### Frame — Duration, Next frame и визуальные поля

Поддерживается `Duration = 1..32767` для существующих состояний; Frame индексы
**с нуля** в оригинальной таблице, но `Frame 0` (`S_NULL`) неизменяем и отклоняется.
В отличие от Thing, подписи в скобках у Frame пока не принимаются. `Next frame`
меняет только существующий `state.nextstate`. `Sprite number` и `Sprite subnumber`
меняют визуальные свойства этого состояния; action pointers и misc не меняются.

```text
Frame 13
Duration = 60
Next frame = 15
```

`13` — исходный `S_PISTOL1`: задержка до следующего состояния становится 60
тиков. `Next frame = 15` в том же блоке переводит `S_PISTOL1` на существующий
`S_PISTOL3`, пропуская `S_PISTOL2` и его action. Индексы — нулевые, диапазон
`0..NUMSTATES-1`; `0` (`S_NULL`) разрешён как конечный переход.

Меняются только `state.tics`, `state.nextstate`, `state.sprite`, `state.frame`
и (для weapon states) `state.misc1/misc2`.
Источник staged целиком вместе с другими секциями; при ошибке в конце файла
не применяются даже предыдущие корректные Duration assignments. Digest subset
обновлён до v13, чтобы peers не путали разные semantics.

Намеренно не поддержаны `Duration=0` (instant transition chains), `-1` и другие
отрицательные значения. Диапазон `S_SARG_RUN1..S_SARG_PAIN2`, который `-fast`/
Nightmare штатно сдвигает вправо/влево, пока **не patchable**. Это не полная
историческая Frame совместимость. Исходные нулевые/бесконечные vanilla states
остаются прежними, пока пользователь не меняет их разрешённой положительной
длительностью. После всех staged assignments parser проверяет граф нулевых
состояний: цикл, состоящий только из zero-tic states, отклоняется, чтобы
`P_SetMobjState`/`P_SetPsprite` не зависали. Циклы с положительной или
бесконечной (`-1`) длительностью допустимы. Source reset возвращает исходные
длительности, переходы и визуальные поля, не копируя function pointers.

#### Sprite number / Sprite subnumber

- `Sprite number = 0..NUMSPRITES-1`: индекс существующей `sprnames` таблицы,
  не номер WAD lump и не новая sprite definition.
- `Sprite subnumber`: frame index **0..28** (A..], предел текущего renderer),
  опционально OR с **32768** (`FF_FULLBRIGHT`). Например, 32769 — B fullbright.
  Остальные биты, отрицательные числа и переполнение отклоняются parser-ом.
- Применение числовых полей атомарно вместе с другими секциями источника;
  промежуточная пара sprite/frame не проверяется по ресурсам до конца загрузки.
- После IWAD/PWAD и `R_InitSpriteDefs` проверяются **все визуально изменённые
  состояния**, включая неиспользованные в текущем уровне: frame должен реально
  существовать в загруженной `sprites[]` таблице. Отсутствующий frame/sprite —
  startup error до симуляции, без silent fallback. Проверку полноты rotations
  выполняет штатный sprite loader. Числовой диапазон сам по себе недостаточен.
- Ошибка ресурса завершает startup; это не rollback уже принятых источников.
  Порядок file/lump assignments сохраняется, допустимо исправить промежуточную
  неверную пару поздним patch. Unit parser без IWAD вызывает отдельный validator
  с synthetic frame counts; настоящий engine вызывает его всегда при startup.

```text
Frame 10
Sprite number = 1
Sprite subnumber = 32769
```

Пример меняет готовый pistol state на существующий shotgun frame B fullbright;
не меняет action, длительность или урон оружия. Новые sprite names/ресурсы,
самостоятельная секция `Sprite` и произвольные executable offsets не добавлены.

#### Unknown 1 / Unknown 2 (psprite offsets)

- Classic `Unknown 1`/`Unknown 2` мапятся на `state.misc1`/`state.misc2` и
  допустимы **только для weapon states**: в этом движке их читает лишь
  `P_SetPsprite` для координат оружия на экране. Mobj-state assignment отклоняется,
  чтобы не принимать значение, которое движок никогда не прочитает.
- Диапазон `-128..128` целых map units; переполнение и значения вне диапазона
  отклоняются. Значение переводится в fixed-point умножением, а не сдвигом
  отрицательного числа (сдвиг отрицательного `int` — undefined behavior в C).
- `misc1 == 0` означает «координаты не задавать», поэтому `Unknown 2` без
  ненулевого `Unknown 1` не даёт визуального эффекта — это vanilla-семантика.
- Ready-состояния (`A_WeaponReady`) каждый тик пересчитывают bob и перезаписывают
  `sx/sy`, поэтому offset на них не виден; для постоянного смещения используют
  firing/raising состояния без bob-перезаписи. Это тоже vanilla-поведение, не
  ошибка parser-а.
- Значения staged и восстанавливаются `DPT_DehReset`; digest subset — v12.

```text
Frame 12
Unknown 1 = 64
Unknown 2 = 64
```

`code pointers`, произвольные weapon state references и новые state индексы
по-прежнему отклоняются. Для них нужны отдельные проверки
graph/resources/action compatibility.

Диапазоны — поддерживаемый безопасный subset, **не весь исторический DeHackEd**.
Sound/Sprite/Pointer/Cheat, `[STRINGS]`, неподдерживаемые Frame/Thing fields,
BEX, include directives и executable offsets пока **отклоняются**, а не молча
игнорируются. Значения — только signed decimal, без hex/суффиксов/inline comment.
Имена поддерживаемых полей/секций нечувствительны к ASCII-регистру. Строки `#`/`;`
после ведущих пробелов считаются комментариями. CRLF/LF поддерживаются.

Ограничения: максимум 16 источников, суммарно 256 KiB, строка до 2047 bytes,
embedded NUL запрещён. Переполнение strtol/неверное число/пустой patch/unknown
section/field дают ошибку с номером строки до игрового старта. Parser работает
в памяти, без temp/scratch-файлов. Каждый источник применяется атомарно:
при ошибке его присваивания и hash/count не меняются. Ранее успешно применённые
источники остаются в таблице, но engine всё равно завершает startup при ошибке.

## Сеть, saves и demos

- Если patches применены, raw bytes/размер/порядок и метка subset версии
  включаются в ResourceDigest, одинаково для публикации master и direct/relay
  handshake. Digest не меняется при повторном чтении, не содержит ASLR/pointers
  или путей. Даже разное оформление одинаковых значений считается разным patch.
- Разные внешние patches при одинаковом IWAD отвергаются до симуляции как
  resource content/order mismatch. Совпадающие bytes из разных путей проходят.
  Wire game protocol остаётся v2; source fingerprint требует одной сборки peers.
- Vanilla DSG не содержит patch manifest. Патченные сохранения используют
  **отдельный 16-байтовый version marker `DPTDEH1`** вместо `version N`:
  далее идут 20-байтовый SHA-1 digest набора патчей (тот же, что в сетевом
  ResourceDigest) и 4-байтовое число применённых источников.
  - Тот же набор патчей (`-deh`/`-dehlump` в том же порядке) — сохранение
    загружается нормально, включая поля Thing/Frame/Text.
  - Другой набор или запуск без патчей — fail-fast до симуляции:
    `Savegame was created with different DeHackEd patches`.
  - Vanilla-сохранение при активных патчах отвергается
    (`Vanilla savegame cannot be loaded while DeHackEd patches are active`),
    чтобы не применить изменённые правила к несовместимому snapshot.
  - Vanilla-сохранения и vanilla-движок не видят новый marker как валидную
    версию, поэтому старые DSG остаются байт-совместимыми и не портятся.
  - Manifest не заменяет данные сохранения: RNG/target/tracer по-прежнему
    хранятся по ванильной схеме DSG.
- Demo recording при активных патчах **по-прежнему запрещена**: vanilla `.lmp`
  не имеет места под manifest, а обещание совместимости с patched demos без
  отдельного формата было бы ложным. Demo playback с явно выбранным patch
  остаётся возможным, но не заявляется совместимость/синхронизация с
  произвольными patched demos. Непатченный attract-demo save/takeover и state
  baselines остаются без изменений.

## Проверки

`deh-misc`: synthetic grammar/ranges/overflow/NUL/line/size/count limits, Text
old/new exact bytes and unsafe payload rejection, atomic rollback, digest
repeatability и load order, без IWAD/окна/Internet.

`deh-engine` (headless + локальный Doom II): реальные player health/ammo/capacity,
per-ammo/type в patch report, attack commands обычного/переназначенного pistol, внешний
patch, повторные patches, два DEHACKED lumps и explicit opt-in/order, bad source,
patched-save manifest (same/different/no patches, vanilla save rejection) и
одинаковые/разные patches в настоящих two-peer
процессах с matching traces (Misc, Ammo/Weapon, Thing). Для Thing создаётся
временный MAP01 overlay с двумя starts и одной barrel: сравниваются поля
**реально созданного mobj**, не только parser table, через file/lump paths.
Unit tests не линкуют action code: используют synthetic mobjinfo; настоящий
info.c и игровой spawn проверяет deh-engine. Все patch/WAD fixtures создаются во временных
каталогах; IWAD/извлечённые ресурсы не коммитятся.

Duration/Frame/Weapon-frame tests проверяют реальный firing cadence (по расходу ammo с
одинаковыми attack commands), file/lump equivalence, rollback нескольких
секций/digest, index/range/overflow/fast exclusion, неизменные action pointers и
reset. Text проверяется через engine report, file/lump equivalence и same/different
patch peers. Отдельно проверяются Next frame, пропуск pistol action, границы state
table, zero-tic cycle, weapon action skip, same/different patch peers и matching
traces с `-fast`.
Frame visual tests: spawned barrel и psprite report, изменившийся framebuffer,
повторяемость и file/lump pixel equivalence, validation отсутствующего TROO frame
до уровня, исправление поздним assignment, полная проверка range/fullbright,
reset и atomic rollback/digest/count. Psprite-offset tests проверяют точные
`sx/sy` raising-состояния, изменение кадра, отказ для mobj state и границ,
file/lump pixel equivalence и same/different peer patches. Matching peers
сравнивают tic traces; framebuffer checksum не считается доказательством полной
сетевой детерминированности.
`deh-engine` имеет общий
deadline 240s из-за дополнительных настоящих игровых прогонов (visual/psprite
framebuffer и network pairs), а не снятия таймаутов отдельных child operations.

`Monsters Infight` проверяется parser-ом и engine patch report, а также
same/different peer digest. Автоматического сценария с двумя видами монстров,
дерущимися между собой, пока нет: значение читается существующей проверкой
`PIT_CheckThing` в `p_map.c`, отдельный боевой сценарий остаётся ручной проверкой.

Полная compatibility matrix/будущие модули — `DEHACKED-MBF-AUDIT.md`.
