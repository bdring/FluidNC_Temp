// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
 * UART driver that accesses the ESP32 hardware FIFOs directly.
 */

#include "Uart.h"

#include <driver/uart.h>
#include <esp_ipc.h>
#include "hal/uart_hal.h"

std::string encodeUartMode(UartData wordLength, UartParity parity, UartStop stopBits) {
    std::string s;
    s += std::to_string(int(wordLength) - int(UartData::Bits5) + 5);
    switch (parity) {
        case UartParity::Even:
            s += 'E';
            break;
        case UartParity::Odd:
            s += 'O';
            break;
        case UartParity::None:
            s += 'N';
            break;
    }
    switch (stopBits) {
        case UartStop::Bits1:
            s += '1';
            break;
        case UartStop::Bits1_5:
            s += "1.5";
            break;
        case UartStop::Bits2:
            s += '2';
            break;
    }
    return s;
}

const char* decodeUartMode(std::string_view str, UartData& wordLength, UartParity& parity, UartStop& stopBits) {
    str = string_util::trim(str);
    if (str.length() == 5 || str.length() == 3) {
        int32_t wordLenInt;
        if (!string_util::is_int(str.substr(0, 1), wordLenInt)) {
            return "Uart mode should be specified as [Bits Parity Stopbits] like [8N1]";
        } else if (wordLenInt < 5 || wordLenInt > 8) {
            return "Number of data bits for uart is out of range. Expected format like [8N1].";
        }
        wordLength = UartData(int(UartData::Bits5) + (wordLenInt - 5));

        switch (str[1]) {
            case 'N':
            case 'n':
                parity = UartParity::None;
                break;
            case 'O':
            case 'o':
                parity = UartParity::Odd;
                break;
            case 'E':
            case 'e':
                parity = UartParity::Even;
                break;
            default:
                return "Uart mode should be specified as [Bits Parity Stopbits] like [8N1]";
                break;  // Omits compiler warning. Never hit.
        }

        auto stop = str.substr(2, str.length() - 2);
        if (stop == "1") {
            stopBits = UartStop::Bits1;
        } else if (stop == "1.5") {
            stopBits = UartStop::Bits1_5;
        } else if (stop == "2") {
            stopBits = UartStop::Bits2;
        } else {
            return "Uart stopbits can only be 1, 1.5 or 2. Syntax is [8N1]";
        }

    } else {
        return "Uart mode should be specified as [Bits Parity Stopbits] like [8N1]";
    }
    return "";
}

Uart::Uart(int uart_num) : _uart_num(uart_num), _name("uart") {
    _name += std::to_string(uart_num);
}

static void uart_driver_n_install(void* arg) {
    uart_driver_install((uart_port_t)arg, 256, 0, 0, NULL, ESP_INTR_FLAG_IRAM);
}

void Uart::changeMode(unsigned long baud, UartData dataBits, UartParity parity, UartStop stopBits) {
    uart_config_t conf;
    conf.source_clk          = UART_SCLK_APB;
    conf.baud_rate           = baud;
    conf.data_bits           = uart_word_length_t(dataBits);
    conf.parity              = uart_parity_t(parity);
    conf.stop_bits           = uart_stop_bits_t(stopBits);
    conf.flow_ctrl           = UART_HW_FLOWCTRL_DISABLE;
    conf.rx_flow_ctrl_thresh = 0;
    auto ret                 = uart_param_config(uart_port_t(_uart_num), &conf);
}
void Uart::restoreMode() {
    changeMode(_baud, _dataBits, _parity, _stopBits);
}

void Uart::enterPassthrough() {
    changeMode(_passthrough_baud, _passthrough_databits, _passthrough_parity, _passthrough_stopbits);
}

void Uart::exitPassthrough() {
    restoreMode();
    if (_sw_flowcontrol_enabled) {
        setSwFlowControl(_sw_flowcontrol_enabled, _xon_threshold, _xoff_threshold);
    }
}

// This version is used for the initial console UART where we do not want to change the pins
void Uart::begin(unsigned long baud, UartData dataBits, UartStop stopBits, UartParity parity) {
    //    uart_driver_delete(_uart_num);
    changeMode(baud, dataBits, parity, stopBits);

    // We init UARTs on core 0 so the interrupt handler runs there,
    // thus avoiding conflict with the StepTimer interrupt
    esp_ipc_call_blocking(0, uart_driver_n_install, (void*)_uart_num);
}

// This version is used when we have a config section with all the parameters
void Uart::begin() {
    auto txd = _txd_pin.getNative(Pin::Capabilities::UART | Pin::Capabilities::Output);
    auto rxd = _rxd_pin.getNative(Pin::Capabilities::UART | Pin::Capabilities::Input);
    auto rts = _rts_pin.undefined() ? -1 : _rts_pin.getNative(Pin::Capabilities::UART | Pin::Capabilities::Output);
    auto cts = _cts_pin.undefined() ? -1 : _cts_pin.getNative(Pin::Capabilities::UART | Pin::Capabilities::Input);

    if (setPins(txd, rxd, rts, cts)) {
        Assert(false, "Uart pin config failed");
        return;
    }

    begin(_baud, _dataBits, _stopBits, _parity);
    config_message("UART", std::to_string(_uart_num).c_str());
}

int Uart::read() {
    if (_pushback != -1) {
        int ret   = _pushback;
        _pushback = -1;
        return ret;
    }
    uint8_t c;
    int     res = uart_read_bytes(uart_port_t(_uart_num), &c, 1, 0);
    return res == 1 ? c : -1;
}

size_t Uart::write(uint8_t c) {
    // Use Uart::write(buf, len) instead of uart_write_bytes() for _addCR
    return write(&c, 1);
}

size_t Uart::write(const uint8_t* buffer, size_t length) {
    return uart_write_bytes(uart_port_t(_uart_num), (const char*)buffer, length);
}

// size_t Uart::write(const char* text) {
//    return uart_write_bytes(_uart_num, text, strlen(text));
// }

size_t Uart::timedReadBytes(char* buffer, size_t len, TickType_t timeout) {
    int res = uart_read_bytes(uart_port_t(_uart_num), buffer, len, timeout);
    // If res < 0, no bytes were read

    return res < 0 ? 0 : res;
}

void Uart::forceXon() {
    uart_ll_force_xon(uart_port_t(_uart_num));
}

void Uart::forceXoff() {
    uart_ll_force_xoff(uart_port_t(_uart_num));
}

void Uart::setSwFlowControl(bool on, int xon_threshold, int xoff_threshold) {
    if (xon_threshold <= 0) {
        xon_threshold = 126;
    }
    if (xoff_threshold <= 0) {
        xoff_threshold = 127;
    }
    _sw_flowcontrol_enabled = true;
    _xon_threshold          = xon_threshold;
    _xoff_threshold         = xoff_threshold;
    uart_set_sw_flow_ctrl(uart_port_t(_uart_num), on, xon_threshold, xoff_threshold);
}
void Uart::getSwFlowControl(bool& enabled, int& xon_threshold, int& xoff_threshold) {
    enabled        = _sw_flowcontrol_enabled;
    xon_threshold  = _xon_threshold;
    xoff_threshold = _xoff_threshold;
}
bool Uart::setHalfDuplex() {
    return uart_set_mode(uart_port_t(_uart_num), UART_MODE_RS485_HALF_DUPLEX) != ESP_OK;
}
bool Uart::setPins(int tx_pin, int rx_pin, int rts_pin, int cts_pin) {
    return uart_set_pin(uart_port_t(_uart_num), tx_pin, rx_pin, rts_pin, cts_pin) != ESP_OK;
}
bool Uart::flushTxTimed(TickType_t ticks) {
    return uart_wait_tx_done(uart_port_t(_uart_num), ticks) != ESP_OK;
}

void Uart::config_message(const char* prefix, const char* usage) {
    log_info(prefix << usage << " Tx:" << _txd_pin.name() << " Rx:" << _rxd_pin.name() << " RTS:" << _rts_pin.name() << " Baud:" << _baud);
}

int Uart::rx_buffer_available(void) {
    return UART_FIFO_LEN - available();
}

int Uart::peek() {
    if (_pushback != -1) {
        return _pushback;
    }
    int ch = read();
    if (ch == -1) {
        return -1;
    }
    _pushback = ch;
    return ch;
}

int Uart::available() {
    size_t size;
    uart_get_buffered_data_len(uart_port_t(_uart_num), &size);
    return size + (_pushback != -1);
}

void Uart::flushRx() {
    _pushback = -1;
    uart_flush_input(uart_port_t(_uart_num));
}

#if 0
namespace {
    UartFactory::InstanceBuilder<Uart> uart2("uart2");
    UartFactory::InstanceBuilder<Uart> uart3("uart3");
}
#endif
