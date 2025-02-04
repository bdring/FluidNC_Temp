// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// The Channel class is for controlling FluidNC via a two-way communications
// channel, such as a USB serial link, a Bluetooth serial link, a Telnet-style
// TCP connection or a WebSocket stream.  Channel inherits from the Arduino
// File Stream (character input), which inherits from Print (character output).
//
// On top of those base classes, Channel adds the pollLine() method to collect
// a line of input - delimited by newline - while processing "realtime characters"
// that take effect immediately without regard to line boundaries.  It further
// adds the ack() method for flow control, to prevent GCode senders from
// overrunning input buffers.  The default implementation of ack() sends
// "ok" and "error:" messages via the standard Grbl serial protocol, but it
// could be implemented in other ways for different channel protocols.

#pragma once

#include "src/Error.h"        // Error
#include "src/GCode.h"        // gc_modal_t
#include "src/Types.h"        // State
#include "src/RealtimeCmd.h"  // Cmd
#include "src/UTF8.h"

#include "src/Pins/PinAttributes.h"
#include "src/Machine/EventPin.h"

#include <Stream.h>
#include <freertos/FreeRTOS.h>  // TickType_T
#include <queue>

class Channel : public Stream {
private:
    void pin_event(uint32_t pinnum, bool active);

    static constexpr int PinLowFirst  = 0x100;
    static constexpr int PinLowLast   = 0x13f;
    static constexpr int PinHighFirst = 0x140;
    static constexpr int PinHighLast  = 0x17f;

    static constexpr int PinACK = 0xB2;
    static constexpr int PinNAK = 0xB3;

    static constexpr int timeout = 2000;

public:
    static constexpr int maxLine = 255;

    int _message_level = MsgLevelVerbose;

protected:
    std::string _name;
    char        _line[maxLine] = {};
    size_t      _linelen       = 0;
    bool        _addCR         = false;
    char        _lastWasCR     = false;

    std::queue<uint8_t> _queue;

    uint32_t _reportInterval = 0;
    int32_t  _nextReportTime = 0;

    gc_modal_t  _lastModal        = modal_defaults;
    uint8_t     _lastTool         = 0;
    float       _lastSpindleSpeed = 0;
    float       _lastFeedRate     = 0;
    const char* _lastStateName    = "";
    MotorMask   _lastLimits       = 0;
    bool        _lastProbe        = false;
    bool        _lastJobActive    = false;
    std::string _lastPinString    = "";

    bool       _reportOvr = true;
    bool       _reportWco = true;
    CoordIndex _reportNgc = CoordIndex::End;

    Cmd _last_rt_cmd = Cmd::None;

    std::map<int, EventPin*> _events;
    std::map<int, bool*>     _pin_values;

    UTF8 _utf8;

    bool _ended   = false;
    bool _percent = false;

protected:
    bool _active = true;
    bool _paused = false;

public:
    explicit Channel(const std::string& name, bool addCR = false);
    explicit Channel(const char* name, bool addCR = false);
    Channel(const char* name, int num, bool addCR = false);
    virtual ~Channel() = default;

    bool _ackwait = false;

    virtual void       handle() {};
    virtual Error      pollLine(char* line);
    virtual void       ack(Error status);
    const std::string& name() { return _name; }

    virtual void sendLine(MsgLevel level, const char* line);
    virtual void sendLine(MsgLevel level, const std::string* line);
    virtual void sendLine(MsgLevel level, const std::string& line);

    size_t _line_number = 0;

    std::string _progress;

    // rx_buffer_available() is the number of bytes that can be sent without overflowing
    // a reception buffer, even if the system is busy.  Channels that can handle external
    // input via an interrupt or other background mechanism should override it to return
    // the remaining space that mechanism has available.
    // The queue can handle more than 256 characters but we don't want it to get too
    // large, so we report a limited size.
    virtual int rx_buffer_available() { return std::max(0, 256 - int(_queue.size())); }

    // flushRx() discards any characters that have already been received.  It is used
    // after a reset, so that anything already sent will not be processed.
    virtual void flushRx();

    // realtimeOkay() returns true if the channel can currently interpret the character as
    // a Grbl realtime character.  Some situations where it might return false are when
    // the channel is being used for file upload or if the channel is doing line editing
    // and is in the middle of an escape sequence that could include what would otherwise
    // be a realtime character.
    virtual bool realtimeOkay(char c) { return true; }

    void handleRealtimeCharacter(uint8_t byte);

    // lineComplete() accumulates the character into the line, returning true if a line
    // end is seen.
    virtual bool lineComplete(char* line, char c);

    virtual size_t timedReadBytes(char* buffer, size_t length, TickType_t timeout) {
        setTimeout(timeout);
        return readBytes(buffer, length);
    }

    virtual bool is_visible(const std::string& stem, std::string extension, bool isdir);

    size_t timedReadBytes(uint8_t* buffer, size_t length, TickType_t timeout) {
        return timedReadBytes(reinterpret_cast<char*>(buffer), length, timeout);
    }

    bool setCr(bool on) {
        bool retval = _addCR;
        _addCR      = on;
        return retval;
    }

    void notifyOvr() { _reportOvr = true; }
    void notifyWco() { _reportWco = true; }
    void notifyNgc(CoordIndex coord) { _reportNgc = coord; }

    int peek() override { return -1; }
    int read() override { return -1; }
    int available() override { return _queue.size(); }

    virtual void print_msg(MsgLevel level, const char* msg);

    void print_msg(MsgLevel level, const std::string& msg) { print_msg(level, msg.c_str()); }

    uint32_t     setReportInterval(uint32_t ms);
    uint32_t     getReportInterval() { return _reportInterval; }
    virtual void autoReport();
    void         autoReportGCodeState();

    void push(uint8_t byte);
    void push(const uint8_t* data, size_t length) {
        while (length--) {
            push(*data++);
        }
    }
    void push(std::string_view data) {
        for (auto const& c : data) {
            push((uint8_t)c);
        }
    }
    void push(const std::string& s) { push(reinterpret_cast<const uint8_t*>(s.c_str()), s.length()); }

    void end() { _ended = true; }
    void percent() { _percent = true; }

    // Pin extender functions
    virtual void out(const char* s, const char* tag);
    virtual void out(const std::string& s, const char* tag);
    virtual void out_acked(const std::string& s, const char* tag);

    void setAttr(int index, bool* valuep, const std::string& s, const char* tag);

    void ready();
    void registerEvent(uint8_t code, EventPin* obj);

    size_t lineNumber() { return _line_number; }

    virtual void   save() {}
    virtual void   restore() {}
    virtual size_t position() { return 0; }
    virtual void   set_position(size_t pos) {}

    void pause();
    void resume();
};
