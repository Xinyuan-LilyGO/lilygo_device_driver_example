/*
 * @Description: tca8418
 * @Author: LILYGO_L
 * @Date: 2025-06-13 14:20:16
 * @LastEditTime: 2026-04-30 10:31:48
 * @License: GPL 3.0
 */
#include "common.h"

namespace keyboard = common::board::keyboard_expansion;

extern "C" void app_main(void) {
  printf("TCA8418 example on %s\n", common::kBoardName);

  auto& driver = common::GetDriver();
  if (!common::InitMinimalDriver()) {
    printf("Minimal device driver initialization failed\n");
    return;
  }
  // The expansion initializer creates the TCA8418 driver and handles its reset.
  driver.InitKeyboardExpansion();
  if (!driver.IsTca8418Ready()) {
    printf("TCA8418 initialization failed\n");
    return;
  }

  auto& tca8418 = driver.chip().tca8418;

  while (true) {
    cpp_bus_driver::Tca8418::IrqStatus irq_status;
    const uint8_t irq_flags = tca8418->GetIrqFlag();
    if (!tca8418->ParseIrqStatus(irq_flags, irq_status)) {
      printf("Read IRQ status failed\n");
    } else {
      if (irq_status.fifo_overflow_flag) {
        printf("Keyboard event FIFO overflow\n");
      }

      // Poll the FIFO too, including events arriving while IRQ flags are cleared.
      const uint8_t event_count = tca8418->GetFingerCount();
      if (event_count == 0xFF) {
        printf("Read keyboard event count failed\n");
      } else if (event_count > 0) {
        cpp_bus_driver::Tca8418::TouchPoint tp;

        if (tca8418->GetMultipleTouchPoint(tp) == true) {
          printf("Keyboard events: %d\n", tp.finger_count);

          for (uint8_t i = 0; i < tp.info.size(); i++) {
            switch (tp.info[i].event_type) {
              case cpp_bus_driver::Tca8418::EventType::kKeypad: {
                cpp_bus_driver::Tca8418::TouchPosition touch_position;

                if (tca8418->ParseTouchNum(tp.info[i].num, touch_position) ==
                    true) {
                  printf("Keypad event\n");
                  printf(
                      "   Touch num:[%d] num: %d x: %d y: %d press flag: %d\n",
                      i + 1, tp.info[i].num, touch_position.x, touch_position.y,
                      tp.info[i].press_flag);

                  const auto& key_map = keyboard::device::tca8418::kMap;
                  const size_t key_count = key_map.size();
                  if ((tp.info[i].num > 0) && (tp.info[i].num <= key_count)) {
                    const auto& key = key_map[tp.info[i].num - 1];
                    printf("   Key code: %u\n", static_cast<unsigned>(key.key));
                    if (key.character != '\0') {
                      printf("   Character: '%c'\n", key.character);
                    }
                    if (key.function_character != '\0') {
                      printf("   Fn character: '%c'\n", key.function_character);
                    }
                  }
                }

                break;
              }

              case cpp_bus_driver::Tca8418::EventType::kGpio:
                printf("Gpio event\n");
                printf("   Touch num:[%d] num: %d press flag: %d\n", i + 1,
                    tp.info[i].num, tp.info[i].press_flag);
                break;

              default:
                break;
            }
          }
        } else {
          printf("Read keyboard events failed\n");
        }
      }

      if (irq_status.gpio_interrupt_flag) {
        uint32_t gpio_flags = 0;
        if (!tca8418->GetClearGpioIrqFlag(&gpio_flags)) {
          printf("Read GPIO IRQ status failed\n");
        }
      }
      if (irq_flags != 0 && !tca8418->ClearIrqFlag(irq_flags)) {
        printf("Clear IRQ status failed\n");
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
