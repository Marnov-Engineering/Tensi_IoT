//----------- OTA (pola Program_CP_Logger / OTA_Function.ino) ------------//

#include "config.h"

static const uint32_t kOtaFirmwareRetryWindowMs = OTA_FIRMWARE_RETRY_MS;

extern const char *firmwareURL;
extern const char *ota_sta_ssid;
extern const char *ota_sta_pass;
extern volatile bool Update_OTA;
extern volatile bool bOtaDownloading;
extern unsigned long ulOtaFirmwareWindowStartMs;

void vOtaSessionAbort(void);

static void vOtaAbortFirmwareRetryWindow(void) {
  Serial.println(F("[OTA] Pengaman: percobaan unduh/update > 2 menit — batal."));
  bOtaDownloading = false;
  Update_OTA = false;
  ulOtaFirmwareWindowStartMs = 0;
  vOtaSessionAbort();
}

void updateFirmware(void) {
  if (ulOtaFirmwareWindowStartMs == 0) {
    ulOtaFirmwareWindowStartMs = millis();
  }
  if (millis() - ulOtaFirmwareWindowStartMs > kOtaFirmwareRetryWindowMs) {
    vOtaAbortFirmwareRetryWindow();
    return;
  }

  HTTPClient http;
  http.setTimeout(120000);
  http.begin(firmwareURL);
  int httpCode = http.GET();
  if (httpCode <= 0) {
    Serial.printf("[OTA] HTTP failed, error: %s\n", http.errorToString(httpCode).c_str());
    http.end();
    return;
  }
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[OTA] HTTP status bukan 200: %d\n", httpCode);
    http.end();
    return;
  }
  int contentLen = http.getSize();
  Serial.printf("[OTA] Content-Length: %d\n", contentLen);
  if (!Update.begin(contentLen)) {
    Serial.println(F("[OTA] Not enough space to begin OTA"));
    http.end();
    return;
  }
  bOtaDownloading = true;
  WiFiClient *stream = http.getStreamPtr();
  size_t written = Update.writeStream(*stream);
  Serial.printf("[OTA] %d/%d bytes written.\n", (int)written, contentLen);
  if (written != (size_t)contentLen) {
    bOtaDownloading = false;
    Serial.println(F("[OTA] Wrote partial binary. Giving up."));
    http.end();
    return;
  }
  if (!Update.end()) {
    bOtaDownloading = false;
    Serial.println("[OTA] Error from Update.end(): " + String(Update.getError()));
    http.end();
    return;
  }
  bOtaDownloading = false;
  http.end();
  if (Update.isFinished()) {
    ulOtaFirmwareWindowStartMs = 0;
    Serial.println(F("[OTA] Update successfully completed. Rebooting."));
    delay(5000);
    ESP.restart();
  } else {
    Serial.println("[OTA] Error from Update.isFinished(): " + String(Update.getError()));
  }
}
