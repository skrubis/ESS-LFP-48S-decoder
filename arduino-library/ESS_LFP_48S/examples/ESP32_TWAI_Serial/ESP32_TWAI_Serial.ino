#include <Arduino.h>
#include "ESS_LFP_48S.h"
#include "driver/twai.h"

// Adjust pins for your transceiver wiring
#ifndef TWAI_RX_PIN
#define TWAI_RX_PIN 4
#endif
#ifndef TWAI_TX_PIN
#define TWAI_TX_PIN 5
#endif

ESS_LFP_48S bms;

void setupTWAI() {
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TWAI_TX_PIN, TWAI_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
    Serial.println("TWAI driver install failed");
    while (1) delay(1000);
  }
  if (twai_start() != ESP_OK) {
    Serial.println("TWAI start failed");
    while (1) delay(1000);
  }
  Serial.println("TWAI started @250k, listening extended frames");
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  Serial.println("ESS_LFP_48S Serial Monitor");
  setupTWAI();
}

unsigned long lastPrint = 0;

void loop() {
  twai_message_t msg;
  while (twai_receive(&msg, pdMS_TO_TICKS(1)) == ESP_OK) {
    if (!msg.rtr && msg.extd) {
      bms.updateFromFrame(msg.identifier, msg.data_length_code, msg.data);
    }
  }

  if (millis() - lastPrint > 1000) {
    lastPrint = millis();
    Serial.printf("Pack: %.1f V, Delta: %.3f V, Max: %.3f, Min: %.3f, AvgT: %.2f C, MinT: %.2f C\n",
                  bms.packVoltage(), bms.cellDeltaV(), bms.maxCellV(), bms.minCellV(), bms.avgTempC(), bms.minTempC());
    Serial.print("Cells: ");
    for (int i = 0; i < 48; ++i) {
      float v = bms.getCellV(i);
      if (!isnan(v)) Serial.printf("%.3f ", v);
      else Serial.print("-- ");
    }
    Serial.println();
    Serial.print("Temps: ");
    for (int i = 0; i < 24; ++i) {
      float t = bms.getTempC(i);
      if (!isnan(t)) Serial.printf("%.2f ", t);
      else Serial.print("-- ");
    }
    Serial.println();
  }
}
