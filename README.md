# DPT.DOOM

Свой source port Doom на основе [DoomGeneric](https://github.com/ozkl/doomgeneric).
Основная платформа разработки — Windows; переносимые бэкенды сохраняются.

![DPT.DOOM icon](resources/dptdoom.png)

## Быстрый старт: Windows

Нужны CMake 3.21+, Visual Studio 2022 (C++/Windows SDK) и собственный IWAD.
Для локальной разработки положить Doom II в `refs/iwads/doom2.wad`.
`refs/` и WAD-файлы не попадают в Git.

```powershell
cmake --preset windows
cmake --build --preset windows
& ./build/windows/Release/dptdoom.exe -iwad refs/iwads/doom2.wad
```

В текущем этапе есть Win32-сборка, брендинг/иконка и headless для автоматических
проверок. Мультиплеер, мастер-сервер, лаунчер, PK3 и DECORATE **ещё не реализованы**.
Нативный Win32 backend пока без звука; для звука использовать SDL backend.
Для игры со звуком есть проверенная Windows SDL2-сборка:

```powershell
powershell -NoProfile -File tools/fetch-sdl.ps1
cmake --preset windows-sdl
cmake --build --preset windows-sdl
& ./build/windows-sdl/Release/dptdoom.exe -iwad refs/iwads/doom2.wad
```

SDL2 2.32.10 и SDL2_mixer 2.8.2 скачиваются из официальных релизов в
`build/deps` с проверкой SHA-256. Библиотеки не устанавливаются в систему.
Сборка копирует нужные SDL DLL рядом с EXE. MIDI под Windows использует
доступный системный синтезатор; звук через RDP подтверждён владельцем.

- [Сборка и тестирование, включая headless](docs/TESTING.md)
- [План и текущее состояние](docs/ROADMAP.md)
- [Потиковая диагностика и тесты сохранений](docs/STATE-TESTING.md)
- [Разбор upstream PR](docs/UPSTREAM.md)
- [Требования владельца](PROMPT.md)

## Другие платформы и настройки

Корневой CMake выбирает Win32 на Windows, X11 на Linux/Unix и SDL на macOS.
Можно явно указать `-DDPTDOOM_BACKEND=win32|x11|sdl|headless`.
Для SDL нужны CMake-пакеты SDL2 и, с `-DDPTDOOM_SOUND=ON`, SDL2_mixer.
Конфигурации Linux/macOS в форке пока не протестированы.

Release/MinSizeRel используют LTO при поддержке компилятора; отключение:
`-DDPTDOOM_LTO=OFF`. CRT остаётся штатным. `fast-math` не используется.
Старые Makefile и VS solution сохранены как legacy; рекомендуемый путь — CMake.
Названия исходных файлов/API DoomGeneric пока намеренно сохранены для upstream diff.

## Лицензия и происхождение

См. [LICENSE](LICENSE) и copyright-заголовки исходных файлов. DPT.DOOM сохраняет
происхождение DoomGeneric, Chocolate Doom и оригинального Doom от id Software.
Коммерческие игровые данные не включены; лицензия движка не распространяет права
на IWAD. README.TXT — исторический документ исходной кодовой базы.

---

## Исходная документация DoomGeneric

The purpose of doomgeneric is to make porting Doom easier.
Of course Doom is already portable but with doomgeneric it is possible with just a few functions.

To try it you will need a WAD file (game data). If you don't own the game, shareware version is freely available (doom1.wad).

# porting
Create a file named doomgeneric_yourplatform.c and just implement these functions to suit your platform.
* DG_Init
* DG_DrawFrame
* DG_SleepMs
* DG_GetTicksMs
* DG_GetKey

|Functions            |Description|
|---------------------|-----------|
|DG_Init              |Initialize your platfrom (create window, framebuffer, etc...).
|DG_DrawFrame         |Frame is ready in DG_ScreenBuffer. Copy it to your platform's screen.
|DG_SleepMs           |Sleep in milliseconds.
|DG_GetTicksMs        |The ticks passed since launch in milliseconds.
|DG_GetKey            |Provide keyboard events.
|DG_SetWindowTitle    |Not required. This is for setting the window title as Doom sets this from WAD file.

### main loop
At start, call doomgeneric_Create().

In a loop, call doomgeneric_Tick().

In simplest form:
```
int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);

    while (1)
    {
        doomgeneric_Tick();
    }
    
    return 0;
}
```

# sound
Sound is much harder to implement! If you need sound, take a look at SDL port. It fully supports sound and music! Where to start? Define FEATURE_SOUND, assign DG_sound_module and DG_music_module.

# platforms
Ported platforms include Windows, X11, SDL, emscripten. Just look at (doomgeneric_win.c, doomgeneric_xlib.c, doomgeneric_sdl.c).
Makefiles provided for each platform.

## emscripten
You can try it directly here:
https://ozkl.github.io/doomgeneric/

emscripten port is based on SDL port, so it supports sound and music! For music, timidity backend is used.

## Windows
![Windows](screenshots/windows.png)

## X11 - Ubuntu
![Ubuntu](screenshots/ubuntu.png)

## X11 - FreeBSD
![FreeBSD](screenshots/freebsd.png)

## SDL
![SDL](screenshots/sdl.png)
