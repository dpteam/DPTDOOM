# Тестирование DPT.DOOM

Коммерческие IWAD не распространяются с исходниками. Владелец хранит Doom II
локально в `refs/iwads/doom2.wad`; весь `refs/` игнорируется Git.
Python 3 нужен для smoke tests, но не для сборки игры.

## Windows: обычная сборка

Нужны CMake 3.21+ и Visual Studio 2022 с компонентами C++ и Windows SDK.

```powershell
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
ctest --preset windows-sdl -E windows-smoke
```

SDL-тесты ввода используют dummy video, аудио — dummy audio и синтетические
сигналы, не громкоговорители. Они проверяют реальный ненулевой PCM и панораму,
паузы/возобновление музыки, отключение подсистем и ошибку аудиодрайвера.
Тестовый WAV подставляется через штатный механизм music substitutions; это
не доказательство работы аппаратного MIDI на любой машине. Конвертация MUS
проверяется отдельно, а слышимость через RDP подтверждена владельцем вручную.
При заданном IWAD `sdl-engine` прогоняет настоящий движок с dummy video/audio,
проверяет flags/shutdown и graceful failure. Никаких extracted IWAD ресурсов
или сгенерированных WAV не сохраняется в Git.

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
