#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <string.h>
#include <stdio.h>
#include <furi_hal.h>
#include <furi_hal_serial.h>

#define TARGET_VISIBLE_LINES 5
// Display width in characters including cursor and selection marker
#define TARGET_DISPLAY_CHARS 24
#define SCROLL_STEP_DELAY 3
#define MAX_NETWORKS 32
#define LOG_HISTORY 6

// strncat is disabled in Flipper firmware API, so implement a minimal
// replacement similar to BSD strlcat for safe string concatenation.
static size_t safe_strlcat(char* dst, const char* src, size_t dstsize) {
    size_t dlen = strlen(dst);
    size_t slen = strlen(src);
    if(dlen >= dstsize) return dstsize + slen;
    size_t copy = dstsize - dlen - 1;
    if(copy > slen) copy = slen;
    memcpy(dst + dlen, src, copy);
    dst[dlen + copy] = '\0';
    return dlen + slen;
}

typedef enum {
    ScreenMainMenu,
    ScreenScan,
    ScreenTargets,
    ScreenAttack,
    ScreenSniffer,
    ScreenWardrive,
} AppScreen;

typedef enum {
    AttackModeNone,
    AttackModeEvilTwin,
    AttackModeDeauth,
    AttackModeSaeOverflow,
} AttackMode;

typedef struct {
    bool exit_app;
    bool attacking;
    bool scan_in_progress;
    bool waiting_for_results;
    bool scan_results_ready;
    bool sniffer_running;
    bool wardrive_running;
    uint8_t menu_index;
    uint8_t attack_menu_index;
    char attack_notice[32];
    uint8_t target_scroll;      // index of first visible item
    int selected_target;        // index of highlighted item
    uint8_t target_name_offset; // horizontal scroll position
    uint8_t target_scroll_tick; // delay counter for scrolling
    bool target_selected[MAX_NETWORKS]; // selected for attack
    uint8_t network_count;
    uint8_t count_open;
    uint8_t count_wpa;
    uint8_t count_wpa2;
    uint8_t count_wpa3;
    uint8_t network_indices[MAX_NETWORKS];
    char networks[MAX_NETWORKS][48];
    char line_buf[192];
    uint8_t line_pos;
    char log_lines[LOG_HISTORY][64];
    uint8_t log_write_index;
    uint8_t log_size;
    AttackMode current_attack_mode;
    AppScreen screen;
    FuriHalSerialHandle* serial;
    ViewPort* viewport;
} ScanApp;

static void scan_app_send_cmd(ScanApp* app, const char* cmd) {
    if(!app->serial || !cmd) return;
    size_t len = strlen(cmd);
    if(len == 0) return;
    furi_hal_serial_tx(app->serial, (const uint8_t*)cmd, len);
    furi_hal_serial_tx_wait_complete(app->serial);
}

static void scan_app_push_log(ScanApp* app, const char* line) {
    if(!line || line[0] == '\0') return;
    size_t len = strlen(line);
    if(len >= sizeof(app->log_lines[0])) len = sizeof(app->log_lines[0]) - 1;
    uint8_t index = app->log_write_index % LOG_HISTORY;
    memcpy(app->log_lines[index], line, len);
    app->log_lines[index][len] = '\0';
    app->log_write_index = (index + 1) % LOG_HISTORY;
    if(app->log_size < LOG_HISTORY) app->log_size++;
}

static void scan_app_clear_networks(ScanApp* app) {
    app->network_count = 0;
    app->count_open = 0;
    app->count_wpa = 0;
    app->count_wpa2 = 0;
    app->count_wpa3 = 0;
    app->target_scroll = 0;
    app->selected_target = 0;
    app->target_name_offset = 0;
    app->target_scroll_tick = 0;
    memset(app->network_indices, 0, sizeof(app->network_indices));
    memset(app->networks, 0, sizeof(app->networks));
    memset(app->target_selected, 0, sizeof(app->target_selected));
}

static bool scan_app_parse_csv_field(const char** ptr, char* out, size_t out_size) {
    if(!ptr || !*ptr || !out || out_size == 0) return false;
    const char* p = *ptr;
    if(*p != '"') return false;
    p++;
    size_t len = 0;
    while(*p) {
        if(*p == '"') {
            if(p[1] == '"') {
                if(len < out_size - 1) out[len++] = '"';
                p += 2;
            } else {
                p++;
                break;
            }
        } else {
            if(len < out_size - 1) out[len++] = *p;
            p++;
        }
    }
    out[len] = '\0';
    while(*p == ' ') p++;
    if(*p == ',') {
        p++;
        while(*p == ' ') p++;
    }
    *ptr = p;
    return true;
}

static bool scan_app_parse_network_csv(ScanApp* app, const char* line) {
    if(!line || line[0] != '"') return false;
    const char* ptr = line;
    char index_buf[8];
    char ssid_buf[64];
    char bssid_buf[32];
    char channel_buf[8];
    char auth_buf[32];
    char rssi_buf[8];
    char band_buf[16];

    if(!scan_app_parse_csv_field(&ptr, index_buf, sizeof(index_buf))) return false;
    if(!scan_app_parse_csv_field(&ptr, ssid_buf, sizeof(ssid_buf))) return false;
    if(!scan_app_parse_csv_field(&ptr, bssid_buf, sizeof(bssid_buf))) return false;
    if(!scan_app_parse_csv_field(&ptr, channel_buf, sizeof(channel_buf))) return false;
    if(!scan_app_parse_csv_field(&ptr, auth_buf, sizeof(auth_buf))) return false;
    if(!scan_app_parse_csv_field(&ptr, rssi_buf, sizeof(rssi_buf))) return false;
    if(!scan_app_parse_csv_field(&ptr, band_buf, sizeof(band_buf))) return false;

    if(app->network_count >= MAX_NETWORKS) return true;

    int index = atoi(index_buf);
    int channel = atoi(channel_buf);
    int rssi = atoi(rssi_buf);
    (void)rssi;
    (void)band_buf;

    uint8_t slot = app->network_count;
    app->network_indices[slot] = (index > 0) ? index : (slot + 1);

    char entry[48];
    snprintf(entry, sizeof(entry), "Ch%02d %s %s", channel, ssid_buf, auth_buf);
    strncpy(app->networks[slot], entry, sizeof(app->networks[slot]) - 1);
    app->networks[slot][sizeof(app->networks[slot]) - 1] = '\0';

    if(strstr(auth_buf, "WPA3")) {
        app->count_wpa3++;
    } else if(strstr(auth_buf, "WPA2")) {
        app->count_wpa2++;
    } else if(strstr(auth_buf, "WPA")) {
        app->count_wpa++;
    } else {
        app->count_open++;
    }

    app->target_selected[slot] = false;
    app->network_count++;
    app->scan_results_ready = true;
    app->waiting_for_results = false;
    app->scan_in_progress = false;
    return true;
}

static void scan_app_draw_logs(Canvas* canvas, const ScanApp* app, uint8_t start_y, uint8_t max_lines) {
    if(!canvas || !app || app->log_size == 0 || max_lines == 0) return;
    uint8_t lines = app->log_size;
    if(lines > max_lines) lines = max_lines;
    uint8_t start = (app->log_write_index + LOG_HISTORY - app->log_size) % LOG_HISTORY;
    for(uint8_t i = 0; i < lines; i++) {
        uint8_t idx = (start + i) % LOG_HISTORY;
        canvas_draw_str(canvas, 2, start_y + i * 12, app->log_lines[idx]);
    }
}

static uint8_t scan_app_count_selected(const ScanApp* app) {
    if(!app) return 0;
    uint8_t count = 0;
    for(uint8_t i = 0; i < app->network_count; i++) {
        if(app->target_selected[i]) count++;
    }
    return count;
}

static uint8_t scan_app_build_select_command(ScanApp* app, char* buffer, size_t buffer_size) {
    if(!app || !buffer || buffer_size == 0) return 0;
    buffer[0] = '\0';
    snprintf(buffer, buffer_size, "select_networks");
    uint8_t selected = 0;
    for(uint8_t i = 0; i < app->network_count; i++) {
        if(app->target_selected[i]) {
            char tmp[8];
            snprintf(tmp, sizeof(tmp), " %u", app->network_indices[i] ? app->network_indices[i] : (i + 1));
            safe_strlcat(buffer, tmp, buffer_size);
            selected++;
        }
    }
    safe_strlcat(buffer, "\n", buffer_size);
    return selected;
}

static void scan_app_handle_stop(ScanApp* app) {
    if(!app) return;
    app->scan_in_progress = false;
    app->waiting_for_results = false;
    app->attacking = false;
    app->sniffer_running = false;
    app->wardrive_running = false;
    app->current_attack_mode = AttackModeNone;
    app->attack_notice[0] = '\0';
}

static void uart_rx_cb(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* ctx) {
    ScanApp* app = ctx;
    if(event != FuriHalSerialRxEventData) return;
    while(furi_hal_serial_async_rx_available(handle)) {
        char ch = (char)furi_hal_serial_async_rx(handle);
        if(ch == '\n' || ch == '\r') {
            if(app->line_pos > 0) {
                app->line_buf[app->line_pos] = '\0';
                bool update = false;
                if(scan_app_parse_network_csv(app, app->line_buf)) {
                    update = true;
                } else {
                    const char* line = app->line_buf;
                    if(strstr(line, "Background scan started")) {
                        app->scan_in_progress = true;
                        update = true;
                    } else if(strstr(line, "Starting background WiFi scan")) {
                        app->scan_in_progress = true;
                        update = true;
                    } else if(strstr(line, "Scan still in progress")) {
                        app->waiting_for_results = false;
                        app->scan_in_progress = true;
                        update = true;
                    } else if(strstr(line, "Scan results printed")) {
                        app->waiting_for_results = false;
                        app->scan_results_ready = true;
                        update = true;
                    } else if(strstr(line, "No scan has been performed")) {
                        app->waiting_for_results = false;
                        app->scan_results_ready = false;
                        update = true;
                    } else if(strstr(line, "No networks found in last scan")) {
                        scan_app_clear_networks(app);
                        app->waiting_for_results = false;
                        app->scan_results_ready = true;
                        update = true;
                    } else if(strstr(line, "Failed to start scan")) {
                        app->scan_in_progress = false;
                        app->waiting_for_results = false;
                        update = true;
                    } else if(strstr(line, "Background scan stopped")) {
                        app->scan_in_progress = false;
                        update = true;
                    } else if(strstr(line, "Deauth attack task finished") || strstr(line, "SAE overflow task finished")) {
                        app->attacking = false;
                        app->current_attack_mode = AttackModeNone;
                        update = true;
                    } else if(strstr(line, "Deauth attack started")) {
                        if(app->current_attack_mode == AttackModeNone) app->current_attack_mode = AttackModeEvilTwin;
                        app->attacking = true;
                        app->attack_notice[0] = '\0';
                        update = true;
                    } else if(strstr(line, "SAE attack started")) {
                        app->attacking = true;
                        app->current_attack_mode = AttackModeSaeOverflow;
                        app->attack_notice[0] = '\0';
                        update = true;
                    } else if(strstr(line, "SAE overflow: Stop requested")) {
                        app->attacking = false;
                        app->current_attack_mode = AttackModeNone;
                        update = true;
                    } else if(strstr(line, "Failed to create deauth attack task") ||
                              strstr(line, "Failed to create SAE overflow task") ||
                              strstr(line, "SAE overflow attack already running")) {
                        app->attacking = false;
                        if(strstr(line, "SAE overflow")) {
                            app->current_attack_mode = AttackModeNone;
                            snprintf(app->attack_notice, sizeof(app->attack_notice),
                                     strstr(line, "already running") ? "SAE already running" : "SAE start failed");
                        } else if(strstr(line, "deauth")) {
                            snprintf(app->attack_notice, sizeof(app->attack_notice), "Deauth start failed");
                        }
                        update = true;
                    } else if(strstr(line, "Evil twin: no selected APs")) {
                        app->attacking = false;
                        app->current_attack_mode = AttackModeNone;
                        snprintf(app->attack_notice, sizeof(app->attack_notice), "Select targets first");
                        update = true;
                    } else if(strstr(line, "Sniffer started")) {
                        app->sniffer_running = true;
                        update = true;
                    } else if(strstr(line, "Sniffer stopped") || strstr(line, "Sniffer channel task ending")) {
                        app->sniffer_running = false;
                        update = true;
                    } else if(strstr(line, "Failed to start scan for sniffer")) {
                        app->sniffer_running = false;
                        update = true;
                    } else if(strstr(line, "Sniffer already active")) {
                        app->sniffer_running = true;
                        update = true;
                    } else if(strstr(line, "Wardrive task started") || strstr(line, "Wardrive started")) {
                        app->wardrive_running = true;
                        update = true;
                    } else if(strstr(line, "Wardrive stopped") || strstr(line, "Wardrive task forcefully stopped") || strstr(line, "Wardrive: Stop requested")) {
                        app->wardrive_running = false;
                        update = true;
                    } else if(strstr(line, "Failed to initialize GPS") || strstr(line, "Failed to initialize SD card")) {
                        app->wardrive_running = false;
                        update = true;
                    } else if(strstr(line, "All operations stopped")) {
                        scan_app_handle_stop(app);
                        update = true;
                    }

                    scan_app_push_log(app, line);
                    update = true;
                }
                app->line_pos = 0;
                if(update) view_port_update(app->viewport);
            }
        } else if(app->line_pos < sizeof(app->line_buf) - 1) {
            app->line_buf[app->line_pos++] = ch;
        }
    }
}

static const char* scan_app_attack_mode_label(AttackMode mode) {
    switch(mode) {
        case AttackModeEvilTwin:
            return "Evil Twin";
        case AttackModeDeauth:
            return "Deauth";
        case AttackModeSaeOverflow:
            return "SAE Overflow";
        default:
            return "Idle";
    }
}

static void scan_app_draw_callback(Canvas* canvas, void* ctx) {
    ScanApp* app = ctx;
    canvas_clear(canvas);

    if(app->screen == ScreenMainMenu) {
        static const char* labels[] = {
            "Scan",
            "Targets",
            "Attacks",
            "Sniffer",
            "Wardrive",
            "Stop",
            "Reboot",
        };
        for(uint8_t i = 0; i < 7; i++) {
            char line[32];
            const char* suffix = "";
            if(i == 0 && app->scan_in_progress) suffix = " *";
            else if(i == 2 && app->attacking) suffix = " *";
            else if(i == 3 && app->sniffer_running) suffix = " *";
            else if(i == 4 && app->wardrive_running) suffix = " *";
            snprintf(line, sizeof(line), "%c %s%s", (i == app->menu_index) ? '>' : ' ', labels[i], suffix);
            canvas_draw_str(canvas, 2, 12 + i * 12, line);
        }
    } else if(app->screen == ScreenScan) {
        char buf[32];
        if(app->scan_in_progress) {
            canvas_draw_str(canvas, 2, 12, "Scanning...");
            if(app->waiting_for_results) {
                canvas_draw_str(canvas, 2, 24, "Waiting for data");
            } else {
                canvas_draw_str(canvas, 2, 24, "Use Targets to view");
            }
            canvas_draw_str(canvas, 2, 36, "Back: stop");
            scan_app_draw_logs(canvas, app, 60, 1);
        } else {
            canvas_draw_str(canvas, 2, 12, "OK: start scan");
            if(app->waiting_for_results) {
                canvas_draw_str(canvas, 2, 24, "Requesting results...");
            } else if(app->scan_results_ready && app->network_count > 0) {
                snprintf(buf, sizeof(buf), "Found:%d", app->network_count);
                canvas_draw_str(canvas, 2, 24, buf);
                snprintf(buf, sizeof(buf), "Open:%d WPA:%d", app->count_open, app->count_wpa);
                canvas_draw_str(canvas, 2, 36, buf);
                snprintf(buf, sizeof(buf), "WPA2:%d W3:%d", app->count_wpa2, app->count_wpa3);
                canvas_draw_str(canvas, 2, 48, buf);
            } else {
                canvas_draw_str(canvas, 2, 24, "No results yet");
            }
            canvas_draw_str(canvas, 2, 60, "Back");
        }
    } else if(app->screen == ScreenTargets) {
        if(app->waiting_for_results) {
            canvas_draw_str(canvas, 2, 12, "Waiting for results...");
            canvas_draw_str(canvas, 2, 24, "Back");
        } else if(app->network_count == 0) {
            canvas_draw_str(canvas, 2, 12, "No targets");
            canvas_draw_str(canvas, 2, 24, "OK: refresh");
            canvas_draw_str(canvas, 2, 36, "Back");
        } else {
            for(uint8_t i = 0; i < TARGET_VISIBLE_LINES; i++) {
                uint8_t idx = app->target_scroll + i;
                if(idx >= app->network_count) break;
                char line[64];
                char arrow = (idx == app->selected_target) ? '>' : ' ';
                char star = app->target_selected[idx] ? '*' : ' ';
                const char* name = app->networks[idx];
                size_t offset = 0;
                if(idx == app->selected_target) {
                    size_t len = strlen(name);
                    if(len > TARGET_DISPLAY_CHARS - 2) {
                        if(app->target_name_offset > len - (TARGET_DISPLAY_CHARS - 2)) {
                            offset = len - (TARGET_DISPLAY_CHARS - 2);
                        } else {
                            offset = app->target_name_offset;
                        }
                        name = name + offset;
                    }
                }
                snprintf(line, sizeof(line), "%c%c%.*s", arrow, star, TARGET_DISPLAY_CHARS - 2, name);
                canvas_draw_str(canvas, 2, 12 + i * 12, line);
            }
            char footer[32];
            snprintf(footer, sizeof(footer), "Sel:%d/%d", scan_app_count_selected(app), app->network_count);
            canvas_draw_str(canvas, 2, 60, footer);
        }
    } else if(app->screen == ScreenAttack) {
        if(app->attacking) {
            char line[32];
            snprintf(line, sizeof(line), "Running: %s", scan_app_attack_mode_label(app->current_attack_mode));
            canvas_draw_str(canvas, 2, 12, line);
            canvas_draw_str(canvas, 2, 24, "Back: stop");
            scan_app_draw_logs(canvas, app, 36, 3);
        } else {
            uint8_t selected = scan_app_count_selected(app);
            char summary[32];
            snprintf(summary, sizeof(summary), "Selected: %d", selected);
            canvas_draw_str(canvas, 2, 12, summary);
            static const char* attack_labels[] = {"Evil Twin", "Deauth", "SAE Overflow"};
            for(uint8_t i = 0; i < 3; i++) {
                char line[32];
                snprintf(line, sizeof(line), "%c %s", (i == app->attack_menu_index) ? '>' : ' ', attack_labels[i]);
                canvas_draw_str(canvas, 2, 24 + i * 12, line);
            }
            if(app->attack_notice[0]) {
                canvas_draw_str(canvas, 2, 60, app->attack_notice);
            } else {
                canvas_draw_str(canvas, 2, 60, "OK:start  Back");
            }
        }
    } else if(app->screen == ScreenSniffer) {
        if(app->sniffer_running) {
            canvas_draw_str(canvas, 2, 12, "Sniffer running");
            canvas_draw_str(canvas, 2, 24, "OK: results");
            canvas_draw_str(canvas, 2, 36, "Left: probes");
            canvas_draw_str(canvas, 2, 48, "Back: stop");
        } else {
            canvas_draw_str(canvas, 2, 12, "OK: start sniffer");
            canvas_draw_str(canvas, 2, 24, "Right: results");
            canvas_draw_str(canvas, 2, 36, "Left: probes");
            canvas_draw_str(canvas, 2, 48, "Back");
        }
        scan_app_draw_logs(canvas, app, 60, 1);
    } else if(app->screen == ScreenWardrive) {
        if(app->wardrive_running) {
            canvas_draw_str(canvas, 2, 12, "Wardrive running");
            canvas_draw_str(canvas, 2, 24, "Back: stop");
        } else {
            canvas_draw_str(canvas, 2, 12, "OK: start wardrive");
            canvas_draw_str(canvas, 2, 24, "Back");
        }
        scan_app_draw_logs(canvas, app, 36, 3);
    }
}

static void scan_app_input_callback(InputEvent* event, void* ctx) {
    ScanApp* app = ctx;
    if(event->type != InputTypeShort) return;

    switch(app->screen) {
        case ScreenMainMenu:
            if(event->key == InputKeyUp) {
                if(app->menu_index > 0) app->menu_index--;
                view_port_update(app->viewport);
            } else if(event->key == InputKeyDown) {
                if(app->menu_index < 6) app->menu_index++;
                view_port_update(app->viewport);
            } else if(event->key == InputKeyOk) {
                switch(app->menu_index) {
                    case 0:
                        app->screen = ScreenScan;
                        break;
                    case 1:
                        app->screen = ScreenTargets;
                        app->target_scroll = 0;
                        app->selected_target = 0;
                        app->target_name_offset = 0;
                        app->target_scroll_tick = 0;
                        app->waiting_for_results = true;
                        app->scan_results_ready = false;
                        scan_app_clear_networks(app);
                        scan_app_send_cmd(app, "show_scan_results\n");
                        break;
                    case 2:
                        app->screen = ScreenAttack;
                        app->attack_notice[0] = '\0';
                        break;
                    case 3:
                        app->screen = ScreenSniffer;
                        break;
                    case 4:
                        app->screen = ScreenWardrive;
                        break;
                    case 5:
                        scan_app_send_cmd(app, "stop\n");
                        scan_app_handle_stop(app);
                        break;
                    case 6:
                        scan_app_send_cmd(app, "reboot\n");
                        break;
                }
                view_port_update(app->viewport);
            } else if(event->key == InputKeyBack) {
                app->exit_app = true;
            }
            break;

        case ScreenScan:
            if(event->key == InputKeyOk) {
                if(app->scan_in_progress) {
                    app->waiting_for_results = true;
                    app->scan_results_ready = false;
                    scan_app_clear_networks(app);
                    scan_app_send_cmd(app, "show_scan_results\n");
                } else {
                    scan_app_clear_networks(app);
                    app->target_scroll = 0;
                    app->selected_target = 0;
                    app->target_name_offset = 0;
                    app->target_scroll_tick = 0;
                    app->scan_results_ready = false;
                    app->waiting_for_results = false;
                    scan_app_send_cmd(app, "scan_networks\n");
                    app->scan_in_progress = true;
                }
                view_port_update(app->viewport);
            } else if(event->key == InputKeyBack) {
                if(app->scan_in_progress) {
                    scan_app_send_cmd(app, "stop\n");
                    scan_app_handle_stop(app);
                } else {
                    app->screen = ScreenMainMenu;
                }
                view_port_update(app->viewport);
            }
            break;

        case ScreenTargets:
            if(app->waiting_for_results) {
                if(event->key == InputKeyBack) {
                    app->screen = ScreenMainMenu;
                    view_port_update(app->viewport);
                }
            } else if(app->network_count == 0) {
                if(event->key == InputKeyOk) {
                    app->waiting_for_results = true;
                    app->scan_results_ready = false;
                    scan_app_clear_networks(app);
                    scan_app_send_cmd(app, "show_scan_results\n");
                    view_port_update(app->viewport);
                } else if(event->key == InputKeyBack) {
                    app->screen = ScreenMainMenu;
                    view_port_update(app->viewport);
                }
            } else {
                if(event->key == InputKeyUp && app->selected_target > 0) {
                    app->selected_target--;
                    app->target_name_offset = 0;
                    app->target_scroll_tick = 0;
                    if(app->selected_target < app->target_scroll) app->target_scroll--;
                    view_port_update(app->viewport);
                } else if(event->key == InputKeyDown && app->selected_target < (int)app->network_count - 1) {
                    app->selected_target++;
                    app->target_name_offset = 0;
                    app->target_scroll_tick = 0;
                    if(app->selected_target >= app->target_scroll + TARGET_VISIBLE_LINES) app->target_scroll++;
                    view_port_update(app->viewport);
                } else if(event->key == InputKeyLeft) {
                    if(app->target_name_offset > 0) {
                        app->target_name_offset--;
                        app->target_scroll_tick = 0;
                        view_port_update(app->viewport);
                    }
                } else if(event->key == InputKeyRight) {
                    size_t len = strlen(app->networks[app->selected_target]);
                    size_t max_offset = 0;
                    if(len > TARGET_DISPLAY_CHARS - 2) max_offset = len - (TARGET_DISPLAY_CHARS - 2);
                    if(app->target_name_offset < max_offset) {
                        app->target_name_offset++;
                        app->target_scroll_tick = 0;
                        view_port_update(app->viewport);
                    }
                } else if(event->key == InputKeyOk) {
                    app->target_selected[app->selected_target] = !app->target_selected[app->selected_target];
                    view_port_update(app->viewport);
                } else if(event->key == InputKeyBack) {
                    app->screen = ScreenMainMenu;
                    app->target_name_offset = 0;
                    app->target_scroll_tick = 0;
                    view_port_update(app->viewport);
                }
            }
            break;

        case ScreenAttack:
            if(app->attacking) {
                if(event->key == InputKeyBack) {
                    scan_app_send_cmd(app, "stop\n");
                    scan_app_handle_stop(app);
                    view_port_update(app->viewport);
                }
            } else {
                if(event->key == InputKeyUp && app->attack_menu_index > 0) {
                    app->attack_menu_index--;
                    app->attack_notice[0] = '\0';
                    view_port_update(app->viewport);
                } else if(event->key == InputKeyDown && app->attack_menu_index < 2) {
                    app->attack_menu_index++;
                    app->attack_notice[0] = '\0';
                    view_port_update(app->viewport);
                } else if(event->key == InputKeyOk) {
                    uint8_t selected = scan_app_count_selected(app);
                    if(selected == 0) {
                        snprintf(app->attack_notice, sizeof(app->attack_notice), "Select targets first");
                    } else if(app->attack_menu_index == 2 && selected != 1) {
                        snprintf(app->attack_notice, sizeof(app->attack_notice), "SAE needs 1 target");
                    } else {
                        char cmd[192];
                        uint8_t sent = scan_app_build_select_command(app, cmd, sizeof(cmd));
                        if(sent > 0) {
                            scan_app_send_cmd(app, cmd);
                            const char* attack_cmd = NULL;
                            switch(app->attack_menu_index) {
                                case 0:
                                    attack_cmd = "start_evil_twin\n";
                                    app->current_attack_mode = AttackModeEvilTwin;
                                    break;
                                case 1:
                                    attack_cmd = "start_deauth\n";
                                    app->current_attack_mode = AttackModeDeauth;
                                    break;
                                case 2:
                                    attack_cmd = "start_sae_overflow\n";
                                    app->current_attack_mode = AttackModeSaeOverflow;
                                    break;
                            }
                            if(attack_cmd) {
                                scan_app_send_cmd(app, attack_cmd);
                                app->attacking = true;
                                app->attack_notice[0] = '\0';
                            }
                        }
                    }
                    view_port_update(app->viewport);
                } else if(event->key == InputKeyBack) {
                    app->screen = ScreenMainMenu;
                    view_port_update(app->viewport);
                }
            }
            break;

        case ScreenSniffer:
            if(event->key == InputKeyOk) {
                if(app->sniffer_running) {
                    scan_app_send_cmd(app, "show_sniffer_results\n");
                } else {
                    scan_app_send_cmd(app, "start_sniffer\n");
                    app->sniffer_running = true;
                }
                view_port_update(app->viewport);
            } else if(event->key == InputKeyLeft) {
                scan_app_send_cmd(app, "show_probes\n");
            } else if(event->key == InputKeyRight) {
                scan_app_send_cmd(app, "show_sniffer_results\n");
            } else if(event->key == InputKeyBack) {
                if(app->sniffer_running) {
                    scan_app_send_cmd(app, "stop\n");
                    scan_app_handle_stop(app);
                } else {
                    app->screen = ScreenMainMenu;
                }
                view_port_update(app->viewport);
            }
            break;

        case ScreenWardrive:
            if(event->key == InputKeyOk) {
                if(!app->wardrive_running) {
                    scan_app_send_cmd(app, "start_wardrive\n");
                    app->wardrive_running = true;
                }
                view_port_update(app->viewport);
            } else if(event->key == InputKeyBack) {
                if(app->wardrive_running) {
                    scan_app_send_cmd(app, "stop\n");
                    scan_app_handle_stop(app);
                } else {
                    app->screen = ScreenMainMenu;
                }
                view_port_update(app->viewport);
            }
            break;
    }
}

int32_t scan_app(void* p) {
    (void)p;
    ScanApp app = {
        .exit_app = false,
        .attacking = false,
        .scan_in_progress = false,
        .waiting_for_results = false,
        .scan_results_ready = false,
        .sniffer_running = false,
        .wardrive_running = false,
        .menu_index = 0,
        .attack_menu_index = 0,
        .attack_notice = "",
        .target_scroll = 0,
        .selected_target = 0,
        .target_name_offset = 0,
        .target_scroll_tick = 0,
        .network_count = 0,
        .count_open = 0,
        .count_wpa = 0,
        .count_wpa2 = 0,
        .count_wpa3 = 0,
        .line_pos = 0,
        .log_write_index = 0,
        .log_size = 0,
        .current_attack_mode = AttackModeNone,
        .screen = ScreenMainMenu,
        .serial = NULL,
    };

    app.serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!app.serial) {
        return 0; // Serial interface unavailable
    }

    bool init_serial = !furi_hal_bus_is_enabled(FuriHalBusUSART1);
    if(init_serial) {
        furi_hal_serial_init(app.serial, 115200);
    } else {
        /* Reinitialize to clear any pending data from console */
        furi_hal_serial_deinit(app.serial);
        furi_hal_serial_init(app.serial, 115200);
    }

    const char* reboot_cmd = "reboot\n";
    furi_hal_serial_tx(app.serial, (const uint8_t*)reboot_cmd, strlen(reboot_cmd));
    furi_hal_serial_tx_wait_complete(app.serial);
    furi_delay_ms(500);

    furi_hal_serial_async_rx_start(app.serial, uart_rx_cb, &app, false);

    Gui* gui = furi_record_open(RECORD_GUI);
    app.viewport = view_port_alloc();
    view_port_draw_callback_set(app.viewport, scan_app_draw_callback, &app);
    view_port_input_callback_set(app.viewport, scan_app_input_callback, &app);
    gui_add_view_port(gui, app.viewport, GuiLayerFullscreen);

    while(!app.exit_app) {
        furi_delay_ms(100);
        if(app.screen == ScreenTargets && app.network_count > 0) {
            size_t len = strlen(app.networks[app.selected_target]);
            if(len > TARGET_DISPLAY_CHARS - 2) {
                if(app.target_scroll_tick++ >= SCROLL_STEP_DELAY) {
                    app.target_scroll_tick = 0;
                    if(app.target_name_offset >= len - (TARGET_DISPLAY_CHARS - 2)) {
                        app.target_name_offset = 0;
                    } else {
                        app.target_name_offset++;
                    }
                    view_port_update(app.viewport);
                }
            } else {
                app.target_name_offset = 0;
                app.target_scroll_tick = 0;
            }
        }
    }

    gui_remove_view_port(gui, app.viewport);
    view_port_free(app.viewport);
    furi_record_close(RECORD_GUI);

    if(app.serial) {
        furi_hal_serial_async_rx_stop(app.serial);
        furi_hal_serial_deinit(app.serial);
        furi_hal_serial_control_release(app.serial);
    }

    return 0;
}
