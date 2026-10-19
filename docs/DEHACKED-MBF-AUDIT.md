# DeHackEd / BEX / Boom / MBF: аудит перед реализацией

Проверено после добавления владельцем `refs/csDoomNew`, `refs/csDoomOld07` и
`refs/doom_ace-master`. Это **аудит**, не заявление о готовой совместимости и
не разрешение автоматически копировать код.

## Состояние DPT.DOOM сейчас

- `doomgeneric/doomfeatures.h` содержит `#undef FEATURE_DEHACKED`.
- В tracked engine есть `deh_main.h`, `deh_str.h`, `deh_misc.h` и вызовы
  entrypoint-ов в `d_main.c`/`d_net.c`, но отдельный полноценный parser/handlers
  не входит в текущий CMake `engine_sources`.
- `DEH_LoadLump*`, `DEH_ParseCommandLine` и `DEH_Checksum` не являются рабочим
  runtime-path в текущем slim build. Нельзя включить один define и считать,
  что поддержка появилась.
- Старый network scaffold содержит вызов `DEH_Checksum` под выключенным ORIGCODE;
  текущий direct v2 использует собственный ResourceDigest, не этот entrypoint.
  До реальной загрузки patches это не сверка изменённых states. При реализации
  checksum/manifest нужно добавить applied patch bytes/order и compatibility level,
  не отправлять абсолютные пути.
- Нет отдельного compatibility selector для vanilla/DeHackEd/BEX/Boom/MBF/MBF21.
  Включать WAD с новым special без правильной семантики опаснее, чем понятно
  отказать.

## Что найдено в reference trees

### csDoomNew и csDoomOld07

Оба игровых дерева имеют client/server `d_dehacked.cpp` и поддерживают заметно
более широкий parser, чем нужен для первого среза:

- classic sections: `Thing`, `Sound`, `Frame`, `Sprite`, `Ammo`, `Weapon`,
  `Pointer`, `Cheat`, `Misc`, `Text`;
- BEX sections: `[STRINGS]`, `[PARS]`, `[CODEPTR]`;
- встроенный `DEHACKED` lump и внешний patch file;
- Boom-related flags/fields, generalized sector/floor code, push/pull things,
  MBF linedef-controlled skies и другие расширения в map/gameplay code.

Это хороший reference для names/field mapping и regression cases, но не drop-in:
старый C++ engine имеет другие `state_t`/`mobjinfo`/sound/string tables,
allocator, command system, save format, network model и 32-bit assumptions.
`csDoomNew` и `csDoomOld07` не побайтовые копии. `csDoomMaster` не заменяет
игровые исходники.

### DOOM ACE

`refs/doom_ace-master/engine/dehacked.c` — компактный отдельный parser с
config-controlled enable, `DEHACKED` lump и собственными runtime structs.
README прямо описывает DOS Doom II 1.9, генерацию `code.lmp` и WAD injection
через exploit. Использовать можно только безопасные идеи/парсер после
file-level provenance and license review.

**Не переносить и не запускать** `exploit/`, memory-injection/WAD-injection chain,
DOS address hooks или код, который рассчитывает на оригинальный EXE layout.
ACE LICENSE — LGPL-2.1; это отдельная обязанность по notices/совместимости,
не blanket license всего `refs`.

## Предлагаемая матрица совместимости

| Уровень | Первый целевой scope | Пока |
|---|---|---|
| `vanilla` | Текущие IWAD/PWAD без patches | Работает, baseline закреплены |
| `deh-classic` | bounded `.deh`, `DEHACKED` lump; Thing/Frame/Weapon/Ammo/Misc/Text | Следующий parser vertical slice |
| `bex-basic` | `[STRINGS]`, безопасные `[PARS]`, ограниченные compatibility aliases | После classic tests |
| `bex-codeptr` | `[CODEPTR]` только allowlist существующих DPT action functions | Не исполнять произвольные pointers |
| `boom` | generalized linedefs/sectors, Boom flags/specials, blockmap/physics cases | Отдельный map/runtime этап |
| `mbf` | MBF things/flags/skies/AI semantics и demos | После Boom matrix |
| `mbf21` | отдельный declared compatibility level/manifest | Не найден в refs как готовая полная реализация |

Не принимать `.deh`/BEX silently: unsupported section/field должен дать понятный
error или warning policy, а не частично изменить игру без отчёта. Для network
включать canonical patch digest/order/compatibility level в handshake. Для save/
demo сохранять applied patch identity или запрещать несовместимую загрузку.

## Безопасный первый vertical slice

1. Отдельный C parser module с bounded line/section/number/string limits и
   overflow-safe assignments в DPT tables; внешний `.deh` и lump.
2. Manifest applied patch digest до запуска уровня; ошибки/unknown fields в log.
3. Только classic `Thing`, `Frame`, `Ammo`, `Weapon`, `Misc`, `Text` поля,
   которые имеют прямой безопасный mapping на существующие структуры. Не
   включать arbitrary code pointers, новые states/actions и memory offsets.
4. Synthetic PWAD/DEH fixtures в tests, не коммитить IWAD/extracted resources:
   changed monster health, weapon ammo, player start health, strings и state
   tics; malformed/truncated/duplicate/oversized/unknown cases.
5. Headless trace + save/load + two-peer handshake tests: одинаковый patch
   проходит, различный patch отвергается до игровой симуляции.

## Лицензия и заимствование

- Сначала написать собственный bounded adapter вокруг проверенной модели таблиц,
  либо перенести только конкретный parser fragment с сохранением его copyright/
  license notice после проверки совместимости.
- `csDoomNew/client/license.txt` — MIDAS license, `server/Licence` — NASM;
  это не общая лицензия csDoom. Старые `doomdef.h` содержат DOOM Source Code
  License headers. `csDoomOld07/readme.txt` заявляет GPL, но file-level notices
  и внешние зависимости всё равно проверять.
- `refs/doom_ace-master/LICENSE` LGPL-2.1; exploit не использовать.
- Нет подтверждения, что произвольный PWAD/DeHackEd patch можно перераспространять:
  tests используют синтетические fixtures, пользовательские mods остаются его
  ответственностью.

Следующий кодовый этап после этого аудита — только `deh-classic` parser + tests.
Boom/MBF/MBF21 не отмечать готовыми от наличия reference source.

Реализованный subset описан в `DEHACKED.md`: **Misc 0, Ammo max/per и Weapon
Ammo type**, отдельный DPT_DEH_CLASSIC path, без включения полного FEATURE_DEHACKED.
Thing/Frame/Text, weapon frames и BEX остаются очередью следующих срезов.
