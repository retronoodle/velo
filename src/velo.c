/* Entry point and main loop: wires menus and keys to the editor modules. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "brace.h"
#include "buffer.h"
#include "term.h"
#include "ui.h"
#include "menu.h"
#include "options.h"
#include "aisetup.h"
#include "aiedit.h"
#include "aicomplete.h"
#include "aichat.h"
#include "sidebar.h"
#include "aitools.h"
#include "aiinstr.h"
#include "aiexplain.h"
#include "files.h"
#include "undo.h"
#include "clipboard.h"
#include "search.h"
#include "utf8.h"
#include "recovery.h"
#include "mouse.h"
#include "tabs.h"
#include "recent.h"
#include "gitmodal.h"
#include "watch.h"
#include "quickopen.h"

#define IDLE_POLL_MS 1000   /* how often the main loop wakes when no key is pressed */

static int needs_redraw = 1;

/* Types one character (n UTF-8 bytes) at the cursor. Over a selection, the
   selection is replaced in one undo step. */
static void type_chars(const char *s, int n) {
    /* Brace help: only for a plain typed bracket or quote with no selection */
    if (n == 1 && brace_type_char((unsigned char)s[0])) return;
    if (sel_begin_replace()) {
        insert_text(s, n);
        undo_end_group();
        return;
    }
    insert_utf8(s, n);
}

/* Runs a menu action. Returns 1 if the editor should quit. */
static int run_action(int action) {
    switch (action) {
        case ACT_NEW:        sel_clear(); do_new(); break;
        case ACT_OPEN:       sel_clear(); do_open(); break;
        case ACT_RECENT:     sel_clear(); do_open_recent(); break;
        case ACT_QUICK_OPEN: sel_clear(); do_quick_open(); break;
        case ACT_SAVE:       do_save(); break;
        case ACT_SAVE_AS:    do_save_as(); break;
        case ACT_CLOSE_TAB:  do_close_tab(); break;
        case ACT_NEXT_TAB:   tab_next(); break;
        case ACT_PREV_TAB:   tab_prev(); break;
        case ACT_EXIT:       return do_exit();

        case ACT_UNDO:       sel_clear(); undo_undo(); break;
        case ACT_REDO:       sel_clear(); undo_redo(); break;
        case ACT_CUT:        clip_cut(); break;
        case ACT_COPY:       clip_copy(); break;
        case ACT_PASTE:      clip_paste(); break;
        case ACT_SELECT_ALL: sel_select_all(); break;

        case ACT_FIND:       search_find(); break;
        case ACT_FIND_NEXT:  search_next(); break;
        case ACT_REPLACE:    search_replace(); break;
        case ACT_GOTO_LINE:  search_goto_line(); break;

        case ACT_TAB_WIDTH:
        case ACT_LINE_ENDS:
        case ACT_COLOR_SCHEME:
        case ACT_LINE_NUMBERS:
        case ACT_SYNTAX:
        case ACT_WRAP:
        case ACT_BRACE:
            options_cycle(action);
            break;
        case ACT_LANGUAGE:   /* per file, not saved: Auto, Plain, then each language */
            editor.syn_override = (editor.syn_override + 1) % 10;
            break;
        case ACT_AI_SETUP:   ai_setup(); break;
        case ACT_AI_ASK:     ai_ask_selection(); break;
        case ACT_AI_EXPLAIN: ai_explain_selection(); break;
        case ACT_AI_COMPLETE: aicomplete_request(); break;
        case ACT_AI_CHAT:    sidebar_blur(); aichat_toggle(); break;
        case ACT_SIDEBAR:    sidebar_toggle(); break;
        case ACT_AI_PLAN:    sidebar_blur(); aichat_toggle_plan(); break;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int running = 1;

    init_editor();
    tabs_init();
    recent_load();
    options_load();   /* before opening files: a file's own line endings still win */
    aiinstr_set_root();
    aitools_set_root();     /* the AI chat's file tools stay inside this folder */
    sidebar_set_root();     /* so does the file tree */
    quickopen_set_root();   /* Ctrl+P searches from here, whatever folder the browser visits */

    /* Each file named on the command line opens in its own tab. The first
       one replaces the empty starting tab; the first tab is shown at start. */
    for (int i = 1; i < argc; i++) {
        open_path(argv[i], 1);
    }
    if (argc > 1) tab_switch(0);

#ifndef _WIN32
    enable_raw_mode();
#endif

#ifdef _WIN32
    configure_console();
#else
    set_line_wrap(0);
#endif
    term_update_size();
    clear_screen();
    set_cursor_visibility(1);

    recovery_check_on_startup();

    while (running) {
        recovery_tick();
        if (!view_detached) adjust_scroll();
        if (needs_redraw) {
            refresh_screen();
            needs_redraw = 0;
        }

        int key = read_key_wait(aicomplete_busy() ? 100 : IDLE_POLL_MS);
        if (key == -1) {        /* timed out (or EOF): check the file on disk, then loop so backups keep running */
            if (aicomplete_tick()) needs_redraw = 1;
            if (watch_tick()) needs_redraw = 1;
            continue;
        }
        if (key == KEY_RESIZE) {    /* window changed size: re-read it and redraw all */
            term_update_size();
            clear_screen();
            needs_redraw = 1;
            continue;
        }
        view_detached = 0;          /* a key brings the view back to the cursor */
        if (key == KEY_TAB && aicomplete_accept()) {   /* Tab takes a shown AI suggestion */
            needs_redraw = 1;
            continue;
        }
        if (key != KEY_RESIZE) aicomplete_dismiss();   /* any other input drops it */
        if (key == KEY_PASTE) {     /* text pasted by the terminal (Ctrl+Shift+V): insert it as typed text */
            int len = 0;
            char *text = term_take_paste(&len);
            if (text && sidebar_focused()) { /* a tree has nothing to paste into */ }
            else if (text && aichat_focused()) aichat_paste(text, len);
            else if (text && !editor.menu_active) clip_paste_text(text, len);
            free(text);
            needs_redraw = 1;
            continue;
        }
        if (key == KEY_MOUSE) {     /* click, drag or wheel: mouse.c maps it to editing or menu actions */
            MouseEvent ev;
            term_get_mouse(&ev);
            needs_redraw = 1;
            if (aichat_mouse(&ev)) {
                if (ev.kind == MEV_PRESS || ev.kind == MEV_RIGHT_PRESS) sidebar_blur();
                continue;
            }   /* clicks and wheel inside the chat pane */
            if (sidebar_mouse(&ev)) continue;  /* clicks, right-click menu and wheel inside the file tree */
            int action = mouse_handle(&ev);
            if (action != ACT_NONE && run_action(action)) {
                running = 0;
            }
            continue;
        }
        needs_redraw = 1;

        // Menu activation keys (F10, Alt+F/E/S/O)
        if (menu_open_key(key)) {
            continue;
        }

        // Menu mode: the menu consumes every key while it is open
        if (editor.menu_active) {
            int action = menu_handle_key(key);
            if (action != ACT_NONE && run_action(action)) {
                running = 0;
            }
            continue;
        }

        // The AI chat pane takes typing while it has focus (other keys fall through)
        if (sidebar_focused() && sidebar_key(key)) continue;
        if (aichat_focused() && aichat_key(key)) continue;

        // Normal editing mode
        switch (key) {
            case KEY_ALT_J: sidebar_blur(); aichat_toggle(); break;
            case KEY_ALT_B: sidebar_toggle(); break;
            case KEY_ALT_P: run_action(ACT_AI_PLAN); break;
            case KEY_ALT_G: git_modal(); break;
            case KEY_ALT_I: run_action(ACT_AI_ASK); break;
            case KEY_ALT_SPACE: run_action(ACT_AI_COMPLETE); break;
            case KEY_SHIFT_UP: sel_extend(); move_cursor(0, -1); break;
            case KEY_SHIFT_DOWN: sel_extend(); move_cursor(0, 1); break;
            case KEY_SHIFT_LEFT: sel_extend(); move_cursor(-1, 0); break;
            case KEY_SHIFT_RIGHT: sel_extend(); move_cursor(1, 0); break;
            case KEY_BLOCK_UP: sel_extend_block(); move_cursor(0, -1); break;
            case KEY_BLOCK_DOWN: sel_extend_block(); move_cursor(0, 1); break;
            case KEY_BLOCK_LEFT: sel_extend_block(); move_cursor(-1, 0); break;
            case KEY_BLOCK_RIGHT: sel_extend_block(); move_cursor(1, 0); break;

            case KEY_UP: sel_clear(); move_cursor(0, -1); break;
            case KEY_DOWN: sel_clear(); move_cursor(0, 1); break;
            case KEY_LEFT: sel_clear(); move_cursor(-1, 0); break;
            case KEY_RIGHT: sel_clear(); move_cursor(1, 0); break;

            case KEY_F1: show_help(); break;

            case KEY_CTRL_N: sel_clear(); do_new(); break;
            case KEY_CTRL_O: sel_clear(); do_open(); break;
            case KEY_CTRL_E: sel_clear(); do_open_recent(); break;
            case KEY_CTRL_P: sel_clear(); do_quick_open(); break;
            case KEY_CTRL_S: do_save(); break;
            case KEY_CTRL_Q: if (do_exit()) running = 0; break;
            case KEY_CTRL_Z: sel_clear(); undo_undo(); break;
            case KEY_CTRL_Y: sel_clear(); undo_redo(); break;
            case KEY_CTRL_C: clip_copy(); break;
            case KEY_CTRL_X: clip_cut(); break;
            case KEY_CTRL_V: clip_paste(); break;
            case KEY_CTRL_A: sel_select_all(); break;
            case KEY_CTRL_W: do_close_tab(); break;
            case KEY_F6: tab_next(); break;
            case KEY_F7: tab_prev(); break;
            case KEY_CTRL_F: search_find(); break;
            case KEY_CTRL_R: search_replace(); break;
            case KEY_CTRL_G: search_goto_line(); break;
            case KEY_F3: search_next(); break;

            case KEY_INSERT: editor.insert_mode = !editor.insert_mode; break;

            case KEY_DELETE:
                if (sel_begin_replace()) undo_end_group();
                else delete_char();
                break;

            case KEY_ENTER: {
                int replaced = sel_begin_replace();
                if (!replaced) undo_begin_group();   /* split + indent undo as one step */
                insert_newline();
                undo_end_group();
                break;
            }
            case KEY_BACKSPACE:
                if (sel_begin_replace()) undo_end_group();
                else if (!brace_backspace()) backspace();
                break;
            case KEY_TAB: {
                /* Width comes from Options; the longest option is 8 spaces */
                static const char spaces[] = "        ";
                if (sel_begin_replace()) {
                    insert_text(spaces, options.tab_width);
                    undo_end_group();
                } else {
                    for (int i = 0; i < options.tab_width; i++) insert_utf8(" ", 1);
                }
                break;
            }

            default:
                /* Any printable Unicode character (not a control code, not a key code) */
                if (key >= 32 && key != 127 && (key < 0x80 || key >= 0xA0) && key < KEY_BASE) {
                    char buf[4];
                    int n = utf8_encode(key, buf);
                    type_chars(buf, n);
                }
                break;
        }
    }

    clear_screen();
#ifdef _WIN32
    restore_console();
#else
    set_line_wrap(1);
#endif
    set_cursor_visibility(1);
    tabs_free_all();
    return 0;
}
