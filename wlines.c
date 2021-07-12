// Windows XP
#define WINVER 0x0501
#define _WIN32_WINNT 0x0501

// Unicode
#define UNICODE
#define _UNICODE
#undef _MBCS

// Use MinGW STDIO implementations
#define __USE_MINGW_ANSI_STDIO

#include <windows.h>
#include <windowsx.h>
#include <shlwapi.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define ASSERT_WIN32_RESULT(result) do { \
		if (!(result)) { \
			fprintf(stderr, "Windows error %ld on line %d\n", GetLastError(), __LINE__); \
			exit(1); \
		} \
	} while (0);

#define FOREGROUND_TIMER_ID 1
#define SELECTED_INDEX_NO_RESULT ((size_t)-1)
#define DRAWTEXT_PARAMS (DT_NOCLIP | DT_NOPREFIX | DT_END_ELLIPSIS)
#define FONT_HMARGIN(sz) (int)(state->settings.fontSize / 6)

typedef struct {
	wchar_t *wndClass;
	int padding;
	char *fontName;
	char *promptText;
	int fontSize;
	bool caseSensitiveSearch;
	bool outputIndex;
	COLORREF bg, fg, bgSelect, fgSelect, bgEdit, fgEdit;
	int lineCount;
	int selectedIndex;
	size_t width;
	bool centerWindow;
} settings_t;

typedef struct {
	settings_t settings;
	HFONT font;
	HWND mainWnd, editWnd;
	WNDPROC editWndProc;
	size_t width, height;
	size_t lineCount;
	bool hadForeground;
	size_t promptWidth;

	size_t entryCount;
	wchar_t **entries;
	size_t searchResultCount;
	size_t *searchResults; // index into `entries`
	size_t selectedResultIndex; // index into `searchResults`
} state_t;

typedef struct {
	void *data;
	size_t count, cap;
} buf_t;

void *xrealloc(void *ptr, size_t sz)
{
	ptr = realloc(ptr, sz);
	if (!ptr) {
		fprintf(stderr, "Out of memory\n");
		exit(1);
	}
	return ptr;
}

void bufEnsure(buf_t *buf, size_t sz)
{
	if (buf->cap < sz) {
		if (!buf->cap) {
			buf->cap = 1024;
		}
		while (buf->cap < sz) {
			buf->cap <<= 1;
		}
		buf->data = xrealloc(buf->data, buf->cap);
	}
}

void *bufAdd(buf_t *buf, size_t sz)
{
	bufEnsure(buf, buf->count + sz);
	buf->count += sz;
	return ((char*)buf->data) + (buf->count - sz);
}

void bufShrink(buf_t *buf)
{
	buf->cap = buf->count;
	buf->data = xrealloc(buf->data, buf->cap);
}

void windowEventLoop()
{
	MSG msg;
	while (GetMessageW(&msg, 0, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
}

void printUtf16AsUtf8(wchar_t *data)
{
	static buf_t buf = { 0 };
	const size_t len = wcslen(data);
	const int bytecount = WideCharToMultiByte(CP_UTF8, 0, data, len, 0, 0, 0, 0);
	bufEnsure(&buf, bytecount);
	WideCharToMultiByte(CP_UTF8, 0, data, len, buf.data, bytecount, 0, 0);
	printf("%.*s\n", bytecount, (char*)buf.data);
}

wchar_t *getTextboxString(state_t *state)
{
	static buf_t buf = { 0 };
	const size_t length = CallWindowProc(state->editWndProc, state->editWnd, EM_LINELENGTH, 0, 0);
	bufEnsure(&buf, (length + 1) * sizeof(wchar_t));
	((wchar_t*)buf.data)[0] = length; // EM_GETLINE requires this
	CallWindowProc(state->editWndProc, state->editWnd, EM_GETLINE, 0, (LPARAM)buf.data);
	((wchar_t*)buf.data)[length] = 0; // null term
	return buf.data;
}

void updateSearchResults(state_t *state)
{
	const wchar_t *str = getTextboxString(state);
	state->searchResultCount = 0;
	if (wcslen(str) > 0) {
		if (state->settings.caseSensitiveSearch) {
			for (size_t i = 0; i < state->entryCount; i++) {
				if (wcsstr(state->entries[i], str)) {
					state->searchResults[state->searchResultCount++] = i;
				}
			}
		} else {
			for (size_t i = 0; i < state->entryCount; i++) {
				if (StrStrIW(state->entries[i], str)) {
					state->searchResults[state->searchResultCount++] = i;
				}
			}
		}
	} else {
		for (size_t i = 0; i < state->entryCount; i++) {
			state->searchResults[state->searchResultCount++] = i;
		}
	}

	state->selectedResultIndex = state->searchResultCount > 0 ? 0 : SELECTED_INDEX_NO_RESULT;

	RedrawWindow(state->mainWnd, 0, 0, RDW_INVALIDATE);
}

LRESULT CALLBACK editWndProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	state_t *state = (state_t*)GetWindowLongPtrA(wnd, GWLP_USERDATA);
	if (!state) {
		return DefWindowProc(wnd, msg, wparam, lparam);
	}

	switch (msg) {
	case WM_KILLFOCUS: // When focus is lost
		exit(1);
	case WM_CHAR:; // When a character is written
		LRESULT result = 0;
		switch (wparam) {
		case 0x01:; // Ctrl+A - Select everythinig
			const size_t length = CallWindowProc(state->editWndProc, wnd, EM_LINELENGTH, 0, 0);
			CallWindowProc(state->editWndProc, wnd, EM_SETSEL, 0, length);
			return 0;
		case 0x7f:; // Ctrl+Backspace - Simulate traditional behavior
			int start_sel = 0, end_sel = 0;
			CallWindowProc(state->editWndProc, wnd, EM_GETSEL, 0, (LPARAM)&end_sel);
			CallWindowProc(state->editWndProc, wnd, WM_KEYDOWN, VK_LEFT, 0);
			CallWindowProc(state->editWndProc, wnd, WM_KEYUP, VK_LEFT, 0);
			CallWindowProc(state->editWndProc, wnd, EM_GETSEL, (WPARAM)&start_sel, 0);
			CallWindowProc(state->editWndProc, wnd, EM_SETSEL, start_sel, end_sel);
			CallWindowProc(state->editWndProc, wnd, WM_CHAR, 0x08, 0); // Backspace
			break;
		case 0x09: // Tab - Autocomplete
			if (state->selectedResultIndex != SELECTED_INDEX_NO_RESULT) {
				const wchar_t *str = state->entries[state->searchResults[state->selectedResultIndex]];
				const size_t length = wcslen(str);
				SetWindowTextW(wnd, str);
				CallWindowProc(state->editWndProc, wnd, EM_SETSEL, length, length);
			}
			break;
		// Return - Ignore (handled in WM_KEYDOWN)
		case 0x0a:
		case 0x0d:
			return 0;
		default:
			result = CallWindowProc(state->editWndProc, wnd, msg, wparam, lparam);
		}
		updateSearchResults(state);
		return result;
	case WM_KEYDOWN: // When a key is pressed
		switch (wparam) {
		case VK_RETURN: // Enter - Output choice
			// If no results or shift is held: print input, else: print result
			if (state->selectedResultIndex == SELECTED_INDEX_NO_RESULT || (GetKeyState(VK_SHIFT) & 0x8000)) {
				if (state->settings.outputIndex) {
					printf("-1\n");
				} else {
					printUtf16AsUtf8(getTextboxString(state));
				}
			} else {
				if (state->settings.outputIndex) {
					printf("%zu\n", state->searchResults[state->selectedResultIndex]);
				} else {
					printUtf16AsUtf8(state->entries[state->searchResults[state->selectedResultIndex]]);
				}
			}

			// Quit if control isn't held
			if (!(GetKeyState(VK_CONTROL) & 0x8000)) {
				exit(0);
			}
			return 0;
		case VK_ESCAPE: // Escape - Exit
			exit(1);
		case VK_UP: // Up - Previous result
			if (state->selectedResultIndex > 0) {
				state->selectedResultIndex--;
				RedrawWindow(state->mainWnd, 0, 0, RDW_INVALIDATE);
			}
			return 0;
		case VK_DOWN: // Down - Next result
			if (state->selectedResultIndex + 1 < state->searchResultCount) {
				state->selectedResultIndex++;
				RedrawWindow(state->mainWnd, 0, 0, RDW_INVALIDATE);
			}
			return 0;
		case VK_HOME: // Home - First result
			if (state->selectedResultIndex > 0) {
				state->selectedResultIndex = 0;
				RedrawWindow(state->mainWnd, 0, 0, RDW_INVALIDATE);
			}
			return 0;
		case VK_END: // End - Last result
			if (state->selectedResultIndex + 1 < state->searchResultCount) {
				state->selectedResultIndex = state->searchResultCount - 1;
				RedrawWindow(state->mainWnd, 0, 0, RDW_INVALIDATE);
			}
			return 0;
		case VK_PRIOR: // Page Up - Previous page
			if (state->selectedResultIndex > 0) {
				const ssize_t n = (state->selectedResultIndex / state->lineCount - 1) * state->lineCount;
				state->selectedResultIndex = max(0, n);
				RedrawWindow(state->mainWnd, 0, 0, RDW_INVALIDATE);
			}
			return 0;
		case VK_NEXT: // Page Down - Next page
			if (state->selectedResultIndex + 1 < state->searchResultCount) {
				const size_t n = (state->selectedResultIndex / state->lineCount + 1) * state->lineCount;
				state->selectedResultIndex = min(state->searchResultCount - 1, n);
				RedrawWindow(state->mainWnd, 0, 0, RDW_INVALIDATE);
			}
			return 0;
		}
	}

	return CallWindowProc(state->editWndProc, wnd, msg, wparam, lparam);
}

void forceForeground(HWND hwnd)
{
	// Use trick from https://stackoverflow.com/a/59659421
	const DWORD foregroundThreadId = GetWindowThreadProcessId(GetForegroundWindow(), 0);
	const DWORD currentThreadId = GetCurrentThreadId();
	AttachThreadInput(foregroundThreadId, currentThreadId, true);
	BringWindowToTop(hwnd);
	ShowWindow(hwnd, SW_SHOW);
	SetForegroundWindow(hwnd);
	AttachThreadInput(foregroundThreadId, currentThreadId, false);
}

LRESULT CALLBACK mainWndProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	state_t *state = (state_t*)GetWindowLongPtrA(wnd, GWLP_USERDATA);
	if (!state) {
		return DefWindowProc(wnd, msg, wparam, lparam);
	}

	const int entriesTop = state->settings.fontSize + state->settings.padding;
	const size_t page = state->lineCount ? (state->selectedResultIndex / state->lineCount) : 0;
	const size_t pageStartI = page * state->lineCount;

	switch (msg) {
	case WM_TIMER: // Repeating timer to make sure we're the foreground window
		if (wparam == FOREGROUND_TIMER_ID) {
			if (GetForegroundWindow() == wnd) {
				state->hadForeground = true;
			} else if (state->hadForeground) {
				exit(1);
			} else {
				forceForeground(state->mainWnd);
			}
		}
		break;
	case WM_PAINT:; // Paint window
		// Begin
		PAINTSTRUCT ps = { 0 };
		HDC real_hdc = BeginPaint(wnd, &ps);

		// Use a draw buffer device
		static HDC bfhdc = 0;
		static HBITMAP buffer_bitmap = 0;
		if (!bfhdc) {
			// Create
			ASSERT_WIN32_RESULT(bfhdc = CreateCompatibleDC(real_hdc));
			ASSERT_WIN32_RESULT(buffer_bitmap = CreateCompatibleBitmap(real_hdc, state->width, state->height));

			// Setup
			SelectObject(bfhdc, buffer_bitmap);
			SelectObject(bfhdc, state->font);
			SelectObject(bfhdc, GetStockObject(DC_PEN));
			SelectObject(bfhdc, GetStockObject(DC_BRUSH));
			SetBkMode(bfhdc, TRANSPARENT);
		}

		// Clear window
		SetDCPenColor(bfhdc, state->settings.bg);
		SetDCBrushColor(bfhdc, state->settings.bg);
		Rectangle(bfhdc, 0, 0, state->width, state->height);

		// Draw prompt
		if (state->settings.promptText) {
			RECT promptRect = {
				.left = state->settings.padding + FONT_HMARGIN(state->settings.fontSize),
				.top = state->settings.padding,
				.right = state->width / 2 - FONT_HMARGIN(state->settings.fontSize),
				.bottom = state->settings.padding + state->settings.fontSize * 2,
			};

			SetDCPenColor(bfhdc, state->settings.bgSelect);
			SetDCBrushColor(bfhdc, state->settings.bgSelect);
			Rectangle(bfhdc, state->settings.padding, promptRect.top,
					state->settings.padding + state->promptWidth,
					promptRect.top + state->settings.fontSize);

			SetTextColor(bfhdc, state->settings.fgSelect);
			DrawTextA(bfhdc, state->settings.promptText, -1, &promptRect, DRAWTEXT_PARAMS);
		}

		// Draw texts
		RECT textRect = {
			.left = state->settings.padding + FONT_HMARGIN(state->settings.fontSize),
			.top = entriesTop,
			.right = state->width - state->settings.padding - FONT_HMARGIN(state->settings.fontSize),
			.bottom = state->height,
		};
		SetTextColor(bfhdc, state->settings.fg);
		const size_t count = min(state->lineCount, state->searchResultCount - pageStartI);
		for (size_t idx = pageStartI; idx < pageStartI + count; idx++) {
			// Set text color and color background
			if (idx == state->selectedResultIndex) {
				SetDCPenColor(bfhdc, state->settings.bgSelect);
				SetDCBrushColor(bfhdc, state->settings.bgSelect);
				Rectangle(bfhdc, state->settings.padding, textRect.top,
						state->width - state->settings.padding,
						textRect.top + state->settings.fontSize);
				SetTextColor(bfhdc, state->settings.fgSelect);
			}

			// Draw this line
			DrawTextW(bfhdc, state->entries[state->searchResults[idx]], -1,
					&textRect, DRAWTEXT_PARAMS);
			textRect.top += state->settings.fontSize;

			// Reset text colors
			if (idx == state->selectedResultIndex) {
				SetTextColor(bfhdc, state->settings.fg);
			}
		}

		// Blit
		BitBlt(real_hdc, 0, 0, state->width, state->height, bfhdc, 0, 0, SRCCOPY);

		// End
		EndPaint(wnd, &ps);
		return 0;
	case WM_CTLCOLOREDIT:; // Textbox colors
		HDC hdc = (HDC)wparam;
		SetTextColor(hdc, state->settings.fgEdit);
		SetBkColor(hdc, state->settings.bgEdit);
		SetDCBrushColor(hdc, state->settings.bgEdit);
		return (LRESULT)GetStockObject(DC_BRUSH);
	case WM_CLOSE:
		exit(1);
	case WM_LBUTTONDOWN:;
		const int mx = GET_Y_LPARAM(lparam);
		if (mx < entriesTop) {
			return 0;
		}
		const size_t newIdx = max(0, min(state->searchResultCount - 1,
				pageStartI + (mx - entriesTop) / state->settings.fontSize));
		if (newIdx == state->selectedResultIndex) {
			if (state->settings.outputIndex) {
				printf("%zu\n", state->searchResults[state->selectedResultIndex]);
			} else {
				printUtf16AsUtf8(state->entries[state->searchResults[state->selectedResultIndex]]);
			}

			// Quit if control isn't held
			if (!(GetKeyState(VK_CONTROL) & 0x8000)) {
				exit(0);
			}
		} else {
			state->selectedResultIndex = newIdx;
			RedrawWindow(state->mainWnd, 0, 0, RDW_INVALIDATE);
		}
		return 0;
	case WM_MOUSEWHEEL:;
		const int ydelta = GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
		state->selectedResultIndex = max(0,
				min((ssize_t)(state->searchResultCount - 1),
				(ssize_t)(state->selectedResultIndex - ydelta)));
		RedrawWindow(state->mainWnd, 0, 0, RDW_INVALIDATE);
		return 0;
	}

	return DefWindowProc(wnd, msg, wparam, lparam);
}


void createWindow(state_t *state)
{
	// Register window class
	WNDCLASSEXW wc = { 0 };
	wc.cbSize = sizeof(WNDCLASSEXW);
	wc.lpfnWndProc = mainWndProc;
	wc.lpszClassName = state->settings.wndClass;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	ASSERT_WIN32_RESULT(RegisterClassExW(&wc));

	// Create window
	const int displayWidth = GetSystemMetrics(SM_CXSCREEN);
	const int displayHeight = GetSystemMetrics(SM_CYSCREEN);

	if (state->settings.width) {
		state->width = state->settings.width;
	} else {
		state->width = displayWidth;
	}
	state->height = state->settings.fontSize * (state->lineCount + 1)
			+ state->settings.padding * 2;

	int x = 0, y = 0;
	if (state->settings.centerWindow) {
		x = (displayWidth - state->width) / 2;
		y = (displayHeight - state->height) / 2;
	}

	state->mainWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
			state->settings.wndClass, L"wlines", WS_POPUP,
			x, y, state->width, state->height, 0, 0, 0, 0);
	ASSERT_WIN32_RESULT(state->mainWnd);

	// Calculate prompt width
	if (state->settings.promptText) {
		RECT promptRect = {
			.right = state->width / 2 - state->settings.padding,
			.bottom = state->settings.fontSize * 2,
		};
		const HDC tmpHDC = CreateCompatibleDC(NULL);
		SelectObject(tmpHDC, state->font);
		DrawTextA(tmpHDC, state->settings.promptText, -1,
				&promptRect, DRAWTEXT_PARAMS | DT_CALCRECT);
		DeleteDC(tmpHDC);
		state->promptWidth = promptRect.right - promptRect.left
				+ FONT_HMARGIN(state->settings.fontSize) * 2;
	}

	// Create textbox
	const size_t textboxLeft = state->settings.padding + state->promptWidth;
	state->editWnd = CreateWindowExW(0, L"EDIT", L"",
		WS_VISIBLE | WS_CHILD | ES_LEFT | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
		textboxLeft, state->settings.padding,
		state->width - textboxLeft - state->settings.padding, state->settings.fontSize,
		state->mainWnd, (HMENU)101, 0, 0);
	ASSERT_WIN32_RESULT(state->editWnd);

	SendMessage(state->editWnd, WM_SETFONT, (WPARAM)state->font, MAKELPARAM(1, 0));
	SendMessage(state->editWnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
			MAKELPARAM(FONT_HMARGIN(state->settings.fontSize),
				FONT_HMARGIN(state->settings.fontSize)));
	state->editWndProc = (WNDPROC)SetWindowLongPtr(state->editWnd, GWLP_WNDPROC, (LONG_PTR)&editWndProc);

	// Add state pointer
	SetWindowLongPtrA(state->mainWnd, GWLP_USERDATA, (LONG_PTR)state);
	SetWindowLongPtrA(state->editWnd, GWLP_USERDATA, (LONG_PTR)state);

	// Remove default window styling
	LONG lStyle = GetWindowLong(state->mainWnd, GWL_STYLE);
	lStyle &= ~WS_OVERLAPPEDWINDOW;
	SetWindowLong(state->mainWnd, GWL_STYLE, lStyle);

	// Show and attempt to focus window
	ASSERT_WIN32_RESULT(UpdateWindow(state->mainWnd));
	forceForeground(state->mainWnd);
	SetFocus(state->editWnd);

	// Start foreground timer
	SetTimer(state->mainWnd, FOREGROUND_TIMER_ID, 50, 0);
}

void parseStdinEntries(state_t *state)
{
	// Read utf8 stdin
	char buf[1024];
	buf_t stdinUtf8 = { 0 };
	size_t lineLen;
	while ((lineLen = fread(buf, 1, sizeof(buf), stdin))) {
		memcpy(bufAdd(&stdinUtf8, lineLen), buf, lineLen);
	}

	// Convert to utf16
	const size_t charCount = MultiByteToWideChar(CP_UTF8, 0, stdinUtf8.data, stdinUtf8.count, 0, 0);
	wchar_t *stdinUtf16 = xrealloc(0, charCount * 2 + 1);
	memset(stdinUtf16, 0, charCount * 2 + 1);
	MultiByteToWideChar(CP_UTF8, 0, stdinUtf8.data, stdinUtf8.count, stdinUtf16, charCount);
	free(stdinUtf8.data);

	// Read menu entries
	state->entryCount = 0;
	buf_t entryBuf = { 0 };
	size_t lineStartI = 0;
	for (size_t i = 0; i < charCount; i++) {
		if (stdinUtf16[i] == '\n' || i == charCount - 1) {
			bufAdd(&entryBuf, sizeof(wchar_t*));
			((wchar_t**)entryBuf.data)[state->entryCount] = &stdinUtf16[lineStartI];
			stdinUtf16[i + (stdinUtf16[i] != '\n')] = 0; // set null terminator
			lineStartI = i + 1;
			state->entryCount++;
		}
	}
	bufShrink(&entryBuf);
	state->entries = entryBuf.data;

	// Alloc result array
	state->searchResults = xrealloc(0, state->entryCount * sizeof(size_t));
}

void loadFont(state_t *state)
{
	state->font = CreateFontA(state->settings.fontSize, 0, 0, 0,
		FW_NORMAL, 0, 0, 0, 0, 0, 0, 0x04, 0, state->settings.fontName);
	ASSERT_WIN32_RESULT(state->font);
}

COLORREF parseColor(char *str)
{
	if (str[0] == '#') {
		str++;
	}

	if (strlen(str) != 6) {
		fprintf(stderr, "Invalid color format, expected 6 digit hexadecimal.\n");
		exit(1);
	}

	// Windows colors are BGR, swap R and B
	int color = strtol(str, 0, 16);
	char* raw = (char*)&color;
	const char tmp = raw[0];
	raw[0] = raw[2];
	raw[2] = tmp;

	return color;
}

void usage()
{
	fprintf(stderr,
		"wlines " WLINES_VERSION "\n"
		"\n"
		"USAGE:\n"
		"\twlines.exe [FLAGS] [OPTIONS]\n"
		"\n"
		"FLAGS:\n"
		"\t-h    Show help and exit\n"
		"\t-cs   Case-sensitive filter\n"
		"\t-id   Output index of the selected line, or -1 when no match\n"
		"\n"
		"OPTIONS:\n"
		"\t-l    <count>   Amount of lines to show in list\n"
		"\t-p    <text>    Prompt to show before input\n"
		"\t-si   <index>   Initial selected line index\n"
		"\t-px   <pixels>  Sets padding on window\n"
		"\t-wx   <pixels>  Sets width of the window and centers it on the screen\n"
		"\t-bg   <hex>     Background color\n"
		"\t-fg   <hex>     Foreground color\n"
		"\t-sbg  <hex>     Selected background color\n"
		"\t-sfg  <hex>     Selected foreground color\n"
		"\t-tbg  <hex>     Text input background color\n"
		"\t-tfg  <hex>     Text input foreground color\n"
		"\t-f    <font>    Font name\n"
		"\t-fs   <size>    Font size\n"
		"\n"
		"KEYBINDS:\n"
		"\tEnter           Output selected line\n"
		"\t[HELD] Ctrl     Don't quit after outputting\n"
		"\t[HELD] Shift    Output entered text, ignoring the selected line\n"
		"\tEscape          Exit without outputting anything\n"
		"\n"
		"\tArrow Up/Down   Select line\n"
		"\tPage Up/Down    Jump pages\n"
		"\tHome/End        Jump to first/last line\n"
		"\n"
		"\tMouse Click     Select or output line\n"
		"\tMouse Scroll    Select line\n"
		"\n");
	exit(1);
}

int main(int argc, char **argv)
{
	// Turn off stdout buffering
	setvbuf(stdout, 0, _IONBF, 0);

	// Init state with default settings
	state_t state = {
		.settings = {
			.wndClass = L"wlines_window",
			.padding = 4,
			.caseSensitiveSearch = false,
			.bg = parseColor("#000000"),
			.fg = parseColor("#ffffff"),
			.bgSelect = parseColor("#ffffff"),
			.fgSelect = parseColor("#000000"),
			.bgEdit = parseColor("#111111"),
			.fgEdit = parseColor("#ffffff"),
			.fontName = "Courier New",
			.fontSize = 24,
			.lineCount = 15,
		},
	};

	// Parse arguments
	for (int i = 1; i < argc; i++) {
		// Flags
		if (!strcmp(argv[i], "-h")) {
			usage();
		} else if (!strcmp(argv[i], "-cs")) {
			state.settings.caseSensitiveSearch = true;
		} else if (!strcmp(argv[i], "-id")) {
			state.settings.outputIndex = true;
		} else if (i + 1 == argc) {
			usage();
		// Options
		} else if (!strcmp(argv[i], "-l")) {
			state.settings.lineCount = atoi(argv[++i]);
			if (state.settings.lineCount < 1) {
				usage();
			}
		} else if (!strcmp(argv[i], "-p")) {
			// TODO: encoding for windows arugments is strange
			//       look into using `wmain` or `GetCommandLineW`
			state.settings.promptText = argv[++i];
		} else if (!strcmp(argv[i], "-si")) {
			state.settings.selectedIndex = atoi(argv[++i]);
			if (state.settings.selectedIndex < 0) {
				usage();
			}
		} else if (!strcmp(argv[i], "-px")) {
			state.settings.padding = atoi(argv[++i]);
			if (state.settings.padding < 0) {
				usage();
			}
		} else if (!strcmp(argv[i], "-wx")) {
			state.settings.width = atoi(argv[++i]);
			state.settings.centerWindow = true;
			if (state.settings.width < 1) {
				usage();
			}
		} else if (!strcmp(argv[i], "-bg")) {
			state.settings.bg = parseColor(argv[++i]);
		} else if (!strcmp(argv[i], "-fg")) {
			state.settings.fg = parseColor(argv[++i]);
		} else if (!strcmp(argv[i], "-sbg")) {
			state.settings.bgSelect = parseColor(argv[++i]);
		} else if (!strcmp(argv[i], "-sfg")) {
			state.settings.fgSelect = parseColor(argv[++i]);
		} else if (!strcmp(argv[i], "-tbg")) {
			state.settings.bgEdit = parseColor(argv[++i]);
		} else if (!strcmp(argv[i], "-tfg")) {
			state.settings.fgEdit = parseColor(argv[++i]);
		} else if (!strcmp(argv[i], "-f")) {
			state.settings.fontName = argv[++i];
		} else if (!strcmp(argv[i], "-fs")) {
			state.settings.fontSize = atoi(argv[++i]);
			if (state.settings.fontSize < 1) {
				usage();
			}
		} else {
			usage();
		}
	}

	loadFont(&state);
	parseStdinEntries(&state);
	state.lineCount = min((size_t)state.settings.lineCount, state.entryCount);
	createWindow(&state);
	updateSearchResults(&state);
	state.selectedResultIndex = min((size_t)state.settings.selectedIndex, state.entryCount - 1);
	windowEventLoop();

	return 1;
}
