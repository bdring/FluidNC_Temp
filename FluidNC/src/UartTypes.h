#pragma once

#include <driver/uart.h>
#include <string>

const int MAX_N_UARTS = UART_NUM_MAX;

enum class UartData : int {
    Bits5 = UART_DATA_5_BITS,
    Bits6 = UART_DATA_6_BITS,
    Bits7 = UART_DATA_7_BITS,
    Bits8 = UART_DATA_8_BITS,
};

enum class UartStop : int {
    Bits1   = UART_STOP_BITS_1,
    Bits1_5 = UART_STOP_BITS_1_5,
    Bits2   = UART_STOP_BITS_2,
};

enum class UartParity : int {
    None = UART_PARITY_DISABLE,
    Even = UART_PARITY_EVEN,
    Odd  = UART_PARITY_ODD,
};

const char* decodeUartMode(std::string_view str, UartData& wordLength, UartParity& parity, UartStop& stopBits);
std::string encodeUartMode(UartData wordLength, UartParity parity, UartStop stopBits);
