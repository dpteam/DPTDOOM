// Exercise production audio modules with synthetic WAD lumps and dummy audio.
// No Doom IWAD, extracted sound, soundfont or physical device is needed.
#define WIN32_LEAN_AND_MEAN
#include "i_sound.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_config.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"
#include "memio.h"
#include "mus2mid.h"
#include "sha1.h"
#include <SDL.h>
#include <SDL_mixer.h>
#include <stdarg.h>

boolean screensaver_mode = false;
const char *configdir = "." DIR_SEPARATOR_S;
lumpinfo_t *lumpinfo;
unsigned int numlumps;
static byte sound_data[4104];
static int releases;
static uint64_t left_energy, right_energy, samples;
static const byte mus_data[] = {
    'M','U','S',26, 8,0, 16,0, 1,0, 0,0, 0,0, 0,0,
    0x90, 0xbc, 100, 70, 0x80, 60, 1, 0x60
};

void I_Error(char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    exit(1);
}
void I_Quit(void) { exit(1); }
void I_AtExit(atexit_func_t func, boolean on_error) { (void)func; (void)on_error; }
void M_BindVariable(char *name, void *variable) { (void)name; (void)variable; }
void *Z_Malloc(int size, int tag, void *user)
{
    void *ptr = malloc(size);
    (void)tag;
    if (!ptr) I_Error("Out of memory");
    if (user) *(void **)user = ptr;
    return ptr;
}
void Z_Free(void *ptr) { free(ptr); }
int W_CheckNumForName(char *name) { (void)name; return 0; }
int W_GetNumForName(char *name) { (void)name; return 0; }
int W_LumpLength(unsigned int lump) { (void)lump; return sizeof(sound_data); }
void *W_CacheLumpNum(int lump, int tag) { (void)lump; (void)tag; return sound_data; }
void W_ReleaseLumpNum(int lump) { (void)lump; ++releases; }

// Mix_GetError() keeps the last error text, which may come from an earlier
// probe (for example a missing MIDI backend) and not from the failing call.
// Print it as context, never as the claimed cause.
static void FailCheck(int line, const char *condition)
{
    const char *mixer_error = Mix_GetError();

    if (mixer_error != NULL && *mixer_error != '\0')
    {
        I_Error("Line %d: %s (last SDL_mixer error, not necessarily the "
                "cause: %s)\n", line, condition, mixer_error);
    }
    I_Error("Line %d: %s\n", line, condition);
}

#define CHECK(condition) do { if (!(condition)) FailCheck(__LINE__, #condition); } while (0)

static void SDLCALL Capture(void *user, Uint8 *stream, int len)
{
    const Sint16 *pcm = (const Sint16 *)stream;
    int i;
    (void)user;
    for (i = 0; i + 1 < len / 2; i += 2)
    {
        left_energy += abs((int)pcm[i]);
        right_energy += abs((int)pcm[i + 1]);
        ++samples;
    }
}

static void BeginCapture(void)
{
    SDL_LockAudio();
    left_energy = right_energy = samples = 0;
    SDL_UnlockAudio();
}

// `pan` is the expected channel balance: -1 = hard left, +1 = hard right,
// 0 = both channels. Checking both extremes catches a swapped or ignored
// separation, which a one-sided check cannot.
static void CheckCapture(int pan)
{
    uint64_t left, right, count;
    SDL_Delay(120);
    SDL_LockAudio();
    left = left_energy; right = right_energy; count = samples;
    SDL_UnlockAudio();
    CHECK(count > 0 && left + right > 0);
    if (pan < 0)
        CHECK(left > 0 && right == 0);
    else if (pan > 0)
        CHECK(right > 0 && left == 0);
    else
        CHECK(left > 0 && right > 0);
}

static void WriteTone(void)
{
    FILE *file = fopen("tone.wav", "wb");
    SDL_RWops *rw;
    int i;
    CHECK(file != NULL);
    fclose(file);
    rw = SDL_RWFromFile("tone.wav", "wb");
    CHECK(rw != NULL);
    SDL_RWwrite(rw, "RIFF", 4, 1); SDL_WriteLE32(rw, 36 + 88200);
    SDL_RWwrite(rw, "WAVEfmt ", 8, 1); SDL_WriteLE32(rw, 16);
    SDL_WriteLE16(rw, 1); SDL_WriteLE16(rw, 1);
    SDL_WriteLE32(rw, 44100); SDL_WriteLE32(rw, 88200);
    SDL_WriteLE16(rw, 2); SDL_WriteLE16(rw, 16);
    SDL_RWwrite(rw, "data", 4, 1); SDL_WriteLE32(rw, 88200);
    for (i = 0; i < 44100; ++i) SDL_WriteLE16(rw, (i / 100) % 2 ? 6000 : -6000);
    SDL_RWclose(rw);
}

int main(int argc, char **argv)
{
    sha1_context_t context;
    sha1_digest_t digest;
    FILE *file;
    int i, freq, channels, no_sfx, no_music, failed;
    Uint16 format;
    void *song;
    sfxinfo_t sfx = {0};
    MEMFILE *input, *output;
    void *midi;
    size_t length;

    myargc = argc; myargv = argv;
    SDL_SetMainReady();
    failed = M_ParmExists("-bad-driver");
    if (failed) SDL_setenv("SDL_AUDIODRIVER", "does-not-exist", 1);
    no_sfx = failed || M_ParmExists("-nosfx") || M_ParmExists("-nosound");
    no_music = failed || M_ParmExists("-nomusic") || M_ParmExists("-nosound");

    // Verify the real MUS converter with a hand-authored single note.
    input = mem_fopen_read((void *)mus_data, sizeof(mus_data));
    output = mem_fopen_write();
    CHECK(mus2mid(input, output) == 0);
    mem_get_buf(output, &midi, &length);
    CHECK(length > 14 && !memcmp(midi, "MThd", 4));
    mem_fclose(input); mem_fclose(output);

    WriteTone();
    M_MakeDirectory("music");
    file = fopen("music/doom2-music.cfg", "w");
    CHECK(file != NULL);
    SHA1_Init(&context); SHA1_Update(&context, (byte *)mus_data, sizeof(mus_data));
    SHA1_Final(digest, &context);
    for (i = 0; i < sizeof(digest); ++i) fprintf(file, "%02x", digest[i]);
    fprintf(file, " = ..%stone.wav\n", DIR_SEPARATOR_S);
    fclose(file);

    sound_data[0] = 3; sound_data[2] = 0x11; sound_data[3] = 0x2b; // 11025 Hz
    sound_data[5] = 0x10; // 4096 samples
    for (i = 8; i < sizeof(sound_data); ++i) sound_data[i] = (i / 25) % 2 ? 190 : 66;
    strcpy(sfx.name, "tone");
    I_InitSound(true); I_InitMusic();
    if (no_sfx && no_music)
    {
        CHECK(!Mix_QuerySpec(&freq, &format, &channels));
        CHECK(!I_RegisterSong((void *)mus_data, sizeof(mus_data)));
        CHECK(!I_MusicIsPlaying());
    }
    else
    {
        CHECK(Mix_QuerySpec(&freq, &format, &channels));
        CHECK(format == AUDIO_S16SYS && channels == 2);
        Mix_SetPostMix(Capture, NULL);
        if (!no_sfx)
        {
            // Invalid lump must be rejected and released exactly once.
            sound_data[2] = sound_data[3] = 0;
            CHECK(I_StartSound(&sfx, 0, 127, 0) == -1);
            CHECK(releases == 1 && !sfx.driver_data);
            sound_data[2] = 0x11; sound_data[3] = 0x2b;
            // Hard left first, then hard right. Both must be panned from the
            // very first mixed slice: if the separation were registered after
            // the chunk was queued, the opening slice would leak into the
            // opposite channel and these checks would fail.
            BeginCapture();
            CHECK(I_StartSound(&sfx, 0, 127, 0) == 0);
            CHECK(I_SoundIsPlaying(0));
            CheckCapture(-1);
            BeginCapture();
            CHECK(I_StartSound(&sfx, 0, 127, 254) == 0);
            CheckCapture(1);
            I_StopSound(0);
            CHECK(!I_SoundIsPlaying(0));
        }
        song = I_RegisterSong((void *)mus_data, sizeof(mus_data));
        if (no_music) CHECK(song == NULL);
        else
        {
            CHECK(song != NULL);
            I_SetMusicVolume(127); I_PlaySong(song, true);
            CHECK(I_MusicIsPlaying());
            BeginCapture(); CheckCapture(0);
            I_PauseSong();
            SDL_Delay(60); BeginCapture(); SDL_Delay(100);
            SDL_LockAudio();
            CHECK(samples > 0 && left_energy == 0 && right_energy == 0);
            SDL_UnlockAudio();
            I_ResumeSong(); BeginCapture(); CheckCapture(0);
            I_StopSong(); CHECK(!I_MusicIsPlaying());
            I_UnRegisterSong(song);
            // Non-looping playback must end after one track.
            I_SetMusicVolume(127);
            song = I_RegisterSong((void *)mus_data, sizeof(mus_data));
            I_PlaySong(song, false);
            SDL_Delay(1250);
            CHECK(!I_MusicIsPlaying());
            I_UnRegisterSong(song);
        }
        if (!no_music)
        {
            // Exercise MUS/MIDI loading without a soundfont/native MIDI
            // device. A missing decoder must return NULL, not crash or keep
            // a freed conversion buffer. This path is host-independent.
            SDL_SetHint("SDL_MIXER_DISABLE_TIMIDITY", "1");
            SDL_SetHint("SDL_MIXER_DISABLE_FLUIDSYNTH", "1");
            SDL_SetHint("SDL_MIXER_DISABLE_NATIVEMIDI", "1");
            {
                byte other_mus[sizeof(mus_data)];
                memcpy(other_mus, mus_data, sizeof(mus_data));
                other_mus[18] = 90; // distinct hash; no synthetic substitution
                CHECK(I_RegisterSong(other_mus, sizeof(other_mus)) == NULL);
            }
        }
        Mix_SetPostMix(NULL, NULL);
    }
    I_ShutdownSound(); I_ShutdownMusic(); I_ShutdownSound();
    CHECK(!Mix_QuerySpec(&freq, &format, &channels));
    CHECK(sfx.driver_data == NULL);
    SDL_Quit();
    puts("SDL audio: MUS conversion, PCM output/panning, music, flags and shutdown OK");
    return 0;
}
