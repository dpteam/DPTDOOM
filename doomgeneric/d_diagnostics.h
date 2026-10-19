// DPT.DOOM test instrumentation. GPL-2.0-or-later; see LICENSE.
#ifndef D_DIAGNOSTICS_H
#define D_DIAGNOSTICS_H

#ifdef DPTDOOM_DIAGNOSTICS
void D_DiagnosticsInit(void);
void D_DiagnosticsTic(void);
void D_DiagnosticsLoaded(void);
#else
#define D_DiagnosticsTic() ((void)0)
#define D_DiagnosticsLoaded() ((void)0)
#endif

#endif
