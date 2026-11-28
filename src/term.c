/* Terminal layer: frame buffer, console/raw mode and key input. */
#ifndef _WIN32
    #define _POSIX_C_SOURCE 200809L   /* sigaction, SIGWINCH under -std=c99 */
    #ifdef __APPLE__
        #define _DARWIN_C_SOURCE      /* SIGWINCH is hidden by _POSIX_C_SOURCE on macOS */
    #endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifdef _WIN32
    #include <conio.h>
    #include <windows.h>
    #ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
        #define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
    #endif
#else
    #include <termios.h>
    #include <unistd.h>
    #include <signal.h>
    #include <sys/ioctl.h>
    #include <time.h>
#endif

#include "editor.h"
#include "term.h"
#include "utf8.h"
#include "clipcodec.h"

/* Screen size in cells. Set by term_update_size(). */
int screen_w = SCREEN_DEFAULT_W;
int screen_h = SCREEN_DEFAULT_H;

/* Frame buffer: all screen output is built here and written in one go,
   so the terminal never shows a half-drawn frame (fixes flicker).
   It grows as needed, since a large window can need more than a few KB. */
#define FRAME_INIT 16384
static char *frame_buf = NULL;
static int frame_len = 0;
static int frame_cap = 0;

/* Makes room for `extra` more bytes. Returns 0 if out of memory. */
static int fb_reserve(int extra) {
    if (extra <= frame_cap - frame_len) return 1;
    int cap = frame_cap ? frame_cap : FRAME_INIT;
    while (cap - frame_len < extra) cap *= 2;
    char *p = realloc(frame_buf, cap);
    if (!p) return 0;
    frame_buf = p;
    frame_cap = cap;
    return 1;
}

void fb_reset(void) {
    frame_len = 0;
}

void fb_write(const char *s, int n) {
    if (n <= 0 || !fb_reserve(n)) return;
    memcpy(frame_buf + frame_len, s, n);
    frame_len += n;
}

void fb_puts(const char *s) {
    fb_write(s, (int)strlen(s));
}

void fb_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n <= 0 || !fb_reserve(n + 1)) return;
    va_start(ap, fmt);
    vsnprintf(frame_buf + frame_len, n + 1, fmt, ap);
    va_end(ap);
    frame_len += n;
}

void fb_gotoxy(int x, int y) {
    fb_printf("\x1b[%d;%dH", y + 1, x + 1);
}

/* The last mouse event read. Set by the platform input code, copied out by term_get_mouse(). */
static MouseEvent g_mouse;

void term_get_mouse(MouseEvent *ev) {
    *ev = g_mouse;
}

/* The text of the last bracketed paste (KEY_PASTE). Ownership passes to term_take_paste(). */
static char *g_paste = NULL;
static int g_paste_len = 0;

char *term_take_paste(int *len) {
    char *p = g_paste;
    *len = g_paste_len;
    g_paste = NULL;
    g_paste_len = 0;
    return p;
}

unsigned long term_ticks_ms(void) {
#ifdef _WIN32
    return (unsigned long)GetTickCount();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)ts.tv_sec * 1000UL + (unsigned long)(ts.tv_nsec / 1000000L);
#endif
}

#ifdef _WIN32
void gotoxy(int x, int y) {
    /* VT sequence: relative to the visible window, so it stays right if the window is scrolled */
    printf("\x1b[%d;%dH", y + 1, x + 1);
}

void clear_screen() {
    fputs("\x1b[2J\x1b[H", stdout);
    fflush(stdout);
}

void set_cursor_visibility(int visible) {
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &cursorInfo);
    cursorInfo.bVisible = visible;
    SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &cursorInfo);
}

int kbhit() {
    return 0;   /* input is read as events; see next_key_event() */
}

/* Console input is read as UTF-16 events (ReadConsoleInputW), so typed
   non-ASCII characters arrive intact. Key-down events only. */
static HANDLE g_in = NULL;
static DWORD g_orig_in_mode = 0;
static UINT g_orig_cp = 0;
static int g_cp_changed = 0;
static int pending_high = 0;   /* UTF-16 high surrogate waiting for its pair */

#define RESIZE_CH (-2)   /* pseudo-character: the console window was resized */
#define MOUSE_CH (-3)    /* pseudo-character: a mouse event (in g_mouse) */

#ifndef ENABLE_QUICK_EDIT_MODE
    #define ENABLE_QUICK_EDIT_MODE 0x0040
#endif
#ifndef ENABLE_EXTENDED_FLAGS
    #define ENABLE_EXTENDED_FLAGS 0x0080
#endif

static int g_prev_left = 0;    /* left button state at the previous mouse record */
static int g_prev_right = 0;   /* right button state at the previous mouse record */

/* Turns a mouse record into g_mouse. Returns 1 if it is an event the editor uses
   (press, drag, release or wheel), 0 for moves with no button held. */
static int mouse_record(const MOUSE_EVENT_RECORD *m) {
    CONSOLE_SCREEN_BUFFER_INFO info;
    int ox = 0, oy = 0;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        ox = info.srWindow.Left;   /* buffer -> window coordinates */
        oy = info.srWindow.Top;
    }
    g_mouse.x = m->dwMousePosition.X - ox;
    g_mouse.y = m->dwMousePosition.Y - oy;
    g_mouse.shift = (m->dwControlKeyState & SHIFT_PRESSED) != 0;

    if (m->dwEventFlags & MOUSE_WHEELED) {
        short delta = (short)HIWORD(m->dwButtonState);
        g_mouse.kind = delta > 0 ? MEV_WHEEL_UP : MEV_WHEEL_DOWN;
        return 1;
    }

    int right = (m->dwButtonState & RIGHTMOST_BUTTON_PRESSED) != 0;
    int was_right = g_prev_right;
    g_prev_right = right;
    if (right && !was_right) {
        g_mouse.kind = MEV_RIGHT_PRESS;
        return 1;
    }

    int left = (m->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0;
    int was_left = g_prev_left;
    g_prev_left = left;
    if (left && !was_left) g_mouse.kind = MEV_PRESS;
    else if (!left && was_left) g_mouse.kind = MEV_RELEASE;
    else if (left && (m->dwEventFlags & MOUSE_MOVED)) g_mouse.kind = MEV_DRAG;
    else return 0;
    return 1;
}

/* Waits for a key-down event, at most timeout_ms (INFINITE to wait forever).
   Returns 0 with the UTF-16 unit or code point in *ch (0 for keys with no
   character), the virtual-key code in *vk, and the control-key state in *ctrl.
   Returns 1 if the timeout passed with no key. */
static int next_key_event(int *vk, int *ch, DWORD *ctrl, DWORD timeout_ms) {
    DWORD start = GetTickCount();
    for (;;) {
        DWORD wait = timeout_ms;
        if (timeout_ms != INFINITE) {
            DWORD elapsed = GetTickCount() - start;   /* unsigned, so wrap-safe */
            if (elapsed >= timeout_ms) return 1;
            wait = timeout_ms - elapsed;
        }
        /* Wakes on any console input, including events we discard below, so loop */
        DWORD r = WaitForSingleObject(g_in, wait);
        if (r == WAIT_TIMEOUT) return 1;
        if (r != WAIT_OBJECT_0) exit(1);

        INPUT_RECORD rec;
        DWORD n = 0;
        if (!ReadConsoleInputW(g_in, &rec, 1, &n)) {
            /* Not a console (or the handle died): nothing to read, so stop cleanly */
            exit(1);
        }
        if (n == 0) continue;
        if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            *vk = 0;
            *ch = RESIZE_CH;
            *ctrl = 0;
            return 0;
        }
        if (rec.EventType == MOUSE_EVENT) {
            if (!mouse_record(&rec.Event.MouseEvent)) continue;
            *vk = 0;
            *ch = MOUSE_CH;
            *ctrl = 0;
            return 0;
        }
        if (rec.EventType != KEY_EVENT) continue;
        /* cmd.exe swallows the key-down of Ctrl+S (flow control) and sends only the key-up: take that one */
        static int ctrl_s_down = 0;
        int is_ctrl_s = rec.Event.KeyEvent.uChar.UnicodeChar == 19;
        if (rec.Event.KeyEvent.bKeyDown) {
            if (is_ctrl_s) ctrl_s_down = 1;
        } else {
            if (!is_ctrl_s) continue;
            if (ctrl_s_down) { ctrl_s_down = 0; continue; }
        }

        int u = rec.Event.KeyEvent.uChar.UnicodeChar;
        if (u >= 0xD800 && u <= 0xDBFF) {          /* high surrogate: wait for low */
            pending_high = u;
            continue;
        }
        if (u >= 0xDC00 && u <= 0xDFFF && pending_high) {
            u = 0x10000 + ((pending_high - 0xD800) << 10) + (u - 0xDC00);
            pending_high = 0;
        } else {
            pending_high = 0;
        }
        *vk = rec.Event.KeyEvent.wVirtualKeyCode;
        *ch = u;
        *ctrl = rec.Event.KeyEvent.dwControlKeyState;
        return 0;
    }
}

int getch_cp(void) {
    int vk, ch;
    DWORD ctrl;
    do {
        next_key_event(&vk, &ch, &ctrl, INFINITE);
    } while (ch == MOUSE_CH);   /* prompts never see mouse events */
    return ch;
}

/* Windows sends Alt+key as a key event, not an ESC prefix, so there is nothing extra to eat. */
int getch_prompt(void) {
    return getch_cp();
}

int getch_impl(void) {
    return getch_cp();
}

static DWORD g_orig_out_mode = 0;
void configure_console() {
    g_in = GetStdHandle(STD_INPUT_HANDLE);
    if (g_in != INVALID_HANDLE_VALUE && GetConsoleMode(g_in, &g_orig_in_mode)) {
        /* Raw-ish input: Ctrl+C arrives as a key, not a signal; no line editing or echo */
        /* ENABLE_WINDOW_INPUT reports window resizes as events. ENABLE_MOUSE_INPUT reports
           clicks; QuickEdit must be off or the console takes the mouse for itself. */
        SetConsoleMode(g_in, (g_orig_in_mode & ~(ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_QUICK_EDIT_MODE))
                             | ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);
    }

    g_orig_cp = GetConsoleOutputCP();
    if (SetConsoleOutputCP(65001)) g_cp_changed = 1;   /* UTF-8 output */
    atexit(restore_console);                           /* also covers the exit(1) above */

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    if (!GetConsoleMode(hOut, &g_orig_out_mode)) return;
    DWORD mode = g_orig_out_mode;
    mode &= ~ENABLE_WRAP_AT_EOL_OUTPUT;
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(hOut, mode)) {
        /* Older consoles lack VT support; fall back to the original mode */
        SetConsoleMode(hOut, g_orig_out_mode & ~ENABLE_WRAP_AT_EOL_OUTPUT);
    }
}

void restore_console() {
    if (g_in && g_in != INVALID_HANDLE_VALUE && g_orig_in_mode) {
        SetConsoleMode(g_in, g_orig_in_mode);
    }
    if (g_cp_changed) SetConsoleOutputCP(g_orig_cp);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    if (g_orig_out_mode) {
        SetConsoleMode(hOut, g_orig_out_mode);
    }
}

#else
struct termios orig_termios;

/* Set by SIGWINCH when the terminal is resized; read_key() reports it. */
static volatile sig_atomic_t g_winch = 0;

static void on_winch(int sig) {
    (void)sig;
    g_winch = 1;
}

void disable_raw_mode() {
    printf("\x1b[?2004l\x1b[?1006l\x1b[?1002l");   /* bracketed paste and mouse reporting off */
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_winch;
    sa.sa_flags = SA_RESTART;   /* keep reads going across the signal */
    sigaction(SIGWINCH, &sa, NULL);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_cflag |= (CS8);
    raw.c_oflag &= ~(OPOST);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    /* Button-event tracking (1002) with SGR coordinates (1006): clicks, drags and wheel.
       Bracketed paste (2004): the terminal wraps pasted text in ESC[200~ ... ESC[201~. */
    printf("\x1b[?2004h\x1b[?1002h\x1b[?1006h");
    fflush(stdout);
}

void gotoxy(int x, int y) {
    printf("\x1b[%d;%dH", y + 1, x + 1);
}

void clear_screen() {
    printf("\x1b[2J");
    printf("\x1b[H");
}

void set_cursor_visibility(int visible) {
    if (visible) {
        printf("\x1b[?25h");
    } else {
        printf("\x1b[?25l");
    }
}

void set_line_wrap(int enable) {
    if (enable) {
        printf("\x1b[?7h");
    } else {
        printf("\x1b[?7l");
    }
}

/* The termios read times out (VTIME) when idle and returns 0 bytes. glibc
   then latches stdin's EOF flag, so every later getchar() fails without
   reading the keyboard. Clear the flag on each timeout so input resumes. */
static int read_paste(void);   /* defined below; getch_cp() needs it for ESC[200~ */

static int posix_getc(void) {
    int ch = getchar();
    if (ch == EOF) clearerr(stdin);
    return ch;
}

/* The byte after ESC (Alt+key, Esc-[ ...) can be late right after the window
   regains focus. A single read gives up after VTIME (0.1 s), so the ESC would
   register as a bare Esc and the letter would be typed. Retry for about 0.3 s. */
static int esc_follow_getc(void) {
    int ch = EOF;
    for (int i = 0; i < 3 && ch == EOF; i++) ch = posix_getc();
    return ch;
}

int kbhit() {
    int ch = posix_getc();
    if (ch != EOF) {
        ungetc(ch, stdin);
        return 1;
    }
    return 0;
}

int getch_impl() {
    int ch = posix_getc();
    return ch;
}

/* Reads one UTF-8 character as a code point (-1 on EOF, -1 if invalid). */
int getch_cp() {
    for (;;) {
        int ch = posix_getc();
        if (ch == EOF) return -1;
        if (ch == 27) {
            /* ESC: a mouse report (ESC [ < ...) is swallowed like on Windows, so it
               neither cancels the prompt nor gets typed. Anything else is put back
               and returned as Esc. */
            int a = esc_follow_getc();
            if (a == '[') {
                int b = posix_getc();
                if (b == '<') {
                    int c;
                    do {
                        c = posix_getc();
                    } while (c != EOF && c != 'M' && c != 'm');
                    continue;
                }
                if (b == '2') {
                    /* ESC[200~ is a bracketed paste: prompts take it too, not just the main loop */
                    int c1 = posix_getc();
                    int c2 = c1 == '0' ? posix_getc() : EOF;
                    int c3 = c2 == '0' ? posix_getc() : EOF;
                    if (c3 == '~') return read_paste();
                    /* Not a paste: put the bytes back, last read first */
                    if (c3 != EOF) ungetc(c3, stdin);
                    if (c2 != EOF) ungetc(c2, stdin);
                    if (c1 != EOF) ungetc(c1, stdin);
                }
                if (b != EOF) ungetc(b, stdin);
            }
            if (a != EOF) ungetc(a, stdin);
            return 27;
        }
        if (ch < 0x80) return ch;
        if (ch < 0xC2 || ch > 0xF4) return -1;
        int need = ch >= 0xF0 ? 3 : ch >= 0xE0 ? 2 : 1;
        char buf[4];
        buf[0] = (char)ch;
        for (int i = 1; i <= need; i++) {
            int c = posix_getc();
            if (c == EOF) return -1;
            buf[i] = (char)c;
        }
        int cp;
        if (utf8_decode(buf, need + 1, &cp) != need + 1) return -1;
        if (cp == 0xA0) return KEY_ALT_SPACE;   // macOS Option+Space without Option-as-Alt
        return cp;
    }
}

/* Y/N and Esc prompts. An Esc answers the prompt, but the rest of what it
   started must not be typed later: an Alt+key chord (ESC then a printable byte)
   or an arrow's CSI sequence (ESC [ ... final). getch_cp() puts those bytes back,
   so eat them here. A second Esc is left for the next read. */
int getch_prompt(void) {
    int c = getch_cp();
    if (c != 27) return c;
    int b = posix_getc();
    if (b == '[' || b == 'O') {
        int f;
        do {
            f = posix_getc();
        } while (f != EOF && (f < 0x40 || f > 0x7E));
    } else if (b != EOF && !(b >= 0x20 && b < 0x7F)) {
        ungetc(b, stdin);
    }
    return 27;
}
#endif

/* Unified key codes and key reader to normalize Windows and POSIX input.
   Fixes:
   - Backspace handling on terminals that send 127 (DEL) instead of 8.
   - Function keys on POSIX (which begin with ESC) no longer trigger immediate exit.
*/

#ifdef _WIN32
int read_key_wait(int ms) {
    int vk, ch;
    DWORD ctrl;
    if (next_key_event(&vk, &ch, &ctrl, ms < 0 ? INFINITE : (DWORD)ms)) return -1;
    if (ch == RESIZE_CH) return KEY_RESIZE;
    if (ch == MOUSE_CH) return KEY_MOUSE;
    int shift = (ctrl & SHIFT_PRESSED) != 0;
    int alt = (ctrl & LEFT_ALT_PRESSED) != 0;

    /* Keys with no character: arrows, function keys, Insert/Delete */
    if (ch == 0) {
        if (alt && shift) {
            switch (vk) {
                case VK_UP:    return KEY_BLOCK_UP;
                case VK_DOWN:  return KEY_BLOCK_DOWN;
                case VK_LEFT:  return KEY_BLOCK_LEFT;
                case VK_RIGHT: return KEY_BLOCK_RIGHT;
            }
        }
        switch (vk) {
            case VK_UP:     return shift ? KEY_SHIFT_UP : KEY_UP;
            case VK_DOWN:   return shift ? KEY_SHIFT_DOWN : KEY_DOWN;
            case VK_LEFT:   return shift ? KEY_SHIFT_LEFT : KEY_LEFT;
            case VK_RIGHT:  return shift ? KEY_SHIFT_RIGHT : KEY_RIGHT;
            case VK_F1:     return KEY_F1;
            case VK_F2:     return KEY_F2;
            case VK_F3:     return KEY_F3;
            case VK_F4:     return KEY_F4;
            case VK_F5:     return KEY_F5;
            case VK_F6:     return KEY_F6;
            case VK_F7:     return KEY_F7;
            case VK_F10:    return KEY_F10;
            case VK_INSERT: return KEY_INSERT;
            case VK_DELETE: return KEY_DELETE;
            default:        return -1;
        }
    }

    /* Alt+letter for the menu shortcuts */
    if (alt && (ch == 'f' || ch == 'F')) return KEY_ALT_F;
    if (alt && (ch == 'e' || ch == 'E')) return KEY_ALT_E;
    if (alt && (ch == 's' || ch == 'S')) return KEY_ALT_S;
    if (alt && (ch == 'o' || ch == 'O')) return KEY_ALT_O;
    if (alt && (ch == 'g' || ch == 'G')) return KEY_ALT_G;
    if (alt && (ch == 'i' || ch == 'I')) return KEY_ALT_I;
    if (alt && (ch == 'j' || ch == 'J')) return KEY_ALT_J;
    if (alt && (ch == 'b' || ch == 'B')) return KEY_ALT_B;
    if (alt && (ch == 'p' || ch == 'P')) return KEY_ALT_P;
    if (alt && ch == ' ') return KEY_ALT_SPACE;

    switch (ch) {
        case 27:  return KEY_ESC;
        case 13:  return KEY_ENTER;
        case 8:
        case 127: return KEY_BACKSPACE;
        case 9:   return KEY_TAB;
        case 14:  return KEY_CTRL_N;
        case 15:  return KEY_CTRL_O;
        case 17:  return KEY_CTRL_Q;
        case 19:  return KEY_CTRL_S;
        case 25:  return KEY_CTRL_Y;
        case 26:  return KEY_CTRL_Z;
        case 3:   return KEY_CTRL_C;
        case 22:  return KEY_CTRL_V;
        case 24:  return KEY_CTRL_X;
        case 5:   return KEY_CTRL_E;
        case 6:   return KEY_CTRL_F;
        case 18:  return KEY_CTRL_R;
        case 7:   return KEY_CTRL_G;
        case 1:   return KEY_CTRL_A;
        case 23:  return KEY_CTRL_W;
        case 16:  return KEY_CTRL_P;
        case 12:  return KEY_CTRL_L;
        default:  return ch;   /* printable text, any Unicode code point */
    }
}
int read_key() {
    int k;
    do {
        k = read_key_wait(-1);
    } while (k == KEY_MOUSE);   /* prompts and browsers ignore the mouse */
    return k;
}
#else
/* Reads decimal digits; *term gets the first character after them. */
static int read_num(int *term) {
    int n = 0, c;
    while ((c = getch_impl()) != EOF && c >= '0' && c <= '9') n = n * 10 + (c - '0');
    *term = c;
    return n;
}

/* SGR mouse report, read after "ESC [ <": "b;x;yM" (press, drag, wheel) or "b;x;ym" (release). */
static int read_sgr_mouse(void) {
    int term;
    int b = read_num(&term);
    if (term != ';') return -1;
    int x = read_num(&term);
    if (term != ';') return -1;
    int y = read_num(&term);
    if (term != 'M' && term != 'm') return -1;

    int base = b & ~(4 | 8 | 16);      /* strip the shift, meta and ctrl bits */
    g_mouse.x = x - 1;
    g_mouse.y = y - 1;
    g_mouse.shift = (b & 4) != 0;
    if (base == 64) g_mouse.kind = MEV_WHEEL_UP;
    else if (base == 65) g_mouse.kind = MEV_WHEEL_DOWN;
    else if (base == 32) g_mouse.kind = MEV_DRAG;           /* left button held while moving */
    else if (base == 0) g_mouse.kind = (term == 'M') ? MEV_PRESS : MEV_RELEASE;
    else if (base == 2 && term == 'M') g_mouse.kind = MEV_RIGHT_PRESS;   /* right button press only */
    else return -1;                                           /* other buttons and releases are ignored */
    return KEY_MOUSE;
}

static int read_key_any(void);

/* POSIX: VTIME already makes the read return after ~100 ms (as -1), so ms is unused */
int read_key_wait(int ms) {
    (void)ms;
    return read_key_any();
}

/* Like read_key_wait, but skips mouse events and pastes (prompts and browsers use this). */
int read_key() {
    int k;
    do {
        k = read_key_any();
        if (k == KEY_PASTE) {
            int len;
            free(term_take_paste(&len));
        }
    } while (k == KEY_MOUSE || k == KEY_PASTE);
    return k;
}

/* Reads a bracketed paste after ESC[200~ up to ESC[201~. The text is cleaned and
   kept for term_take_paste(). Returns KEY_PASTE, or -1 if memory ran out. */
static int read_paste(void) {
    static const char end[] = "\x1b[201~";
    const size_t end_len = sizeof end - 1;
    size_t cap = 0, len = 0, seen = 0;
    char *buf = NULL;
    char tail[sizeof end - 1];   /* last bytes read, to spot the end marker */
    int failed = 0;              /* out of memory: keep reading (and dropping) so the rest is not typed */
    for (;;) {
        int c = getch_impl();
        if (c == EOF) break;
        if (!failed) {
            if (len == cap) {
                size_t ncap = cap ? cap * 2 : 4096;
                char *p = realloc(buf, ncap);
                if (p) { buf = p; cap = ncap; }
                else failed = 1;
            }
            if (!failed) buf[len++] = (char)c;
        }
        memmove(tail, tail + 1, end_len - 1);
        tail[end_len - 1] = (char)c;
        if (seen < end_len) seen++;
        if (seen == end_len && memcmp(tail, end, end_len) == 0) {
            if (!failed) len -= end_len;   /* the end marker is not part of the text */
            break;
        }
    }
    if (failed) {
        free(buf);
        return -1;
    }
    if (buf) len = (size_t)clip_clean_paste(buf, (int)len);
    free(g_paste);
    g_paste = buf;
    g_paste_len = (int)len;
    return KEY_PASTE;
}

static int read_key_any(void) {
    if (g_winch) {            /* resized since the last key: report it first */
        g_winch = 0;
        return KEY_RESIZE;
    }
    int ch = getch_impl(); // getchar() in POSIX branch
    if (ch == EOF) return -1;

    if (ch == 0) return KEY_ALT_SPACE;              // Ctrl+Space sends NUL: AI autocomplete too
    if (ch == 127 || ch == 8) return KEY_BACKSPACE; // DEL or BS
    if (ch == 9) return KEY_TAB;
    if (ch == 13 || ch == 10) return KEY_ENTER;
    if (ch == 5) return KEY_CTRL_E;
    if (ch == 14) return KEY_CTRL_N;
    if (ch == 15) return KEY_CTRL_O;
    if (ch == 17) return KEY_CTRL_Q;
    if (ch == 19) return KEY_CTRL_S;
    if (ch == 25) return KEY_CTRL_Y;
    if (ch == 26) return KEY_CTRL_Z;
    if (ch == 3) return KEY_CTRL_C;
    if (ch == 22) return KEY_CTRL_V;
    if (ch == 24) return KEY_CTRL_X;
    if (ch == 6) return KEY_CTRL_F;
    if (ch == 18) return KEY_CTRL_R;
    if (ch == 7) return KEY_CTRL_G;
    if (ch == 1) return KEY_CTRL_A;
    if (ch == 23) return KEY_CTRL_W;
    if (ch == 16) return KEY_CTRL_P;
    if (ch == 12) return KEY_CTRL_L;

    if (ch >= 0xC2 && ch <= 0xF4) {   // UTF-8 lead byte: return the code point
        int need = ch >= 0xF0 ? 3 : ch >= 0xE0 ? 2 : 1;
        char buf[4];
        buf[0] = (char)ch;
        for (int i = 1; i <= need; i++) {
            int c = getch_impl();
            if (c == EOF) return -1;
            buf[i] = (char)c;
        }
        int cp;
        if (utf8_decode(buf, need + 1, &cp) != need + 1) return -1;
        return cp;
    }
    if ((ch < 0x20 && ch != 27) || (ch >= 0x80 && ch < 0xC2) || ch > 0xF4) return -1;

    if (ch != 27) { // Not an escape sequence
        return ch;
    }

    // Begin parsing escape sequences
    int a = esc_follow_getc();   // retries: the byte after ESC can be late (see esc_follow_getc)
    if (a == EOF) return KEY_ESC; // Bare ESC

    if (a == '[') {
        int b = getch_impl();
        if (b == EOF) return KEY_ESC;
        if (b == '<') return read_sgr_mouse();

        if (b >= '0' && b <= '9') {
            // CSI with digits, possibly ending with '~'
            char digits[8];   /* room for ESC[200~ and ESC[201~ (three digits) */
            int idx = 0;
            digits[idx++] = (char)b;
            int c;
            while (idx < 7) {
                c = getch_impl();
                if (c == EOF) break;
                if (c >= '0' && c <= '9') {
                    digits[idx++] = (char)c;
                } else {
                    if (c == ';') {
                        /* Modified key, e.g. ESC[1;2A is Shift+Up */
                        int m = getch_impl();
                        int f = getch_impl();
                        if (m == '2') {
                            switch (f) {
                                case 'A': return KEY_SHIFT_UP;
                                case 'B': return KEY_SHIFT_DOWN;
                                case 'C': return KEY_SHIFT_RIGHT;
                                case 'D': return KEY_SHIFT_LEFT;
                            }
                        } else if (m == '4') {   /* Alt+Shift (xterm modifier 4) */
                            switch (f) {
                                case 'A': return KEY_BLOCK_UP;
                                case 'B': return KEY_BLOCK_DOWN;
                                case 'C': return KEY_BLOCK_RIGHT;
                                case 'D': return KEY_BLOCK_LEFT;
                            }
                        }
                        return -1;
                    }
                    if (c == '~') {
                        digits[idx] = '\0';
                        int num = atoi(digits);
                        switch (num) {
                            case 2:  return KEY_INSERT; // ESC[2~
                            case 3:  return KEY_DELETE; // ESC[3~
                            case 11: return KEY_F1;     // ESC[11~
                            case 12: return KEY_F2;     // ESC[12~
                            case 13: return KEY_F3;     // ESC[13~
                            case 14: return KEY_F4;     // ESC[14~
                            case 15: return KEY_F5;     // ESC[15~
                            case 17: return KEY_F6;     // ESC[17~
                            case 18: return KEY_F7;     // ESC[18~
                            case 21: return KEY_F10;    // ESC[21~
                            case 200: return read_paste(); // ESC[200~ starts a bracketed paste
                            default: return -1;
                        }
                    }
                    return -1;
                }
            }
            return -1;
        } else {
            // Single-letter CSI
            switch (b) {
                case 'A': return KEY_UP;
                case 'B': return KEY_DOWN;
                case 'C': return KEY_RIGHT;
                case 'D': return KEY_LEFT;
                default:  return -1;
            }
        }
    } else if (a == 'O') {
        // SS3 sequences (F1-F4 commonly)
        int b = getch_impl();
        switch (b) {
            case 'P': return KEY_F1; // ESCOP
            case 'Q': return KEY_F2; // ESCOQ
            case 'R': return KEY_F3; // ESCOR
            case 'S': return KEY_F4; // ESCOS
            default:  return -1;
        }
    } else {
        // Alt+letter combinations (ESC followed by letter)
        switch (a) {
            case 'f':
            case 'F': return KEY_ALT_F;
            case 'e':
            case 'E': return KEY_ALT_E;
            case 's':
            case 'S': return KEY_ALT_S;
            case 'o':
            case 'O': return KEY_ALT_O;
            case 'g':
            case 'G': return KEY_ALT_G;
            case 'i':
            case 'I': return KEY_ALT_I;
            case 'j':
            case 'J': return KEY_ALT_J;
            case 'b':
            case 'B': return KEY_ALT_B;
            case 'p':
            case 'P': return KEY_ALT_P;
            case ' ': return KEY_ALT_SPACE;
            default: return KEY_ESC; // Treat as ESC if not recognized
        }
    }

    return -1;
}
#endif

/* Sends the whole frame to the terminal in one write. */
void fb_flush(void) {
    if (frame_len > 0) fwrite(frame_buf, 1, frame_len, stdout);
    fflush(stdout);
}

/* Reads the console window size into screen_w/screen_h. Falls back to 80x25
   when the size is unknown, and never goes below the minimum. */
void term_update_size(void) {
    int w = 0, h = 0;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        w = info.srWindow.Right - info.srWindow.Left + 1;
        h = info.srWindow.Bottom - info.srWindow.Top + 1;
    }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        w = ws.ws_col;
        h = ws.ws_row;
    }
#endif
    if (w <= 0 || h <= 0) {
        w = SCREEN_DEFAULT_W;
        h = SCREEN_DEFAULT_H;
    }
    if (w < SCREEN_MIN_W) w = SCREEN_MIN_W;
    if (h < SCREEN_MIN_H) h = SCREEN_MIN_H;
    screen_w = w;
    screen_h = h;
}
