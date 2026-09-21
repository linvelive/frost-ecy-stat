#pragma once
#include "esp_event_base.h"
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
extern const esp_event_base_t IP_EVENT;
constexpr int32_t IP_EVENT_STA_GOT_IP = 1;
esp_err_t esp_event_handler_register(esp_event_base_t, int32_t,
                                     esp_event_handler_t, void*);
esp_err_t esp_event_handler_unregister(esp_event_base_t, int32_t,
                                       esp_event_handler_t);
