// Windows graphical Debug builds only. GPL-2.0-or-later; see LICENSE.
#ifndef DPT_DEBUG_CONSOLE_H
#define DPT_DEBUG_CONSOLE_H
#ifdef DPTDOOM_DEBUG_CONSOLE
void DPT_DebugConsoleInit(void);
#else
#define DPT_DebugConsoleInit() ((void)0)
#endif
#endif
