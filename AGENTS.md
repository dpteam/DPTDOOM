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
  Не коммитить IWAD, извлечённые игровые ресурсы, сохранения или скриншоты игры.
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
- `ctest --preset windows-sdl -E windows-smoke` не открывает видимые окна:
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

## Архитектурные ограничения

- `FEATURE_MULTIPLAYER` выключен, `net_*.c` отсутствуют. Заголовки не означают,
  что сеть работает. В `refs/csDoomMaster` — только мастер-сервер, не netcode.
- Звук/музыка работают в проверенном Windows SDL-бэкенде; нативная
  Win32/headless сборка без звука. Mouse/gamepad/OPL пока не реализованы.
- DECORATE, PK3 и загрузка вложенных WAD пока не реализованы.
- Upstream PR и решения по ним фиксируются в `docs/UPSTREAM.md`.
