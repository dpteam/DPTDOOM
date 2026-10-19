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

При отсутствии `DPTDOOM_TEST_IWAD` CTest запускает только тест версии.
Для shareware 1.8 дополнительно передать `-DDPTDOOM_TEST_GAMEVERSION=1.8`.
Скрипты также запускаются напрямую:

```powershell
python tests/headless-smoke.py --exe build/headless/dptdoom-headless.exe --iwad refs/iwads/doom2.wad
python tests/windows-smoke.py --exe build/msvc/Release/dptdoom.exe --iwad refs/iwads/doom2.wad
```

Тесты пока не проверяют save/load игровых сохранений, input replay, звук, сеть,
или совместимость модов. GCC/MSVC сохраняют ряд legacy-предупреждений Doom;
успешная сборка не означает, что они все исправлены.
