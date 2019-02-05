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
#define WLINES_MARGIN 4

#define MINC(a, b) ((a) < (b) ? (a) : (b))

typedef vec_t(wchar_t) vec_wchar_t;
typedef vec_t(wchar_t*) vec_wcharp_t;

int wnd_width, wnd_height;
HFONT font = 0;
HWND main_wnd = 0;
int line_count = 5;
WNDPROC prev_edit_wndproc;

vec_wcharp_t menu_entries = { 0 };
int selected_result = -1;
vec_int_t search_results = { 0 };

char* font_name = "Arial";
int font_size = 18;
char case_insensitive_search = 0;
COLORREF clr_nrm_bg = 0x00000000, clr_nrm_fg = 0x00ffffff,
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
}

void print_as_utf8(wchar_t* utf16str)
{
    static int bufsz = 0;
    static char* buf = 0;

    int len = wcslen(utf16str);
    int bytecount = WideCharToMultiByte(CP_UTF8, 0, utf16str, len, 0, 0, 0, 0);
    if (bytecount > bufsz) {
        bufsz = bytecount;
        E(!(buf = realloc(buf, bufsz)));
    }
    WideCharToMultiByte(CP_UTF8, 0, utf16str, len, buf, bytecount, 0, 0);
    printf("%.*s\n", bytecount, buf);
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

wchar_t* get_textbox_string(HWND textbox_wnd)
{
    static int bufsz = 0;
    static wchar_t* buf = 0;

    int length = CallWindowProc(prev_edit_wndproc, textbox_wnd, EM_LINELENGTH, 0, 0);
    int bytecount = (length + 1) * sizeof(wchar_t);
    if (bytecount > bufsz) {
        bufsz = bytecount;
        E(!(buf = realloc(buf, bytecount)));
    }
    buf[0] = length; // EM_GETLINE requires this
    CallWindowProc(prev_edit_wndproc, textbox_wnd, EM_GETLINE, 0, (LPARAM)buf);
    buf[length] = 0;
    return buf;
}

void update_search_results(wchar_t* search_str)
{
    // Clear previous results
    vec_clear(&search_results);

    if (wcslen(search_str) > 0) {
        // Match entries
        if (case_insensitive_search) {
            for (int i = 0; i < menu_entries.length; i++)
                if (StrStrIW(menu_entries.data[i], search_str))
                    vec_push(&search_results, i);
        } else {
            for (int i = 0; i < menu_entries.length; i++)
                if (wcsstr(menu_entries.data[i], search_str))
                    vec_push(&search_results, i);
        }
    } else {
        for (int i = 0; i < menu_entries.length; i++)
            vec_push(&search_results, i);
    }

    selected_result = search_results.length ? 0 : -1;
}

// Window procedure for the main window's textbox
LRESULT CALLBACK edit_wndproc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_CHAR:;
        LRESULT result = 0;
        switch (wparam) {
        // Ctrl+A - Select everythinig
        case 0x01:;
            int length = CallWindowProc(prev_edit_wndproc, wnd, EM_LINELENGTH, 0, 0);
            CallWindowProc(prev_edit_wndproc, wnd, EM_SETSEL, 0, length);
            return 0;

        // Ctrl+Backspace todo: simulate proper ctrl+bksp behavior
        case 0x7f:
            CallWindowProc(prev_edit_wndproc, wnd, WM_CHAR, 0x08, 0); // Backspace
            break;

        // Tab - Autocomplete
        case 0x09:
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

        update_search_results(get_textbox_string(wnd));
        RedrawWindow(main_wnd, 0, 0, RDW_INVALIDATE);
        return result;

    case WM_KEYDOWN:
        switch (wparam) {
        // Enter - Output choice
        case VK_RETURN:
            if (selected_result < 0 || (GetKeyState(VK_SHIFT) & 0x8000)) {
                // If no results or shift is held, print input
                wchar_t* str = get_textbox_string(wnd);
                print_as_utf8(str);
            } else {
                // Else print the selected result
                print_as_utf8(menu_entries.data[search_results.data[selected_result]]);
            }
            exit(0);

        // Escape - Exit
        case VK_ESCAPE:
            exit(1);

        // Up - Previous result
        case VK_UP:
            if (selected_result > 0)
                selected_result--;
            RedrawWindow(main_wnd, 0, 0, RDW_INVALIDATE);
            return 0;

        // Down - Next result
        case VK_DOWN:
            if (selected_result + 1 < search_results.length)
                selected_result++;
            RedrawWindow(main_wnd, 0, 0, RDW_INVALIDATE);
            return 0;

        // Home - First result
        case VK_HOME:
            if (selected_result > 0)
                selected_result = 0;
            RedrawWindow(main_wnd, 0, 0, RDW_INVALIDATE);
            return 0;

        // End - Last result
        case VK_END:
            if (search_results.length)
                selected_result = search_results.length - 1;
            RedrawWindow(main_wnd, 0, 0, RDW_INVALIDATE);
            return 0;
        }
    }

    return CallWindowProc(prev_edit_wndproc, wnd, msg, wparam, lparam);
}

// Window procedure for the main window
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
            .left = WLINES_MARGIN,
            .top = font_size + WLINES_MARGIN * 2,
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
                    Rectangle(hdc, 0, text_rect.top, wnd_width, text_rect.top + font_size);
                }

                // Draw this line
                DrawTextW(hdc, menu_entries.data[search_results.data[idx]], -1, &text_rect,
                    DT_NOCLIP | DT_NOPREFIX | DT_WORDBREAK | DT_EDITCONTROL);
                text_rect.top += font_size;

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

void create_window(void)
{
    // Load font
    font = CreateFontA(font_size, 0, 0, 0,
        FW_NORMAL, 0, 0, 0, 0, 0, 0, 0x04, 0, font_name);
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
    wnd_height = font_size * (line_count + 1) + WLINES_MARGIN * 3;
    main_wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        WLINES_WND_CLASS, L"wlines", WS_POPUP,
        0, 0, wnd_width, wnd_height, 0, 0, 0, 0);
    WE(main_wnd);

    // Create textbox
    HWND textbox = CreateWindowExW(0, L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | ES_LEFT | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
        0, WLINES_MARGIN, wnd_width, font_size,
        main_wnd, (HMENU)101, 0, 0);
    WE(textbox);

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
    while (GetMessageW(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void usage(void)
{
    printf(
        "Usage: wlines.exe [-v] [-i] [-l <count>] [-nb <color>] [-nf <color>] [-sb <color>] [-sf <color>] [-fn <font>] [-fs <size>]\n"
        "Options:\n"
        "  -v              Print version information\n"
        "  -i              Case-insensitive filter\n"
        "  -l  <count>     Amount of lines to show in list\n"
        "  -nb <color>     Normal background color\n"
        "  -nf <color>     Normal foreground color\n"
        "  -sb <color>     Selected background color\n"
        "  -sf <color>     Selected foreground color\n"
        "  -fn <font>      Font name\n"
        "  -fs <size>      Font size\n"
        "\n"
        "Notes:\n"
        "  All colors are 6 digit hexadecimal\n"
        "\n");

    exit(1);
}

void version(void)
{
    printf("wlines (rev " WLINES_REVISION ")\n");
    exit(0);
}

COLORREF parse_hex(char* arg)
{
    if (strlen(arg) != 6)
        usage();

    int color = strtol(arg, 0, 16);

    // Windows' colors are reversed, swap R and B
    char* raw = (char*)&color;
    char tmp = raw[0];
    raw[0] = raw[2];
    raw[2] = tmp;

    return color;
}

int main(int argc, char** argv)
{
    // Turn off stdout buffering
    setvbuf(stdout, 0, _IONBF, 0);

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v"))
            version();
        else if (!strcmp(argv[i], "-i"))
            case_insensitive_search = 1;
        else if (i + 1 == argc)
            usage();
        // Following flags require an argument
        else if (!strcmp(argv[i], "-l")) {
            line_count = atoi(argv[++i]);
            if (line_count < 1)
                usage();
        } else if (!strcmp(argv[i], "-nb"))
            clr_nrm_bg = parse_hex(argv[++i]);
        else if (!strcmp(argv[i], "-nf"))
            clr_nrm_fg = parse_hex(argv[++i]);
        else if (!strcmp(argv[i], "-sb"))
            clr_sel_bg = parse_hex(argv[++i]);
        else if (!strcmp(argv[i], "-sf"))
            clr_sel_fg = parse_hex(argv[++i]);
        else if (!strcmp(argv[i], "-fn"))
            font_name = argv[++i];
        else if (!strcmp(argv[i], "-fs")) {
            font_size = atoi(argv[++i]);
            if (font_size < 1)
                usage();
        } else
            usage();
    }

    // Read stdin
    vec_wchar_t stdin_utf16 = { 0 };
    read_stdin_as_utf8(&stdin_utf16);

    // Read entries
    read_menu_entries(&stdin_utf16, &menu_entries);
    line_count = MINC(line_count, menu_entries.length);
    update_search_results(L"");

    // Show window
    create_window();

    return -1;
}
