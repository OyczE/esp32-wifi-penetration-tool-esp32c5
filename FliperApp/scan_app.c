#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <string.h>
#include <furi_hal.h>
#include <furi_hal_serial.h>

typedef struct {
    bool scanning;
    bool stop_requested;
    bool exit_app;
    ViewPort* viewport;
} ScanApp;

static void scan_app_draw_callback(Canvas* canvas, void* ctx) {
    ScanApp* app = ctx;
    canvas_clear(canvas);
    if(!app->scanning) {
        canvas_draw_str(canvas, 2, 12, "Press OK to Scan");
    } else if(!app->stop_requested) {
        canvas_draw_str(canvas, 2, 12, "Scanning...");
        canvas_draw_str(canvas, 2, 24, "Back: stop");
    } else {
        canvas_draw_str(canvas, 2, 12, "Scan Stopped");
        canvas_draw_str(canvas, 2, 24, "Back: exit");
    }
}

static void scan_app_input_callback(InputEvent* event, void* ctx) {
    ScanApp* app = ctx;
    if(event->type != InputTypeShort) return;
    if(event->key == InputKeyOk && !app->scanning) {
        const char* cmd = "scan\n";
        furi_hal_serial_tx((const uint8_t*)cmd, strlen(cmd));
        app->scanning = true;
        view_port_update(app->viewport);
    } else if(event->key == InputKeyBack) {
        if(app->scanning && !app->stop_requested) {
            const char* cmd = "scanstop\n";
            furi_hal_serial_tx((const uint8_t*)cmd, strlen(cmd));
            app->stop_requested = true;
            view_port_update(app->viewport);
        } else {
            app->exit_app = true;
        }
    }
}

int32_t scan_app(void* p) {
    (void)p;
    ScanApp app = {.scanning=false, .stop_requested=false, .exit_app=false};

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
    return 0;
}
