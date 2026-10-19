#include "doomkeys.h"

#include "doomgeneric.h"
#include "config.h"
#include "i_system.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static BITMAPINFO s_Bmi = { { sizeof(BITMAPINFOHEADER), DOOMGENERIC_RESX, -DOOMGENERIC_RESY, 1, 32 } };
static HWND s_Hwnd = 0;
static HDC s_Hdc = 0;


#define KEYQUEUE_SIZE 16

static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex = 0;

static unsigned char convertToDoomKey(unsigned char key)
{
	switch (key)
	{
	case VK_RETURN:
		key = KEY_ENTER;
		break;
	case VK_ESCAPE:
		key = KEY_ESCAPE;
		break;
	case VK_LEFT:
		key = KEY_LEFTARROW;
		break;
	case VK_RIGHT:
		key = KEY_RIGHTARROW;
		break;
	case VK_UP:
		key = KEY_UPARROW;
		break;
	case VK_DOWN:
		key = KEY_DOWNARROW;
		break;
	case VK_CONTROL:
		key = KEY_FIRE;
		break;
	case VK_SPACE:
		key = KEY_USE;
		break;
	case VK_SHIFT:
		key = KEY_RSHIFT;
		break;
	case VK_MENU:
		key = KEY_RALT;
		break;
	case VK_BACK:
		key = KEY_BACKSPACE;
		break;
	case VK_PAUSE:
		key = KEY_PAUSE;
		break;
	case VK_F1: key = KEY_F1; break;
	case VK_F2: key = KEY_F2; break;
	case VK_F3: key = KEY_F3; break;
	case VK_F4: key = KEY_F4; break;
	case VK_F5: key = KEY_F5; break;
	case VK_F6: key = KEY_F6; break;
	case VK_F7: key = KEY_F7; break;
	case VK_F8: key = KEY_F8; break;
	case VK_F9: key = KEY_F9; break;
	case VK_F10: key = KEY_F10; break;
	case VK_F11: key = KEY_F11; break;
	case VK_F12: key = KEY_F12; break;
	case VK_OEM_PLUS: key = KEY_EQUALS; break;
	case VK_OEM_MINUS: key = KEY_MINUS; break;
	default:
		key = tolower(key);
		break;
	}

	return key;
}

static void addKeyToQueue(int pressed, unsigned char keyCode)
{
	unsigned char key = convertToDoomKey(keyCode);

	unsigned short keyData = (pressed << 8) | key;

	s_KeyQueue[s_KeyQueueWriteIndex] = keyData;
	s_KeyQueueWriteIndex++;
	s_KeyQueueWriteIndex %= KEYQUEUE_SIZE;
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_CLOSE:
		// Run Doom's exit callbacks (including saving configuration).
		I_Quit();
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	case WM_KEYDOWN:
		addKeyToQueue(1, (unsigned char)wParam);
		break;
	case WM_KEYUP:
		addKeyToQueue(0, (unsigned char)wParam);
		break;
	default:
		return DefWindowProcA(hwnd, msg, wParam, lParam);
	}
	return 0;
}

void DG_Init()
{
	// window creation
	const char windowClassName[] = "DPTDOOMWindowClass";
	const char windowTitle[] = PACKAGE_NAME;
	WNDCLASSEXA wc;

	wc.cbSize = sizeof(WNDCLASSEXA);
	wc.style = 0;
	wc.lpfnWndProc = wndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = GetModuleHandleA(NULL);
	wc.hIcon = LoadIconA(wc.hInstance, MAKEINTRESOURCEA(1));
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = 0;
	wc.lpszMenuName = 0;
	wc.lpszClassName = windowClassName;
	wc.hIconSm = wc.hIcon;

	if (!RegisterClassExA(&wc))
	{
		printf("Window Registration Failed!");

		exit(-1);
	}

	RECT rect;
	rect.left = rect.top = 0;
	rect.right = DOOMGENERIC_RESX;
	rect.bottom = DOOMGENERIC_RESY;
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

	HWND hwnd = CreateWindowExA(0, windowClassName, windowTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, 0, 0, wc.hInstance, 0);
	if (hwnd)
	{
		s_Hwnd = hwnd;

		s_Hdc = GetDC(hwnd);
		ShowWindow(hwnd, SW_SHOW);
	}
	else
	{
		printf("Window Creation Failed!");

		exit(-1);
	}

	memset(s_KeyQueue, 0, KEYQUEUE_SIZE * sizeof(unsigned short));
}

void DG_DrawFrame()
{
	MSG msg;
	memset(&msg, 0, sizeof(msg));

	while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE) > 0)
	{
		if (msg.message == WM_QUIT)
		{
			I_Quit();
		}
		TranslateMessage(&msg);
		DispatchMessageA(&msg);
	}

	StretchDIBits(s_Hdc, 0, 0, DOOMGENERIC_RESX, DOOMGENERIC_RESY, 0, 0, DOOMGENERIC_RESX, DOOMGENERIC_RESY, DG_ScreenBuffer, &s_Bmi, 0, SRCCOPY);

}

void DG_SleepMs(uint32_t ms)
{
	Sleep(ms);
}

uint32_t DG_GetTicksMs()
{
	return GetTickCount();
}

int DG_GetKey(int* pressed, unsigned char* doomKey)
{
	if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex)
	{
		//key queue is empty

		return 0;
	}
	else
	{
		unsigned short keyData = s_KeyQueue[s_KeyQueueReadIndex];
		s_KeyQueueReadIndex++;
		s_KeyQueueReadIndex %= KEYQUEUE_SIZE;

		*pressed = keyData >> 8;
		*doomKey = keyData & 0xFF;

		return 1;
	}
}

void DG_SetWindowTitle(const char * title)
{
	if (s_Hwnd)
	{
		SetWindowTextA(s_Hwnd, title);
	}
}

int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);

    for (;;)
    {
        doomgeneric_Tick();
    }
    

    return 0;
}
