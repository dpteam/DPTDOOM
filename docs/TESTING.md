# Тестирование DPT.DOOM

Коммерческие IWAD не распространяются с исходниками. Владелец хранит Doom II
локально в `refs/iwads/doom2.wad`; весь `refs/` игнорируется Git.
Python 3 нужен для smoke tests, но не для сборки игры.
CI без игровых данных и его ограничения описаны в [CI.md](CI.md).

DeHackEd subset: `deh-misc` — data-free parser/rollback/limits/digest;
`deh-engine` — headless с локальным IWAD, synthetic patches/lumps, реальные
health/ammo/capacity, weapon ammo use и two-peer patch mismatch. Это classic
Misc 0 / Ammo / bounded Text / Weapon frames / Thing numeric/flags/states/sounds / Frame graph, не full
DeHackEd/BEX; patched saves/recording запрещены до manifest. См. [DEHACKED.md](DEHACKED.md).

Native master directory собирается вместе с engine (`DPTDOOM_MASTER=ON`).
`master-model` и `master-loopback` — data-free, без окон; второй тест запускает
настоящий C EXE и общается с ним по loopback TCP. Протокол и ограничения —
[MASTER.md](MASTER.md). Python используется только как тестовый клиент.

## Windows: обычная сборка

### Debug-консоль (Windows graphical backend)

Только конфигурация **Debug** для Win32/SDL клиента вызывает AllocConsole, если
нет видимой консоли. Для launcher `CREATE_NO_WINDOW` скрытая console association
сначала отсоединяется. Release/RelWithDebInfo/headless не получают этот код.
`--version` остаётся тихим probe, без консоли и Debug-файла.

stdout и stderr сохраняются отдельно в унаследованных pipes/files; одновременно
обе строки идут в консоль и уникальный `dptdoom-debug-<pid>-<ticks>.log` в CWD игры
(для launcher — `game/profileN/`). Session log launcher не теряется. Relay
выводит строки/блоки до 4096 bytes, без гарантии общего межпоточного порядка
stdout/stderr; при нормальном exit дочитывает хвост. Если файл недоступен, есть
предупреждение в остальных sinks. Это Debug-диагностика: логи не ротируются и
могут содержать локальные пути/сообщения; не публиковать их без просмотра.

```powershell
cmake --build build/windows-sdl --config Debug --target dptdoom
```

`debug-console` — interactive data-free probe реального AllocConsole, больших
stdout/stderr, file/pipe tee и exit flush. `release-console` — отсутствие
дополнительной консоли/Debug-файла. Тесты не меняют реальные профили игрока.

### Обычная сборка

Нужны CMake 3.21+ и Visual Studio 2022 с компонентами C++ и Windows SDK.

```powershell
powershell -NoProfile -File tools/fetch-webview2.ps1
cmake --preset windows -DDPTDOOM_TEST_IWAD="$PWD/refs/iwads/doom2.wad"
cmake --build --preset windows
ctest --preset windows
& ./build/windows/Release/dptdoom.exe -iwad refs/iwads/doom2.wad -warp 1
```

Win32 backend пока без звука. Тест окна проверяет заголовок и `WM_CLOSE`;
он ненадолго открывает игру. Для регулярных проверок использовать headless.

## Windows SDL2: звук и музыка

```powershell
powershell -NoProfile -File tools/fetch-sdl.ps1
powershell -NoProfile -File tools/fetch-webview2.ps1
cmake --preset windows-sdl -DDPTDOOM_TEST_IWAD="$PWD/refs/iwads/doom2.wad"
cmake --build --preset windows-sdl
ctest --preset windows-sdl
& ./build/windows-sdl/Release/dptdoom.exe -iwad refs/iwads/doom2.wad -warp 1
```

Скрипт скачивает **официальные VC development ZIP** SDL 2.32.10 и SDL_mixer 2.8.2,
проверяет фиксированные SHA-256 и распаковывает в `build/deps`. Он не ставит
пакеты глобально и не скачивает игровые данные. Скрипт можно не использовать,
если уже есть совместимые библиотеки: передать `SDL2_DIR`/`SDL2_mixer_DIR` в CMake.
Нужны SDL2 2.0.7+ и, для звука, SDL2_mixer 2.6+.
Сборка копирует `SDL2.dll` и `SDL2_mixer.dll` рядом с EXE. Опциональные codec DLL
не копируются; уведомления о лицензиях — `docs/SDL-LICENSES.txt`.

MSVC Windows-сборка также включает HTML launcher/WebView2Loader.dll, SDK берётся
из отдельного проверяемого fetch. Runtime автоматически не устанавливается.
Engine-only/MinGW: `-DDPTDOOM_LAUNCHER=OFF`. Подробности portable и tests —
[LAUNCHER.md](LAUNCHER.md); `launcher-windows` имеет метку `interactive`.

MUS конвертируется в MIDI в памяти, без общего временного `doom.mid`.
Windows использует доступный native MIDI synth; для других платформ может
понадобиться отдельная настройка синтезатора/патчей. Можно задать
`timidity_cfg_path` в `dptdoom-doom.cfg`; пути к инструментам в таком конфиге
должны быть настроены самим пользователем. Soundfont/Windows MIDI bank не
поставляется с форком. RDP-перенаправление звука должно быть включено до запуска;
если аудиоустройство сменилось, перезапустить игру.

Параметры: `-nosound` — всё аудио, `-nosfx` — только эффекты, `-nomusic` — музыка.
SDL backend поддерживает изменение размера окна, nearest-neighbor scaling и
software fallback. Мышь/геймпады пока не добавлены.

Частые проверки без видимых окон:

```powershell
ctest --preset windows-sdl -LE interactive
```

SDL-тесты ввода используют dummy video, аудио — dummy audio и синтетические
сигналы, не громкоговорители. Они проверяют реальный ненулевой PCM и панораму,
паузы/возобновление музыки, отключение подсистем и ошибку аудиодрайвера.

Оконный `net-windows` проверяет peer close → напарник продолжает карту → свой
F7 → меню → SP/save/load; `net-windows-local-end` и `net-windows-guest-end` —
F7 cancel/confirm для обеих ролей, rules/fast/chat/pause handling.
Игровые packet errors проверяются отдельно в fail-fast и menu-recovery режимах.
Тестовый WAV подставляется через штатный механизм music substitutions; это
не доказательство работы аппаратного MIDI на любой машине. Конвертация MUS
проверяется отдельно, а слышимость через RDP подтверждена владельцем вручную.
При заданном IWAD `sdl-engine` прогоняет настоящий движок с dummy video/audio,
проверяет flags/shutdown и graceful failure. Никаких extracted IWAD ресурсов
или сгенерированных WAV не сохраняется в Git.

Тесты с меткой `interactive` открывают окно: обычный `windows-smoke` и
`demo-snapshot` (F2-save в attract-demo, F3-load, переход к живому игроку).
Они выполняются последовательно. Для обычной игры демо-сохранение включено
по умолчанию: дождаться игрового attract-demo, F2 → слот → имя → Enter.
Демо продолжится; F3 загружает сохранённый момент как single-player.
На title/credit screen без игрового уровня сохранение не предлагается.
Применяются ограничения ванильного DSG; это не полный rewind с RNG.

ASan-сборка и реальные границы проверки: [SANITIZERS.md](SANITIZERS.md).
Direct coop loopback/packet/window tests: [NETWORK.md](NETWORK.md).

## Headless

Это отдельный исполняемый файл без создания окна, клавиатуры и вывода звука.
Рендерер Doom работает в памяти; файл **не** является сетевым dedicated server.

```powershell
cmake --preset windows-headless -DDPTDOOM_TEST_IWAD="$PWD/refs/iwads/doom2.wad"
cmake --build --preset windows-headless
ctest --preset windows-headless

& ./build/windows-headless/Release/dptdoom-headless.exe `
  -iwad refs/iwads/doom2.wad -warp 1 `
  -headless-frames 70 -headless-dump build/frame.ppm
```

MinGW/GCC (в текущем PowerShell), Linux/macOS с Ninja:

```powershell
cmake --preset headless -DDPTDOOM_TEST_IWAD="$PWD/refs/iwads/doom2.wad"
cmake --build --preset headless
ctest --preset headless
```

Параметры только headless backend:

| Аргумент | Поведение |
|---|---|
| `-headless-frames N` | Успешный выход после N вызовов вывода кадра, включая кадры wipe-перехода. N — положительное целое. Это не количество игровых тиков. |
| `-headless-dump path.ppm` | Сохранение последнего RGB-кадра в бинарный PPM P6 при завершении. Родительский каталог должен существовать. |
| `-timedemo demo1` | Максимально быстрый прогон встроенного демо. Переход-melt пропускается для воспроизводимого счётчика кадров. |
| `-playdemo file.lmp` | Обычное воспроизведение и успешный выход после демо. |
| `--version` | Версия без IWAD и без инициализации окна (в любом backend). |

Потиковая диагностика, checkpoint/save-load и закреплённые эталоны описаны в
[STATE-TESTING.md](STATE-TESTING.md). `-headless-save-tic` перезаписывает слот 0;
использовать только в отдельном тестовом рабочем каталоге.

При выходе печатается `Headless: frames=... rgb-fnv1a=...`. FNV-1a считается по
RGB-байтам; alpha/padding не участвуют. Это диагностический checksum изображения,
а не полного игрового состояния. Без лимита/демо игра работает до внешней остановки.
`-nogui` и `-nosound` добавляются headless backend автоматически.

**Важно:** исторический `-timedemo` завершает работу через `I_Error`, код
255 / -1 / 4294967295 в зависимости от среды. Успех определяется сообщением
`timed N gametics ...`, а не только кодом выхода. Это не относится к лимиту
кадров и обычному `-playdemo`: они обязаны завершаться с кодом 0.

## Состав smoke tests

- Версия без WAD/дисплея.
- Полный `DEMO1`, в headless — два прогона и сравнение тиков/кадров/RGB.
- Короткое внешнее демо, извлечённое из локального IWAD во временный каталог;
  нормальный выход, корректное сохранение конфигов; headless также проверяет
  чтение изменённой настройки из конфига при следующем запуске.
- Headless: точный лимит кадров, непустой PPM правильного размера,
  неверные параметры и отсутствующий IWAD без всплывающих диалогов.
- Win32: брендированный заголовок и закрытие окна с кодом 0.

При отсутствии `DPTDOOM_TEST_IWAD` CTest запускает тест версии и, для SDL,
самодостаточные тесты ввода/аудио без игровых данных.
Для shareware 1.8 дополнительно передать `-DDPTDOOM_TEST_GAMEVERSION=1.8`.
Скрипты также запускаются напрямую:

```powershell
python tests/headless-smoke.py --exe build/headless/dptdoom-headless.exe --iwad refs/iwads/doom2.wad
python tests/windows-smoke.py --exe build/msvc/Release/dptdoom.exe --iwad refs/iwads/doom2.wad
```

`state-smoke` проверяет save/load и повторяемость трёх демо по каждому тику.
Тесты пока не проверяют сетевую сессию, меню save/load через ввод и совместимость
модов. GCC/MSVC сохраняют ряд legacy-предупреждений Doom;
успешная сборка не означает, что они все исправлены.
