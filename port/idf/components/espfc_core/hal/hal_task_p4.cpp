// ESP32-P4 implementation of Hal::Task on native FreeRTOS.
#include "Hal/Task.hpp"

#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace Hal {

bool Task::create(Function function, const char* name, size_t stackSize, void* arg,
                  Priority priority, uint8_t core) {
  const UBaseType_t prio = priority == Priority::High ? 20 : 5;
  TaskHandle_t handle = nullptr;
  const BaseType_t rc = xTaskCreatePinnedToCore(
      reinterpret_cast<TaskFunction_t>(function), name, stackSize, arg, prio,
      &handle, core > 1 ? 1 : core);
  return rc == pdPASS;
}

Task::Handle Task::currentHandle() { return xTaskGetCurrentTaskHandle(); }

bool Task::notifyFromIsr(Handle handle) {
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(static_cast<TaskHandle_t>(handle), &woken);
  return woken == pdTRUE;
}

void Task::waitNotify() { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); }

void Task::exitCurrent() { vTaskDelete(nullptr); }

void Task::disableIdleWatchdog() { esp_task_wdt_delete(nullptr); }

} // namespace Hal
