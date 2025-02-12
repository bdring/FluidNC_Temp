#pragma once

#include <cstdint>
#include "../Spindle.h"

namespace Spindles {
    class VFDSpindle;

    namespace VFD {
        // VFDProtocol resides in a separate class because it doesn't need to be in IRAM. This contains all the
        // VFD specific code, which is called from a separate task.
        class VFDProtocol {
        public:
            using response_parser = bool (*)(const uint8_t* response, VFDSpindle* spindle, VFDProtocol* detail);

            static const int VFD_RS485_MAX_MSG_SIZE = 16;  // more than enough for a modbus message

            struct ModbusCommand {
                bool critical;  // TODO SdB: change into `uint8_t critical : 1;`: We want more flags...

                uint8_t tx_length;
                uint8_t rx_length;
                uint8_t msg[VFD_RS485_MAX_MSG_SIZE];
            };
            virtual void group(Configuration::HandlerBase& handler) {};
            virtual void afterParse() {};

        protected:
            // Enable spindown / spinup settings:
            virtual bool use_delay_settings() const { return true; }

            // Commands:
            virtual void direction_command(SpindleState mode, ModbusCommand& data) = 0;
            virtual void set_speed_command(uint32_t rpm, ModbusCommand& data)      = 0;

            // Commands that return the status. Returns nullptr if unavailable by this VFD (default):

            virtual response_parser initialization_sequence(int index, ModbusCommand& data, VFDSpindle* vfd) { return nullptr; }
            virtual response_parser get_current_speed(ModbusCommand& data) { return nullptr; }
            virtual response_parser get_current_direction(ModbusCommand& data) { return nullptr; }
            virtual response_parser get_status_ok(ModbusCommand& data) = 0;
            virtual bool            safety_polling() const { return true; }

        private:
            friend class Spindles::VFDSpindle;  // For ISR related things.

            enum VFDactionType : uint8_t { actionSetSpeed, actionSetMode };

            struct VFDaction {
                VFDactionType action;
                bool          critical;
                uint32_t      arg;
            };

            // Careful observers will notice that these *shouldn't* be static, but they are. The reason is
            // hard to track down. In the spindle class, you can find:
            //
            // 'virtual void init() = 0;  // not in constructor because this also gets called when $$ settings change'
            //
            // With init being called multiple times, static suddenly makes more sense - especially since there is
            // no de-init. Oh well...

            static QueueHandle_t vfd_cmd_queue;
            static TaskHandle_t  vfd_cmdTaskHandle;
            static void          vfd_cmd_task(void* pvParameters);

            static uint16_t ModRTU_CRC(uint8_t* buf, int msg_len);
            bool            prepareSetModeCommand(SpindleState mode, ModbusCommand& data, VFDSpindle* spindle);
            bool            prepareSetSpeedCommand(uint32_t speed, ModbusCommand& data, VFDSpindle* spindle);

            static void reportParsingErrors(ModbusCommand cmd, uint8_t* rx_message, size_t read_length);
            static void reportCmdErrors(ModbusCommand cmd, uint8_t* rx_message, size_t read_length, uint8_t id);

        public:
            VFDProtocol() {}
            VFDProtocol(const VFDProtocol&)            = delete;
            VFDProtocol(VFDProtocol&&)                 = delete;
            VFDProtocol& operator=(const VFDProtocol&) = delete;
            VFDProtocol& operator=(VFDProtocol&&)      = delete;
        };
    }
}
