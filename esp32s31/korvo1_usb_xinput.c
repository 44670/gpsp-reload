#include "korvo1_usb_xinput.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

#include <libretro.h>

#define XINPUT_TASK_STACK 4096
#define XINPUT_TASK_PRIORITY 4
#define XINPUT_CLIENT_EVENTS 8
#define XINPUT_REGISTER_TIMEOUT_MS 1000

#define XINPUT_DPAD_UP UINT16_C(0x0001)
#define XINPUT_DPAD_DOWN UINT16_C(0x0002)
#define XINPUT_DPAD_LEFT UINT16_C(0x0004)
#define XINPUT_DPAD_RIGHT UINT16_C(0x0008)
#define XINPUT_START UINT16_C(0x0010)
#define XINPUT_BACK UINT16_C(0x0020)
#define XINPUT_LEFT_THUMB UINT16_C(0x0040)
#define XINPUT_RIGHT_THUMB UINT16_C(0x0080)
#define XINPUT_LEFT_SHOULDER UINT16_C(0x0100)
#define XINPUT_RIGHT_SHOULDER UINT16_C(0x0200)
#define XINPUT_GUIDE UINT16_C(0x0400)
#define XINPUT_A UINT16_C(0x1000)
#define XINPUT_B UINT16_C(0x2000)
#define XINPUT_X UINT16_C(0x4000)
#define XINPUT_Y UINT16_C(0x8000)

typedef struct
{
  usb_host_client_handle_t client;
  usb_device_handle_t device;
  usb_transfer_t *input_transfer;
  usb_transfer_t *output_transfer;
  uint8_t pending_address;
  uint8_t interface_number;
  uint8_t endpoint_in;
  uint8_t endpoint_out;
  uint16_t endpoint_in_mps;
  uint16_t endpoint_out_mps;
  uint8_t output_phase;
  bool interface_claimed;
  bool device_gone;
} xinput_context_t;

static const char *TAG = "korvo1-xinput";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static xinput_context_t s_xinput;
static esp32s31_xinput_stats_t s_stats;
static esp_err_t s_register_result = ESP_FAIL;

static uint16_t xinput_to_libretro(uint16_t buttons)
{
  uint16_t mask = 0u;
  if (buttons & XINPUT_DPAD_UP)
    mask |= UINT16_C(1) << RETRO_DEVICE_ID_JOYPAD_UP;
  if (buttons & XINPUT_DPAD_DOWN)
    mask |= UINT16_C(1) << RETRO_DEVICE_ID_JOYPAD_DOWN;
  if (buttons & XINPUT_DPAD_LEFT)
    mask |= UINT16_C(1) << RETRO_DEVICE_ID_JOYPAD_LEFT;
  if (buttons & XINPUT_DPAD_RIGHT)
    mask |= UINT16_C(1) << RETRO_DEVICE_ID_JOYPAD_RIGHT;
  if (buttons & XINPUT_START)
    mask |= UINT16_C(1) << RETRO_DEVICE_ID_JOYPAD_START;
  if (buttons & XINPUT_BACK)
    mask |= UINT16_C(1) << RETRO_DEVICE_ID_JOYPAD_SELECT;
  if (buttons & XINPUT_LEFT_SHOULDER)
    mask |= UINT16_C(1) << RETRO_DEVICE_ID_JOYPAD_L;
  if (buttons & XINPUT_RIGHT_SHOULDER)
    mask |= UINT16_C(1) << RETRO_DEVICE_ID_JOYPAD_R;

  /* Retropad uses Nintendo geometry: east is A and south is B. */
  if (buttons & XINPUT_B)
    mask |= UINT16_C(1) << RETRO_DEVICE_ID_JOYPAD_A;
  if (buttons & XINPUT_A)
    mask |= UINT16_C(1) << RETRO_DEVICE_ID_JOYPAD_B;
  return mask;
}

typedef struct
{
  uint16_t bit;
  const char *name;
} button_name_t;

static void append_button_names(char *output, size_t output_size,
                                uint16_t buttons)
{
  static const button_name_t names[] = {
      {XINPUT_DPAD_UP, "UP"},
      {XINPUT_DPAD_DOWN, "DOWN"},
      {XINPUT_DPAD_LEFT, "LEFT"},
      {XINPUT_DPAD_RIGHT, "RIGHT"},
      {XINPUT_A, "A"},
      {XINPUT_B, "B"},
      {XINPUT_X, "X"},
      {XINPUT_Y, "Y"},
      {XINPUT_LEFT_SHOULDER, "L"},
      {XINPUT_RIGHT_SHOULDER, "R"},
      {XINPUT_BACK, "SELECT"},
      {XINPUT_START, "START"},
      {XINPUT_LEFT_THUMB, "L3"},
      {XINPUT_RIGHT_THUMB, "R3"},
      {XINPUT_GUIDE, "GUIDE"},
  };

  size_t used = 0u;
  output[0] = '\0';
  for (size_t i = 0u; i < sizeof(names) / sizeof(names[0]); i++)
  {
    if ((buttons & names[i].bit) == 0u)
      continue;
    const int written = snprintf(output + used, output_size - used,
                                 "%s%s", used == 0u ? "" : ",",
                                 names[i].name);
    if (written < 0 || (size_t)written >= output_size - used)
      break;
    used += (size_t)written;
  }
  if (used == 0u)
    strlcpy(output, "-", output_size);
}

static void note_xinput_report(const uint8_t *data, size_t length)
{
  /* Wired Xbox 360 input: 00 14, buttons LE16, triggers, four axes. */
  if (length < 20u || data[0] != 0x00u || data[1] != 0x14u)
    return;

  const uint16_t buttons = (uint16_t)data[2] | (uint16_t)data[3] << 8;
  uint16_t previous;
  uint32_t changed_sequence;
  taskENTER_CRITICAL(&s_lock);
  previous = s_stats.raw_buttons;
  s_stats.raw_buttons = buttons;
  s_stats.joypad_mask = xinput_to_libretro(buttons);
  s_stats.input_reports++;
  if (buttons != previous)
    s_stats.changed_reports++;
  changed_sequence = s_stats.changed_reports;
  taskEXIT_CRITICAL(&s_lock);

  if (buttons == previous)
    return;

  char pressed[80];
  char released[80];
  append_button_names(pressed, sizeof(pressed),
                      (uint16_t)(buttons & ~previous));
  append_button_names(released, sizeof(released),
                      (uint16_t)(previous & ~buttons));
  printf("result=PASS command=usb_gamepad_event protocol=xinput "
         "seq=%" PRIu32 " raw=0x%04x pressed=%s released=%s "
         "gba_mask=0x%04x\n",
         changed_sequence, buttons, pressed, released,
         xinput_to_libretro(buttons));
  fflush(stdout);
}

static void input_transfer_done(usb_transfer_t *transfer)
{
  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED)
  {
    note_xinput_report(transfer->data_buffer,
                       (size_t)transfer->actual_num_bytes);
    const esp_err_t error = usb_host_transfer_submit(transfer);
    if (error == ESP_OK)
      return;
    ESP_LOGW(TAG, "resubmit XInput transfer failed: %s",
             esp_err_to_name(error));
  }
  else if (transfer->status == USB_TRANSFER_STATUS_NO_DEVICE ||
           transfer->status == USB_TRANSFER_STATUS_CANCELED)
  {
    return;
  }
  else
  {
    ESP_LOGW(TAG, "XInput transfer status=%d", transfer->status);
  }

  taskENTER_CRITICAL(&s_lock);
  s_stats.transfer_errors++;
  taskEXIT_CRITICAL(&s_lock);
}

static void output_transfer_done(usb_transfer_t *transfer)
{
  xinput_context_t *context = (xinput_context_t *)transfer->context;
  if (transfer->status != USB_TRANSFER_STATUS_COMPLETED)
  {
    if (transfer->status != USB_TRANSFER_STATUS_NO_DEVICE &&
        transfer->status != USB_TRANSFER_STATUS_CANCELED)
    {
      ESP_LOGW(TAG, "XInput output transfer status=%d", transfer->status);
      taskENTER_CRITICAL(&s_lock);
      s_stats.transfer_errors++;
      taskEXIT_CRITICAL(&s_lock);
    }
    context->output_phase = 0u;
    return;
  }

  if (context->output_phase == 1u)
  {
    static const uint8_t rumble_stop[8] = {
        0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    memcpy(transfer->data_buffer, rumble_stop, sizeof(rumble_stop));
    transfer->num_bytes = sizeof(rumble_stop);
    context->output_phase = 2u;
    const esp_err_t error = usb_host_transfer_submit(transfer);
    if (error == ESP_OK)
      return;
    ESP_LOGW(TAG, "submit XInput rumble-stop failed: %s",
             esp_err_to_name(error));
    taskENTER_CRITICAL(&s_lock);
    s_stats.transfer_errors++;
    taskEXIT_CRITICAL(&s_lock);
  }
  context->output_phase = 0u;
}

static bool interface_is_wired_xinput(const usb_intf_desc_t *interface)
{
  return interface->bNumEndpoints >= 2u &&
         interface->bInterfaceClass == 0xffu &&
         interface->bInterfaceSubClass == 0x5du &&
         interface->bInterfaceProtocol == 0x01u;
}

static esp_err_t find_xinput_interface(const usb_config_desc_t *config,
                                       const usb_intf_desc_t **found_interface,
                                       const usb_ep_desc_t **found_in,
                                       const usb_ep_desc_t **found_out)
{
  const uint8_t *bytes = (const uint8_t *)config;
  const size_t total = config->wTotalLength;
  const usb_intf_desc_t *candidate = NULL;
  const usb_ep_desc_t *endpoint_in = NULL;
  const usb_ep_desc_t *endpoint_out = NULL;

  for (size_t offset = config->bLength; offset + 2u <= total;)
  {
    const usb_standard_desc_t *descriptor =
        (const usb_standard_desc_t *)(bytes + offset);
    if (descriptor->bLength < 2u || offset + descriptor->bLength > total)
      return ESP_ERR_INVALID_RESPONSE;

    if (descriptor->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE)
    {
      const usb_intf_desc_t *interface =
          (const usb_intf_desc_t *)descriptor;
      printf("result=PASS command=usb_interface iface=%u alt=%u eps=%u "
             "class=0x%02x subclass=0x%02x protocol=0x%02x\n",
             interface->bInterfaceNumber, interface->bAlternateSetting,
             interface->bNumEndpoints, interface->bInterfaceClass,
             interface->bInterfaceSubClass, interface->bInterfaceProtocol);
      if (candidate != NULL)
        break;
      if (interface_is_wired_xinput(interface))
        candidate = interface;
    }
    else if (descriptor->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT &&
             candidate != NULL)
    {
      const usb_ep_desc_t *endpoint = (const usb_ep_desc_t *)descriptor;
      if (USB_EP_DESC_GET_XFERTYPE(endpoint) == USB_TRANSFER_TYPE_INTR)
      {
        if (USB_EP_DESC_GET_EP_DIR(endpoint))
          endpoint_in = endpoint;
        else
          endpoint_out = endpoint;
      }
    }
    offset += descriptor->bLength;
  }

  if (candidate == NULL || endpoint_in == NULL || endpoint_out == NULL)
    return ESP_ERR_NOT_FOUND;
  *found_interface = candidate;
  *found_in = endpoint_in;
  *found_out = endpoint_out;
  return ESP_OK;
}

static esp_err_t open_xinput_device(uint8_t address)
{
  xinput_context_t *context = &s_xinput;
  esp_err_t error =
      usb_host_device_open(context->client, address, &context->device);
  if (error != ESP_OK)
    return error;

  const usb_device_desc_t *device_desc = NULL;
  const usb_config_desc_t *config_desc = NULL;
  if ((error = usb_host_get_device_descriptor(context->device,
                                               &device_desc)) != ESP_OK ||
      (error = usb_host_get_active_config_descriptor(context->device,
                                                      &config_desc)) != ESP_OK)
    goto fail;

  const usb_intf_desc_t *interface = NULL;
  const usb_ep_desc_t *endpoint_in = NULL;
  const usb_ep_desc_t *endpoint_out = NULL;
  error = find_xinput_interface(config_desc, &interface, &endpoint_in,
                                &endpoint_out);
  if (error != ESP_OK)
    goto fail;

  error = usb_host_interface_claim(context->client, context->device,
                                   interface->bInterfaceNumber,
                                   interface->bAlternateSetting);
  if (error != ESP_OK)
    goto fail;
  context->interface_claimed = true;
  context->interface_number = interface->bInterfaceNumber;
  context->endpoint_in = endpoint_in->bEndpointAddress;
  context->endpoint_out = endpoint_out->bEndpointAddress;
  context->endpoint_in_mps = USB_EP_DESC_GET_MPS(endpoint_in);
  context->endpoint_out_mps = USB_EP_DESC_GET_MPS(endpoint_out);

  error = usb_host_transfer_alloc(context->endpoint_in_mps, 0,
                                  &context->input_transfer);
  if (error != ESP_OK)
    goto fail;
  context->input_transfer->device_handle = context->device;
  context->input_transfer->bEndpointAddress = context->endpoint_in;
  context->input_transfer->num_bytes = context->endpoint_in_mps;
  context->input_transfer->callback = input_transfer_done;
  context->input_transfer->context = context;
  context->input_transfer->timeout_ms = 0u;

  error = usb_host_transfer_alloc(context->endpoint_out_mps, 0,
                                  &context->output_transfer);
  if (error != ESP_OK)
    goto fail;
  context->output_transfer->device_handle = context->device;
  context->output_transfer->bEndpointAddress = context->endpoint_out;
  context->output_transfer->callback = output_transfer_done;
  context->output_transfer->context = context;
  context->output_transfer->timeout_ms = 0u;

  taskENTER_CRITICAL(&s_lock);
  s_stats.connected = true;
  s_stats.vid = device_desc->idVendor;
  s_stats.pid = device_desc->idProduct;
  s_stats.connect_count++;
  s_stats.raw_buttons = 0u;
  s_stats.joypad_mask = 0u;
  taskEXIT_CRITICAL(&s_lock);

  printf("result=PASS command=usb_xinput_connect addr=%u iface=%u "
         "vid=0x%04x pid=0x%04x ep_in=0x%02x ep_out=0x%02x "
         "mps_in=%u mps_out=%u\n",
         address, context->interface_number, device_desc->idVendor,
         device_desc->idProduct, context->endpoint_in,
         context->endpoint_out, context->endpoint_in_mps,
         context->endpoint_out_mps);
  fflush(stdout);

  static const uint8_t player_one_led[3] = {0x01, 0x03, 0x06};
  memcpy(context->output_transfer->data_buffer, player_one_led,
         sizeof(player_one_led));
  context->output_transfer->num_bytes = sizeof(player_one_led);
  context->output_phase = 1u;
  error = usb_host_transfer_submit(context->output_transfer);
  if (error != ESP_OK)
  {
    ESP_LOGW(TAG, "submit XInput LED init failed: %s",
             esp_err_to_name(error));
    context->output_phase = 0u;
  }

  error = usb_host_transfer_submit(context->input_transfer);
  if (error == ESP_OK)
    return ESP_OK;

fail:
  if (error != ESP_ERR_NOT_FOUND)
    ESP_LOGW(TAG, "XInput open addr=%u failed: %s", address,
             esp_err_to_name(error));
  if (context->input_transfer != NULL)
  {
    (void)usb_host_transfer_free(context->input_transfer);
    context->input_transfer = NULL;
  }
  if (context->output_transfer != NULL)
  {
    (void)usb_host_transfer_free(context->output_transfer);
    context->output_transfer = NULL;
  }
  if (context->interface_claimed)
  {
    (void)usb_host_interface_release(context->client, context->device,
                                     context->interface_number);
    context->interface_claimed = false;
  }
  if (context->device != NULL)
  {
    (void)usb_host_device_close(context->client, context->device);
    context->device = NULL;
  }
  return error;
}

static void close_xinput_device(void)
{
  xinput_context_t *context = &s_xinput;
  taskENTER_CRITICAL(&s_lock);
  const bool was_connected = s_stats.connected;
  s_stats.connected = false;
  s_stats.raw_buttons = 0u;
  s_stats.joypad_mask = 0u;
  if (was_connected)
    s_stats.disconnect_count++;
  taskEXIT_CRITICAL(&s_lock);

  if (context->input_transfer != NULL)
  {
    (void)usb_host_transfer_free(context->input_transfer);
    context->input_transfer = NULL;
  }
  if (context->output_transfer != NULL)
  {
    (void)usb_host_transfer_free(context->output_transfer);
    context->output_transfer = NULL;
  }
  if (context->interface_claimed)
  {
    (void)usb_host_interface_release(context->client, context->device,
                                     context->interface_number);
    context->interface_claimed = false;
  }
  if (context->device != NULL)
  {
    (void)usb_host_device_close(context->client, context->device);
    context->device = NULL;
  }
  context->device_gone = false;
  if (was_connected)
    ESP_LOGI(TAG, "XInput gamepad disconnected");
}

static void xinput_client_callback(const usb_host_client_event_msg_t *event,
                                   void *arg)
{
  xinput_context_t *context = (xinput_context_t *)arg;
  switch (event->event)
  {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
      if (context->device == NULL && context->pending_address == 0u)
        context->pending_address = event->new_dev.address;
      break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
      if (event->dev_gone.dev_hdl == context->device)
        context->device_gone = true;
      break;
    default:
      break;
  }
}

static void xinput_task(void *arg)
{
  TaskHandle_t task_to_notify = (TaskHandle_t)arg;
  const usb_host_client_config_t config = {
      .is_synchronous = false,
      .max_num_event_msg = XINPUT_CLIENT_EVENTS,
      .async = {
          .client_event_callback = xinput_client_callback,
          .callback_arg = &s_xinput,
      },
  };
  s_register_result = usb_host_client_register(&config, &s_xinput.client);
  taskENTER_CRITICAL(&s_lock);
  s_stats.ready = s_register_result == ESP_OK;
  taskEXIT_CRITICAL(&s_lock);
  xTaskNotifyGive(task_to_notify);
  if (s_register_result != ESP_OK)
  {
    vTaskDelete(NULL);
    return;
  }

  for (;;)
  {
    if (s_xinput.device_gone)
      close_xinput_device();
    if (s_xinput.pending_address != 0u && s_xinput.device == NULL)
    {
      const uint8_t address = s_xinput.pending_address;
      s_xinput.pending_address = 0u;
      (void)open_xinput_device(address);
    }
    const esp_err_t error = usb_host_client_handle_events(
        s_xinput.client, pdMS_TO_TICKS(50));
    if (error != ESP_OK && error != ESP_ERR_TIMEOUT)
    {
      ESP_LOGW(TAG, "XInput client event error: %s",
               esp_err_to_name(error));
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

esp_err_t esp32s31_korvo1_xinput_init(void)
{
  if (s_stats.ready)
    return ESP_OK;
  if (xTaskCreate(xinput_task, "usb_xinput", XINPUT_TASK_STACK,
                  xTaskGetCurrentTaskHandle(), XINPUT_TASK_PRIORITY,
                  NULL) != pdPASS)
    return ESP_ERR_NO_MEM;
  if (ulTaskNotifyTake(pdTRUE,
                       pdMS_TO_TICKS(XINPUT_REGISTER_TIMEOUT_MS)) == 0u)
    return ESP_ERR_TIMEOUT;
  return s_register_result;
}

uint16_t esp32s31_korvo1_xinput_mask(void)
{
  uint16_t mask;
  taskENTER_CRITICAL(&s_lock);
  mask = s_stats.joypad_mask;
  taskEXIT_CRITICAL(&s_lock);
  return mask;
}

void esp32s31_korvo1_xinput_get_stats(esp32s31_xinput_stats_t *stats)
{
  if (stats == NULL)
    return;
  taskENTER_CRITICAL(&s_lock);
  *stats = s_stats;
  taskEXIT_CRITICAL(&s_lock);
}
