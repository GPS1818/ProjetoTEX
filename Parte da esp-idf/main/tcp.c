
#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "protocol_examples_common.h"

static const char *TAG = "MQTT_ESP32";

// Cliente MQTT global
static esp_mqtt_client_handle_t mqtt_client = NULL;

// Indica se o broker esta conectado
static atomic_bool mqtt_conectado = false;

// Topico que o Node-RED ira receber
#define MQTT_TOPICO "/topic/GPS0"


// ============================================
// EVENTOS MQTT
// ============================================
static void mqtt_event_handler(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
        case MQTT_EVENT_CONNECTED:
            atomic_store(&mqtt_conectado, true);

            ESP_LOGI(TAG, "MQTT conectado ao broker!");
            break;

        case MQTT_EVENT_DISCONNECTED:
            atomic_store(&mqtt_conectado, false);

            ESP_LOGW(TAG, "MQTT desconectado!");
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(
                TAG,
                "Mensagem confirmada pelo broker, ID=%d",
                event->msg_id
            );
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "Erro de comunicacao MQTT");
            break;

        default:
            break;
    }
}


// ============================================
// INICIAR WIFI E MQTT
// ============================================
void mqtt_start(void)
{
    ESP_LOGI(TAG, "Iniciando Wi-Fi...");

    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Usa Wi-Fi configurado pelo menuconfig
    ESP_ERROR_CHECK(example_connect());

    ESP_LOGI(TAG, "Wi-Fi conectado!");

    // Broker configurado no menuconfig
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_BROKER_URL
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);

    if (mqtt_client == NULL)
    {
        ESP_LOGE(TAG, "Falha ao criar cliente MQTT");
        return;
    }

    ESP_ERROR_CHECK(
        esp_mqtt_client_register_event(
            mqtt_client,
            ESP_EVENT_ANY_ID,
            mqtt_event_handler,
            NULL
        )
    );

    ESP_ERROR_CHECK(
        esp_mqtt_client_start(mqtt_client)
    );

    ESP_LOGI(TAG, "Cliente MQTT iniciado");
}


// ============================================
// ENVIAR LEITURA AO APERTAR O BOTAO
// ============================================
bool mqtt_enviar_leitura(float pressao, float temperatura)
{
    if (mqtt_client == NULL ||
        !atomic_load(&mqtt_conectado))
    {
        ESP_LOGW(
            TAG,
            "MQTT nao conectado. Envio cancelado."
        );

        return false;
    }

    // Montar mensagem JSON
    char mensagem[128];

    int tamanho = snprintf(
        mensagem,
        sizeof(mensagem),
        "{\"pressao\":%.1f,\"temperatura\":%.1f}",
        pressao,
        temperatura
    );

    if (tamanho < 0 || tamanho >= (int)sizeof(mensagem))
    {
        ESP_LOGE(TAG, "Erro ao montar JSON");
        return false;
    }

    // Colocar mensagem na fila de envio MQTT
    int msg_id = esp_mqtt_client_enqueue(
        mqtt_client,
        MQTT_TOPICO,
        mensagem,
        0,
        1,       // QoS 1
        0,       // Nao reter mensagem
        false
    );

    if (msg_id < 0)
    {
        ESP_LOGE(
            TAG,
            "Falha ao colocar mensagem na fila MQTT"
        );

        return false;
    }

    ESP_LOGI(
        TAG,
        "Mensagem MQTT enfileirada: %s",
        mensagem
    );

    return true;
}
