#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <string.h>
#include <furi_hal.h>
#include <furi_hal_serial.h>

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
    ScreenAttack,
    ScreenTargets,
} AppScreen;

#define NETWORK_STR_LEN 160
#define DISPLAY_CHARS   21

typedef struct {
    bool scanning;
    bool stop_requested;
    bool exit_app;
    bool attacking;
    uint8_t menu_index;
    uint8_t target_scroll;    // index of first visible item
    int selected_target;      // index of highlighted item
    bool target_selected[32]; // selected for attack
    uint8_t network_count;
    char networks[32][NETWORK_STR_LEN];
    char line_buf[NETWORK_STR_LEN];
    uint8_t line_pos;
    uint8_t target_offset;
    AppScreen screen;
    FuriHalSerialHandle* serial;
    ViewPort* viewport;
} ScanApp;

static void uart_rx_cb(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* ctx) {
    ScanApp* app = ctx;
    if(event != FuriHalSerialRxEventData) return;
    while(furi_hal_serial_async_rx_available(handle)) {
        char ch = (char)furi_hal_serial_async_rx(handle);
        if(ch == '\n' || ch == '\r') {
            if(app->line_pos > 0) {
                app->line_buf[app->line_pos] = '\0';
                if(app->line_buf[0] == '[' && app->network_count < 32) {
                    int idx = 0;
                    char band[8];
                    int rssi = 0;
                    int ch = 0;
                    unsigned int b0,b1,b2,b3,b4,b5;
                    char essid[64];
                    int parsed = sscanf(
                        app->line_buf,
                        "[%d] Band: %7s RSSI: %d Ch: %d BSSID: %x:%x:%x:%x:%x:%x ESSID: %63[^\n]",
                        &idx, band, &rssi, &ch,
                        &b0,&b1,&b2,&b3,&b4,&b5, essid);
                    if(parsed == 10) {
                        snprintf(
                            app->networks[app->network_count],
                            NETWORK_STR_LEN,
                            "[%d] SSID: %s BSSID: %02x:%02x:%02x:%02x:%02x:%02x Ch: %d Band: %s RSSI: %d",
                            idx,
                            essid,
                            b0,b1,b2,b3,b4,b5,
                            ch,
                            band,
                            rssi);
                    } else {
                        strncpy(app->networks[app->network_count], app->line_buf, NETWORK_STR_LEN-1);
                        app->networks[app->network_count][NETWORK_STR_LEN-1] = '\0';
                    }
                    app->network_count++;
                }
                app->line_pos = 0;
            }
        } else if(app->line_pos < NETWORK_STR_LEN-1) {
            app->line_buf[app->line_pos++] = ch;
        }
    }
}

static void discard_rx_cb(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* ctx) {
    (void)ctx;
    if(event != FuriHalSerialRxEventData) return;
    while(furi_hal_serial_async_rx_available(handle)) {
        furi_hal_serial_async_rx(handle);
    }
}

static void scan_app_draw_callback(Canvas* canvas, void* ctx) {
    ScanApp* app = ctx;
    canvas_clear(canvas);

    if(app->screen == ScreenMainMenu) {
        canvas_draw_str(canvas, 2, 12, app->menu_index == 0 ? "> Scan" : "  Scan");
        canvas_draw_str(canvas, 2, 24, app->menu_index == 1 ? "> Targets" : "  Targets");
        canvas_draw_str(canvas, 2, 36, app->menu_index == 2 ? "> Attack" : "  Attack");
        canvas_draw_str(canvas, 2, 48, app->menu_index == 3 ? "> Reboot" : "  Reboot");
    } else if(app->screen == ScreenScan) {
        char buf[32];
        if(!app->scanning) {
            canvas_draw_str(canvas, 2, 12, "Press OK to Start");
            canvas_draw_str(canvas, 2, 24, "Back");
        } else if(!app->stop_requested) {
            snprintf(buf, sizeof(buf), "Scanning %d", app->network_count);
            canvas_draw_str(canvas, 2, 12, buf);
            canvas_draw_str(canvas, 2, 24, "Back: stop");
        } else {
            canvas_draw_str(canvas, 2, 12, "Scan Stopped");
            canvas_draw_str(canvas, 2, 24, "Back");
        }
    } else if(app->screen == ScreenAttack) {
        if(!app->attacking) {
            int count = 0;
            char list[64] = "";
            for(int i=0;i<app->network_count;i++) {
                if(app->target_selected[i]) {
                    if(count > 0) safe_strlcat(list, " ", sizeof(list));
                    char buf[4];
                    snprintf(buf, sizeof(buf), "%d", i);
                    safe_strlcat(list, buf, sizeof(list));
                    count++;
                }
            }
            if(count > 0) {
                char buf[80];
                snprintf(buf, sizeof(buf), "OK: attack %s", list);
                canvas_draw_str(canvas, 2, 12, buf);
            } else {
                canvas_draw_str(canvas, 2, 12, "Select targets first");
            }
            canvas_draw_str(canvas, 2, 24, "Back");
        } else {
            canvas_draw_str(canvas, 2, 12, "Attacking...");
            canvas_draw_str(canvas, 2, 24, "Back: stop");
        }
    } else if(app->screen == ScreenTargets) {
        if(app->network_count == 0) {
            canvas_draw_str(canvas, 2, 12, "No targets");
            canvas_draw_str(canvas, 2, 24, "Back");
        } else {
            for(int i=0;i<6;i++) {
                int idx = app->target_scroll + i;
                if(idx >= app->network_count) break;
                const char* text = app->networks[idx];
                size_t len = strlen(text);
                size_t off = (idx == app->selected_target) ? app->target_offset : 0;
                if(off > len) off = len;
                char view[DISPLAY_CHARS + 1];
                strncpy(view, text + off, DISPLAY_CHARS);
                view[DISPLAY_CHARS] = '\0';
                char line[64];
                char arrow = (idx == app->selected_target) ? '>' : ' ';
                char star = app->target_selected[idx] ? '*' : ' ';
                snprintf(line, sizeof(line), "%c%c%s", arrow, star, view);
                canvas_draw_str(canvas, 2, 12 + i*12, line);
            }
        }
    }
}

static void scan_app_input_callback(InputEvent* event, void* ctx) {
    ScanApp* app = ctx;
    if(event->type != InputTypeShort) return;

    if(app->screen == ScreenMainMenu) {
        if(event->key == InputKeyUp) {
            if(app->menu_index > 0) app->menu_index--;
            view_port_update(app->viewport);
        } else if(event->key == InputKeyDown) {
            if(app->menu_index < 3) app->menu_index++;
            view_port_update(app->viewport);
        } else if(event->key == InputKeyOk) {
            if(app->menu_index == 0) app->screen = ScreenScan;
            else if(app->menu_index == 1) app->screen = ScreenTargets;
            else if(app->menu_index == 2) app->screen = ScreenAttack;
            else {
                const char* cmd = "reboot\n";
                furi_hal_serial_tx(app->serial, (const uint8_t*)cmd, strlen(cmd));
                furi_hal_serial_tx_wait_complete(app->serial);
            }
            view_port_update(app->viewport);
        } else if(event->key == InputKeyBack) {
            app->exit_app = true;
        }
    } else if(app->screen == ScreenScan) {
        if(event->key == InputKeyOk && !app->scanning) {
            app->network_count = 0;
            app->target_scroll = 0;
            app->selected_target = 0;
            app->target_offset = 0;
            memset(app->target_selected, 0, sizeof(app->target_selected));
            app->line_pos = 0;
            const char* cmd = "scan\n";
            furi_hal_serial_tx(app->serial, (const uint8_t*)cmd, strlen(cmd));
            furi_hal_serial_tx_wait_complete(app->serial);
            app->scanning = true;
            view_port_update(app->viewport);
        } else if(event->key == InputKeyBack) {
            if(app->scanning && !app->stop_requested) {
                const char* cmd = "scanstop\n";
                furi_hal_serial_tx(app->serial, (const uint8_t*)cmd, strlen(cmd));
                furi_hal_serial_tx_wait_complete(app->serial);
                app->stop_requested = true;
            } else {
                app->screen = ScreenMainMenu;
                app->scanning = false;
                app->stop_requested = false;
            }
            view_port_update(app->viewport);
        }
    } else if(app->screen == ScreenAttack) {
        if(event->key == InputKeyOk && !app->attacking) {
            char cmd[128] = "attack";
            int count = 0;
            for(int i=0;i<app->network_count;i++) {
                if(app->target_selected[i]) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), " %d", i);
                    safe_strlcat(cmd, buf, sizeof(cmd));
                    count++;
                }
            }
            safe_strlcat(cmd, "\n", sizeof(cmd));
            if(count > 0) {
                furi_hal_serial_tx(app->serial, (const uint8_t*)cmd, strlen(cmd));
                furi_hal_serial_tx_wait_complete(app->serial);
                app->attacking = true;
            }
            view_port_update(app->viewport);
        } else if(event->key == InputKeyBack) {
            if(app->attacking) {
                const char* cmd = "attackstop\n";
                furi_hal_serial_tx(app->serial, (const uint8_t*)cmd, strlen(cmd));
                furi_hal_serial_tx_wait_complete(app->serial);
                app->attacking = false;
            } else {
                app->screen = ScreenMainMenu;
            }
            view_port_update(app->viewport);
        }
    } else if(app->screen == ScreenTargets) {
        if(app->network_count == 0) {
            if(event->key == InputKeyBack) {
                app->screen = ScreenMainMenu;
                view_port_update(app->viewport);
            }
        } else {
            if(event->key == InputKeyUp && app->selected_target > 0) {
                app->selected_target--;
                app->target_offset = 0;
                if(app->selected_target < app->target_scroll) app->target_scroll--;
                view_port_update(app->viewport);
            } else if(event->key == InputKeyDown && app->selected_target < app->network_count - 1) {
                app->selected_target++;
                app->target_offset = 0;
                if(app->selected_target >= app->target_scroll + 6) app->target_scroll++;
                view_port_update(app->viewport);
            } else if(event->key == InputKeyLeft) {
                if(app->target_offset > 0) {
                    app->target_offset--;
                    view_port_update(app->viewport);
                }
            } else if(event->key == InputKeyRight) {
                size_t len = strlen(app->networks[app->selected_target]);
                if((size_t)app->target_offset + DISPLAY_CHARS < len) {
                    app->target_offset++;
                    view_port_update(app->viewport);
                }
            } else if(event->key == InputKeyOk) {
                app->target_selected[app->selected_target] = !app->target_selected[app->selected_target];
                view_port_update(app->viewport);
            } else if(event->key == InputKeyBack) {
                app->screen = ScreenMainMenu;
                view_port_update(app->viewport);
            }
        }
    }
}

int32_t scan_app(void* p) {
    (void)p;
    ScanApp* app = malloc(sizeof(ScanApp));
    if(!app) return 0;
    *app = (ScanApp){
        .scanning=false,
        .stop_requested=false,
        .exit_app=false,
        .attacking=false,
        .menu_index=0,
        .target_scroll=0,
        .selected_target=0,
        .network_count=0,
        .line_pos=0,
        .target_offset=0,
        .screen=ScreenMainMenu,
        .serial=NULL};

    app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!app->serial) {
        free(app);
        return 0; // Serial interface unavailable
    }

    bool init_serial = !furi_hal_bus_is_enabled(FuriHalBusUSART1);
    if(init_serial) {
        furi_hal_serial_init(app->serial, 115200);
    } else {
        /* Reinitialize to clear any pending data from console */
        furi_hal_serial_deinit(app->serial);
        furi_hal_serial_init(app->serial, 115200);
    }

    /* Flush any garbage from previous session */
    furi_hal_serial_async_rx_start(app->serial, discard_rx_cb, NULL, false);
    furi_delay_ms(100);
    furi_hal_serial_async_rx_stop(app->serial);

    const char* reboot_cmd = "reboot\n";
    furi_hal_serial_tx(app->serial, (const uint8_t*)reboot_cmd, strlen(reboot_cmd));
    furi_hal_serial_tx_wait_complete(app->serial);

    /* Wait for reboot and discard boot log */
    furi_hal_serial_async_rx_start(app->serial, discard_rx_cb, NULL, false);
    furi_delay_ms(1000);
    furi_hal_serial_async_rx_stop(app->serial);

    /* Reinitialize once more so buffers are empty */
    furi_hal_serial_deinit(app->serial);
    furi_hal_serial_init(app->serial, 115200);

    furi_hal_serial_async_rx_start(app->serial, uart_rx_cb, app, false);

    Gui* gui = furi_record_open(RECORD_GUI);
    app->viewport = view_port_alloc();
    view_port_draw_callback_set(app->viewport, scan_app_draw_callback, app);
    view_port_input_callback_set(app->viewport, scan_app_input_callback, app);
    gui_add_view_port(gui, app->viewport, GuiLayerFullscreen);

    while(!app->exit_app) {
        furi_delay_ms(10);
    }

    gui_remove_view_port(gui, app->viewport);
    view_port_free(app->viewport);
    furi_record_close(RECORD_GUI);

    if(app->serial) {
        furi_hal_serial_async_rx_stop(app->serial);
        furi_hal_serial_deinit(app->serial);
        furi_hal_serial_control_release(app->serial);
    }

    free(app);

    return 0;
}
