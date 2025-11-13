#include <WiFi.h>
#include "esp_task_wdt.h"

// ==================== CONFIGURAÇÕES GERAIS ====================
#define WDT_TIMEOUT_SEC 10
#define QUEUE_LENGTH 5
#define ALERT_QUEUE_LENGTH 3

// Lista de redes seguras (simuladas)
const char* secureNetworks[] = {
  "MinhaRedeSegura",
  "LabIoT_FIAP",
  "CasaPedro",
  "Oficial_RescueNet",
  "FIAP_WiFi"
};
const int secureCount = sizeof(secureNetworks) / sizeof(secureNetworks[0]);

// Protótipos de tarefas
void WiFiMonitorTask(void *pvParameters);
void ValidatorTask(void *pvParameters);
void LoggerTask(void *pvParameters);

// Handles e filas
TaskHandle_t wifiMonitorHandle = NULL;
TaskHandle_t validatorHandle = NULL;
TaskHandle_t loggerHandle = NULL;

QueueHandle_t ssidQueue;
QueueHandle_t alertQueue;
SemaphoreHandle_t secureListMutex;

// Variáveis globais
bool alertActive = false;

// Simulação de redes detectadas (revezando)
const char* simulatedNetworks[] = {
  "MinhaRedeSegura", "RedeInsegura1", "LabIoT_FIAP", 
  "CaféLivre", "Oficial_RescueNet", "Desconhecida_5G"
};
int simulatedIndex = 0;

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== WiFi Secure Monitor (FreeRTOS + Simulação) ===");

  // Criação das primitivas FreeRTOS
  secureListMutex = xSemaphoreCreateMutex();
  ssidQueue = xQueueCreate(QUEUE_LENGTH, sizeof(char*));
  alertQueue = xQueueCreate(ALERT_QUEUE_LENGTH, sizeof(char*));

  if (!secureListMutex || !ssidQueue || !alertQueue) {
    Serial.println("[FATAL] Falha ao criar primitivas FreeRTOS!");
    while (true) delay(1000);
  }

  // ======= CONFIGURAÇÃO DO WDT (somente se ainda não ativo) =======
  if (esp_task_wdt_status(NULL) == ESP_ERR_NOT_FOUND) {
    const esp_task_wdt_config_t wdtConfig = {
      .timeout_ms = WDT_TIMEOUT_SEC * 1000,
      .idle_core_mask = (1 << 0),
      .trigger_panic = false
    };
    esp_task_wdt_init(&wdtConfig);
  }
  // ===============================================================

  // Criação das tarefas com diferentes prioridades
  xTaskCreatePinnedToCore(WiFiMonitorTask, "WiFiMonitor", 4096, NULL, 3, &wifiMonitorHandle, 1);
  xTaskCreatePinnedToCore(ValidatorTask, "Validator", 4096, NULL, 2, &validatorHandle, 1);
  xTaskCreatePinnedToCore(LoggerTask, "Logger", 4096, NULL, 1, &loggerHandle, 1);
}

// ==================== LOOP (não usado) ====================
void loop() {
  vTaskDelay(portMAX_DELAY);
}

// ==================== TASK 1: MONITOR WiFi ====================
/*
  Simula a varredura de redes Wi-Fi a cada 5 segundos.
  Envia o SSID detectado para a fila "ssidQueue".
*/
void WiFiMonitorTask(void *pvParameters) {
  (void) pvParameters;
  for (;;) {
    // Simula troca de rede
    const char* currentSSID = simulatedNetworks[simulatedIndex];
    simulatedIndex = (simulatedIndex + 1) % (sizeof(simulatedNetworks) / sizeof(simulatedNetworks[0]));

    Serial.printf("\n[SCAN] Rede detectada: %s\n", currentSSID);

    // Envia para fila de validação
    if (xQueueSend(ssidQueue, &currentSSID, pdMS_TO_TICKS(500)) != pdPASS) {
      Serial.println("[WARN] Fila de SSIDs cheia — valor descartado.");
    }

    vTaskDelay(pdMS_TO_TICKS(5000)); // 5 segundos entre simulações
  }
}

// ==================== TASK 2: VALIDADOR ====================
/*
  Recebe SSIDs da fila e verifica se pertencem à lista de redes seguras.
  Caso contrário, envia alerta para a fila "alertQueue".
*/
void ValidatorTask(void *pvParameters) {
  (void) pvParameters;
  for (;;) {
    char* receivedSSID = NULL;

    if (xQueueReceive(ssidQueue, &receivedSSID, portMAX_DELAY) == pdPASS) {
      bool isSecure = false;

      // Protege a lista com semáforo
      if (xSemaphoreTake(secureListMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        for (int i = 0; i < secureCount; i++) {
          if (strcmp(receivedSSID, secureNetworks[i]) == 0) {
            isSecure = true;
            break;
          }
        }
        xSemaphoreGive(secureListMutex);
      }

      if (!isSecure) {
        Serial.printf("[ALERTA] Rede NÃO AUTORIZADA detectada: %s\n", receivedSSID);
        xQueueSend(alertQueue, &receivedSSID, pdMS_TO_TICKS(500));
        alertActive = true;
      } else {
        Serial.printf("[INFO] Rede segura: %s\n", receivedSSID);
        alertActive = false;
      }
    }
  }
}

// ==================== TASK 3: LOGGER / ALERTAS ====================
/*
  Registra alertas e eventos informativos no log.
  Representa a camada de segurança e resposta (ex: LED, buzzer, etc.).
*/
void LoggerTask(void *pvParameters) {
  (void) pvParameters;
  for (;;) {
    char* alertSSID = NULL;

    if (xQueueReceive(alertQueue, &alertSSID, pdMS_TO_TICKS(5000)) == pdPASS) {
      Serial.printf("[LOG] ALERTA REGISTRADO: Conexão a %s (rede não segura)\n", alertSSID);
      Serial.println("[ACTION] Simulando alerta visual/sonoro!");
    } else {
      if (alertActive == false) {
        Serial.println("[LOG] Sistema estável — nenhuma rede não segura detectada.");
      }
    }

    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}
