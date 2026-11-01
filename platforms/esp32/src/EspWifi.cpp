#include "brick/platform/esp32/network/EspWifi.h"

#include "brick/platform/esp32/EspStatus.h"
#include "brick/platform/esp32/network/EspNetworkStack.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

namespace brick::platform::esp32
{

namespace
{
    constexpr char kTag[] = "brick_wifi";

    /// The Wi-Fi driver keeps its RF calibration in NVS.
    Status start_nvs()
    {
        esp_err_t error = nvs_flash_init();
        if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            // Full, or written by another ESP-IDF version: start it empty, as the ESP-IDF examples do.
            ESP_LOGW(kTag, "Erasing the NVS partition: %s", esp_err_to_name(error));
            error = nvs_flash_erase();
            if (error == ESP_OK)
                error = nvs_flash_init();
        }
        return to_status(error);
    }
}  // namespace

EspWifi::~EspWifi()
{
    if (!started_)
        return;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_events_));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, ip_events_));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_stop());
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_deinit());
    esp_netif_destroy_default_wifi(station_.netif());
    esp_netif_destroy_default_wifi(access_point_.netif());
}

Status EspWifi::begin()
{
    if (started_)
        return Status::ok;
    if (const Status status = start_nvs(); !is_ok(status))
        return status;
    if (const Status status = start_network_stack(); !is_ok(status))
        return status;
    if (const Status status = create_netifs_(); !is_ok(status))
        return status;
    if (const Status status = start_driver_(); !is_ok(status))
        return status;
    started_ = true;
    ESP_LOGI(kTag, "Wi-Fi radio started");
    return Status::ok;
}

Status EspWifi::create_netifs_()
{
    if (station_.netif() == nullptr)
        station_.attach(esp_netif_create_default_wifi_sta());
    if (access_point_.netif() == nullptr)
        access_point_.attach(esp_netif_create_default_wifi_ap());
    return station_.netif() != nullptr && access_point_.netif() != nullptr ? Status::ok : Status::no_memory;
}

Status EspWifi::start_driver_()
{
    const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (const esp_err_t error = esp_wifi_init(&init); error != ESP_OK)
        return to_status(error);
    // The application owns the credentials: nothing is written to flash.
    if (const esp_err_t error = esp_wifi_set_storage(WIFI_STORAGE_RAM); error != ESP_OK)
        return to_status(error);
    if (const esp_err_t error =
            esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event_, this, &wifi_events_);
        error != ESP_OK)
        return to_status(error);
    if (const esp_err_t error =
            esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, on_event_, this, &ip_events_);
        error != ESP_OK)
        return to_status(error);
    if (const esp_err_t error = esp_wifi_set_mode(WIFI_MODE_STA); error != ESP_OK)
        return to_status(error);
    apply_settings_();
    return to_status(esp_wifi_start());
}

void EspWifi::apply_settings_()
{
    // Optional: the radio works without them, so a failure is only logged.
    if (config_.country_code != nullptr)
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_country_code(config_.country_code, true));
    if (config_.hostname != nullptr)
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_set_hostname(station_.netif(), config_.hostname));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_ps(config_.power_save ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE));
}

Status EspWifi::enable_access_point(bool enabled)
{
    if (!started_)
        return Status::not_ready;
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (const esp_err_t error = esp_wifi_get_mode(&mode); error != ESP_OK)
        return to_status(error);
    const wifi_mode_t wanted = enabled ? WIFI_MODE_APSTA : WIFI_MODE_STA;
    return mode == wanted ? Status::ok : to_status(esp_wifi_set_mode(wanted));
}

void EspWifi::on_event_(void* context, esp_event_base_t base, std::int32_t id, void* data)
{
    EspWifi& radio = *static_cast<EspWifi*>(context);
    if (base == WIFI_EVENT)
    {
        radio.station_.on_wifi_event(id, data);
        radio.access_point_.on_wifi_event(id);
    }
    else if (base == IP_EVENT)
    {
        radio.station_.on_ip_event(id);
    }
}

}  // namespace brick::platform::esp32
