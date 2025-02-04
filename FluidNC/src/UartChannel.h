// Copyright (c) 2023 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Uart.h"
#include "Channel.h"
#include "lineedit.h"

class UartChannel : public Channel, public Configuration::Configurable {
private:
    Lineedit* _lineedit;
    Uart*     _uart;

    int _uart_num           = 0;
    int _report_interval_ms = 0;

    static constexpr int _ack_timeout = 2000;

public:
    UartChannel(int num, bool addCR = false);

    void init();
    void init(Uart* uart);

    // Print methods (Stream inherits from Print)
    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buf, size_t len) override;

    // Stream methods (Channel inherits from Stream)
    int peek(void) override;
    int available(void) override;
    int read() override;

    // Channel methods
    int    rx_buffer_available() override;
    void   flushRx() override;
    size_t timedReadBytes(char* buffer, size_t length, TickType_t timeout);
    size_t timedReadBytes(uint8_t* buffer, size_t length, TickType_t timeout) { return timedReadBytes((char*)buffer, length, timeout); };
    bool   realtimeOkay(char c) override;
    bool   lineComplete(char* line, char c) override;
    int    uart_num() { return _uart_num; }
    Uart*  uart() { return _uart; }

    void out(const std::string& s, const char* tag) override;
    void out_acked(const std::string& s, const char* tag) override;

    // Configuration methods
    void group(Configuration::HandlerBase& handler) override {
        handler.item("report_interval_ms", _report_interval_ms);
        handler.item("uart_num", _uart_num);
        handler.item("message_level", _message_level, messageLevels2);
    }
};

extern UartChannel Uart0;

extern void uartInit();
