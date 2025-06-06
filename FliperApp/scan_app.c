#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <string.h>
#include <furi_hal.h>
#include <furi_hal_serial.h>

typedef enum {
    ScreenMainMenu,
    ScreenScan,
    ScreenAttack,
    ScreenTargets,
} AppScreen;

typedef struct {
    bool scanning;
    bool stop_requested;
    bool exit_app;
    bool attacking;
    uint8_t menu_index;
    uint8_t target_scroll;
    int selected_target;
    uint8_t network_count;
    char networks[32][48];
    char line_buf[48];
    uint8_t line_pos;
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
                    sscanf(app->line_buf, "[%d]", &idx);
                    strncpy(app->networks[app->network_count], app->line_buf, 47);
                    app->networks[app->network_count][47] = '\0';
                    app->network_count++;
                }
                app->line_pos = 0;
            }
        } else if(app->line_pos < 47) {
            app->line_buf[app->line_pos++] = ch;
        }
    }
}

static void scan_app_draw_callback(Canvas* canvas, void* ctx) {
    ScanApp* app = ctx;
    canvas_clear(canvas);

    if(app->screen == ScreenMainMenu) {
        canvas_draw_str(canvas, 2, 12, app->menu_index == 0 ? "> Scan" : "  Scan");
        canvas_draw_str(canvas, 2, 24, app->menu_index == 1 ? "> Attack" : "  Attack");
        canvas_draw_str(canvas, 2, 36, app->menu_index == 2 ? "> Targets" : "  Targets");
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
            if(app->selected_target >= 0) {
                char buf[32];
                snprintf(buf, sizeof(buf), "OK: attack %d", app->selected_target);
                canvas_draw_str(canvas, 2, 12, buf);
            } else {
                canvas_draw_str(canvas, 2, 12, "Select target first");
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
            for(int i=0;i<2;i++) {
                int idx = app->target_scroll + i;
                if(idx >= app->network_count) break;
                char line[54];
                snprintf(line, sizeof(line), "%c%s", idx==app->selected_target?'>' :' ', app->networks[idx]);
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
            if(app->menu_index < 2) app->menu_index++;
            view_port_update(app->viewport);
        } else if(event->key == InputKeyOk) {
            if(app->menu_index == 0) app->screen = ScreenScan;
            else if(app->menu_index == 1) app->screen = ScreenAttack;
            else app->screen = ScreenTargets;
            view_port_update(app->viewport);
        } else if(event->key == InputKeyBack) {
            app->exit_app = true;
        }
    } else if(app->screen == ScreenScan) {
        if(event->key == InputKeyOk && !app->scanning) {
            app->network_count = 0;
            app->target_scroll = 0;
            app->selected_target = -1;
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
        if(event->key == InputKeyOk && !app->attacking && app->selected_target >= 0) {
            char cmd[16];
            snprintf(cmd, sizeof(cmd), "atack %d\n", app->selected_target);
            furi_hal_serial_tx(app->serial, (const uint8_t*)cmd, strlen(cmd));
            furi_hal_serial_tx_wait_complete(app->serial);
            app->attacking = true;
            view_port_update(app->viewport);
        } else if(event->key == InputKeyBack) {
            if(app->attacking) {
                const char* cmd = "atackstop\n";
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
            if(event->key == InputKeyUp && app->target_scroll > 0) {
                app->target_scroll--;
                view_port_update(app->viewport);
            } else if(event->key == InputKeyDown && app->target_scroll < app->network_count - 1) {
                app->target_scroll++;
                view_port_update(app->viewport);
            } else if(event->key == InputKeyOk) {
                app->selected_target = app->target_scroll;
                app->screen = ScreenMainMenu;
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
    ScanApp app = {
        .scanning=false,
        .stop_requested=false,
        .exit_app=false,
        .attacking=false,
        .menu_index=0,
        .target_scroll=0,
        .selected_target=-1,
        .network_count=0,
        .line_pos=0,
        .screen=ScreenMainMenu,
        .serial=NULL};

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
        furi_delay_ms(10);
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
