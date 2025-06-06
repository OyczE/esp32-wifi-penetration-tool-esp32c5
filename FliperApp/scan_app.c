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
} AppScreen;

typedef struct {
    bool scanning;
    bool stop_requested;
    bool exit_app;
    bool attacking;
    uint8_t menu_index;
    AppScreen screen;
    FuriHalSerialHandle* serial;
    ViewPort* viewport;
} ScanApp;

static void scan_app_draw_callback(Canvas* canvas, void* ctx) {
    ScanApp* app = ctx;
    canvas_clear(canvas);

    if(app->screen == ScreenMainMenu) {
        canvas_draw_str(canvas, 2, 12, app->menu_index == 0 ? "> Scan" : "  Scan");
        canvas_draw_str(canvas, 2, 24, app->menu_index == 1 ? "> Attack" : "  Attack");
    } else if(app->screen == ScreenScan) {
        if(!app->scanning) {
            canvas_draw_str(canvas, 2, 12, "Press OK to Start");
            canvas_draw_str(canvas, 2, 24, "Back");
        } else if(!app->stop_requested) {
            canvas_draw_str(canvas, 2, 12, "Scanning...");
            canvas_draw_str(canvas, 2, 24, "Back: stop");
        } else {
            canvas_draw_str(canvas, 2, 12, "Scan Stopped");
            canvas_draw_str(canvas, 2, 24, "Back");
        }
    } else if(app->screen == ScreenAttack) {
        if(!app->attacking) {
            canvas_draw_str(canvas, 2, 12, "Press OK to Attack");
            canvas_draw_str(canvas, 2, 24, "Back");
        } else {
            canvas_draw_str(canvas, 2, 12, "Attacking...");
            canvas_draw_str(canvas, 2, 24, "Back: stop");
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
            if(app->menu_index < 1) app->menu_index++;
            view_port_update(app->viewport);
        } else if(event->key == InputKeyOk) {
            app->screen = (app->menu_index == 0) ? ScreenScan : ScreenAttack;
            view_port_update(app->viewport);
        } else if(event->key == InputKeyBack) {
            app->exit_app = true;
        }
    } else if(app->screen == ScreenScan) {
        if(event->key == InputKeyOk && !app->scanning) {
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
            const char* cmd = "attack\n";
            furi_hal_serial_tx(app->serial, (const uint8_t*)cmd, strlen(cmd));
            furi_hal_serial_tx_wait_complete(app->serial);
            app->attacking = true;
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
        furi_hal_serial_deinit(app.serial);
        furi_hal_serial_control_release(app.serial);
    }

    return 0;
}
