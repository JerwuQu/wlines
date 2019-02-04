// Windows XP
#define WINVER 0x0501
#define _WIN32_WINNT 0x0501

// Enable unicode
#define UNICODE
#define _UNICODE
#undef _MBCS

#include "util.h"
#include "vec/vec.h"
#include <Windows.h>
#include <assert.h>
#include <shlwapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WLINES_WND_CLASS L"wlines_wnd"
#define WLINES_FONT_SIZE 18
#define WLINES_EDIT_PADDING 4
#define WLINES_LINES 5
#define WLINES_LINE_WMARGIN 6
#define WLINES_LINE_HMARGIN 4

#define MINC(a, b) ((a) < (b) ? (a) : (b))

typedef vec_t(wchar_t) vec_wchar_t;
typedef vec_t(wchar_t*) vec_wcharp_t;

static char case_insensitive_search = 0;
static char running = 1;
static HWND main_wnd = 0;
static WNDPROC prev_edit_wndproc;

static vec_wcharp_t menu_entries = { 0 };
static int selected_result = -1;
static vec_int_t search_results = { 0 };

static int line_count = 0;
static int wnd_width, wnd_height;
static HFONT font = 0;
static COLORREF clr_nrm_bg = 0x00000000, clr_nrm_fg = 0x00ffffff,
                clr_sel_bg = 0x00ffffff, clr_sel_fg = 0x00000000;

void read_stdin_as_utf8(vec_wchar_t* utf16vec)
{
    // Read stdin as utf8
    size_t len;
    char buf[128];
    vec_char_t stdin_utf8 = { 0 };
    while ((len = fread(buf, 1, sizeof(buf), stdin)))
        vec_pusharr(&stdin_utf8, buf, len);
    E(vec_push(&stdin_utf8, 0));

    // Convert to utf16
    int charcount = MultiByteToWideChar(CP_UTF8, 0, stdin_utf8.data, stdin_utf8.length, 0, 0);
    vec_reserve(utf16vec, charcount * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, stdin_utf8.data, stdin_utf8.length, utf16vec->data, charcount);
    utf16vec->length = wcslen(utf16vec->data);

    // Free unused utf8 buffer
    vec_deinit(&stdin_utf8);
}

void print_as_utf8(wchar_t* utf16str)
{
    int len = wcslen(utf16str);
    int bytecount = WideCharToMultiByte(CP_UTF8, 0, utf16str, len, 0, 0, 0, 0);
    char* buf = malloc(bytecount);
    E(!buf);
    WideCharToMultiByte(CP_UTF8, 0, utf16str, len, buf, bytecount, 0, 0);
    printf("%.*s\n", bytecount, buf);
    free(buf);
}

void read_menu_entries(vec_wchar_t* str_vec, vec_wcharp_t* entries_out)
{
    wchar_t* current = str_vec->data;
    for (int i = 0; i < str_vec->length; i++) {
        if (str_vec->data[i] == '\n') {
            str_vec->data[i] = 0;
            E(vec_push(entries_out, current));
            current = &str_vec->data[i + 1];
        }
    }

    if (current < str_vec->data + str_vec->length) {
        E(vec_push(entries_out, current));
    }
}

LRESULT CALLBACK wndproc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    HDC hdc;
    switch (msg) {
    case WM_PAINT:;
        // Begin
        PAINTSTRUCT ps = { 0 };
        hdc = BeginPaint(wnd, &ps);

        // Select objects
        SelectObject(hdc, GetStockObject(DC_PEN));
        SelectObject(hdc, GetStockObject(DC_BRUSH));
        SelectObject(hdc, font);

        // Clear
        SetDCPenColor(hdc, clr_nrm_bg);
        SetDCBrushColor(hdc, clr_nrm_bg);
        Rectangle(hdc, 0, 0, wnd_width, wnd_height);

        // Set text color
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, clr_nrm_fg);

        // Calculate rects
        RECT text_rect = {
            .left = WLINES_LINE_WMARGIN,
            .top = WLINES_FONT_SIZE + WLINES_EDIT_PADDING + WLINES_LINE_HMARGIN,
            .right = wnd_width,
            .bottom = wnd_height
        };

        // Draw texts
        if (search_results.length) {
            int page = selected_result / line_count;
            int start = page * line_count;
            int count = MINC(line_count, search_results.length - start);
            for (int idx = start; idx < start + count; idx++) {
                // Set text color and color background
                if (idx == selected_result) {
                    SetTextColor(hdc, clr_sel_fg);
                    SetDCPenColor(hdc, clr_sel_bg);
                    SetDCBrushColor(hdc, clr_sel_bg);
                    Rectangle(hdc, 0, text_rect.top, wnd_width, text_rect.top + WLINES_FONT_SIZE);
                }

                // Draw this line
                DrawTextW(hdc, menu_entries.data[search_results.data[idx]], -1, &text_rect,
                    DT_NOCLIP | DT_NOPREFIX | DT_WORDBREAK | DT_EDITCONTROL);
                text_rect.top += WLINES_FONT_SIZE;

                // Reset text colors
                if (idx == selected_result)
                    SetTextColor(hdc, clr_nrm_fg);
            }
        }

        // End
        EndPaint(wnd, &ps);
        return 0;
    case WM_CTLCOLOREDIT:;
        hdc = (HDC)wparam;
        SetTextColor(hdc, clr_nrm_fg);
        SetBkColor(hdc, clr_nrm_bg);
        SetDCBrushColor(hdc, clr_nrm_bg);
        return (LRESULT)GetStockObject(DC_BRUSH);
    }

    return DefWindowProc(wnd, msg, wparam, lparam);
}

wchar_t* get_textbox_string(HWND textbox_wnd)
{
    int length = CallWindowProc(prev_edit_wndproc, textbox_wnd, EM_LINELENGTH, 0, 0);
    wchar_t* str = malloc((length + 1) * sizeof(wchar_t));
    E(!str);
    str[0] = length; // EM_GETLINE requires this
    CallWindowProc(prev_edit_wndproc, textbox_wnd, EM_GETLINE, 0, (LPARAM)str);
    str[length] = 0;
    return str;
}

void update_search_results(HWND textbox_wnd)
{
    // Clear previous results
    vec_clear(&search_results);

    // Match new ones
    wchar_t* search_str = get_textbox_string(textbox_wnd);
    if (case_insensitive_search) {
        for (int i = 0; i < menu_entries.length; i++)
            if (StrStrIW(menu_entries.data[i], search_str))
                vec_push(&search_results, i);
    } else {
        for (int i = 0; i < menu_entries.length; i++)
            if (wcsstr(menu_entries.data[i], search_str))
                vec_push(&search_results, i);
    }

    free(search_str);

    // Set selected result
    selected_result = search_results.length ? 0 : -1;
}

LRESULT CALLBACK edit_wndproc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_CHAR:;
        LRESULT result = 0;
        switch (wparam) {
        case 0x01:; // Ctrl+A - Select everythinig
            int length = CallWindowProc(prev_edit_wndproc, wnd, EM_LINELENGTH, 0, 0);
            result = CallWindowProc(prev_edit_wndproc, wnd, EM_SETSEL, 0, length);
            return 0;
        case 0x7f: // Ctrl+Backspace
            // todo: Simulate proper behavior
            result = CallWindowProc(prev_edit_wndproc, wnd, WM_CHAR, 0x08, 0); // Backspace
            break;
        case 0x09: // Tab - Autocomplete
            if (selected_result >= 0) {
                wchar_t* str = menu_entries.data[search_results.data[selected_result]];
                int length = wcslen(str);
                SetWindowTextW(wnd, str);
                CallWindowProc(prev_edit_wndproc, wnd, EM_SETSEL, length, length);
            }
            break;
        default:
            result = CallWindowProc(prev_edit_wndproc, wnd, msg, wparam, lparam);
        }
        update_search_results(wnd);
        RedrawWindow(main_wnd, 0, 0, RDW_INVALIDATE);
        return result;
    case WM_KEYDOWN:
        switch (wparam) {
        case VK_RETURN: // Enter - Output choice
            if (selected_result < 0 || (GetKeyState(VK_SHIFT) & 0x8000)) {
                // If no results or shift is held, print input
                wchar_t* str = get_textbox_string(wnd);
                print_as_utf8(str);
                free(str);
            } else {
                // Else print the selected result
                print_as_utf8(menu_entries.data[search_results.data[selected_result]]);
            }
        case VK_ESCAPE: // Escape - Exit
            running = 0;
            return 0;
        case VK_UP: // Up - Previous result
            if (selected_result > 0)
                selected_result--;
            RedrawWindow(main_wnd, 0, 0, RDW_INVALIDATE);
            return 0;
        case VK_DOWN: // Down - Next result
            if (selected_result + 1 < search_results.length)
                selected_result++;
            RedrawWindow(main_wnd, 0, 0, RDW_INVALIDATE);
            return 0;
        }
    }

    return CallWindowProc(prev_edit_wndproc, wnd, msg, wparam, lparam);
}

void create_window(void)
{
    // Load font
    font = CreateFontW(WLINES_FONT_SIZE, 0, 0, 0,
        FW_NORMAL, 0, 0, 0, 0, 0, 0, 0x04, 0, L"Arial");
    WE(font);

    // Register window class
    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = wndproc;
    wc.lpszClassName = WLINES_WND_CLASS;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    WE(RegisterClassExW(&wc));

    // Create window
    wnd_width = GetSystemMetrics(SM_CXSCREEN); // Display width
    wnd_height = WLINES_FONT_SIZE * (line_count + 1) + WLINES_EDIT_PADDING + WLINES_LINE_HMARGIN * 2;
    main_wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        WLINES_WND_CLASS, L"wlines", 0,
        0, 0, wnd_width, wnd_height, 0, 0, 0, 0);
    WE(main_wnd);

    // Create textbox
    HWND textbox = CreateWindowExW(0, L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | WS_BORDER | WS_TABSTOP | ES_LEFT | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
        0, 0, wnd_width, WLINES_FONT_SIZE + WLINES_EDIT_PADDING,
        main_wnd, (HMENU)101, 0, 0);

    SendMessage(textbox, WM_SETFONT, (WPARAM)font, MAKELPARAM(1, 0));
    prev_edit_wndproc = (WNDPROC)SetWindowLongPtr(textbox, GWL_WNDPROC, (LONG_PTR)&edit_wndproc);

    // Remove default window styling
    LONG lStyle = GetWindowLong(main_wnd, GWL_STYLE);
    lStyle &= ~WS_OVERLAPPEDWINDOW;
    SetWindowLong(main_wnd, GWL_STYLE, lStyle);

    // Show window
    ShowWindow(main_wnd, SW_SHOW);
    WE(UpdateWindow(main_wnd));
    SetForegroundWindow(main_wnd);
    SetFocus(textbox);

    // Event loop
    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0) && running) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void destroy_window(void)
{
    // todo
}

void usage(void)
{
    printf("Usage: wlines.exe [-i]\n");
    printf("Options:\n");
    printf("  -i              Case-insensitive filter\n");
    printf("\n");
    exit(1);
}

int main(int argc, char** argv)
{
    // Make sure unicode is supported
    assert(sizeof(TEXT("U")) == 4);

    // Turn off stdout buffering
    setvbuf(stdout, 0, _IONBF, 0);

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-i")) {
            case_insensitive_search = 1;
        } else {
            usage();
        }
    }

    // Read stdin
    vec_wchar_t stdin_utf16 = { 0 };
    read_stdin_as_utf8(&stdin_utf16);

    // Read entries
    read_menu_entries(&stdin_utf16, &menu_entries);

    // Set line count
    line_count = MINC(WLINES_LINES, menu_entries.length);

    // Set initial search results (everything)
    for (int i = 0; i < menu_entries.length; i++)
        vec_push(&search_results, i);

    if (search_results.length)
        selected_result = 0;

    // Show window
    create_window();
    destroy_window();

    // Clean up
    vec_deinit(&stdin_utf16);
    vec_deinit(&menu_entries);
    vec_deinit(&search_results);

    return 0;
}
