#ifndef FURI_HAL_SERIAL_H
#define FURI_HAL_SERIAL_H

#include <stdint.h>
#include <stddef.h>

void furi_hal_serial_tx(const uint8_t* data, size_t length);

#endif // FURI_HAL_SERIAL_H
