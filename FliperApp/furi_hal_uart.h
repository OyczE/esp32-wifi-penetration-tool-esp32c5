#ifndef FURI_HAL_UART_H
#define FURI_HAL_UART_H

#include <stdint.h>
#include <stddef.h>

void furi_hal_uart_tx(const uint8_t* data, size_t length);

#endif // FURI_HAL_UART_H
