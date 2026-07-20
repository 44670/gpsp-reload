#include "korvo1_usb_gamepad.h"
#include "korvo1_usb_xinput.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb/hid.h"
#include "usb/hid_host.h"
#include "usb/usb_host.h"

#include <libretro.h>

#define USB_EVENT_TASK_STACK 3072
#define USB_EVENT_TASK_PRIORITY 5
#define HID_EVENT_TASK_STACK 4096
#define HID_EVENT_TASK_PRIORITY 4
#define GAMEPAD_TASK_STACK 3072
#define GAMEPAD_TASK_PRIORITY 3
#define GAMEPAD_EVENT_QUEUE_LEN 8
#define GAMEPAD_REPORT_MAX 64
#define GAMEPAD_CHANGED_REPORT_LOG_LIMIT 64

typedef struct
{
  hid_host_device_handle_t handle;
  hid_host_driver_event_t event;
} gamepad_driver_event_t;

static const char *TAG = "korvo1-usb-pad";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t s_event_queue;
static TaskHandle_t s_usb_event_task;
static TaskHandle_t s_gamepad_task;
static hid_host_device_handle_t s_active_handle;
static uint8_t s_last_report[GAMEPAD_REPORT_MAX];
static size_t s_last_report_len;
static esp32s31_usb_gamepad_stats_t s_stats;

static void stats_set_ready(bool ready)
{
  taskENTER_CRITICAL(&s_lock);
  s_stats.ready = ready;
  taskEXIT_CRITICAL(&s_lock);
}

static void stats_note_error(void)
{
  taskENTER_CRITICAL(&s_lock);
  s_stats.transfer_errors++;
  taskEXIT_CRITICAL(&s_lock);
}

static void wide_to_ascii(const wchar_t *source, char *destination,
                          size_t destination_size)
{
  if (destination_size == 0u)
    return;

  size_t written = 0u;
  while (source != NULL && source[written] != 0 &&
         written + 1u < destination_size)
  {
    const wchar_t value = source[written];
    destination[written] = value >= 0x20 && value <= 0x7e ? (char)value : '?';
    written++;
  }
  destination[written] = '\0';
}

static void print_hex_line(const char *command, size_t offset,
                           const uint8_t *data, size_t length)
{
  char hex[GAMEPAD_REPORT_MAX * 2u + 1u];
  size_t cursor = 0u;
  for (size_t i = 0u; i < length; i++)
  {
    static const char digits[] = "0123456789abcdef";
    hex[cursor++] = digits[data[i] >> 4];
    hex[cursor++] = digits[data[i] & 0x0fu];
  }
  hex[cursor] = '\0';
  printf("result=PASS command=%s offset=%u bytes=%u data=%s\n",
         command, (unsigned)offset, (unsigned)length, hex);
}

static void print_report_descriptor(const uint8_t *descriptor, size_t length)
{
  printf("result=PASS command=usb_hid_descriptor bytes=%u\n",
         (unsigned)length);
  for (size_t offset = 0u; offset < length; offset += 16u)
  {
    size_t chunk = length - offset;
    if (chunk > 16u)
      chunk = 16u;
    print_hex_line("usb_hid_descriptor_chunk", offset,
                   descriptor + offset, chunk);
  }
  fflush(stdout);
}

static uint32_t hid_item_value(const uint8_t *data, size_t size)
{
  uint32_t value = 0u;
  for (size_t i = 0u; i < size; i++)
    value |= (uint32_t)data[i] << (i * 8u);
  return value;
}

static bool descriptor_has_gamepad_application(const uint8_t *descriptor,
                                               size_t length)
{
  uint32_t usage_page = 0u;
  uint32_t usage = 0u;
  for (size_t offset = 0u; offset < length;)
  {
    const uint8_t prefix = descriptor[offset++];
    if (prefix == 0xfeu)
    {
      if (offset + 2u > length)
        return false;
      const size_t long_size = descriptor[offset];
      offset += 2u;
      if (offset + long_size > length)
        return false;
      offset += long_size;
      continue;
    }

    size_t item_size = prefix & 0x03u;
    if (item_size == 3u)
      item_size = 4u;
    if (offset + item_size > length)
      return false;
    const uint8_t type = (prefix >> 2) & 0x03u;
    const uint8_t tag = (prefix >> 4) & 0x0fu;
    const uint32_t value = hid_item_value(descriptor + offset, item_size);
    offset += item_size;

    if (type == 1u && tag == 0u)
      usage_page = value;
    else if (type == 2u && tag == 0u)
      usage = value;
    else if (type == 0u && tag == 10u && value == 1u &&
             usage_page == 0x01u && (usage == 0x04u || usage == 0x05u))
      return true;

    if (type == 0u)
      usage = 0u;
  }
  return false;
}

/*
 * The first hardware run deliberately keeps decoding conservative. HID button
 * and axis fields are descriptor-defined; the descriptor and changed raw
 * reports are emitted on UART so the connected pad can be mapped exactly.
 */
static uint16_t decode_probe_report(const uint8_t *data, size_t length)
{
  (void)data;
  (void)length;
  return 0u;
}

static void handle_input_report(hid_host_device_handle_t handle)
{
  uint8_t report[GAMEPAD_REPORT_MAX];
  size_t report_length = 0u;
  const esp_err_t error = hid_host_device_get_raw_input_report_data(
      handle, report, sizeof(report), &report_length);
  if (error != ESP_OK)
  {
    ESP_LOGW(TAG, "read HID report failed: %s", esp_err_to_name(error));
    stats_note_error();
    return;
  }

  const bool changed = report_length != s_last_report_len ||
                       memcmp(report, s_last_report, report_length) != 0;
  uint32_t changed_index;
  taskENTER_CRITICAL(&s_lock);
  s_stats.input_reports++;
  s_stats.last_report_bytes =
      report_length <= UINT8_MAX ? (uint8_t)report_length : UINT8_MAX;
  if (changed)
    s_stats.changed_reports++;
  changed_index = s_stats.changed_reports;
  s_stats.joypad_mask = decode_probe_report(report, report_length);
  taskEXIT_CRITICAL(&s_lock);

  if (!changed)
    return;

  s_last_report_len = report_length;
  memcpy(s_last_report, report, report_length);
  if (changed_index <= GAMEPAD_CHANGED_REPORT_LOG_LIMIT)
  {
    print_hex_line("usb_hid_report", changed_index, report, report_length);
    fflush(stdout);
  }
}

static void hid_interface_callback(hid_host_device_handle_t handle,
                                   hid_host_interface_event_t event,
                                   void *arg)
{
  (void)arg;
  switch (event)
  {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
      handle_input_report(handle);
      break;

    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
      if (handle == s_active_handle)
      {
        taskENTER_CRITICAL(&s_lock);
        s_stats.connected = false;
        s_stats.joypad_mask = 0u;
        s_stats.disconnect_count++;
        taskEXIT_CRITICAL(&s_lock);
        s_active_handle = NULL;
        s_last_report_len = 0u;
        ESP_LOGI(TAG, "USB HID gamepad disconnected");
      }
      (void)hid_host_device_close(handle);
      break;

    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
      ESP_LOGW(TAG, "USB HID interrupt transfer failed");
      stats_note_error();
      break;

    default:
      break;
  }
}

static esp_err_t open_hid_interface(hid_host_device_handle_t handle)
{
  hid_host_dev_params_t params = {0};
  ESP_RETURN_ON_ERROR(hid_host_device_get_params(handle, &params), TAG,
                      "get HID parameters failed");

  if (s_active_handle != NULL)
  {
    ESP_LOGW(TAG, "ignore extra HID interface addr=%u iface=%u proto=%u",
             params.addr, params.iface_num, params.proto);
    return ESP_ERR_NOT_SUPPORTED;
  }

  const hid_host_device_config_t device_config = {
      .callback = hid_interface_callback,
      .callback_arg = NULL,
  };
  ESP_RETURN_ON_ERROR(hid_host_device_open(handle, &device_config), TAG,
                      "open HID interface failed");

  hid_host_dev_info_t info = {0};
  const esp_err_t info_error = hid_host_get_device_info(handle, &info);
  char manufacturer[HID_STR_DESC_MAX_LENGTH] = {0};
  char product[HID_STR_DESC_MAX_LENGTH] = {0};
  if (info_error == ESP_OK)
  {
    wide_to_ascii(info.iManufacturer, manufacturer, sizeof(manufacturer));
    wide_to_ascii(info.iProduct, product, sizeof(product));
  }

  size_t descriptor_length = 0u;
  const uint8_t *descriptor =
      hid_host_get_report_descriptor(handle, &descriptor_length);
  if (descriptor == NULL)
  {
    ESP_LOGE(TAG, "get HID report descriptor failed");
    (void)hid_host_device_close(handle);
    return ESP_FAIL;
  }
  if (!descriptor_has_gamepad_application(descriptor, descriptor_length))
  {
    printf("result=PASS command=usb_hid_skip addr=%u iface=%u "
           "reason=not_gamepad descriptor_bytes=%u\n",
           params.addr, params.iface_num, (unsigned)descriptor_length);
    fflush(stdout);
    (void)hid_host_device_close(handle);
    return ESP_ERR_NOT_SUPPORTED;
  }

  taskENTER_CRITICAL(&s_lock);
  s_stats.connected = true;
  s_stats.connect_count++;
  s_stats.vid = info_error == ESP_OK ? info.VID : 0u;
  s_stats.pid = info_error == ESP_OK ? info.PID : 0u;
  s_stats.report_descriptor_bytes = descriptor_length <= UINT16_MAX
                                        ? (uint16_t)descriptor_length
                                        : UINT16_MAX;
  taskEXIT_CRITICAL(&s_lock);

  s_active_handle = handle;
  s_last_report_len = 0u;
  printf("result=PASS command=usb_hid_connect addr=%u iface=%u "
         "subclass=%u protocol=%u vid=0x%04x pid=0x%04x "
         "manufacturer='%s' product='%s'\n",
         params.addr, params.iface_num, params.sub_class, params.proto,
         info_error == ESP_OK ? info.VID : 0u,
         info_error == ESP_OK ? info.PID : 0u,
         manufacturer, product);
  print_report_descriptor(descriptor, descriptor_length);

  const esp_err_t start_error = hid_host_device_start(handle);
  if (start_error != ESP_OK)
  {
    ESP_LOGE(TAG, "start HID reports failed: %s",
             esp_err_to_name(start_error));
    taskENTER_CRITICAL(&s_lock);
    s_stats.connected = false;
    taskEXIT_CRITICAL(&s_lock);
    s_active_handle = NULL;
    (void)hid_host_device_close(handle);
    return start_error;
  }
  return ESP_OK;
}

static void gamepad_task(void *arg)
{
  (void)arg;
  gamepad_driver_event_t event;
  for (;;)
  {
    if (xQueueReceive(s_event_queue, &event, portMAX_DELAY) != pdTRUE)
      continue;
    if (event.event == HID_HOST_DRIVER_EVENT_CONNECTED)
    {
      const esp_err_t error = open_hid_interface(event.handle);
      if (error != ESP_OK && error != ESP_ERR_NOT_SUPPORTED)
        stats_note_error();
    }
  }
}

static void hid_driver_callback(hid_host_device_handle_t handle,
                                hid_host_driver_event_t event,
                                void *arg)
{
  (void)arg;
  const gamepad_driver_event_t queued = {
      .handle = handle,
      .event = event,
  };
  if (xQueueSend(s_event_queue, &queued, 0) != pdTRUE)
  {
    taskENTER_CRITICAL(&s_lock);
    s_stats.dropped_events++;
    taskEXIT_CRITICAL(&s_lock);
  }
}

static void usb_event_task(void *arg)
{
  (void)arg;
  for (;;)
  {
    uint32_t event_flags = 0u;
    const esp_err_t error =
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
    if (error != ESP_OK)
    {
      ESP_LOGE(TAG, "USB host event loop failed: %s",
               esp_err_to_name(error));
      stats_note_error();
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

esp_err_t esp32s31_korvo1_usb_gamepad_init(void)
{
  if (s_stats.ready)
    return ESP_OK;

  s_event_queue = xQueueCreate(GAMEPAD_EVENT_QUEUE_LEN,
                               sizeof(gamepad_driver_event_t));
  if (s_event_queue == NULL)
    return ESP_ERR_NO_MEM;

  const usb_host_config_t host_config = {
      .skip_phy_setup = false,
      .intr_flags = ESP_INTR_FLAG_LOWMED,
  };
  esp_err_t error = usb_host_install(&host_config);
  if (error != ESP_OK)
  {
    ESP_LOGE(TAG, "install USB host failed: %s", esp_err_to_name(error));
    return error;
  }

  error = esp32s31_korvo1_xinput_init();
  if (error != ESP_OK)
  {
    ESP_LOGE(TAG, "install USB XInput client failed: %s",
             esp_err_to_name(error));
    return error;
  }

  if (xTaskCreate(usb_event_task, "usb_events", USB_EVENT_TASK_STACK, NULL,
                  USB_EVENT_TASK_PRIORITY, &s_usb_event_task) != pdPASS)
    return ESP_ERR_NO_MEM;

  const hid_host_driver_config_t hid_config = {
      .create_background_task = true,
      .task_priority = HID_EVENT_TASK_PRIORITY,
      .stack_size = HID_EVENT_TASK_STACK,
      .core_id = 0,
      .callback = hid_driver_callback,
      .callback_arg = NULL,
  };
  error = hid_host_install(&hid_config);
  if (error != ESP_OK)
  {
    ESP_LOGE(TAG, "install USB HID host failed: %s", esp_err_to_name(error));
    return error;
  }

  if (xTaskCreate(gamepad_task, "usb_gamepad", GAMEPAD_TASK_STACK, NULL,
                  GAMEPAD_TASK_PRIORITY, &s_gamepad_task) != pdPASS)
    return ESP_ERR_NO_MEM;

  stats_set_ready(true);
  ESP_LOGI(TAG, "USB HID host ready on GPIO19/20");
  return ESP_OK;
}

uint16_t esp32s31_korvo1_usb_gamepad_mask(void)
{
  uint16_t mask;
  taskENTER_CRITICAL(&s_lock);
  mask = s_stats.joypad_mask;
  taskEXIT_CRITICAL(&s_lock);
  return (uint16_t)(mask | esp32s31_korvo1_xinput_mask());
}

void esp32s31_korvo1_usb_gamepad_get_stats(
    esp32s31_usb_gamepad_stats_t *stats)
{
  if (stats == NULL)
    return;
  taskENTER_CRITICAL(&s_lock);
  *stats = s_stats;
  taskEXIT_CRITICAL(&s_lock);

  esp32s31_xinput_stats_t xinput = {0};
  esp32s31_korvo1_xinput_get_stats(&xinput);
  stats->ready = stats->ready || xinput.ready;
  stats->connected = stats->connected || xinput.connected;
  stats->joypad_mask |= xinput.joypad_mask;
  stats->connect_count += xinput.connect_count;
  stats->disconnect_count += xinput.disconnect_count;
  stats->input_reports += xinput.input_reports;
  stats->changed_reports += xinput.changed_reports;
  stats->transfer_errors += xinput.transfer_errors;
  if (xinput.connected)
  {
    stats->xinput = true;
    stats->vid = xinput.vid;
    stats->pid = xinput.pid;
    stats->last_report_bytes = 20u;
  }
}
