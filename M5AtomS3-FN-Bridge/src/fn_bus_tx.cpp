#include "fn_bus_tx.h"

#include <Arduino.h>

#include "driver/gpio.h"

#include "esp32-hal-rmt.h"

namespace
{
    // Grove port G1 - see README.md's pinout table. Bare GPIO, not the real
    // FN bus pin - see fn_bus_tx.h's top comment.
    constexpr int kFnTxPin = 2;

    // See fn_bus_tx.h's kFnBusTxMaxWords comment - this reserves the whole
    // 256-word budget regardless of how much a given caller's buffer
    // actually uses, same as before this file supported variable-length
    // buffers.
    constexpr rmt_reserve_memsize_t kMemSize = RMT_MEM_256;

    rmt_obj_t *s_rmt = nullptr;
}

bool fn_bus_tx_start(const rmt_data_t *frame, size_t wordCount)
{
    fn_bus_tx_stop(); // idempotent restart, in case one's already running

    if (wordCount == 0 || wordCount > kFnBusTxMaxWords)
    {
        Serial.printf("fn_bus_tx: refusing to start - %u words is out of range (max %u)\n",
                      static_cast<unsigned>(wordCount), static_cast<unsigned>(kFnBusTxMaxWords));
        return false;
    }

    s_rmt = rmtInit(kFnTxPin, RMT_TX_MODE, kMemSize);
    if (s_rmt == nullptr)
    {
        Serial.println("fn_bus_tx: rmtInit() failed");
        return false;
    }

    gpio_set_drive_capability(GPIO_NUM_2, GPIO_DRIVE_CAP_0);

    float actual_ns = rmtSetTick(s_rmt, 1000.0f); // 1us/tick
    Serial.printf("fn_bus_tx: RMT tick = %.1fns (requested 1000ns)\n", actual_ns);

    bool ok = rmtLoop(s_rmt, const_cast<rmt_data_t *>(frame), wordCount);
    if (!ok)
    {
        Serial.println("fn_bus_tx: rmtLoop() failed");
        rmtDeinit(s_rmt);
        s_rmt = nullptr;
        return false;
    }

    Serial.printf("fn_bus_tx: looping %u-word frame on GPIO%d (hardware auto-loop) - "
                  "bench test only, NOT connected to a real FN bus\n",
                  static_cast<unsigned>(wordCount), kFnTxPin);
    return true;
}

void fn_bus_tx_stop()
{
    if (s_rmt == nullptr)
        return;
    rmtDeinit(s_rmt);
    s_rmt = nullptr;
    Serial.println("fn_bus_tx: stopped");
}

bool fn_bus_tx_is_running()
{
    return s_rmt != nullptr;
}
