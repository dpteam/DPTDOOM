#ifndef DOOM_GENERIC
#define DOOM_GENERIC

#include <stdlib.h>
#include <stdint.h>

#ifndef DOOMGENERIC_RESX
#define DOOMGENERIC_RESX 640
#endif  // DOOMGENERIC_RESX

#ifndef DOOMGENERIC_RESY
#define DOOMGENERIC_RESY 400
#endif  // DOOMGENERIC_RESY


#ifdef CMAP256

typedef uint8_t pixel_t;

#else  // CMAP256

typedef uint32_t pixel_t;

#endif  // CMAP256


extern pixel_t* DG_ScreenBuffer;

#ifdef __cplusplus
extern "C" {
#endif

void doomgeneric_Create(int argc, char **argv);
void doomgeneric_Tick();


//Implement below functions for your platform
void DG_Init();
void DG_DrawFrame();
void DG_SleepMs(uint32_t ms);
uint32_t DG_GetTicksMs();
int DG_GetKey(int* pressed, unsigned char* key);

// Mouse input. Motion is relative and only reported while the mouse is
// captured; buttons are a bitmask of SDL_BUTTON_* values. Returns 0 when there
// is nothing new, so callers can drain in a loop like DG_GetKey.
int DG_GetMouse(int* dx, int* dy, unsigned int* buttons);

// Capture turns relative mouse motion on and hides the cursor. Released in
// menus, when paused, during demo playback and when the window loses focus, so
// the pointer is never trapped with no way to reach the window controls.
void DG_SetMouseCapture(int capture);
void DG_SetWindowTitle(const char * title);

#ifdef __cplusplus
}
#endif

#endif //DOOM_GENERIC
