#include <wayland-client.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include <algorithm>
#include <set>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <cstdlib>

static void log_to_file(const std::string& msg) {
    static const bool enabled = [] {
        const char* value = getenv("UNIKEY_WAYLAND_DEBUG");
        return value && strcmp(value, "0") != 0;
    }();
    if (!enabled) return;

    std::ofstream f("/tmp/uk_debug.log", std::ios::app);
    if (f.is_open()) {
        f << msg << std::endl;
    }
}

#include <QApplication>
#include <QSocketNotifier>
#include "mainwindow.h"
#include "trayicon.h"

#include "input-method-unstable-v1-client-protocol.h"
// No ukengine_wrapper needed
#include "windowtracker.h"
#include "libbamboo.h"

struct WaylandState {
    wl_display* display;
    wl_registry* registry;
    wl_seat* seat;
    zwp_input_method_v1* input_method;
    zwp_input_method_context_v1* context;
    wl_keyboard* keyboard;
    
    bool viet_mode = true;
    bool active;
    uint32_t latest_serial;
    std::string composed_word = "";
};

// Evdev keycodes map
static char get_ascii_from_keycode(uint32_t key, uint32_t mods) {
    bool shift = (mods & 1); // Shift check
    bool capslock = (mods & 2); // CapsLock check
    bool uppercase = shift ^ capslock; // XOR shift and capslock for letter case

    if (key >= 2 && key <= 10) {
        const char* symbols = "!@#$%^&*(";
        return shift ? symbols[key-2] : '1' + (key-2);
    }
    if (key == 11) return shift ? ')' : '0';
    if (key == 12) return shift ? '_' : '-';
    if (key == 13) return shift ? '+' : '=';
    if (key == 41) return shift ? '~' : '`';
    
    if (key >= 16 && key <= 25) {
        const char* row1 = "qwertyuiop";
        return uppercase ? (row1[key-16] - 32) : row1[key-16];
    }
    if (key >= 30 && key <= 38) {
        const char* row2 = "asdfghjkl";
        return uppercase ? (row2[key-30] - 32) : row2[key-30];
    }
    if (key >= 44 && key <= 50) {
        const char* row3 = "zxcvbnm";
        return uppercase ? (row3[key-44] - 32) : row3[key-44];
    }
    
    if (key == 57) return ' '; // Space
    if (key == 14) return '\b'; // Backspace
    if (key == 28) return '\n'; // Enter
    if (key == 26) return shift ? '{' : '[';
    if (key == 27) return shift ? '}' : ']';
    if (key == 43) return shift ? '|' : '\\';
    if (key == 39) return shift ? ':' : ';';
    if (key == 40) return shift ? '"' : '\'';
    if (key == 51) return shift ? '<' : ',';
    if (key == 52) return shift ? '>' : '.';
    if (key == 53) return shift ? '?' : '/';
    
    return 0; // Unhandled
}

static uint32_t g_modifiers = 0;
static std::set<uint32_t> eaten_keys;

static bool g_ctrl_pressed = false;
static bool g_shift_pressed = false;
static bool g_alt_pressed = false;
static bool g_other_pressed = false;

static MainWindow* g_mainWindow = nullptr;
WindowTracker* g_windowTracker = nullptr;

static bool g_app_excluded = false;

// Keep mutable text in preedit. Rewriting committed text with
// delete_surrounding_text is not atomic through KWin's text-input-v3 bridge.
static void update_preedit(WaylandState* state, const std::string& text) {
    if (text.empty()) {
        zwp_input_method_context_v1_preedit_string(
            state->context, state->latest_serial, "", "");
    } else {
        zwp_input_method_context_v1_preedit_cursor(
            state->context, static_cast<int32_t>(text.size()));
        if (g_app_excluded) {
            zwp_input_method_context_v1_preedit_styling(
                state->context, 0, static_cast<uint32_t>(text.size()), 5);
        }
        zwp_input_method_context_v1_preedit_string(
            state->context, state->latest_serial, text.c_str(), text.c_str());
    }
    state->composed_word = text;
}

static void reset_composition(WaylandState* state, bool clear_client_preedit = false) {
    Bamboo_Reset();
    if (state) {
        if (clear_client_preedit && state->context && !state->composed_word.empty()) {
            zwp_input_method_context_v1_preedit_string(
                state->context, state->latest_serial, "", "");
        }
        state->composed_word.clear();
    }
}

static std::string bamboo_string(bool final) {
    char* value = final ? Bamboo_GetCommitString() : Bamboo_GetPreeditString();
    std::string result = value ? value : "";
    if (value) free(value);
    return result;
}

void show_main_window() {
    if (g_mainWindow) {
        QMetaObject::invokeMethod(g_mainWindow, []() {
            g_mainWindow->show();
            g_mainWindow->raise();
            g_mainWindow->activateWindow();
        });
    }
}

static void keyboard_keymap(void* data, struct wl_keyboard* keyboard, uint32_t format, int32_t fd, uint32_t size) {
    close(fd);
}

static void keyboard_enter(void* data, struct wl_keyboard* keyboard, uint32_t serial, struct wl_surface* surface, struct wl_array* keys) {}
static void keyboard_leave(void* data, struct wl_keyboard* keyboard, uint32_t serial, struct wl_surface* surface) {}

static void keyboard_key(void* data, struct wl_keyboard* keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state_key) {
    WaylandState* state = static_cast<WaylandState*>(data);
    
    if (!state->active || !state->context) {
        return;
    }

    static bool g_switched = false;

    // Track modifier key states
    if (state_key == 1) { // Pressed
        if (key == 29 || key == 97) { // Left/Right Ctrl
            g_ctrl_pressed = true;
            if (!g_shift_pressed && !g_alt_pressed) {
                g_other_pressed = false;
                g_switched = false;
            }
        } else if (key == 42 || key == 54) { // Left/Right Shift
            g_shift_pressed = true;
            if (!g_ctrl_pressed && !g_alt_pressed) {
                g_other_pressed = false;
                g_switched = false;
            }
        } else if (key == 56 || key == 100) { // Left/Right Alt
            g_alt_pressed = true;
        } else {
            g_other_pressed = true;
        }
    } else if (state_key == 0) { // Released
        int switchKeyConfig = g_mainWindow ? g_mainWindow->getSwitchKey() : 0;
        if (key == 29 || key == 97) {
            if (switchKeyConfig == 0 && g_ctrl_pressed && g_shift_pressed && !g_other_pressed && !g_switched) {
                reset_composition(state, true);
                if (g_mainWindow) {
                    g_mainWindow->setVietMode(!state->viet_mode);
                } else {
                    state->viet_mode = !state->viet_mode;
                }
                g_switched = true;
            }
            g_ctrl_pressed = false;
            if (!g_shift_pressed) {
                g_other_pressed = false;
                g_switched = false;
            }
        } else if (key == 42 || key == 54) {
            if (switchKeyConfig == 0 && g_ctrl_pressed && g_shift_pressed && !g_other_pressed && !g_switched) {
                reset_composition(state, true);
                if (g_mainWindow) {
                    g_mainWindow->setVietMode(!state->viet_mode);
                } else {
                    state->viet_mode = !state->viet_mode;
                }
                g_switched = true;
            }
            g_shift_pressed = false;
            if (!g_ctrl_pressed) {
                g_other_pressed = false;
                g_switched = false;
            }
        } else if (key == 56 || key == 100) {
            g_alt_pressed = false;
            g_other_pressed = false;
        }
    }



    // Alt + Z hotkey
    int switchKeyConfig = g_mainWindow ? g_mainWindow->getSwitchKey() : 0;
    bool real_alt_pressed = (g_modifiers & 8) != 0; // Better check to avoid sticky Alt bug
    if (switchKeyConfig == 1 && key == 44 && real_alt_pressed && state_key == 1) {
        reset_composition(state, true);
        if (g_mainWindow) {
            g_mainWindow->setVietMode(!state->viet_mode);
        } else {
            state->viet_mode = !state->viet_mode;
        }
        eaten_keys.insert(key);
        return;
    }

    // Ctrl + Shift + F5 (CS+F5) hotkey to show settings
    if (key == 63 && g_ctrl_pressed && g_shift_pressed && state_key == 1) {
        show_main_window();
        eaten_keys.insert(key);
        return;
    }

    if (state_key == 0) {
        if (eaten_keys.count(key)) {
            eaten_keys.erase(key);
            return; // Key release for an eaten key: drop it
        }
        // Key release: forward it so the app doesn't get stuck
        zwp_input_method_context_v1_key(state->context, serial, time, key, state_key);
        return;
    }
    
    bool has_modifiers = (g_modifiers & (4 | 8 | 64)) != 0;
    char c = has_modifiers ? 0 : get_ascii_from_keycode(key, g_modifiers);
    
    std::stringstream ss_key;
    ss_key << "DEBUG: Key received. code=" << key << ", state=" << state_key 
           << ", ascii=" << (c ? c : '?');
    log_to_file(ss_key.str());
    
    std::stringstream ss_viet;
    ss_viet << "DEBUG: viet_mode=" << state->viet_mode;
    log_to_file(ss_viet.str());

    if (!state->viet_mode) {
        log_to_file("DEBUG: Forwarding in E mode");
        reset_composition(state, true);
        zwp_input_method_context_v1_key(state->context, serial, time, key, state_key);
        return;
    }

    if (c != 0) {
        if (c == '\b') {
            if (state->composed_word.empty()) {
                zwp_input_method_context_v1_key(
                    state->context, serial, time, key, state_key);
                return;
            }
            Bamboo_RemoveLastChar();
            update_preedit(state, bamboo_string(false));
            eaten_keys.insert(key);
            return;
        }

        if (!Bamboo_CanProcessKey(c)) {
            std::string final_commit = bamboo_string(true);

            // Gõ tắt (Macro)
            if (g_mainWindow && g_mainWindow->isMacroEnabled()) {
                const auto& macros = g_mainWindow->getMacros();
                auto macro = macros.find(final_commit);
                if (macro != macros.end()) {
                    final_commit = macro->second;
                }
            }

            if (c == '\n') {
                if (!final_commit.empty()) {
                    zwp_input_method_context_v1_commit_string(
                        state->context, state->latest_serial, final_commit.c_str());
                }
                reset_composition(state);
                zwp_input_method_context_v1_key(
                    state->context, serial, time, key, state_key);
                return;
            }

            // Keep the separator in the same text transaction as the word.
            final_commit += c;
            if (!final_commit.empty()) {
                zwp_input_method_context_v1_commit_string(
                    state->context, state->latest_serial, final_commit.c_str());
            }
            reset_composition(state);
            eaten_keys.insert(key);
            return;
        }

        Bamboo_ProcessKey(c);
        update_preedit(state, bamboo_string(false));
        eaten_keys.insert(key);
        return;
    } else {
        // c == 0 (Phím chức năng, phím tắt Ctrl, Alt, Arrow, Esc...)
        std::string final_commit = bamboo_string(true);
        if (!final_commit.empty()) {
            zwp_input_method_context_v1_commit_string(
                state->context, state->latest_serial, final_commit.c_str());
        }
        reset_composition(state);
    }
    
    // If we didn't handle it (or if it was a backspace/unhandled), forward it to the client
    zwp_input_method_context_v1_key(state->context, serial, time, key, state_key);
}

static void keyboard_modifiers(void* data, struct wl_keyboard* keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
    WaylandState* state = static_cast<WaylandState*>(data);
    g_modifiers = mods_depressed | mods_latched | mods_locked;
    
    if (mods_depressed == 0) {
        g_ctrl_pressed = false;
        g_shift_pressed = false;
        g_alt_pressed = false;
        g_other_pressed = false;
    }

    if (state->context) {
        zwp_input_method_context_v1_modifiers(state->context, serial, mods_depressed, mods_latched, mods_locked, group);
    }
}

static void keyboard_repeat_info(void* data, struct wl_keyboard* keyboard, int32_t rate, int32_t delay) {}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};


static void input_method_context_surrounding_text(void* data, struct zwp_input_method_context_v1* context, const char* text, uint32_t cursor, uint32_t anchor) {
}
static void input_method_context_reset(void* data, struct zwp_input_method_context_v1* context) {
    WaylandState* state = static_cast<WaylandState*>(data);
    if (state) {
        reset_composition(state);
    }
}
static void input_method_context_content_type(void* data, struct zwp_input_method_context_v1* context, uint32_t hint, uint32_t purpose) {
    std::stringstream ss_ct;
    ss_ct << "DEBUG: content_type hint=" << hint << ", purpose=" << purpose;
    log_to_file(ss_ct.str());
}
static void input_method_context_invoke_action(void* data, struct zwp_input_method_context_v1* context, uint32_t button, uint32_t index) {}

static void input_method_context_commit_state(void* data, struct zwp_input_method_context_v1* context, uint32_t serial) {
    WaylandState* state = static_cast<WaylandState*>(data);
    state->latest_serial = serial;
}

static void input_method_context_preferred_language(void* data, struct zwp_input_method_context_v1* context, const char* language) {}

static const struct zwp_input_method_context_v1_listener input_method_context_listener = {
    .surrounding_text = input_method_context_surrounding_text,
    .reset = input_method_context_reset,
    .content_type = input_method_context_content_type,
    .invoke_action = input_method_context_invoke_action,
    .commit_state = input_method_context_commit_state,
    .preferred_language = input_method_context_preferred_language,
};


static void input_method_activate(void* data, struct zwp_input_method_v1* input_method, struct zwp_input_method_context_v1* context) {
    log_to_file("DEBUG: input_method_activate triggered by KWin!");
    WaylandState* state = static_cast<WaylandState*>(data);
    state->active = true;

    g_ctrl_pressed = false;
    g_shift_pressed = false;
    g_alt_pressed = false;
    g_other_pressed = false;
    eaten_keys.clear();
    reset_composition(state);

    if (state->keyboard) {
        wl_proxy_destroy((struct wl_proxy*)state->keyboard);
        state->keyboard = nullptr;
    }

    if (state->context) {
        zwp_input_method_context_v1_destroy(state->context);
    }
    
    state->context = context;
    zwp_input_method_context_v1_add_listener(state->context, &input_method_context_listener, state);
    
    state->keyboard = zwp_input_method_context_v1_grab_keyboard(state->context);
    if (state->keyboard) {
        log_to_file("DEBUG: grab_keyboard succeeded! Adding keyboard listener.");
        wl_keyboard_add_listener(state->keyboard, &keyboard_listener, state);
    } else {
        log_to_file("ERROR: grab_keyboard returned NULL!");
    }
}

static void input_method_deactivate(void* data, struct zwp_input_method_v1* input_method, struct zwp_input_method_context_v1* context) {
    WaylandState* state = static_cast<WaylandState*>(data);
    state->active = false;

    g_ctrl_pressed = false;
    g_shift_pressed = false;
    g_alt_pressed = false;
    g_other_pressed = false;
    eaten_keys.clear();
    reset_composition(state);
    
    if (state->keyboard) {
        wl_proxy_destroy((struct wl_proxy*)state->keyboard);
        state->keyboard = nullptr;
    }
    
    if (state->context == context) {
        zwp_input_method_context_v1_destroy(state->context);
        state->context = nullptr;
    } else {
        zwp_input_method_context_v1_destroy(context);
    }
}

static const struct zwp_input_method_v1_listener input_method_listener = {
    .activate = input_method_activate,
    .deactivate = input_method_deactivate,
};

static void registry_global(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    WaylandState* state = static_cast<WaylandState*>(data);
    
    if (strcmp(interface, wl_seat_interface.name) == 0) {
        state->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 7));
    } else if (strcmp(interface, zwp_input_method_v1_interface.name) == 0) {
        state->input_method = static_cast<zwp_input_method_v1*>(wl_registry_bind(registry, name, &zwp_input_method_v1_interface, 1));
    }
}

static void registry_global_remove(void* data, struct wl_registry* registry, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

int main(int argc, char **argv) {
    Bamboo_Init();
    int im_socket_fd = -1;
    char* wayland_socket_env = getenv("WAYLAND_SOCKET");
    if (wayland_socket_env) {
        int orig_fd = atoi(wayland_socket_env);
        im_socket_fd = dup(orig_fd);
        unsetenv("WAYLAND_SOCKET");
    }

    setenv("QT_QPA_PLATFORM", "wayland;xcb", 0); // Prefer Wayland, fallback to xcb.
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    WaylandState state = {};

    if (im_socket_fd >= 0) {
        state.display = wl_display_connect_to_fd(im_socket_fd);
    } else {
        state.display = wl_display_connect(NULL);
    }

    if (!state.display) {
        std::cerr << "Failed to connect to Wayland display. Running in GUI-only mode." << std::endl;
    } else {
        state.registry = wl_display_get_registry(state.display);
        wl_registry_add_listener(state.registry, &registry_listener, &state);
        wl_display_roundtrip(state.display);
    }

    bool has_wayland_im = (state.input_method != nullptr);
    if (state.display && !has_wayland_im) {
        std::cerr << "Compositor does not support zwp_input_method_v1. Running in GUI-only mode." << std::endl;
    } else if (state.display && has_wayland_im) {
        zwp_input_method_v1_add_listener(state.input_method, &input_method_listener, &state);
    }

    bool is_gnome_edition = false;

    MainWindow mainWindow(&state.viet_mode, is_gnome_edition);
    g_mainWindow = &mainWindow;

    WindowTracker windowTracker;
    g_windowTracker = &windowTracker;
    QObject::connect(&windowTracker, &WindowTracker::activeWindowChangedSignal, [&](const QString& windowClass) {
        const bool was_excluded = g_app_excluded;
        g_app_excluded = windowTracker.isAppExcluded(windowClass.toStdString());
        if (was_excluded != g_app_excluded) {
            reset_composition(&state, true);
        }
        if (g_app_excluded) {
            std::stringstream ss;
            ss << "DEBUG: Application excluded: " << windowClass.toStdString();
            log_to_file(ss.str());
        }
    });

    windowTracker.injectKWinScript();

    TrayIcon* trayIcon = new TrayIcon(&state.viet_mode, &mainWindow, false);

    bool showExclude = false;
    if (argc > 1) {
        if (strcmp(argv[1], "--setup") == 0) {
            mainWindow.show();
        } else if (strcmp(argv[1], "--exclude") == 0) {
            mainWindow.show();
            showExclude = true;
        }
    }
    if (showExclude) {
        mainWindow.selectTab("Danh sách loại trừ");
    }

    log_to_file("Wayland IM v1 Client started with Qt GUI. Waiting for events...");

    QSocketNotifier* waylandNotifier = nullptr;
    if (state.display) {
        waylandNotifier = new QSocketNotifier(
            wl_display_get_fd(state.display), QSocketNotifier::Read, &app);
        QObject::connect(waylandNotifier, &QSocketNotifier::activated,
                         [&state, &app, waylandNotifier]() {
            if (wl_display_dispatch(state.display) == -1) {
                waylandNotifier->setEnabled(false);
                std::cerr << "Wayland display disconnected or error." << std::endl;
                app.quit();
                return;
            }
            while (wl_display_dispatch_pending(state.display) > 0) {}
            wl_display_flush(state.display);
        });
        while (wl_display_dispatch_pending(state.display) > 0) {}
        wl_display_flush(state.display);
    }

    int ret = app.exec();

    if (state.keyboard) {
        wl_proxy_destroy((struct wl_proxy*)state.keyboard);
    }
    if (state.context) {
        zwp_input_method_context_v1_destroy(state.context);
    }
    if (state.display) {
        wl_display_disconnect(state.display);
    }
    if (trayIcon) {
        delete trayIcon;
    }

    return ret;
}
