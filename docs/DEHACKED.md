# DeHackEd: первый Misc-only срез

Это **не полная поддержка DeHackEd/BEX**, Boom, MBF или MBF21. Реализован
отдельный `DPT-deh-misc-v1` parser на C, написанный для DPT.DOOM. Из ACE/csDoom
ничего не скопировано; их parser/gameplay код остаётся материалом для дальнейшего
аудита. Полный upstream `FEATURE_DEHACKED` по-прежнему OFF.

## Что работает

Внешние plain-text patches через `-deh FILE [FILE ...]`, несколько `-deh` групп
в порядке командной строки. Встроенные lumps с именем `DEHACKED` — только при
явном **`-dehlump`**. Без этого флага lump не применяется (хотя WAD bytes всё равно
участвуют в существующем resource digest). Automatic Freedoom/HACX patch loading
не объявляется готовым: их patches могут содержать неподдерживаемые секции.

Пока принимается classic signature v3.0, `Doom version = 19`, `Patch format = 6`
и секция **`Misc 0`**, не выдуманная `[Misc]` и не BEX `[STRINGS]`.

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
| Initial Bullets | 0..200 (текущий vanilla bullet capacity) |
| Max Health, Max Soulsphere, Megasphere Health, God Mode Health | 1..10000 |
| Max Armor, Soulsphere Health, IDFA Armor, IDKFA Armor | 0..10000 |
| Green Armor Class, Blue Armor Class, IDFA Armor Class, IDKFA Armor Class | 1..2 |
| BFG Cells/Shot | 1..1000 |

Диапазоны — поддерживаемый безопасный subset, **не весь исторический DeHackEd**.
`Monsters Infight`, Thing/Frame/Ammo/Weapon/Sound/Sprite/Pointer/Cheat/Text,
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
- Vanilla DSG и demo не содержат patch manifest. Пока patches активны,
  **save/load и demo recording запрещены**: меню показывает сообщение, CLI/
  internal вызовы fail-fast до записи файла. Не выдавать vanilla DSG за snapshot
  изменённых rules. Для возвращения к обычным saves — запуск без patches.
- Demo playback пока возможно с явно выбранным patch, но не заявляется
  совместимость/синхронизация с произвольными patched demos. Непатченный
  attract-demo save/takeover и state baselines остаются без изменений.

## Проверки

`deh-misc`: synthetic grammar/ranges/overflow/NUL/line/size/count limits, atomic
rollback, digest repeatability и load order, без IWAD/окна/Internet.

`deh-engine` (headless + локальный Doom II): реальные player health/ammo, внешний
patch, повторные patches, два DEHACKED lumps и explicit opt-in/order, bad source,
save/load/record guards и одинаковые/разные patches в настоящих two-peer
процессах с matching traces. Все patch/WAD fixtures создаются во временных
каталогах; IWAD/извлечённые ресурсы не коммитятся.

Полная compatibility matrix/будущие модули — `DEHACKED-MBF-AUDIT.md`.
