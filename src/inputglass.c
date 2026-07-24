#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601

#include <windows.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "resource.h"
#include "version.h"

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#define APP_TITLE L"InputGlass"
#define TIMER_ID 1
#define TIMER_INTERVAL_MS 16
#define LOG_FLUSH_INTERVAL_MS 250
#define LOG_BUFFER_CAPACITY (64U * 1024U)
#define KEY_COUNT 257
#define KEY_NUMPAD_ENTER 256
#define EVENT_CAPACITY 512
#define PAD_COUNT 4
#define MOUSE_BUTTON_COUNT 5

#define COLOR_BG RGB(8, 12, 20)
#define COLOR_PANEL RGB(16, 23, 36)
#define COLOR_PANEL_ALT RGB(22, 31, 47)
#define COLOR_BORDER RGB(50, 65, 88)
#define COLOR_TEXT RGB(232, 239, 249)
#define COLOR_MUTED RGB(143, 157, 180)
#define COLOR_ACCENT RGB(78, 211, 255)
#define COLOR_ACTIVE RGB(16, 139, 137)
#define COLOR_WARN RGB(242, 91, 112)
#define COLOR_ORANGE RGB(247, 174, 65)
#define COLOR_PRISM RGB(154, 108, 255)

typedef struct {
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
} LabXInputGamepad;

typedef struct {
    DWORD dwPacketNumber;
    LabXInputGamepad Gamepad;
} LabXInputState;

typedef DWORD(WINAPI *XInputGetStateFn)(DWORD, LabXInputState *);

#define XBTN_DPAD_UP 0x0001
#define XBTN_DPAD_DOWN 0x0002
#define XBTN_DPAD_LEFT 0x0004
#define XBTN_DPAD_RIGHT 0x0008
#define XBTN_START 0x0010
#define XBTN_BACK 0x0020
#define XBTN_LEFT_THUMB 0x0040
#define XBTN_RIGHT_THUMB 0x0080
#define XBTN_LEFT_SHOULDER 0x0100
#define XBTN_RIGHT_SHOULDER 0x0200
#define XBTN_A 0x1000
#define XBTN_B 0x2000
#define XBTN_X 0x4000
#define XBTN_Y 0x8000

typedef struct {
    BOOL down;
    ULONGLONG pressedAt;
    ULONG downs;
    ULONG ups;
    ULONG repeats;
} KeyState;

typedef struct {
    ULONGLONG timeMs;
    char source[20];
    char action[12];
    char code[40];
    char name[48];
    char value[128];
} TimelineEvent;

typedef struct {
    BOOL down[MOUSE_BUTTON_COUNT];
    ULONG downs[MOUSE_BUTTON_COUNT];
    ULONG ups[MOUSE_BUTTON_COUNT];
    LONG x;
    LONG y;
    LONG rawDx;
    LONG rawDy;
    LONGLONG totalDx;
    LONGLONG totalDy;
    LONG rawX;
    LONG rawY;
    BOOL rawAbsolute;
    LONG wheelV;
    LONG wheelH;
    LONGLONG wheelVTotal;
    LONGLONG wheelHTotal;
    ULONG wheelVEvents;
    ULONG wheelHEvents;
    BOOL havePosition;
} MouseState;

typedef struct {
    BOOL connected;
    DWORD lastResult;
    LabXInputState state;
    unsigned lastChangedFields;
} PadState;

typedef struct {
    HWND hwnd;
    BOOL focused;
    BOOL rawRegistered;
    BOOL shuttingDown;
    LARGE_INTEGER frequency;
    LARGE_INTEGER startCounter;
    KeyState keys[KEY_COUNT];
    MouseState mouse;
    TimelineEvent events[EVENT_CAPACITY];
    unsigned eventHead;
    unsigned eventCount;
    ULONGLONG totalEvents;
    HANDLE csvLog;
    HANDLE textLog;
    char csvLogBuffer[LOG_BUFFER_CAPACITY];
    char textLogBuffer[LOG_BUFFER_CAPACITY];
    DWORD csvLogLength;
    DWORD textLogLength;
    WCHAR logDir[4096];
    ULONGLONG lastLogFlushAt;
    BOOL logError;
    HMODULE xinputModule;
    XInputGetStateFn xinputGetState;
    WCHAR xinputName[32];
    PadState pads[PAD_COUNT];
    HDC backDc;
    HBITMAP backBitmap;
    HGDIOBJ backOldBitmap;
    int backWidth;
    int backHeight;
    HFONT fontTitle;
    HFONT fontHeading;
    HFONT fontNormal;
    HFONT fontSmall;
    HFONT fontTiny;
    HFONT fontMono;
    RECT clearButton;
    RECT openButton;
} AppState;

typedef struct {
    int code;
    const char *label;
    int x;
    int row;
    int width;
    int height;
} KeyDef;

typedef struct {
    WORD mask;
    const char *name;
} XButtonDef;

static AppState g;

static const char *mouseButtonNames[MOUSE_BUTTON_COUNT] = {"LMB", "MMB", "RMB", "X1", "X2"};

static const XButtonDef xbuttonDefs[] = {{XBTN_DPAD_UP, "D-UP"},
                                         {XBTN_DPAD_RIGHT, "D-RIGHT"},
                                         {XBTN_DPAD_DOWN, "D-DOWN"},
                                         {XBTN_DPAD_LEFT, "D-LEFT"},
                                         {XBTN_A, "A"},
                                         {XBTN_B, "B"},
                                         {XBTN_X, "X"},
                                         {XBTN_Y, "Y"},
                                         {XBTN_LEFT_SHOULDER, "LB"},
                                         {XBTN_RIGHT_SHOULDER, "RB"},
                                         {XBTN_BACK, "BACK"},
                                         {XBTN_START, "START"},
                                         {XBTN_LEFT_THUMB, "L3"},
                                         {XBTN_RIGHT_THUMB, "R3"}};

/* Positions use quarter-key units across a 24-key-wide keyboard. */
static const KeyDef keyDefs[] = {{VK_ESCAPE, "Esc", 0, 0, 4, 1},
                                 {VK_F1, "F1", 8, 0, 4, 1},
                                 {VK_F2, "F2", 12, 0, 4, 1},
                                 {VK_F3, "F3", 16, 0, 4, 1},
                                 {VK_F4, "F4", 20, 0, 4, 1},
                                 {VK_F5, "F5", 26, 0, 4, 1},
                                 {VK_F6, "F6", 30, 0, 4, 1},
                                 {VK_F7, "F7", 34, 0, 4, 1},
                                 {VK_F8, "F8", 38, 0, 4, 1},
                                 {VK_F9, "F9", 44, 0, 4, 1},
                                 {VK_F10, "F10", 48, 0, 4, 1},
                                 {VK_F11, "F11", 52, 0, 4, 1},
                                 {VK_F12, "F12", 56, 0, 4, 1},
                                 {VK_SNAPSHOT, "PrtSc", 64, 0, 4, 1},
                                 {VK_SCROLL, "ScrLk", 68, 0, 4, 1},
                                 {VK_PAUSE, "Pause", 72, 0, 4, 1},

                                 {VK_OEM_3, "`", 0, 1, 4, 1},
                                 {'1', "1", 4, 1, 4, 1},
                                 {'2', "2", 8, 1, 4, 1},
                                 {'3', "3", 12, 1, 4, 1},
                                 {'4', "4", 16, 1, 4, 1},
                                 {'5', "5", 20, 1, 4, 1},
                                 {'6', "6", 24, 1, 4, 1},
                                 {'7', "7", 28, 1, 4, 1},
                                 {'8', "8", 32, 1, 4, 1},
                                 {'9', "9", 36, 1, 4, 1},
                                 {'0', "0", 40, 1, 4, 1},
                                 {VK_OEM_MINUS, "-", 44, 1, 4, 1},
                                 {VK_OEM_PLUS, "=", 48, 1, 4, 1},
                                 {VK_BACK, "Backspace", 52, 1, 8, 1},
                                 {VK_INSERT, "Ins", 64, 1, 4, 1},
                                 {VK_HOME, "Home", 68, 1, 4, 1},
                                 {VK_PRIOR, "PgUp", 72, 1, 4, 1},
                                 {VK_NUMLOCK, "Num", 80, 1, 4, 1},
                                 {VK_DIVIDE, "/", 84, 1, 4, 1},
                                 {VK_MULTIPLY, "*", 88, 1, 4, 1},
                                 {VK_SUBTRACT, "-", 92, 1, 4, 1},

                                 {VK_TAB, "Tab", 0, 2, 6, 1},
                                 {'Q', "Q", 6, 2, 4, 1},
                                 {'W', "W", 10, 2, 4, 1},
                                 {'E', "E", 14, 2, 4, 1},
                                 {'R', "R", 18, 2, 4, 1},
                                 {'T', "T", 22, 2, 4, 1},
                                 {'Y', "Y", 26, 2, 4, 1},
                                 {'U', "U", 30, 2, 4, 1},
                                 {'I', "I", 34, 2, 4, 1},
                                 {'O', "O", 38, 2, 4, 1},
                                 {'P', "P", 42, 2, 4, 1},
                                 {VK_OEM_4, "[", 46, 2, 4, 1},
                                 {VK_OEM_6, "]", 50, 2, 4, 1},
                                 {VK_OEM_5, "\\", 54, 2, 6, 1},
                                 {VK_DELETE, "Del", 64, 2, 4, 1},
                                 {VK_END, "End", 68, 2, 4, 1},
                                 {VK_NEXT, "PgDn", 72, 2, 4, 1},
                                 {VK_NUMPAD7, "7", 80, 2, 4, 1},
                                 {VK_NUMPAD8, "8", 84, 2, 4, 1},
                                 {VK_NUMPAD9, "9", 88, 2, 4, 1},
                                 {VK_ADD, "+", 92, 2, 4, 2},

                                 {VK_CAPITAL, "Caps", 0, 3, 7, 1},
                                 {'A', "A", 7, 3, 4, 1},
                                 {'S', "S", 11, 3, 4, 1},
                                 {'D', "D", 15, 3, 4, 1},
                                 {'F', "F", 19, 3, 4, 1},
                                 {'G', "G", 23, 3, 4, 1},
                                 {'H', "H", 27, 3, 4, 1},
                                 {'J', "J", 31, 3, 4, 1},
                                 {'K', "K", 35, 3, 4, 1},
                                 {'L', "L", 39, 3, 4, 1},
                                 {VK_OEM_1, ";", 43, 3, 4, 1},
                                 {VK_OEM_7, "'", 47, 3, 4, 1},
                                 {VK_RETURN, "Enter", 51, 3, 9, 1},
                                 {VK_NUMPAD4, "4", 80, 3, 4, 1},
                                 {VK_NUMPAD5, "5", 84, 3, 4, 1},
                                 {VK_NUMPAD6, "6", 88, 3, 4, 1},

                                 {VK_LSHIFT, "L Shift", 0, 4, 9, 1},
                                 {'Z', "Z", 9, 4, 4, 1},
                                 {'X', "X", 13, 4, 4, 1},
                                 {'C', "C", 17, 4, 4, 1},
                                 {'V', "V", 21, 4, 4, 1},
                                 {'B', "B", 25, 4, 4, 1},
                                 {'N', "N", 29, 4, 4, 1},
                                 {'M', "M", 33, 4, 4, 1},
                                 {VK_OEM_COMMA, ",", 37, 4, 4, 1},
                                 {VK_OEM_PERIOD, ".", 41, 4, 4, 1},
                                 {VK_OEM_2, "/", 45, 4, 4, 1},
                                 {VK_RSHIFT, "R Shift", 49, 4, 11, 1},
                                 {VK_UP, "Up", 68, 4, 4, 1},
                                 {VK_NUMPAD1, "1", 80, 4, 4, 1},
                                 {VK_NUMPAD2, "2", 84, 4, 4, 1},
                                 {VK_NUMPAD3, "3", 88, 4, 4, 1},
                                 {KEY_NUMPAD_ENTER, "Enter", 92, 4, 4, 2},

                                 {VK_LCONTROL, "L Ctrl", 0, 5, 6, 1},
                                 {VK_LWIN, "Win", 6, 5, 5, 1},
                                 {VK_LMENU, "L Alt", 11, 5, 5, 1},
                                 {VK_SPACE, "Space", 16, 5, 24, 1},
                                 {VK_RMENU, "R Alt", 40, 5, 5, 1},
                                 {VK_RWIN, "Win", 45, 5, 5, 1},
                                 {VK_APPS, "Menu", 50, 5, 5, 1},
                                 {VK_RCONTROL, "R Ctrl", 55, 5, 5, 1},
                                 {VK_LEFT, "Left", 64, 5, 4, 1},
                                 {VK_DOWN, "Down", 68, 5, 4, 1},
                                 {VK_RIGHT, "Right", 72, 5, 4, 1},
                                 {VK_NUMPAD0, "0", 80, 5, 8, 1},
                                 {VK_DECIMAL, ".", 88, 5, 4, 1}};

static ULONGLONG now_ms(void) {
    LARGE_INTEGER now;
    LONGLONG delta;
    QueryPerformanceCounter(&now);
    delta = now.QuadPart - g.startCounter.QuadPart;
    if (g.frequency.QuadPart <= 0 || delta <= 0) return 0;
    return (ULONGLONG)((delta * 1000) / g.frequency.QuadPart);
}

static void write_all(HANDLE handle, const char *data, DWORD length) {
    DWORD written;
    while (handle != INVALID_HANDLE_VALUE && length > 0) {
        if (!WriteFile(handle, data, length, &written, NULL) || written == 0) {
            g.logError = TRUE;
            return;
        }
        data += written;
        length -= written;
    }
}

static void flush_log_buffer(HANDLE handle, char *buffer, DWORD *length) {
    if (*length == 0) return;
    write_all(handle, buffer, *length);
    *length = 0;
}

static void buffer_log_write(HANDLE handle, char *buffer, DWORD *used, const char *data, DWORD length) {
    if (handle == INVALID_HANDLE_VALUE || length == 0) return;
    if (length > LOG_BUFFER_CAPACITY) {
        flush_log_buffer(handle, buffer, used);
        write_all(handle, data, length);
        return;
    }
    if (LOG_BUFFER_CAPACITY - *used < length) flush_log_buffer(handle, buffer, used);
    memcpy(buffer + *used, data, length);
    *used += length;
}

static void flush_logs(BOOL force) {
    ULONGLONG current = now_ms();
    if (!force && current - g.lastLogFlushAt < LOG_FLUSH_INTERVAL_MS) return;
    flush_log_buffer(g.csvLog, g.csvLogBuffer, &g.csvLogLength);
    flush_log_buffer(g.textLog, g.textLogBuffer, &g.textLogLength);
    if (g.csvLog != INVALID_HANDLE_VALUE && !FlushFileBuffers(g.csvLog)) g.logError = TRUE;
    if (g.textLog != INVALID_HANDLE_VALUE && !FlushFileBuffers(g.textLog)) g.logError = TRUE;
    g.lastLogFlushAt = current;
}

static size_t append_csv_field(char *out, size_t capacity, size_t pos, const char *value) {
    const unsigned char *p = (const unsigned char *)(value ? value : "");
    if (pos + 1 < capacity) out[pos++] = '"';
    while (*p && pos + 2 < capacity) {
        unsigned char c = *p++;
        if (c == '\r' || c == '\n') c = ' ';
        if (c == '"') out[pos++] = '"';
        out[pos++] = (char)c;
    }
    if (pos + 1 < capacity) out[pos++] = '"';
    return pos;
}

static void write_log_record(const TimelineEvent *event) {
    char line[2048];
    char timeField[32];
    size_t pos = 0;
    int count;
    const char *fields[5];
    unsigned i;

    snprintf(timeField, sizeof(timeField), "%llu", (unsigned long long)event->timeMs);
    pos = append_csv_field(line, sizeof(line), pos, timeField);
    fields[0] = event->source;
    fields[1] = event->action;
    fields[2] = event->code;
    fields[3] = event->name;
    fields[4] = event->value;
    for (i = 0; i < 5; ++i) {
        if (pos + 1 < sizeof(line)) line[pos++] = ',';
        pos = append_csv_field(line, sizeof(line), pos, fields[i]);
    }
    if (pos + 2 < sizeof(line)) {
        line[pos++] = '\r';
        line[pos++] = '\n';
    }
    buffer_log_write(g.csvLog, g.csvLogBuffer, &g.csvLogLength, line, (DWORD)pos);

    count = snprintf(line, sizeof(line), "%10llu ms | %-12s | %-7s | %-18s | %-22s | %s\r\n",
                     (unsigned long long)event->timeMs, event->source, event->action, event->code,
                     event->name, event->value);
    if (count > 0) {
        DWORD safeCount = (DWORD)((size_t)count < sizeof(line) ? (size_t)count : sizeof(line) - 1);
        buffer_log_write(g.textLog, g.textLogBuffer, &g.textLogLength, line, safeCount);
    }
    flush_logs(FALSE);
}

static void add_event_at(ULONGLONG timeMs, const char *source, const char *action, const char *code,
                         const char *name, const char *value) {
    TimelineEvent *event = &g.events[g.eventHead];
    memset(event, 0, sizeof(*event));
    event->timeMs = timeMs;
    snprintf(event->source, sizeof(event->source), "%s", source ? source : "");
    snprintf(event->action, sizeof(event->action), "%s", action ? action : "");
    snprintf(event->code, sizeof(event->code), "%s", code ? code : "");
    snprintf(event->name, sizeof(event->name), "%s", name ? name : "");
    snprintf(event->value, sizeof(event->value), "%s", value ? value : "");
    g.eventHead = (g.eventHead + 1U) % EVENT_CAPACITY;
    if (g.eventCount < EVENT_CAPACITY) ++g.eventCount;
    ++g.totalEvents;
    write_log_record(event);
}

static void add_event(const char *source, const char *action, const char *code, const char *name,
                      const char *value) {
    add_event_at(now_ms(), source, action, code, name, value);
}

static BOOL ensure_directory(const WCHAR *path) {
    return CreateDirectoryW(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static BOOL create_session_logs(const WCHAR *directory) {
    SYSTEMTIME localTime;
    WCHAR csvPath[4096];
    WCHAR textPath[4096];
    WCHAR sessionName[128];
    DWORD processId = GetCurrentProcessId();
    const char csvHeader[] = "timestamp_ms,source,action,code,name,value\r\n";

    GetLocalTime(&localTime);
    if (swprintf(sessionName, sizeof(sessionName) / sizeof(sessionName[0]),
                 L"InputGlass-session-%04u%02u%02u-%02u%02u%02u-%lu", (unsigned)localTime.wYear,
                 (unsigned)localTime.wMonth, (unsigned)localTime.wDay, (unsigned)localTime.wHour,
                 (unsigned)localTime.wMinute, (unsigned)localTime.wSecond, (unsigned long)processId) < 0 ||
        swprintf(csvPath, sizeof(csvPath) / sizeof(csvPath[0]), L"%ls\\%ls.csv", directory, sessionName) <
            0 ||
        swprintf(textPath, sizeof(textPath) / sizeof(textPath[0]), L"%ls\\%ls.txt", directory, sessionName) <
            0) {
        return FALSE;
    }

    g.csvLog =
        CreateFileW(csvPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g.csvLog == INVALID_HANDLE_VALUE) return FALSE;
    g.textLog =
        CreateFileW(textPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g.textLog == INVALID_HANDLE_VALUE) {
        CloseHandle(g.csvLog);
        g.csvLog = INVALID_HANDLE_VALUE;
        DeleteFileW(csvPath);
        return FALSE;
    }

    wcsncpy(g.logDir, directory, (sizeof(g.logDir) / sizeof(g.logDir[0])) - 1);
    g.logDir[(sizeof(g.logDir) / sizeof(g.logDir[0])) - 1] = L'\0';
    buffer_log_write(g.csvLog, g.csvLogBuffer, &g.csvLogLength, csvHeader, (DWORD)(sizeof(csvHeader) - 1));
    flush_logs(TRUE);
    return TRUE;
}

static void initialize_log_paths(void) {
    WCHAR modulePath[4096];
    WCHAR localAppData[4096];
    WCHAR appDirectory[4096];
    WCHAR logsDirectory[4096];
    WCHAR *slash;
    DWORD localLength;

    g.csvLog = INVALID_HANDLE_VALUE;
    g.textLog = INVALID_HANDLE_VALUE;
    g.logDir[0] = L'\0';

    localLength = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData,
                                          (DWORD)(sizeof(localAppData) / sizeof(localAppData[0])));
    if (localLength > 0 && localLength < (DWORD)(sizeof(localAppData) / sizeof(localAppData[0])) &&
        swprintf(appDirectory, sizeof(appDirectory) / sizeof(appDirectory[0]), L"%ls\\InputGlass",
                 localAppData) >= 0 &&
        swprintf(logsDirectory, sizeof(logsDirectory) / sizeof(logsDirectory[0]), L"%ls\\logs",
                 appDirectory) >= 0 &&
        ensure_directory(appDirectory) && ensure_directory(logsDirectory) &&
        create_session_logs(logsDirectory)) {
        return;
    }

    if (!GetModuleFileNameW(NULL, modulePath, (DWORD)(sizeof(modulePath) / sizeof(modulePath[0])))) {
        wcscpy(g.logDir, L".");
        g.logError = TRUE;
        return;
    }
    modulePath[(sizeof(modulePath) / sizeof(modulePath[0])) - 1] = L'\0';
    slash = wcsrchr(modulePath, L'\\');
    if (slash)
        *slash = L'\0';
    else
        wcscpy(modulePath, L".");
    if (!create_session_logs(modulePath)) {
        wcsncpy(g.logDir, modulePath, (sizeof(g.logDir) / sizeof(g.logDir[0])) - 1);
        g.logDir[(sizeof(g.logDir) / sizeof(g.logDir[0])) - 1] = L'\0';
        g.logError = TRUE;
    }
}

static void close_logs(void) {
    flush_logs(TRUE);
    if (g.csvLog != INVALID_HANDLE_VALUE) CloseHandle(g.csvLog);
    if (g.textLog != INVALID_HANDLE_VALUE) CloseHandle(g.textLog);
    g.csvLog = INVALID_HANDLE_VALUE;
    g.textLog = INVALID_HANDLE_VALUE;
}

static int normalize_key(UINT vk, UINT scanCode, BOOL extended) {
    if (vk == VK_SHIFT) {
        UINT mapped = MapVirtualKeyA(scanCode, MAPVK_VSC_TO_VK_EX);
        if (mapped == VK_LSHIFT || mapped == VK_RSHIFT) return (int)mapped;
    }
    if (vk == VK_CONTROL) return extended ? VK_RCONTROL : VK_LCONTROL;
    if (vk == VK_MENU) return extended ? VK_RMENU : VK_LMENU;
    if (vk == VK_RETURN && extended) return KEY_NUMPAD_ENTER;
    return vk < 256U ? (int)vk : -1;
}

static const char *key_name(int code, UINT scanCode, BOOL extended, char *buffer, size_t capacity) {
    LONG keyData;
    int length;
    if (code == KEY_NUMPAD_ENTER) return "Numpad Enter";
    keyData = (LONG)((scanCode & 0xffU) << 16);
    if (extended) keyData |= (LONG)(1U << 24);
    length = GetKeyNameTextA(keyData, buffer, (int)capacity);
    if (length > 0) return buffer;
    snprintf(buffer, capacity, "VK 0x%02X", code & 0xff);
    return buffer;
}

static void update_key(int code, BOOL down, const char *source, UINT scanCode, BOOL extended) {
    KeyState *state;
    BOOL wasDown;
    ULONGLONG current;
    const char *action;
    char codeText[40];
    char nameBuffer[64];
    char value[96];
    if (code < 0 || code >= KEY_COUNT) return;
    state = &g.keys[code];
    wasDown = state->down;
    current = now_ms();
    if (down) {
        if (state->down) {
            ++state->repeats;
            action = "REPEAT";
        } else {
            state->down = TRUE;
            state->pressedAt = current;
            ++state->downs;
            action = "DOWN";
        }
    } else {
        ++state->ups;
        state->down = FALSE;
        action = "UP";
    }
    if (code == KEY_NUMPAD_ENTER)
        snprintf(codeText, sizeof(codeText), "VK=NUMENTER SC=0x%02X", scanCode & 0xff);
    else
        snprintf(codeText, sizeof(codeText), "VK=0x%02X SC=0x%02X", code & 0xff, scanCode & 0xff);
    snprintf(value, sizeof(value), "extended=%u duration=%llums", extended ? 1U : 0U,
             (unsigned long long)((!down && wasDown && state->pressedAt <= current)
                                      ? current - state->pressedAt
                                      : 0));
    add_event(source, action, codeText, key_name(code, scanCode, extended, nameBuffer, sizeof(nameBuffer)),
              value);
}

static void log_fallback_key(UINT message, WPARAM wParam, LPARAM lParam) {
    BOOL down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    BOOL repeat = down && (lParam & (1L << 30)) != 0;
    UINT scanCode = ((UINT)lParam >> 16) & 0xffU;
    BOOL extended = (lParam & (1L << 24)) != 0;
    int code = normalize_key((UINT)wParam, scanCode, extended);
    char codeText[40];
    char nameBuffer[64];
    char value[64];
    if (!g.rawRegistered) {
        update_key(code, down, "WIN32-KBD", scanCode, extended);
        return;
    }
    snprintf(codeText, sizeof(codeText), "MSG=0x%04X VK=0x%02X", message, (unsigned)wParam & 0xffU);
    snprintf(value, sizeof(value), "fallback integrated=0 extended=%u", extended ? 1U : 0U);
    add_event("WIN32-KBD", repeat ? "REPEAT" : (down ? "DOWN" : "UP"), codeText,
              key_name(code, scanCode, extended, nameBuffer, sizeof(nameBuffer)), value);
}

static void update_cursor_position(void) {
    POINT point;
    if (GetCursorPos(&point) && g.hwnd && ScreenToClient(g.hwnd, &point)) {
        g.mouse.x = point.x;
        g.mouse.y = point.y;
        g.mouse.havePosition = TRUE;
    }
}

static void update_mouse_button(unsigned index, BOOL down, const char *source, const char *code) {
    const char *action;
    char value[64];
    if (index >= MOUSE_BUTTON_COUNT) return;
    if (down) {
        if (g.mouse.down[index])
            action = "REPEAT";
        else {
            g.mouse.down[index] = TRUE;
            ++g.mouse.downs[index];
            action = "DOWN";
        }
    } else {
        g.mouse.down[index] = FALSE;
        ++g.mouse.ups[index];
        action = "UP";
    }
    update_cursor_position();
    snprintf(value, sizeof(value), "x=%ld y=%ld", g.mouse.x, g.mouse.y);
    add_event(source, action, code, mouseButtonNames[index], value);
}

static void update_mouse_wheel(BOOL horizontal, SHORT delta, const char *source, const char *code) {
    char value[80];
    if (horizontal) {
        g.mouse.wheelH = delta;
        g.mouse.wheelHTotal += delta;
        ++g.mouse.wheelHEvents;
    } else {
        g.mouse.wheelV = delta;
        g.mouse.wheelVTotal += delta;
        ++g.mouse.wheelVEvents;
    }
    snprintf(value, sizeof(value), "delta=%d total=%lld", (int)delta,
             (long long)(horizontal ? g.mouse.wheelHTotal : g.mouse.wheelVTotal));
    add_event(source, "CHANGE", code, horizontal ? "Horizontal wheel" : "Vertical wheel", value);
}

static void process_raw_mouse(const RAWMOUSE *mouse) {
    USHORT flags = mouse->usButtonFlags;
    char value[128];
    g.mouse.rawAbsolute = (mouse->usFlags & MOUSE_MOVE_ABSOLUTE) != 0;
    g.mouse.rawX = mouse->lLastX;
    g.mouse.rawY = mouse->lLastY;
    if (g.mouse.rawAbsolute) {
        g.mouse.rawDx = 0;
        g.mouse.rawDy = 0;
    } else {
        g.mouse.rawDx = mouse->lLastX;
        g.mouse.rawDy = mouse->lLastY;
        g.mouse.totalDx += mouse->lLastX;
        g.mouse.totalDy += mouse->lLastY;
    }
    update_cursor_position();
    if (mouse->lLastX != 0 || mouse->lLastY != 0) {
        snprintf(value, sizeof(value), "%s x=%ld y=%ld client=%ld:%ld flags=0x%04X",
                 g.mouse.rawAbsolute ? "absolute" : "relative", mouse->lLastX, mouse->lLastY, g.mouse.x,
                 g.mouse.y, mouse->usFlags);
        add_event("RAW-MOUSE", "CHANGE", "MOVE", g.mouse.rawAbsolute ? "Absolute move" : "Relative move",
                  value);
    }
    if (flags & RI_MOUSE_LEFT_BUTTON_DOWN) update_mouse_button(0, TRUE, "RAW-MOUSE", "BTN1");
    if (flags & RI_MOUSE_LEFT_BUTTON_UP) update_mouse_button(0, FALSE, "RAW-MOUSE", "BTN1");
    if (flags & RI_MOUSE_MIDDLE_BUTTON_DOWN) update_mouse_button(1, TRUE, "RAW-MOUSE", "BTN2");
    if (flags & RI_MOUSE_MIDDLE_BUTTON_UP) update_mouse_button(1, FALSE, "RAW-MOUSE", "BTN2");
    if (flags & RI_MOUSE_RIGHT_BUTTON_DOWN) update_mouse_button(2, TRUE, "RAW-MOUSE", "BTN3");
    if (flags & RI_MOUSE_RIGHT_BUTTON_UP) update_mouse_button(2, FALSE, "RAW-MOUSE", "BTN3");
    if (flags & RI_MOUSE_BUTTON_4_DOWN) update_mouse_button(3, TRUE, "RAW-MOUSE", "BTN4");
    if (flags & RI_MOUSE_BUTTON_4_UP) update_mouse_button(3, FALSE, "RAW-MOUSE", "BTN4");
    if (flags & RI_MOUSE_BUTTON_5_DOWN) update_mouse_button(4, TRUE, "RAW-MOUSE", "BTN5");
    if (flags & RI_MOUSE_BUTTON_5_UP) update_mouse_button(4, FALSE, "RAW-MOUSE", "BTN5");
    if (flags & RI_MOUSE_WHEEL) update_mouse_wheel(FALSE, (SHORT)mouse->usButtonData, "RAW-MOUSE", "WHEEL-V");
    if (flags & RI_MOUSE_HWHEEL) update_mouse_wheel(TRUE, (SHORT)mouse->usButtonData, "RAW-MOUSE", "WHEEL-H");
}

static void process_raw_input(HRAWINPUT input) {
    RAWINPUT raw;
    UINT size = sizeof(raw);
    UINT result = GetRawInputData(input, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER));
    if (result == (UINT)-1 || result < (UINT)sizeof(RAWINPUTHEADER)) {
        add_event("RAW", "CHANGE", "ERROR", "GetRawInputData", "read failed");
        return;
    }
    if (raw.header.dwType == RIM_TYPEKEYBOARD) {
        const RAWKEYBOARD *keyboard = &raw.data.keyboard;
        BOOL extended = (keyboard->Flags & (RI_KEY_E0 | RI_KEY_E1)) != 0;
        BOOL down = (keyboard->Flags & RI_KEY_BREAK) == 0;
        int code;
        if (keyboard->VKey == 255U) return;
        code = normalize_key(keyboard->VKey, keyboard->MakeCode, extended);
        update_key(code, down, "RAW-KBD", keyboard->MakeCode, extended);
    } else if (raw.header.dwType == RIM_TYPEMOUSE) {
        process_raw_mouse(&raw.data.mouse);
    }
}

static void fallback_mouse_button(UINT message, BOOL down, unsigned index) {
    char code[24];
    char value[64];
    snprintf(code, sizeof(code), "MSG=0x%04X", message);
    if (!g.rawRegistered)
        update_mouse_button(index, down, "WIN32-MOUSE", code);
    else {
        update_cursor_position();
        snprintf(value, sizeof(value), "fallback integrated=0 x=%ld y=%ld", g.mouse.x, g.mouse.y);
        add_event("WIN32-MOUSE", down ? "DOWN" : "UP", code, mouseButtonNames[index], value);
    }
}

static void fallback_mouse_move(LPARAM lParam) {
    LONG oldX = g.mouse.x;
    LONG oldY = g.mouse.y;
    char value[96];
    g.mouse.x = (SHORT)LOWORD(lParam);
    g.mouse.y = (SHORT)HIWORD(lParam);
    if (!g.mouse.havePosition) {
        oldX = g.mouse.x;
        oldY = g.mouse.y;
        g.mouse.havePosition = TRUE;
    }
    if (!g.rawRegistered && (oldX != g.mouse.x || oldY != g.mouse.y)) {
        g.mouse.rawDx = g.mouse.x - oldX;
        g.mouse.rawDy = g.mouse.y - oldY;
        g.mouse.totalDx += g.mouse.rawDx;
        g.mouse.totalDy += g.mouse.rawDy;
        snprintf(value, sizeof(value), "client=%ld:%ld delta=%ld:%ld", g.mouse.x, g.mouse.y, g.mouse.rawDx,
                 g.mouse.rawDy);
        add_event("WIN32-MOUSE", "CHANGE", "WM_MOUSEMOVE", "Cursor move", value);
    }
}

static unsigned count_changed_properties(const LabXInputGamepad *oldPad, const LabXInputGamepad *newPad) {
    WORD changed = oldPad->wButtons ^ newPad->wButtons;
    unsigned count = 0;
    unsigned i;
    for (i = 0; i < sizeof(xbuttonDefs) / sizeof(xbuttonDefs[0]); ++i) {
        if (changed & xbuttonDefs[i].mask) ++count;
    }
    if (oldPad->bLeftTrigger != newPad->bLeftTrigger) ++count;
    if (oldPad->bRightTrigger != newPad->bRightTrigger) ++count;
    if (oldPad->sThumbLX != newPad->sThumbLX) ++count;
    if (oldPad->sThumbLY != newPad->sThumbLY) ++count;
    if (oldPad->sThumbRX != newPad->sThumbRX) ++count;
    if (oldPad->sThumbRY != newPad->sThumbRY) ++count;
    return count;
}

static void log_pad_value(unsigned padIndex, DWORD packet, unsigned changedFields, ULONGLONG sampleTime,
                          const char *name, long oldValue, long newValue) {
    char source[20];
    char code[40];
    char value[96];
    snprintf(source, sizeof(source), "XINPUT%u", padIndex + 1U);
    snprintf(code, sizeof(code), "PACKET=%lu", (unsigned long)packet);
    snprintf(value, sizeof(value), "%ld -> %ld fields_changed=%u", oldValue, newValue, changedFields);
    add_event_at(sampleTime, source, "CHANGE", code, name, value);
}

static void log_pad_transitions(unsigned padIndex, const LabXInputState *oldState,
                                const LabXInputState *newState, unsigned changedFields,
                                ULONGLONG sampleTime) {
    const LabXInputGamepad *oldPad = &oldState->Gamepad;
    const LabXInputGamepad *newPad = &newState->Gamepad;
    WORD changed = oldPad->wButtons ^ newPad->wButtons;
    char source[20];
    char code[40];
    char value[64];
    unsigned i;
    snprintf(source, sizeof(source), "XINPUT%u", padIndex + 1U);
    snprintf(code, sizeof(code), "PACKET=%lu", (unsigned long)newState->dwPacketNumber);
    snprintf(value, sizeof(value), "packet=%lu fields_changed=%u", (unsigned long)newState->dwPacketNumber,
             changedFields);
    add_event_at(sampleTime, source, "CHANGE", code, "Packet", value);
    for (i = 0; i < sizeof(xbuttonDefs) / sizeof(xbuttonDefs[0]); ++i) {
        if (changed & xbuttonDefs[i].mask) {
            BOOL down = (newPad->wButtons & xbuttonDefs[i].mask) != 0;
            snprintf(value, sizeof(value), "value=%u fields_changed=%u", down ? 1U : 0U, changedFields);
            add_event_at(sampleTime, source, down ? "DOWN" : "UP", code, xbuttonDefs[i].name, value);
        }
    }
    if (oldPad->bLeftTrigger != newPad->bLeftTrigger)
        log_pad_value(padIndex, newState->dwPacketNumber, changedFields, sampleTime, "LT",
                      oldPad->bLeftTrigger, newPad->bLeftTrigger);
    if (oldPad->bRightTrigger != newPad->bRightTrigger)
        log_pad_value(padIndex, newState->dwPacketNumber, changedFields, sampleTime, "RT",
                      oldPad->bRightTrigger, newPad->bRightTrigger);
    if (oldPad->sThumbLX != newPad->sThumbLX)
        log_pad_value(padIndex, newState->dwPacketNumber, changedFields, sampleTime, "LX", oldPad->sThumbLX,
                      newPad->sThumbLX);
    if (oldPad->sThumbLY != newPad->sThumbLY)
        log_pad_value(padIndex, newState->dwPacketNumber, changedFields, sampleTime, "LY", oldPad->sThumbLY,
                      newPad->sThumbLY);
    if (oldPad->sThumbRX != newPad->sThumbRX)
        log_pad_value(padIndex, newState->dwPacketNumber, changedFields, sampleTime, "RX", oldPad->sThumbRX,
                      newPad->sThumbRX);
    if (oldPad->sThumbRY != newPad->sThumbRY)
        log_pad_value(padIndex, newState->dwPacketNumber, changedFields, sampleTime, "RY", oldPad->sThumbRY,
                      newPad->sThumbRY);
}

static void poll_xinput(void) {
    unsigned i;
    if (!g.xinputGetState) return;
    for (i = 0; i < PAD_COUNT; ++i) {
        LabXInputState state;
        DWORD result;
        ULONGLONG sampleTime;
        memset(&state, 0, sizeof(state));
        result = g.xinputGetState(i, &state);
        sampleTime = now_ms();
        if (result == ERROR_SUCCESS) {
            unsigned changes = count_changed_properties(&g.pads[i].state.Gamepad, &state.Gamepad);
            if (!g.pads[i].connected) {
                char source[20];
                char value[64];
                snprintf(source, sizeof(source), "XINPUT%u", i + 1U);
                snprintf(value, sizeof(value), "packet=%lu", (unsigned long)state.dwPacketNumber);
                add_event_at(sampleTime, source, "CHANGE", "CONNECT", "Controller", value);
            }
            if (!g.pads[i].connected || state.dwPacketNumber != g.pads[i].state.dwPacketNumber ||
                changes > 0) {
                g.pads[i].lastChangedFields = changes;
                log_pad_transitions(i, &g.pads[i].state, &state, changes, sampleTime);
            }
            g.pads[i].connected = TRUE;
            g.pads[i].lastResult = result;
            g.pads[i].state = state;
        } else {
            if (g.pads[i].connected) {
                char source[20];
                char value[64];
                snprintf(source, sizeof(source), "XINPUT%u", i + 1U);
                snprintf(value, sizeof(value), "result=%lu", (unsigned long)result);
                add_event_at(sampleTime, source, "CHANGE", "DISCONNECT", "Controller", value);
            } else if (g.pads[i].lastResult != result && result != ERROR_DEVICE_NOT_CONNECTED) {
                char source[20];
                char value[64];
                snprintf(source, sizeof(source), "XINPUT%u", i + 1U);
                snprintf(value, sizeof(value), "XInputGetState=%lu", (unsigned long)result);
                add_event_at(sampleTime, source, "CHANGE", "ERROR", "Controller poll", value);
            }
            g.pads[i].connected = FALSE;
            g.pads[i].lastResult = result;
            memset(&g.pads[i].state, 0, sizeof(g.pads[i].state));
            g.pads[i].lastChangedFields = 0;
        }
    }
}

static void load_xinput(void) {
    static const WCHAR *names[] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
    WCHAR systemDirectory[4096];
    WCHAR libraryPath[4096];
    UINT systemLength;
    unsigned i;

    systemLength =
        GetSystemDirectoryW(systemDirectory, (UINT)(sizeof(systemDirectory) / sizeof(systemDirectory[0])));
    if (systemLength == 0 || systemLength >= (UINT)(sizeof(systemDirectory) / sizeof(systemDirectory[0]))) {
        wcscpy(g.xinputName, L"not available");
        add_event("SYSTEM", "CHANGE", "XINPUT", "System directory unavailable",
                  "Could not resolve a safe XInput path");
        return;
    }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        HMODULE module;
        if (swprintf(libraryPath, sizeof(libraryPath) / sizeof(libraryPath[0]), L"%ls\\%ls", systemDirectory,
                     names[i]) < 0)
            continue;
        module = LoadLibraryW(libraryPath);
        if (module) {
            union {
                FARPROC generic;
                XInputGetStateFn getState;
            } proc;
            proc.generic = GetProcAddress(module, "XInputGetState");
            if (proc.generic) {
                g.xinputModule = module;
                g.xinputGetState = proc.getState;
                wcsncpy(g.xinputName, names[i], (sizeof(g.xinputName) / sizeof(g.xinputName[0])) - 1);
                g.xinputName[(sizeof(g.xinputName) / sizeof(g.xinputName[0])) - 1] = L'\0';
                add_event("SYSTEM", "CHANGE", "XINPUT", "DLL loaded",
                          i == 0 ? "xinput1_4.dll" : (i == 1 ? "xinput1_3.dll" : "xinput9_1_0.dll"));
                return;
            }
            FreeLibrary(module);
        }
    }
    wcscpy(g.xinputName, L"not available");
    add_event("SYSTEM", "CHANGE", "XINPUT", "DLL unavailable", "No supported XInput DLL found");
}

static BOOL register_raw_input(HWND hwnd) {
    RAWINPUTDEVICE devices[2];
    devices[0].usUsagePage = 0x01;
    devices[0].usUsage = 0x02;
    devices[0].dwFlags = 0;
    devices[0].hwndTarget = hwnd;
    devices[1].usUsagePage = 0x01;
    devices[1].usUsage = 0x06;
    devices[1].dwFlags = 0;
    devices[1].hwndTarget = hwnd;
    g.rawRegistered = RegisterRawInputDevices(devices, 2, sizeof(devices[0]));
    if (g.rawRegistered)
        add_event("SYSTEM", "CHANGE", "RAW", "Raw Input", "keyboard+mouse registered");
    else {
        char value[64];
        snprintf(value, sizeof(value), "registration failed error=%lu", (unsigned long)GetLastError());
        add_event("SYSTEM", "CHANGE", "RAW", "Raw Input", value);
    }
    return g.rawRegistered;
}

static void release_active_input(void) {
    unsigned i;
    for (i = 0; i < KEY_COUNT; ++i)
        g.keys[i].down = FALSE;
    for (i = 0; i < MOUSE_BUTTON_COUNT; ++i)
        g.mouse.down[i] = FALSE;
}

static int clamp_int(int value, int minimum, int maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static void fill_color(HDC dc, const RECT *rect, COLORREF color) {
    SetDCBrushColor(dc, color);
    FillRect(dc, rect, (HBRUSH)GetStockObject(DC_BRUSH));
}

static void draw_box(HDC dc, const RECT *rect, COLORREF fill, COLORREF border, int radius) {
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(DC_BRUSH));
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(DC_PEN));
    SetDCBrushColor(dc, fill);
    SetDCPenColor(dc, border);
    if (radius > 0)
        RoundRect(dc, rect->left, rect->top, rect->right, rect->bottom, radius, radius);
    else
        Rectangle(dc, rect->left, rect->top, rect->right, rect->bottom);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
}

static void draw_line(HDC dc, int x1, int y1, int x2, int y2, COLORREF color) {
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(DC_PEN));
    SetDCPenColor(dc, color);
    MoveToEx(dc, x1, y1, NULL);
    LineTo(dc, x2, y2);
    SelectObject(dc, oldPen);
}

static void draw_text(HDC dc, const char *text, RECT rect, HFONT font, COLORREF color, UINT flags) {
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, flags | DT_NOPREFIX);
    SelectObject(dc, oldFont);
}

static void draw_panel_title(HDC dc, const RECT *panel, const char *title, const char *detail) {
    RECT titleRect = {panel->left + 10, panel->top + 7, panel->right - 10, panel->top + 31};
    draw_text(dc, title, titleRect, g.fontHeading, COLOR_TEXT, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    if (detail) {
        RECT detailRect = titleRect;
        detailRect.left += 160;
        draw_text(dc, detail, detailRect, g.fontSmall, COLOR_MUTED, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }
}

static void draw_status(HDC dc, int width) {
    RECT status = {0, 0, width, 64};
    RECT title = {48, 5, 225, 32};
    RECT tagline = {48, 31, 225, 54};
    RECT line = {242, 17, width - 278, 47};
    RECT prismA = {14, 14, 21, 49};
    RECT prismB = {24, 20, 31, 49};
    RECT prismC = {34, 27, 41, 49};
    char buffer[512];
    char xinput[64];
    ULONGLONG elapsed = now_ms();
    int buttonWidth = width > 1000 ? 150 : 112;

    fill_color(dc, &status, RGB(11, 17, 28));
    draw_line(dc, 0, 63, width, 63, COLOR_BORDER);
    fill_color(dc, &prismA, COLOR_ACCENT);
    fill_color(dc, &prismB, COLOR_PRISM);
    fill_color(dc, &prismC, COLOR_ORANGE);
    draw_text(dc, "INPUTGLASS", title, g.fontTitle, COLOR_TEXT, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    draw_text(dc, "SEE EVERY SIGNAL.", tagline, g.fontTiny, COLOR_ACCENT,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    if (WideCharToMultiByte(CP_UTF8, 0, g.xinputName, -1, xinput, sizeof(xinput), NULL, NULL) == 0)
        strcpy(xinput, "unknown");
    snprintf(buffer, sizeof(buffer),
             "FOCUS %s   RAW %s   XINPUT %s   %llu.%03llus   %llu EVENTS   LOGGING %s",
             g.focused ? "YES" : "NO", g.rawRegistered ? "YES" : "NO", xinput,
             (unsigned long long)(elapsed / 1000), (unsigned long long)(elapsed % 1000),
             (unsigned long long)g.totalEvents, g.logError ? "ERROR" : "ON");
    draw_text(dc, buffer, line, g.fontSmall, g.logError ? COLOR_WARN : COLOR_MUTED,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

    g.openButton.left = width - 12 - buttonWidth;
    g.openButton.right = width - 12;
    g.openButton.top = 13;
    g.openButton.bottom = 49;
    g.clearButton.right = g.openButton.left - 8;
    g.clearButton.left = g.clearButton.right - (buttonWidth * 2 / 3);
    g.clearButton.top = 13;
    g.clearButton.bottom = 49;
    draw_box(dc, &g.clearButton, COLOR_PANEL_ALT, COLOR_BORDER, 6);
    draw_box(dc, &g.openButton, RGB(23, 82, 91), COLOR_ACCENT, 6);
    draw_text(dc, "Clear", g.clearButton, g.fontNormal, COLOR_TEXT, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    draw_text(dc, width > 1200 ? "Open session folder" : "Open logs", g.openButton, g.fontNormal, COLOR_TEXT,
              DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

static void draw_keyboard(HDC dc, const RECT *panel) {
    RECT inner;
    int availableWidth;
    int availableHeight;
    int rowHeight;
    unsigned i;
    ULONGLONG current = now_ms();
    draw_box(dc, panel, COLOR_PANEL, COLOR_BORDER, 8);
    draw_panel_title(dc, panel, "KEYBOARD", "Raw Input state / current hold / D U R counters");
    inner.left = panel->left + 8;
    inner.right = panel->right - 8;
    inner.top = panel->top + 35;
    inner.bottom = panel->bottom - 8;
    availableWidth = inner.right - inner.left;
    availableHeight = inner.bottom - inner.top;
    rowHeight = availableHeight / 6;
    for (i = 0; i < sizeof(keyDefs) / sizeof(keyDefs[0]); ++i) {
        const KeyDef *key = &keyDefs[i];
        const KeyState *state = &g.keys[key->code];
        RECT box;
        RECT labelRect;
        RECT dataRect;
        COLORREF fill = COLOR_PANEL_ALT;
        COLORREF border = COLOR_BORDER;
        char counts[64];
        char duration[32];
        ULONGLONG held = state->down && current >= state->pressedAt ? current - state->pressedAt : 0;
        box.left = inner.left + key->x * availableWidth / 96 + 1;
        box.right = inner.left + (key->x + key->width) * availableWidth / 96 - 1;
        box.top = inner.top + key->row * rowHeight + 1;
        box.bottom = inner.top + (key->row + key->height) * rowHeight - 1;
        if (state->down) {
            fill = held > 2000 ? COLOR_WARN : COLOR_ACTIVE;
            border = held > 2000 ? RGB(255, 145, 145) : COLOR_ACCENT;
        }
        draw_box(dc, &box, fill, border, 4);
        labelRect = box;
        labelRect.left += 2;
        labelRect.right -= 2;
        labelRect.top += 2;
        labelRect.bottom = labelRect.top + 18;
        draw_text(dc, key->label, labelRect, state->down ? g.fontNormal : g.fontSmall, COLOR_TEXT,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        snprintf(counts, sizeof(counts), "D%lu U%lu R%lu", (unsigned long)state->downs,
                 (unsigned long)state->ups, (unsigned long)state->repeats);
        dataRect = box;
        dataRect.left += 1;
        dataRect.right -= 1;
        dataRect.top = box.bottom - 17;
        dataRect.bottom = box.bottom - 1;
        draw_text(dc, counts, dataRect, g.fontTiny, state->down ? COLOR_TEXT : COLOR_MUTED,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        if (state->down && box.bottom - box.top >= 48) {
            snprintf(duration, sizeof(duration), "%llu.%03llus", (unsigned long long)(held / 1000),
                     (unsigned long long)(held % 1000));
            dataRect.top = labelRect.bottom;
            dataRect.bottom = box.bottom - 17;
            draw_text(dc, duration, dataRect, g.fontTiny, COLOR_TEXT,
                      DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        }
    }
}

static void draw_mouse(HDC dc, const RECT *panel) {
    char detail[128];
    char text[256];
    RECT line;
    RECT button;
    int buttonWidth;
    unsigned i;
    draw_box(dc, panel, COLOR_PANEL, COLOR_BORDER, 8);
    snprintf(detail, sizeof(detail), "client %ld:%ld", g.mouse.x, g.mouse.y);
    draw_panel_title(dc, panel, "MOUSE", detail);
    line.left = panel->left + 10;
    line.right = panel->right - 10;
    line.top = panel->top + 34;
    line.bottom = line.top + 21;
    snprintf(text, sizeof(text), "Raw %s: %ld:%ld   last delta: %ld:%ld   accumulated: %lld:%lld",
             g.mouse.rawAbsolute ? "absolute" : "relative", g.mouse.rawX, g.mouse.rawY, g.mouse.rawDx,
             g.mouse.rawDy, (long long)g.mouse.totalDx, (long long)g.mouse.totalDy);
    draw_text(dc, text, line, g.fontSmall, COLOR_MUTED, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    line.top += 21;
    line.bottom += 21;
    snprintf(text, sizeof(text), "Wheel V: last %+ld total %+lld (%lu)   H: last %+ld total %+lld (%lu)",
             g.mouse.wheelV, (long long)g.mouse.wheelVTotal, (unsigned long)g.mouse.wheelVEvents,
             g.mouse.wheelH, (long long)g.mouse.wheelHTotal, (unsigned long)g.mouse.wheelHEvents);
    draw_text(dc, text, line, g.fontSmall, COLOR_MUTED, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    buttonWidth = (panel->right - panel->left - 20) / MOUSE_BUTTON_COUNT;
    for (i = 0; i < MOUSE_BUTTON_COUNT; ++i) {
        char counter[64];
        button.left = panel->left + 10 + (int)i * buttonWidth + 2;
        button.right = panel->left + 10 + ((int)i + 1) * buttonWidth - 2;
        button.top = panel->top + 82;
        button.bottom = panel->bottom - 9;
        draw_box(dc, &button, g.mouse.down[i] ? COLOR_ACTIVE : COLOR_PANEL_ALT,
                 g.mouse.down[i] ? COLOR_ACCENT : COLOR_BORDER, 5);
        snprintf(counter, sizeof(counter), "%s   D%lu U%lu", mouseButtonNames[i],
                 (unsigned long)g.mouse.downs[i], (unsigned long)g.mouse.ups[i]);
        draw_text(dc, counter, button, g.fontSmall, COLOR_TEXT,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    }
}

static void draw_trigger(HDC dc, RECT rect, const char *name, BYTE value) {
    RECT fill = rect;
    char text[32];
    draw_box(dc, &rect, RGB(15, 22, 31), COLOR_BORDER, 3);
    fill.right = fill.left + (fill.right - fill.left) * value / 255;
    if (fill.right > fill.left) fill_color(dc, &fill, COLOR_ORANGE);
    snprintf(text, sizeof(text), "%s %u", name, (unsigned)value);
    draw_text(dc, text, rect, g.fontTiny, COLOR_TEXT, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

static void draw_stick(HDC dc, int cx, int cy, int radius, SHORT x, SHORT y, const char *name) {
    RECT circle = {cx - radius, cy - radius, cx + radius, cy + radius};
    RECT label = {cx - radius - 30, cy + radius + 1, cx + radius + 30, cy + radius + 18};
    int px = cx + (int)((LONGLONG)x * (radius - 4) / 32768);
    int py = cy - (int)((LONGLONG)y * (radius - 4) / 32768);
    RECT dot = {px - 4, py - 4, px + 5, py + 5};
    char text[64];
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(DC_PEN));
    SetDCPenColor(dc, COLOR_BORDER);
    Ellipse(dc, circle.left, circle.top, circle.right, circle.bottom);
    draw_line(dc, cx - radius, cy, cx + radius, cy, RGB(43, 58, 73));
    draw_line(dc, cx, cy - radius, cx, cy + radius, RGB(43, 58, 73));
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SetDCBrushColor(dc, COLOR_ACCENT);
    SetDCPenColor(dc, COLOR_ACCENT);
    Ellipse(dc, dot.left, dot.top, dot.right, dot.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    snprintf(text, sizeof(text), "%s  %d : %d", name, (int)x, (int)y);
    draw_text(dc, text, label, g.fontTiny, COLOR_MUTED, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

static void draw_controller(HDC dc, const RECT *panel, unsigned index) {
    const PadState *pad = &g.pads[index];
    const LabXInputGamepad *state = &pad->state.Gamepad;
    char title[64];
    char detail[96];
    RECT message;
    RECT bar;
    int contentTop;
    int chipWidth;
    int stickRadius;
    unsigned i;
    draw_box(dc, panel, COLOR_PANEL, COLOR_BORDER, 8);
    snprintf(title, sizeof(title), "CONTROLLER %u", index + 1U);
    if (!g.xinputGetState)
        snprintf(detail, sizeof(detail), "XInput DLL unavailable");
    else if (!pad->connected)
        snprintf(detail, sizeof(detail), "not connected");
    else
        snprintf(detail, sizeof(detail), "packet %lu   fields changed %u",
                 (unsigned long)pad->state.dwPacketNumber, pad->lastChangedFields);
    draw_panel_title(dc, panel, title, detail);
    if (!g.xinputGetState || !pad->connected) {
        message.left = panel->left + 12;
        message.right = panel->right - 12;
        message.top = panel->top + 36;
        message.bottom = panel->bottom - 10;
        draw_text(dc,
                  !g.xinputGetState ? "No xinput1_4.dll, xinput1_3.dll or xinput9_1_0.dll found"
                                    : "Controller not connected",
                  message, g.fontNormal, COLOR_MUTED,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        return;
    }
    if (pad->lastChangedFields > 1) {
        RECT changed = {panel->right - 92, panel->top + 5, panel->right - 8, panel->top + 30};
        char changedText[24];
        draw_box(dc, &changed, RGB(70, 48, 111), COLOR_PRISM, 4);
        snprintf(changedText, sizeof(changedText), "CHANGED %u", pad->lastChangedFields);
        draw_text(dc, changedText, changed, g.fontTiny, COLOR_TEXT, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }
    contentTop = panel->top + 34;
    bar.left = panel->left + 10;
    bar.right = panel->left + (panel->right - panel->left) / 2 - 5;
    bar.top = contentTop;
    bar.bottom = contentTop + 16;
    draw_trigger(dc, bar, "LT", state->bLeftTrigger);
    bar.left = bar.right + 10;
    bar.right = panel->right - 10;
    draw_trigger(dc, bar, "RT", state->bRightTrigger);

    chipWidth = (panel->right - panel->left - 20) / 7;
    for (i = 0; i < sizeof(xbuttonDefs) / sizeof(xbuttonDefs[0]); ++i) {
        RECT chip;
        int row = (int)(i / 7U);
        int col = (int)(i % 7U);
        BOOL active = (state->wButtons & xbuttonDefs[i].mask) != 0;
        chip.left = panel->left + 10 + col * chipWidth + 1;
        chip.right = panel->left + 10 + (col + 1) * chipWidth - 1;
        chip.top = contentTop + 22 + row * 23;
        chip.bottom = chip.top + 20;
        draw_box(dc, &chip, active ? COLOR_ACTIVE : COLOR_PANEL_ALT, active ? COLOR_ACCENT : COLOR_BORDER, 3);
        draw_text(dc, xbuttonDefs[i].name, chip, g.fontTiny, COLOR_TEXT,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    }
    stickRadius = clamp_int((panel->bottom - (contentTop + 76) - 20) / 2, 13, 42);
    draw_stick(dc, panel->left + (panel->right - panel->left) / 4, contentTop + 76 + stickRadius, stickRadius,
               state->sThumbLX, state->sThumbLY, "LEFT STICK");
    draw_stick(dc, panel->left + (panel->right - panel->left) * 3 / 4, contentTop + 76 + stickRadius,
               stickRadius, state->sThumbRX, state->sThumbRY, "RIGHT STICK");
}

static void draw_timeline(HDC dc, const RECT *panel) {
    RECT header;
    RECT row;
    int rowHeight = 19;
    int rows;
    unsigned visible;
    unsigned first;
    unsigned i;
    draw_box(dc, panel, COLOR_PANEL, COLOR_BORDER, 8);
    draw_panel_title(dc, panel, "EVENT TIMELINE", "512-event ring / durable 250 ms log flush");
    header.left = panel->left + 8;
    header.right = panel->right - 8;
    header.top = panel->top + 33;
    header.bottom = header.top + rowHeight;
    fill_color(dc, &header, RGB(28, 41, 56));
    draw_text(dc,
              "TIME (ms)       SOURCE       ACTION      CODE                  NAME                    VALUE",
              header, g.fontTiny, COLOR_MUTED, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    rows = (panel->bottom - header.bottom - 6) / rowHeight;
    if (rows < 0) rows = 0;
    visible = g.eventCount < (unsigned)rows ? g.eventCount : (unsigned)rows;
    first = (g.eventHead + EVENT_CAPACITY - visible) % EVENT_CAPACITY;
    for (i = 0; i < visible; ++i) {
        const TimelineEvent *event = &g.events[(first + i) % EVENT_CAPACITY];
        char text[512];
        COLORREF color = COLOR_TEXT;
        row.left = panel->left + 9;
        row.right = panel->right - 9;
        row.top = header.bottom + (int)i * rowHeight;
        row.bottom = row.top + rowHeight;
        if (i & 1U) fill_color(dc, &row, RGB(17, 26, 37));
        if (strcmp(event->action, "DOWN") == 0)
            color = COLOR_ACCENT;
        else if (strcmp(event->action, "UP") == 0)
            color = COLOR_ORANGE;
        else if (strcmp(event->action, "REPEAT") == 0)
            color = COLOR_WARN;
        snprintf(text, sizeof(text), "%10llu   %-12.12s %-10.10s %-21.21s %-23.23s %s",
                 (unsigned long long)event->timeMs, event->source, event->action, event->code, event->name,
                 event->value);
        draw_text(dc, text, row, g.fontMono, color, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    }
}

static void render_dashboard(HDC dc, int width, int height) {
    RECT keyboard;
    RECT mouse;
    RECT pads[PAD_COUNT];
    RECT timeline;
    int margin = 8;
    int statusHeight = 64;
    int timelineHeight;
    int bodyTop;
    int bodyBottom;
    int bodyHeight;
    int leftWidth;
    int rightLeft;
    int rightWidth;
    int mouseHeight;
    int padTop;
    int padHeight;
    int halfRight;

    RECT background = {0, 0, width, height};
    fill_color(dc, &background, COLOR_BG);
    draw_status(dc, width);
    if (width < 960 || height < 640) {
        RECT notice = {8, statusHeight + 8, width - 8, height - 8};
        draw_box(dc, &notice, COLOR_PANEL, COLOR_BORDER, 8);
        draw_text(dc, "InputGlass needs a little more room. Enlarge the window to reveal the live dashboard.",
                  notice, g.fontNormal, COLOR_MUTED,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        return;
    }
    timelineHeight = clamp_int(height * 28 / 100, 170, 330);
    if (height < 650) timelineHeight = clamp_int(height / 3, 130, 210);
    bodyTop = statusHeight + margin;
    bodyBottom = height - timelineHeight - margin * 2;
    bodyHeight = bodyBottom - bodyTop;
    if (bodyHeight < 180) bodyHeight = 180;
    leftWidth = width * 57 / 100;
    rightLeft = leftWidth + margin;
    rightWidth = width - rightLeft - margin;

    keyboard.left = margin;
    keyboard.top = bodyTop;
    keyboard.right = leftWidth;
    keyboard.bottom = bodyBottom;
    draw_keyboard(dc, &keyboard);

    mouseHeight = clamp_int(bodyHeight * 28 / 100, 130, 185);
    mouse.left = rightLeft;
    mouse.top = bodyTop;
    mouse.right = width - margin;
    mouse.bottom = bodyTop + mouseHeight;
    draw_mouse(dc, &mouse);

    padTop = mouse.bottom + margin;
    padHeight = (bodyBottom - padTop - margin) / 2;
    halfRight = rightLeft + rightWidth / 2;
    pads[0].left = rightLeft;
    pads[0].top = padTop;
    pads[0].right = halfRight - margin / 2;
    pads[0].bottom = padTop + padHeight;
    pads[1].left = halfRight + margin / 2;
    pads[1].top = padTop;
    pads[1].right = width - margin;
    pads[1].bottom = padTop + padHeight;
    pads[2].left = rightLeft;
    pads[2].top = padTop + padHeight + margin;
    pads[2].right = halfRight - margin / 2;
    pads[2].bottom = bodyBottom;
    pads[3].left = halfRight + margin / 2;
    pads[3].top = padTop + padHeight + margin;
    pads[3].right = width - margin;
    pads[3].bottom = bodyBottom;
    draw_controller(dc, &pads[0], 0);
    draw_controller(dc, &pads[1], 1);
    draw_controller(dc, &pads[2], 2);
    draw_controller(dc, &pads[3], 3);

    timeline.left = margin;
    timeline.top = bodyBottom + margin;
    timeline.right = width - margin;
    timeline.bottom = height - margin;
    draw_timeline(dc, &timeline);
}

static void destroy_backbuffer(void) {
    if (g.backDc && g.backOldBitmap) SelectObject(g.backDc, g.backOldBitmap);
    if (g.backBitmap) DeleteObject(g.backBitmap);
    if (g.backDc) DeleteDC(g.backDc);
    g.backDc = NULL;
    g.backBitmap = NULL;
    g.backOldBitmap = NULL;
    g.backWidth = 0;
    g.backHeight = 0;
}

static void create_backbuffer(HWND hwnd, int width, int height) {
    HDC windowDc;
    if (width <= 0 || height <= 0) return;
    destroy_backbuffer();
    windowDc = GetDC(hwnd);
    if (!windowDc) return;
    g.backDc = CreateCompatibleDC(windowDc);
    g.backBitmap = CreateCompatibleBitmap(windowDc, width, height);
    if (g.backDc && g.backBitmap) {
        g.backOldBitmap = SelectObject(g.backDc, g.backBitmap);
        g.backWidth = width;
        g.backHeight = height;
    } else
        destroy_backbuffer();
    ReleaseDC(hwnd, windowDc);
}

static HFONT make_font(int pointSize, int weight, int dpi, const WCHAR *face) {
    return CreateFontW(-MulDiv(pointSize, dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_DONTCARE, face);
}

static void create_fonts(int dpi) {
    g.fontTitle = make_font(15, FW_BOLD, dpi, L"Segoe UI");
    g.fontHeading = make_font(11, FW_BOLD, dpi, L"Segoe UI");
    g.fontNormal = make_font(10, FW_SEMIBOLD, dpi, L"Segoe UI");
    g.fontSmall = make_font(9, FW_NORMAL, dpi, L"Segoe UI");
    g.fontTiny = make_font(8, FW_NORMAL, dpi, L"Segoe UI");
    g.fontMono = make_font(8, FW_NORMAL, dpi, L"Consolas");
}

static void destroy_fonts(void) {
    if (g.fontTitle) DeleteObject(g.fontTitle);
    if (g.fontHeading) DeleteObject(g.fontHeading);
    if (g.fontNormal) DeleteObject(g.fontNormal);
    if (g.fontSmall) DeleteObject(g.fontSmall);
    if (g.fontTiny) DeleteObject(g.fontTiny);
    if (g.fontMono) DeleteObject(g.fontMono);
    g.fontTitle = g.fontHeading = g.fontNormal = g.fontSmall = g.fontTiny = g.fontMono = NULL;
}

static void clear_dashboard(void) {
    unsigned i;
    for (i = 0; i < KEY_COUNT; ++i) {
        g.keys[i].downs = 0;
        g.keys[i].ups = 0;
        g.keys[i].repeats = 0;
        if (g.keys[i].down) g.keys[i].pressedAt = now_ms();
    }
    memset(g.mouse.downs, 0, sizeof(g.mouse.downs));
    memset(g.mouse.ups, 0, sizeof(g.mouse.ups));
    g.mouse.totalDx = 0;
    g.mouse.totalDy = 0;
    g.mouse.wheelVTotal = 0;
    g.mouse.wheelHTotal = 0;
    g.mouse.wheelVEvents = 0;
    g.mouse.wheelHEvents = 0;
    g.eventHead = 0;
    g.eventCount = 0;
    add_event("SYSTEM", "CHANGE", "CLEAR", "Dashboard", "timeline and counters cleared; log retained");
}

static BOOL point_in_rect_lparam(const RECT *rect, LPARAM lParam) {
    POINT point = {(SHORT)LOWORD(lParam), (SHORT)HIWORD(lParam)};
    return PtInRect(rect, point);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        g.hwnd = hwnd;
        {
            HDC dc = GetDC(hwnd);
            int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
            if (dc) ReleaseDC(hwnd, dc);
            create_fonts(dpi);
        }
        add_event("SYSTEM", "CHANGE", "START", "Session",
                  "InputGlass " APP_VERSION "; 16 ms XInput polling requested");
        register_raw_input(hwnd);
        load_xinput();
        if (!SetTimer(hwnd, TIMER_ID, TIMER_INTERVAL_MS, NULL)) {
            char value[64];
            snprintf(value, sizeof(value), "SetTimer failed error=%lu", (unsigned long)GetLastError());
            add_event("SYSTEM", "CHANGE", "TIMER", "XInput polling unavailable", value);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *limits = (MINMAXINFO *)lParam;
        limits->ptMinTrackSize.x = 1060;
        limits->ptMinTrackSize.y = 700;
        return 0;
    }
    case WM_SETFOCUS:
        g.focused = TRUE;
        add_event("SYSTEM", "CHANGE", "FOCUS", "Window focus", "gained");
        return 0;
    case WM_KILLFOCUS:
        g.focused = FALSE;
        release_active_input();
        add_event("SYSTEM", "CHANGE", "FOCUS", "Window focus", "lost; active states released");
        return 0;
    case WM_INPUT:
        process_raw_input((HRAWINPUT)lParam);
        return DefWindowProcW(hwnd, message, wParam, lParam);
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        log_fallback_key(message, wParam, lParam);
        break;
    case WM_MOUSEMOVE:
        fallback_mouse_move(lParam);
        return 0;
    case WM_LBUTTONDOWN:
        fallback_mouse_button(message, TRUE, 0);
        return 0;
    case WM_LBUTTONUP:
        fallback_mouse_button(message, FALSE, 0);
        if (point_in_rect_lparam(&g.clearButton, lParam))
            clear_dashboard();
        else if (point_in_rect_lparam(&g.openButton, lParam)) {
            HINSTANCE result = ShellExecuteW(hwnd, L"open", g.logDir, NULL, NULL, SW_SHOWNORMAL);
            char value[64];
            INT_PTR resultCode = (INT_PTR)result;
            snprintf(value, sizeof(value), "ShellExecute result=%lld", (long long)resultCode);
            add_event("SYSTEM", resultCode <= 32 ? "ERROR" : "CHANGE", "OPEN-LOGS", "Logs folder", value);
        }
        return 0;
    case WM_MBUTTONDOWN:
        fallback_mouse_button(message, TRUE, 1);
        return 0;
    case WM_MBUTTONUP:
        fallback_mouse_button(message, FALSE, 1);
        return 0;
    case WM_RBUTTONDOWN:
        fallback_mouse_button(message, TRUE, 2);
        return 0;
    case WM_RBUTTONUP:
        fallback_mouse_button(message, FALSE, 2);
        return 0;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP: {
        unsigned index = HIWORD(wParam) == XBUTTON1 ? 3U : 4U;
        fallback_mouse_button(message, message == WM_XBUTTONDOWN, index);
        return TRUE;
    }
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
        BOOL horizontal = message == WM_MOUSEHWHEEL;
        SHORT delta = GET_WHEEL_DELTA_WPARAM(wParam);
        char code[24];
        char value[64];
        snprintf(code, sizeof(code), "MSG=0x%04X", message);
        if (!g.rawRegistered)
            update_mouse_wheel(horizontal, delta, "WIN32-MOUSE", code);
        else {
            snprintf(value, sizeof(value), "fallback integrated=0 delta=%d", (int)delta);
            add_event("WIN32-MOUSE", "CHANGE", code, horizontal ? "Horizontal wheel" : "Vertical wheel",
                      value);
        }
        return 0;
    }
    case WM_TIMER:
        if (wParam == TIMER_ID) {
            poll_xinput();
            flush_logs(FALSE);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        break;
    case WM_DPICHANGED: {
        const RECT *suggested = (const RECT *)lParam;
        destroy_fonts();
        create_fonts((int)HIWORD(wParam));
        SetWindowPos(hwnd, NULL, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOACTIVATE | SWP_NOZORDER);
        return 0;
    }
    case WM_SIZE:
        create_backbuffer(hwnd, LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(hwnd, &paint);
        RECT client;
        GetClientRect(hwnd, &client);
        if (!g.backDc || g.backWidth != client.right || g.backHeight != client.bottom)
            create_backbuffer(hwnd, client.right, client.bottom);
        if (g.backDc && g.backWidth > 0 && g.backHeight > 0) {
            render_dashboard(g.backDc, g.backWidth, g.backHeight);
            BitBlt(dc, 0, 0, g.backWidth, g.backHeight, g.backDc, 0, 0, SRCCOPY);
        }
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_DESTROY:
        if (!g.shuttingDown) {
            g.shuttingDown = TRUE;
            KillTimer(hwnd, TIMER_ID);
            add_event("SYSTEM", "CHANGE", "STOP", "Session", "window destroyed");
            if (g.xinputModule) FreeLibrary(g.xinputModule);
            g.xinputModule = NULL;
            g.xinputGetState = NULL;
            destroy_backbuffer();
            destroy_fonts();
            close_logs();
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previousInstance, PWSTR commandLine, int showCommand) {
    WNDCLASSEXW windowClass;
    HWND hwnd;
    MSG message;
    int messageResult;
    (void)previousInstance;
    (void)commandLine;
    memset(&g, 0, sizeof(g));
    QueryPerformanceFrequency(&g.frequency);
    QueryPerformanceCounter(&g.startCounter);
    SetProcessDPIAware();
    initialize_log_paths();

    memset(&windowClass, 0, sizeof(windowClass));
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = window_proc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_INPUTGLASS));
    if (!windowClass.hIcon) windowClass.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    windowClass.hbrBackground = NULL;
    windowClass.lpszClassName = L"InputGlassWindow";
    windowClass.hIconSm = windowClass.hIcon;
    if (!RegisterClassExW(&windowClass)) {
        add_event("SYSTEM", "CHANGE", "FATAL", "RegisterClassEx", "failed");
        MessageBoxW(NULL, L"InputGlass could not register its window class.", APP_TITLE,
                    MB_OK | MB_ICONERROR);
        close_logs();
        return 1;
    }

    hwnd = CreateWindowExW(0, windowClass.lpszClassName, APP_TITLE, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           CW_USEDEFAULT, CW_USEDEFAULT, 1500, 900, NULL, NULL, instance, NULL);
    if (!hwnd) {
        add_event("SYSTEM", "CHANGE", "FATAL", "CreateWindowEx", "failed");
        MessageBoxW(NULL, L"InputGlass could not create its main window.", APP_TITLE, MB_OK | MB_ICONERROR);
        close_logs();
        return 1;
    }
    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);
    memset(&message, 0, sizeof(message));
    while ((messageResult = (int)GetMessageW(&message, NULL, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (messageResult == -1) {
        MessageBoxW(NULL, L"The Windows message loop stopped unexpectedly.", APP_TITLE, MB_OK | MB_ICONERROR);
        return 1;
    }
    return (int)message.wParam;
}
