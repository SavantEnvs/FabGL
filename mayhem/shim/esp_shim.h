// Host shim for the ESP-IDF / FreeRTOS API surface FabGL's portable code touches.
// Single-threaded: tasks never run, timers never fire, mutexes are no-ops, queues are plain FIFOs.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>

#define IRAM_ATTR
#define DRAM_ATTR
#define RTC_DATA_ATTR
#define PROGMEM
#define FLASHMEM
#define ICACHE_RAM_ATTR

typedef int32_t esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
#define pdFALSE 0
#define pdTRUE 1
#define pdPASS 1
#define pdFAIL 0
#define errQUEUE_FULL 0
#define portMAX_DELAY 0xffffffffu
#define portTICK_PERIOD_MS 1
#define portTICK_RATE_MS 1
#define configTICK_RATE_HZ 1000
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define tskNO_AFFINITY 0x7fffffff
#define portNUM_PROCESSORS 2
#define configMAX_PRIORITIES 25
#define configMINIMAL_STACK_SIZE 768

typedef struct { int dummy; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED { 0 }
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define portENTER_CRITICAL_ISR(m) ((void)(m))
#define portEXIT_CRITICAL_ISR(m) ((void)(m))
#define portYIELD_FROM_ISR()
#define taskENTER_CRITICAL(m) ((void)(m))
#define taskEXIT_CRITICAL(m) ((void)(m))
#define taskDISABLE_INTERRUPTS()
#define taskENABLE_INTERRUPTS()
#define portDISABLE_INTERRUPTS()
#define portENABLE_INTERRUPTS()

#define MALLOC_CAP_8BIT 1
#define MALLOC_CAP_32BIT 2
#define MALLOC_CAP_DMA 4
#define MALLOC_CAP_INTERNAL 8
#define MALLOC_CAP_SPIRAM 16
#define MALLOC_CAP_DEFAULT 32
static inline void * heap_caps_malloc(size_t s, uint32_t) { return malloc(s); }
static inline void * heap_caps_calloc(size_t n, size_t s, uint32_t) { return calloc(n, s); }
static inline void * heap_caps_realloc(void * p, size_t s, uint32_t) { return realloc(p, s); }
static inline void heap_caps_free(void * p) { free(p); }
static inline size_t heap_caps_get_free_size(uint32_t) { return 1u << 20; }
static inline size_t heap_caps_get_largest_free_block(uint32_t) { return 1u << 20; }
static inline uint32_t esp_get_free_heap_size() { return 1u << 20; }

#ifdef __cplusplus
extern "C++" {
#include <deque>
#include <vector>
#include <cstdint>

// ---- queues -------------------------------------------------------------------------
struct ShimQueue {
  size_t itemSize;
  size_t length;
  std::deque<std::vector<uint8_t>> items;
};
typedef ShimQueue * QueueHandle_t;

// Called when a receive finds a byte queue empty (the input stream is exhausted). The harness may
// supply a byte to hand back (return true) so a sequence the firmware task would still be waiting
// to finish can complete; returning false reports an empty queue.
extern bool (*g_shimQueueEmptyHook)(void * arg, uint8_t * byteOut);
extern void * g_shimQueueEmptyArg;
// Called by taskYIELD(); the harness uses it to drain queues nobody consumes in a single-threaded run.
extern void (*g_shimYieldHook)();
#define taskYIELD() do { if (g_shimYieldHook) g_shimYieldHook(); } while (0)

static inline QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t itemSize) { auto q = new ShimQueue; q->itemSize = itemSize; q->length = len; return q; }
static inline void vQueueDelete(QueueHandle_t q) { delete q; }
static inline BaseType_t xQueueSendToBack(QueueHandle_t q, const void * item, TickType_t) {
  if (q->items.size() >= q->length) return pdFALSE;
  q->items.emplace_back((const uint8_t*)item, (const uint8_t*)item + q->itemSize); return pdTRUE; }
static inline BaseType_t xQueueSendToFront(QueueHandle_t q, const void * item, TickType_t) {
  if (q->items.size() >= q->length) return pdFALSE;
  q->items.emplace_front((const uint8_t*)item, (const uint8_t*)item + q->itemSize); return pdTRUE; }
static inline BaseType_t xQueueSend(QueueHandle_t q, const void * item, TickType_t t) { return xQueueSendToBack(q, item, t); }
static inline BaseType_t xQueueSendToBackFromISR(QueueHandle_t q, const void * item, BaseType_t *) { return xQueueSendToBack(q, item, 0); }
static inline BaseType_t xQueueSendToFrontFromISR(QueueHandle_t q, const void * item, BaseType_t *) { return xQueueSendToFront(q, item, 0); }
static inline BaseType_t xQueueReceive(QueueHandle_t q, void * item, TickType_t) {
  if (q->items.empty()) {
    memset(item, 0, q->itemSize);
    if (g_shimQueueEmptyHook && q->itemSize == 1 && g_shimQueueEmptyHook(g_shimQueueEmptyArg, (uint8_t *)item)) return pdTRUE;
    return pdFALSE;
  }
  memcpy(item, q->items.front().data(), q->itemSize); q->items.pop_front(); return pdTRUE; }
static inline BaseType_t xQueueReceiveFromISR(QueueHandle_t q, void * item, BaseType_t *) { return xQueueReceive(q, item, 0); }
static inline BaseType_t xQueuePeek(QueueHandle_t q, void * item, TickType_t) {
  if (q->items.empty()) return pdFALSE;
  memcpy(item, q->items.front().data(), q->itemSize); return pdTRUE; }
static inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) { return q->items.size(); }
static inline UBaseType_t uxQueueMessagesWaitingFromISR(QueueHandle_t q) { return q->items.size(); }
static inline UBaseType_t uxQueueSpacesAvailable(QueueHandle_t q) { return q->length - q->items.size(); }
static inline BaseType_t xQueueOverwrite(QueueHandle_t q, const void * item) { q->items.clear(); return xQueueSendToBack(q, item, 0); }
static inline BaseType_t xQueueReset(QueueHandle_t q) { q->items.clear(); return pdTRUE; }
}
#endif

// ---- semaphores (single-threaded: all no-ops) ----------------------------------------
typedef void * SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex() { return malloc(1); }
static inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return malloc(1); }
static inline SemaphoreHandle_t xSemaphoreCreateBinary() { return malloc(1); }
static inline SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t, UBaseType_t) { return malloc(1); }
static inline void vSemaphoreDelete(SemaphoreHandle_t s) { free(s); }
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t) { return pdTRUE; }
static inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
static inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t) { return pdTRUE; }
static inline BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t, BaseType_t *) { return pdTRUE; }
static inline BaseType_t xSemaphoreTakeFromISR(SemaphoreHandle_t, BaseType_t *) { return pdTRUE; }

// ---- tasks (never run) --------------------------------------------------------------
typedef void * TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
static inline BaseType_t xTaskCreate(TaskFunction_t, const char *, uint32_t, void *, UBaseType_t, TaskHandle_t * h) { if (h) *h = malloc(1); return pdPASS; }
static inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char *, uint32_t, void *, UBaseType_t, TaskHandle_t * h, BaseType_t) { if (h) *h = malloc(1); return pdPASS; }
static inline void vTaskDelete(TaskHandle_t h) { free(h); }
static inline void vTaskDelay(TickType_t) {}
static inline void vTaskSuspend(TaskHandle_t) {}
static inline void vTaskResume(TaskHandle_t) {}
static inline TaskHandle_t xTaskGetCurrentTaskHandle() { return nullptr; }
static inline TickType_t xTaskGetTickCount() { static TickType_t t; return t++; }
static inline void vTaskSuspendAll() {}
static inline BaseType_t xTaskResumeAll() { return pdTRUE; }
static inline void vTaskPrioritySet(TaskHandle_t, UBaseType_t) {}
static inline BaseType_t xTaskNotifyGive(TaskHandle_t) { return pdTRUE; }
static inline uint32_t ulTaskNotifyTake(BaseType_t, TickType_t) { return 0; }

// ---- timers (never fire) ------------------------------------------------------------
typedef void * TimerHandle_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t);
static inline TimerHandle_t xTimerCreate(const char *, TickType_t, UBaseType_t, void *, TimerCallbackFunction_t) { return malloc(1); }
static inline BaseType_t xTimerStart(TimerHandle_t, TickType_t) { return pdPASS; }
static inline BaseType_t xTimerStop(TimerHandle_t, TickType_t) { return pdPASS; }
static inline BaseType_t xTimerReset(TimerHandle_t, TickType_t) { return pdPASS; }
static inline BaseType_t xTimerChangePeriod(TimerHandle_t, TickType_t, TickType_t) { return pdPASS; }
static inline BaseType_t xTimerDelete(TimerHandle_t h, TickType_t) { free(h); return pdPASS; }
static inline void * pvTimerGetTimerID(TimerHandle_t) { return nullptr; }

// ---- misc ESP-IDF ---------------------------------------------------------------------
static inline int64_t esp_timer_get_time() { static int64_t t; t += 1000; return t; }
static inline void ets_delay_us(uint32_t) {}
static inline uint32_t esp_random() { return 4; }
static inline uint32_t xthal_get_ccount() { static uint32_t c; return c += 100; }
static inline uint32_t millis() { return 0; }
static inline void delay(uint32_t) {}
static inline void delayMicroseconds(uint32_t) {}

#define ESP_LOGE(tag, fmt, ...) ((void)0)
#define ESP_LOGW(tag, fmt, ...) ((void)0)
#define ESP_LOGI(tag, fmt, ...) ((void)0)
#define ESP_LOGD(tag, fmt, ...) ((void)0)
#define ESP_LOGV(tag, fmt, ...) ((void)0)
#define ESP_ERROR_CHECK(x) ((void)(x))

typedef enum { GPIO_NUM_0 = 0, GPIO_NUM_MAX = 40, GPIO_NUM_NC = -1 } gpio_num_t;
typedef int adc1_channel_t;
typedef int adc_unit_t;
typedef struct { int dummy; } sdmmc_card_t;
typedef struct { int dummy; } lldesc_t;

typedef void * intr_handle_t;
typedef void (*intr_handler_t)(void *);
typedef void * esp_timer_handle_t;
typedef int gpio_mode_t;
#ifdef __cplusplus
static inline bool xQueueIsQueueFullFromISR(QueueHandle_t q) { return q->items.size() >= q->length; }
static inline void vTaskNotifyGiveFromISR(TaskHandle_t, BaseType_t *) {}
static inline void taskEmuCheck() {}
#endif
#include <stdarg.h>
#include <limits.h>
#include <stdlib.h>
static inline char * itoa(int v, char * buf, int radix) { snprintf(buf, 16, radix == 16 ? "%x" : "%d", v); return buf; }
enum { GPIO_MODE_DISABLE, GPIO_MODE_INPUT, GPIO_MODE_OUTPUT, GPIO_MODE_OUTPUT_OD, GPIO_MODE_INPUT_OUTPUT_OD, GPIO_MODE_INPUT_OUTPUT };

#include <time.h>
#include <sys/stat.h>
typedef uint8_t Uint8;
typedef uint32_t SDL_AudioDeviceID;
static inline int gpio_set_level(int, uint32_t) { return 0; }
static inline int gpio_get_level(int) { return 0; }
typedef int adc_bits_width_t;
typedef int adc_atten_t;
static inline int adc1_get_raw(int) { return 0; }
static inline int adc1_config_width(int) { return 0; }
static inline int adc1_config_channel_atten(int, int) { return 0; }
#define ADC_WIDTH_BIT_12 3
#define ADC_ATTEN_DB_11 3
#define ADC1_CHANNEL_0 0
#define ADC1_CHANNEL_1 1
#define ADC1_CHANNEL_2 2
#define ADC1_CHANNEL_3 3
#define ADC1_CHANNEL_4 4
#define ADC1_CHANNEL_5 5
#define ADC1_CHANNEL_6 6
#define ADC1_CHANNEL_7 7
#define ADC1_CHANNEL_0_GPIO_NUM 36
#define ADC1_CHANNEL_1_GPIO_NUM 37
#define ADC1_CHANNEL_2_GPIO_NUM 38
#define ADC1_CHANNEL_3_GPIO_NUM 39
#define ADC1_CHANNEL_4_GPIO_NUM 32
#define ADC1_CHANNEL_5_GPIO_NUM 33
#define ADC1_CHANNEL_6_GPIO_NUM 34
#define ADC1_CHANNEL_7_GPIO_NUM 35
#define WIFI_TASK_CORE_ID 0
