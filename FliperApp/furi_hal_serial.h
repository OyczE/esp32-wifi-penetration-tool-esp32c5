#ifndef FURI_HAL_SERIAL_H
#define FURI_HAL_SERIAL_H

#include <stdint.h>
#include <stddef.h>

/* Minimal subset of the Flipper Zero Serial HAL for building outside the firmware */

typedef enum {
    FuriHalSerialIdUsart,
    FuriHalSerialIdLpuart,
    FuriHalSerialIdMax,
} FuriHalSerialId;

typedef struct FuriHalSerialHandle FuriHalSerialHandle;

FuriHalSerialHandle* furi_hal_serial_control_acquire(FuriHalSerialId serial_id);
void furi_hal_serial_control_release(FuriHalSerialHandle* handle);
void furi_hal_serial_init(FuriHalSerialHandle* handle, uint32_t baud);
void furi_hal_serial_deinit(FuriHalSerialHandle* handle);
void furi_hal_serial_tx(FuriHalSerialHandle* handle, const uint8_t* data, size_t length);

#endif // FURI_HAL_SERIAL_H
