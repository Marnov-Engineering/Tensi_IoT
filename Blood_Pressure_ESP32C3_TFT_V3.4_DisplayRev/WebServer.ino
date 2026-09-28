/*
  WebServer.ino — hotspot pairing + HTTP form SSID/password
  + HISTORY: WiFi STA + NTP sekali (MEMORI 1)
*/

#include "config.h"
#include "ota_web.h"
#include "cal_web.h"
#include <time.h>

extern char          g_wifiSSID[];
extern char          g_wifiPassword[];
extern WifiSlot      g_wifiSlots[];
extern uint8_t       g_wifiSlotCount;
extern volatile bool flagHubungkanWifi;
extern volatile bool g_wifiStaConnectHoldActive;
extern volatile bool flagHistoryNtpSync;
extern volatile bool g_historyWifiConnected;
extern volatile bool flagPumpingNtpSync;
extern volatile bool g_pumpingWifiConnected;
extern volatile bool g_pumpingInternetOk;
extern volatile bool g_ntpSyncAttempted;
extern volatile bool g_ntpTimeValid;
extern volatile bool Update_OTA;
extern volatile bool bOtaDownloading;
extern unsigned long ulOtaSessionStartMs;
extern unsigned long ulOtaFirmwareWindowStartMs;
extern unsigned long ulOtaNextStaAttemptMs;
extern unsigned long ulOtaLast1sMs;
extern const char* ota_sta_ssid;
extern const char* ota_sta_pass;
void saveWifiCredsToPrefs(const char* ssid, const char* pass);
void wifiSyncLatestGlobals(void);
void updateFirmware(void);
void vOtaSessionAbort(void);
bool tftTryEnqueueLine(const char* line);
bool rtcSetFromTm(const struct tm* ti);
void rtcRefreshDisplayCache(void);
bool rtcIsTimeValid(void);
uint32_t rtcDriftSecondsFromTm(const struct tm* ti);
void pairingCountdownStop(void);
void pairingCountdownStart(void);
void pairingWebRequestStart(void);
void powerLockUpdate(void);
void calSessionShutdown(void);
void deleteWifiSlotFromPrefs(uint8_t slotIndex);
void saveAdcCalToPrefs(float m, float c);
float adcGetCalM(void);
float adcGetCalC(void);
int adcReadBatteryRawAvg(void);
float adcVoltsFromRaw(int raw);

// API helpers (Api.ino)
void apiInitDeviceId(void);
bool apiFetchActiveCommand(void);
bool apiPostMeasurement(int sbp, int dbp, int bpm, int rot, float mapMmHg,
                        uint8_t mode, float batteryV, uint32_t epochTime);
extern volatile bool g_apiSessionValid;
extern char          g_apiSessionStatus[];
extern char          g_apiPatientName[];
extern char          g_apiPatientId[];
extern char          g_apiSessionId[];
extern int           g_apiLastGetHttpCode;

// Pengukuran selesai → kirim ke server (data di-set oleh TFT_Display)
extern volatile bool flagPostMeasurement;
extern volatile bool g_postCancelWindowActive;
extern volatile bool g_postCanceled;
extern volatile bool g_postSucceeded;
extern volatile int  g_measSbp;
extern volatile int  g_measDbp;
extern volatile int  g_measBpm;
extern volatile int  g_measRot;
extern volatile float g_measMap;
extern volatile uint8_t g_measMode;
extern volatile float g_measBatteryV;
extern volatile uint32_t g_measEpochTime;

AsyncWebServer server(80);
static bool    s_pairRoutesAdded = false;
static bool    s_pairApActive    = false;

static String buildWifiListHtml(void) {
  if (g_wifiSlotCount == 0) {
    return F("<div class=\"wifi-saved\"><h3>WiFi tersimpan</h3>"
             "<p class=\"muted\">Belum ada jaringan tersimpan.</p></div>");
  }
  String html = F("<div class=\"wifi-saved\"><h3>WiFi tersimpan</h3><ul>");
  for (int i = (int)g_wifiSlotCount - 1; i >= 0; i--) {
    html += F("<li><span class=\"wifi-name\">");
    html += g_wifiSlots[i].ssid;
    if (i == (int)g_wifiSlotCount - 1) {
      html += F(" <span class=\"muted\">(terbaru)</span>");
    }
    html += F("</span>");
    html += F("<form method=\"POST\" action=\"/wifi/delete\" class=\"wifi-del-form\" "
              "onsubmit=\"return confirm('Hapus WiFi ");
    html += g_wifiSlots[i].ssid;
    html += F("?');\">");
    html += F("<input type=\"hidden\" name=\"slot\" value=\"");
    html += String(i);
    html += F("\">");
    html += F("<button type=\"submit\" class=\"btn-del\">Hapus</button></form></li>");
  }
  html += F("</ul></div>");
  return html;
}

static bool wifiTryStaConnect(const char* ssid, const char* pass, uint32_t timeoutMs) {
  if (!ssid || ssid[0] == '\0') return false;
  WiFi.disconnect(false);
  vTaskDelay(pdMS_TO_TICKS(100));
  WiFi.mode(WIFI_STA);
  // WIFI_FAST_SCAN: berhenti scan saat SSID pertama cocok (tidak scan semua channel)
  WiFi.setScanMethod(WIFI_FAST_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.begin(ssid, pass ? pass : "");
  const uint32_t t0 = millis();
  while ((millis() - t0) < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  return (WiFi.status() == WL_CONNECTED);
}

// Kirim SSID ke layar boot-connect (pumping).
static void wifiBootNotifySsid(const char* ssid) {
  if (!ssid || ssid[0] == '\0') {
    return;
  }
  char bc[TFT_CMD_LINE_MAX];
  snprintf(bc, sizeof(bc), "CMD:BOOT_SSID %s", ssid);
  tftTryEnqueueLine(bc);
}

// Log percobaan koneksi STA.
static void wifiLogConnectTry(const __FlashStringHelper* logTag, int slotNum1Based,
                              const char* ssid, bool retryLatest) {
  if (!logTag) {
    return;
  }
  Serial.print(logTag);
  Serial.print(F(" try slot "));
  Serial.print(slotNum1Based);
  Serial.print(F("/"));
  Serial.print(g_wifiSlotCount);
  Serial.print(F(" SSID="));
  Serial.print(ssid);
  if (retryLatest) {
    Serial.println(F(" (retry latest)"));
  } else {
    Serial.println();
  }
}

// Coba satu slot; kembalikan true jika terhubung.
static bool wifiTrySlotConnect(const WifiSlot* slot, int slotNum1Based,
                               const __FlashStringHelper* logTag, bool sendBootSsid,
                               bool retryLatest) {
  if (!slot || slot->ssid[0] == '\0') {
    return false;
  }
  if (sendBootSsid) {
    wifiBootNotifySsid(slot->ssid);
  }
  wifiLogConnectTry(logTag, slotNum1Based, slot->ssid, retryLatest);
  return wifiTryStaConnect(slot->ssid, slot->pass, WIFI_CONNECT_TIMEOUT_MS);
}

// Pola favorite-first: latest → older → retry latest → older → ...
// Urutan 5 slot: 1 → 2 → 1 → 3 → 1 → 4 → 1 → 5
static bool wifiConnectFavoriteFirst(const __FlashStringHelper* logTag, bool sendBootSsid,
                                     const WifiSlot** outUsedSlot) {
  if (outUsedSlot) {
    *outUsedSlot = nullptr;
  }
  if (g_wifiSlotCount == 0) {
    return false;
  }

  const int latestIdx = (int)g_wifiSlotCount - 1;
  const WifiSlot* latest = &g_wifiSlots[latestIdx];
  const WifiSlot* usedSlot = nullptr;
  bool connected = false;

  if (wifiTrySlotConnect(latest, latestIdx + 1, logTag, sendBootSsid, false)) {
    connected = true;
    usedSlot  = latest;
  }

  for (int idx = latestIdx - 1; idx >= 0 && !connected; idx--) {
    const WifiSlot* slot = &g_wifiSlots[idx];
    if (slot->ssid[0] == '\0') {
      continue;
    }

    if (wifiTrySlotConnect(slot, idx + 1, logTag, sendBootSsid, false)) {
      connected = true;
      usedSlot  = slot;
      break;
    }

    // Retry latest setelah slot lebih lama — kecuali setelah slot tertua
    // (kecuali hanya ada 2 slot total: 1 → 2 → 1)
    if (idx > 0 || latestIdx == 1) {
      if (wifiTrySlotConnect(latest, latestIdx + 1, logTag, sendBootSsid, true)) {
        connected = true;
        usedSlot  = latest;
        break;
      }
    }
  }

  // Jika terhubung ke slot bukan yang terbaru, promosikan ke favorite (latest)
  // agar koneksi berikutnya langsung ke slot ini duluan.
  if (connected && usedSlot && usedSlot != &g_wifiSlots[latestIdx]) {
    char promSsid[WIFI_SSID_MAX_LEN];
    char promPass[WIFI_PASS_MAX_LEN];
    strncpy(promSsid, usedSlot->ssid, sizeof(promSsid) - 1);
    strncpy(promPass, usedSlot->pass, sizeof(promPass) - 1);
    promSsid[sizeof(promSsid) - 1] = '\0';
    promPass[sizeof(promPass) - 1] = '\0';
    saveWifiCredsToPrefs(promSsid, promPass);
    Serial.print(F("[wifi] promosikan ke latest: "));
    Serial.println(promSsid);
    // pointer lama tidak valid setelah shuffle; update ke slot terbaru
    usedSlot = &g_wifiSlots[g_wifiSlotCount - 1];
  }

  if (outUsedSlot) {
    *outUsedSlot = usedSlot;
  }
  return connected;
}

void pairingWebStartImpl(void) {
  if (s_pairApActive) {
    Serial.println(F("[pair] AP sudah aktif (lewati start)"));
    return;
  }

  Serial.println(F("[pair] Coba nyalakan mode Access Point..."));
  WiFi.mode(WIFI_AP);
  WiFi.softAP(PAIRING_AP_SSID);

  vTaskDelay(pdMS_TO_TICKS(500));

  IPAddress apIP = WiFi.softAPIP();
  if (apIP[0] == 0 && apIP[1] == 0 && apIP[2] == 0 && apIP[3] == 0) {
    Serial.println(F("[pair] GAGAL: softAP tidak dapat IP (0.0.0.0)"));
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    return;
  }

  Serial.print(F("[pair] BERHASIL: AP "));
  Serial.print(PAIRING_AP_SSID);
  Serial.print(F(" — IP "));
  Serial.println(apIP);

  if (!s_pairRoutesAdded) {
    // GET "/" — form + daftar SSID tersimpan.
    //
    // index_html cukup besar (~30KB karena base64 PNG logo). Bila pakai
    // FPSTR + replace, ESP32-C3 perlu 2x salinan di heap → load access fault
    // (heap habis / fragmentasi). Solusi: chunked response yang streaming
    // langsung dari PROGMEM, memasukkan daftar WiFi tepat di posisi
    // placeholder "%WIFI_LIST%". Hanya daftar WiFi (kecil) yang di-heap.
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
      static const char kMarker[] = "%WIFI_LIST%";
      const size_t markerLen = sizeof(kMarker) - 1;
      const char* marker = strstr(index_html, kMarker);
      if (!marker) {
        // Placeholder tidak ditemukan → kirim apa adanya dari PROGMEM.
        request->send_P(200, "text/html", index_html);
        return;
      }

      const char* prefixStart = index_html;
      const size_t prefixLen  = (size_t)(marker - index_html);
      const char* suffixStart = marker + markerLen;
      const size_t suffixLen  = strlen(suffixStart);
      const String wifiList   = buildWifiListHtml();

      AsyncWebServerResponse* response = request->beginChunkedResponse(
        "text/html",
        [prefixStart, prefixLen, suffixStart, suffixLen, wifiList]
        (uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
          const size_t listLen  = wifiList.length();
          const size_t totalLen = prefixLen + listLen + suffixLen;
          if (index >= totalLen) {
            return 0;  // selesai
          }
          size_t copied = 0;

          // Bagian 1: prefix HTML (dari PROGMEM)
          if (index < prefixLen && copied < maxLen) {
            const size_t left  = prefixLen - index;
            const size_t take  = (left < (maxLen - copied)) ? left : (maxLen - copied);
            memcpy(buffer + copied, prefixStart + index, take);
            copied += take;
            index  += take;
          }
          // Bagian 2: daftar WiFi (dari heap String)
          if (index >= prefixLen && index < prefixLen + listLen && copied < maxLen) {
            const size_t listOff = index - prefixLen;
            const size_t left    = listLen - listOff;
            const size_t take    = (left < (maxLen - copied)) ? left : (maxLen - copied);
            memcpy(buffer + copied, wifiList.c_str() + listOff, take);
            copied += take;
            index  += take;
          }
          // Bagian 3: suffix HTML (dari PROGMEM)
          if (index >= prefixLen + listLen && copied < maxLen) {
            const size_t sufOff = index - prefixLen - listLen;
            const size_t left   = suffixLen - sufOff;
            const size_t take   = (left < (maxLen - copied)) ? left : (maxLen - copied);
            memcpy(buffer + copied, suffixStart + sufOff, take);
            copied += take;
          }
          return copied;
        });

      request->send(response);
    });

    // POST "/save" — simpan kredensial, mulai koneksi WiFi
    server.on("/save", HTTP_POST, [](AsyncWebServerRequest* request) {
      String ssid = "";
      String pass = "";

      if (request->hasParam("ssid", true)) {
        ssid = request->getParam("ssid", true)->value();
        ssid.trim();
      }
      if (request->hasParam("pass", true)) {
        pass = request->getParam("pass", true)->value();
      }

      if (ssid.length() == 0 || ssid.length() >= WIFI_SSID_MAX_LEN) {
        // SSID kosong atau terlalu panjang → kembali ke form
        request->redirect("/");
        return;
      }

      // Simpan ke flash (FIFO WIFI_MAX_SLOTS) lalu sync SSID terbaru ke global
      saveWifiCredsToPrefs(ssid.c_str(), pass.c_str());
      wifiSyncLatestGlobals();

      // Pegang power-lock selama percobaan STA; hentikan hitung mundur pairing supaya
      // CMD:PAIRING_TIMEOUT tidak mematikan WiFi di tengah koneksi.
      g_wifiStaConnectHoldActive = true;
      powerLockUpdate();
      pairingCountdownStop();

      // Set flag → TaskPairingNet akan memanggil wifiConnectImpl()
      flagHubungkanWifi = true;

      // Kirim halaman konfirmasi (ganti %SSID% dengan nama jaringan).
      // saved_html kecil (~3KB) → FPSTR + replace aman, tidak perlu chunked.
      String html = FPSTR(saved_html);
      html.replace(F("%SSID%"), ssid);
      request->send(200, "text/html", html);
    });

    server.on("/wifi/delete", HTTP_POST, [](AsyncWebServerRequest* request) {
      if (request->hasParam("slot", true)) {
        const int slot = request->getParam("slot", true)->value().toInt();
        if (slot >= 0 && slot < (int)g_wifiSlotCount) {
          deleteWifiSlotFromPrefs((uint8_t)slot);
        }
      }
      request->redirect("/");
    });

    server.on("/update-firmware", HTTP_GET, [](AsyncWebServerRequest* request) {
      request->send_P(200, "text/html", ota_update_page_html);
    });

    server.on("/updateServer", HTTP_GET, [](AsyncWebServerRequest* request) {
      Serial.println(F("[OTA] SEDANG PROSES OTA"));
      Update_OTA = true;
      ulOtaSessionStartMs = millis();
      ulOtaFirmwareWindowStartMs = 0;
      ulOtaNextStaAttemptMs = millis();
      ulOtaLast1sMs = 0;
      // Tampilkan layar "Alat sedang download program" di TFT
      tftTryEnqueueLine("CMD:OTA_DOWNLOADING");
      request->send(200);
    });

    server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest* request) {
      char buf[80];
      snprintf(buf, sizeof(buf), "{\"version\":\"%s\"}", FIRMWARE_VERSION);
      request->send(200, "application/json", buf);
    });

    // Admin kalibrasi baterai — tidak ditautkan dari halaman pairing
    server.on("/Cal", HTTP_GET, [](AsyncWebServerRequest* request) {
      request->send_P(200, "text/html", cal_page_html);
    });

    server.on("/api/cal", HTTP_GET, [](AsyncWebServerRequest* request) {
      const int raw = adcReadBatteryRawAvg();
      const float v = adcVoltsFromRaw(raw);
      char buf[160];
      snprintf(buf, sizeof(buf),
               "{\"raw\":%d,\"v\":%.3f,\"m\":%.10f,\"c\":%.10f}",
               raw, v, adcGetCalM(), adcGetCalC());
      request->send(200, "application/json", buf);
    });

    server.on("/Cal/save", HTTP_POST, [](AsyncWebServerRequest* request) {
      if (!request->hasParam("m", true) || !request->hasParam("c", true)) {
        request->redirect("/Cal");
        return;
      }
      const float m = request->getParam("m", true)->value().toFloat();
      const float c = request->getParam("c", true)->value().toFloat();
      saveAdcCalToPrefs(m, c);
      request->redirect("/Cal?saved=1");
    });

    server.on("/Cal/off", HTTP_POST, [](AsyncWebServerRequest* request) {
      calSessionShutdown();
      request->send(200, "text/plain", "OK");
    });

    // Tombol Matikan Alat di halaman pairing — sama dengan /Cal/off
    server.on("/poweroff", HTTP_POST, [](AsyncWebServerRequest* request) {
      Serial.println(F("[pair] Permintaan matikan alat dari halaman pairing"));
      request->send(200, "text/html",
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<title>Matikan Alat</title>"
        "<style>body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
        "background:#F7F8FC;display:flex;align-items:center;justify-content:center;"
        "min-height:100vh;margin:0;padding:20px}"
        ".card{background:#fff;border-radius:16px;padding:32px;max-width:360px;text-align:center;"
        "box-shadow:0 8px 32px rgba(28,45,108,.12)}"
        "h1{color:#1C2D6C;font-size:1.2rem;margin-bottom:10px}"
        "p{color:#555;font-size:.9rem}</style></head>"
        "<body><div class=\"card\"><h1>Alat dimatikan</h1>"
        "<p>Anda dapat menutup halaman ini.</p></div></body></html>");
      calSessionShutdown();
    });

    s_pairRoutesAdded = true;
  }
  server.begin();
  s_pairApActive = true;
  Serial.println(F("[pair] BERHASIL: HTTP server port 80"));
}

void pairingWebStopImpl(void) {
  if (!s_pairApActive) {
    return;
  }
  server.end();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  s_pairApActive = false;
  Serial.println(F("[pair] AP mode dimatikan"));
}

// Dipanggil dari TaskPairingNet saat flagHubungkanWifi == true.
// Mode AP+STA: coba STA ke SSID baru WIFI_CONNECT_ATTEMPTS x WIFI_CONNECT_TIMEOUT_MS.
// OK    → tampilkan IP ±PAIRING_PREFLIGHT_SUCCESS_HOLD_MS lalu matikan WiFi + alat mati.
// Gagal → tampilkan pesan gagal ±PAIRING_PREFLIGHT_FAIL_HOLD_MS lalu matikan WiFi + alat mati.
void wifiConnectImpl(void) {
  g_wifiStaConnectHoldActive = true;
  powerLockUpdate();

  Serial.print(F("[WiFi] Mencoba terhubung ke: "));
  Serial.println(g_wifiSSID);
  tftTryEnqueueLine("CMD:WIFI_CONNECTING");

  // Aktifkan mode AP+STA agar halaman web masih bisa diakses selama proses koneksi
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(PAIRING_AP_SSID);
  WiFi.setScanMethod(WIFI_FAST_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  bool connected = false;

  for (uint8_t attempt = 1; attempt <= WIFI_CONNECT_ATTEMPTS && !connected; attempt++) {
    char attemptCmd[40];
    snprintf(attemptCmd, sizeof(attemptCmd), "CMD:WIFI_CONN_ATTEMPT %u", (unsigned)attempt);
    tftTryEnqueueLine(attemptCmd);

    Serial.print(F("[WiFi] Percobaan "));
    Serial.print(attempt);
    Serial.print(F("/"));
    Serial.println(WIFI_CONNECT_ATTEMPTS);

    WiFi.disconnect(false);
    vTaskDelay(pdMS_TO_TICKS(100));
    WiFi.begin(g_wifiSSID, g_wifiPassword);

    const uint32_t deadlineMs = WIFI_CONNECT_TIMEOUT_MS;
    uint32_t       t0         = millis();
    int            lastTickSec = -1;

    while ((millis() - t0) < deadlineMs) {
      if (WiFi.status() == WL_CONNECTED) {
        connected = true;
        break;
      }
      uint32_t elapsedMs = millis() - t0;
      int      secElapsed = (int)(elapsedMs / 1000u);
      if (secElapsed != lastTickSec) {
        lastTickSec = secElapsed;
        char cmd[40];
        snprintf(cmd, sizeof(cmd), "CMD:WIFI_CONN_TICK %d", secElapsed);
        tftTryEnqueueLine(cmd);
      }
      vTaskDelay(pdMS_TO_TICKS(200));
    }
  }

  if (connected) {
    char cmd[72];
    String ip = WiFi.localIP().toString();
    snprintf(cmd, sizeof(cmd), "CMD:WIFI_OK %.55s", ip.c_str());
    tftTryEnqueueLine(cmd);
    Serial.print(F("[WiFi] Terhubung! IP: "));
    Serial.println(WiFi.localIP());

    // Tampilkan IP ±10s lalu matikan WiFi → layar hitam (tidur).
    vTaskDelay(pdMS_TO_TICKS(PAIRING_PREFLIGHT_SUCCESS_HOLD_MS));

    pairingWebStopImpl();
    pairingCountdownStop();

    g_wifiStaConnectHoldActive = false;
    powerLockUpdate();

    tftTryEnqueueLine("CMD:PAIRING_PREFLIGHT_SLEEP");
    Serial.println(F("[WiFi] STA baru OK → hotspot dimatikan, siap tidur"));
    return;
  }

  Serial.println(F("[WiFi] Gagal STA baru → tampilkan pesan gagal lalu matikan alat"));
  tftTryEnqueueLine("CMD:WIFI_FAIL");

  vTaskDelay(pdMS_TO_TICKS(PAIRING_PREFLIGHT_FAIL_HOLD_MS));

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  pairingWebStopImpl();
  pairingCountdownStop();

  g_wifiStaConnectHoldActive = false;
  powerLockUpdate();

  tftTryEnqueueLine("CMD:PAIRING_PREFLIGHT_SLEEP");
}

// ============================================================
// HISTORY — WiFi STA + NTP sekali saat layar MEMORI 1
// ============================================================
static bool s_historyNtpDone = false;

void historySessionBegin(void) {
  s_historyNtpDone       = false;
  g_historyWifiConnected = false;
}

void historySessionEnd(void) {
  if (g_historyWifiConnected || WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  g_historyWifiConnected = false;
  s_historyNtpDone       = false;
}

void historyWifiNtpSyncRequest(void) {
  if (s_historyNtpDone) {
    return;
  }
  flagHistoryNtpSync = true;
}

void vOtaTryStaReconnect(void) {
  server.end();
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ota_sta_ssid, ota_sta_pass);
  Serial.println(F("[OTA] WiFi.mode(WIFI_STA) + begin(hotspot)"));
}

void vOtaSessionAbort(void) {
  Update_OTA = false;
  bOtaDownloading = false;
  ulOtaFirmwareWindowStartMs = 0;
  ulOtaSessionStartMs = 0;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println(F("[OTA] Sesi dibatalkan — nyalakan ulang hotspot pairing"));
  pairingWebRequestStart();
}

void historyWifiNtpSyncImpl(void) {
  if (s_historyNtpDone) {
    return;
  }
  s_historyNtpDone = true;

  if (g_wifiSlotCount == 0) {
    Serial.println(F("[history-ntp] tidak ada slot WiFi, lewati"));
    g_historyWifiConnected = false;
    tftTryEnqueueLine("CMD:HISTORY_WIFI 0");
    return;
  }

  bool connected = false;
  const WifiSlot* usedSlot = nullptr;

  connected = wifiConnectFavoriteFirst(F("[history-ntp] "), false, &usedSlot);

  g_historyWifiConnected = connected;

  if (connected && usedSlot) {
    Serial.print(F("[history-ntp] Terhubung ke "));
    Serial.print(usedSlot->ssid);
    Serial.print(F(" IP "));
    Serial.println(WiFi.localIP());

    configTime(NTP_GMT_OFFSET_SEC, NTP_DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    struct tm ti;
    if (getLocalTime(&ti, NTP_FETCH_TIMEOUT_MS)) {
      if (rtcSetFromTm(&ti)) {
        Serial.println(F("[history-ntp] RTC diupdate dari NTP"));
      } else {
        Serial.println(F("[history-ntp] RTC tidak siap, NTP diabaikan"));
      }
    } else {
      Serial.println(F("[history-ntp] NTP timeout"));
    }
    tftTryEnqueueLine("CMD:HISTORY_WIFI 1");
  } else {
    Serial.println(F("[history-ntp] semua slot gagal"));
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    g_historyWifiConnected = false;
    tftTryEnqueueLine("CMD:HISTORY_WIFI 0");
  }
}

// ============================================================
// PUMPING — WiFi STA + NTP sekali saat boot/start test
// ============================================================
static bool     s_pumpingNtpDone               = false;
static uint32_t s_pumpingWifiKeepaliveStartMs  = 0;
volatile bool   g_pumpingWifiKeepAlive         = false;

static bool ntpTmIsValid(const struct tm* ti) {
  if (!ti) {
    return false;
  }
  const int year = ti->tm_year + 1900;
  return (year >= NTP_VALID_YEAR_MIN && year <= NTP_VALID_YEAR_MAX);
}

void pumpingMeasurementResultBegin(void) {
  g_postCanceled           = false;
  g_postSucceeded          = false;
  g_postCancelWindowActive = false;
  flagPostMeasurement      = false;
}

void pumpingCancelPostAndWifiOff(void) {
  if (g_postSucceeded) {
    Serial.println(F("[meas] POST sudah sukses — cancel diabaikan"));
    return;
  }
  g_postCanceled           = true;
  g_postCancelWindowActive = false;
  flagPostMeasurement      = false;
  g_pumpingWifiKeepAlive   = false;
  g_pumpingWifiConnected   = false;
  g_pumpingInternetOk      = false;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  tftTryEnqueueLine("CMD:PUMP_WIFI 0");
  tftTryEnqueueLine("CMD:PUMP_INTERNET 0");
  tftTryEnqueueLine("CMD:POST_CANCELED");
  Serial.println(F("[meas] POST dibatalkan — WiFi dimatikan"));
}

void pumpingSessionBegin(void) {
  pumpingMeasurementResultBegin();
  s_pumpingNtpDone              = false;
  g_pumpingWifiConnected        = false;
  g_pumpingInternetOk           = false;
  g_pumpingWifiKeepAlive        = false;
  s_pumpingWifiKeepaliveStartMs = 0;
  g_ntpSyncAttempted            = false;
  g_ntpTimeValid                = false;
}

void pumpingWifiNtpSyncRequest(void) {
  if (s_pumpingNtpDone) {
    return;
  }
  flagPumpingNtpSync = true;
}

void pumpingWifiNtpSyncImpl(void) {
  if (s_pumpingNtpDone) {
    return;
  }
  s_pumpingNtpDone = true;

  g_pumpingWifiConnected = false;
  g_pumpingInternetOk    = false;
  tftTryEnqueueLine("CMD:PUMP_WIFI 0");
  tftTryEnqueueLine("CMD:PUMP_INTERNET 0");

  if (g_wifiSlotCount == 0) {
    Serial.println(F("[pump-ntp] tidak ada slot WiFi, lewati"));
    g_ntpSyncAttempted = true;
    tftTryEnqueueLine("CMD:CLOCK_TICK");
    // Boot screen: tidak ada WiFi tersimpan → tampilkan Not Connected lalu ke menu
    tftTryEnqueueLine("CMD:BOOT_WIFI_FAIL");
    vTaskDelay(pdMS_TO_TICKS(1500));
    tftTryEnqueueLine("CMD:BOOT_DONE");
    return;
  }

  bool connected = false;
  const WifiSlot* usedSlot = nullptr;

  connected = wifiConnectFavoriteFirst(F("[pump-ntp] "), true, &usedSlot);

  g_pumpingWifiConnected = connected;
  tftTryEnqueueLine(connected ? "CMD:PUMP_WIFI 1" : "CMD:PUMP_WIFI 0");
  // Boot screen: tampilkan hasil koneksi + tegangan baterai
  tftTryEnqueueLine(connected ? "CMD:BOOT_WIFI_OK" : "CMD:BOOT_WIFI_FAIL");

  if (!connected || !usedSlot) {
    Serial.println(F("[pump-ntp] semua slot gagal"));
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    g_ntpSyncAttempted = true;
    tftTryEnqueueLine("CMD:CLOCK_TICK");
    // Boot screen: jeda singkat supaya user membaca status, lalu ke menu
    vTaskDelay(pdMS_TO_TICKS(1500));
    tftTryEnqueueLine("CMD:BOOT_DONE");
    return;
  }

  Serial.print(F("[pump-ntp] Terhubung ke "));
  Serial.print(usedSlot->ssid);
  Serial.print(F(" IP "));
  Serial.println(WiFi.localIP());

  configTime(NTP_GMT_OFFSET_SEC, NTP_DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  struct tm ti;
  if (!getLocalTime(&ti, NTP_FETCH_TIMEOUT_MS) || !ntpTmIsValid(&ti)) {
    Serial.println(F("[pump-ntp] NTP timeout atau waktu tidak valid"));
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    g_ntpSyncAttempted = true;
    tftTryEnqueueLine("CMD:CLOCK_TICK");
    // Boot screen: WiFi OK tapi NTP gagal — jeda lalu ke menu
    vTaskDelay(pdMS_TO_TICKS(1500));
    tftTryEnqueueLine("CMD:BOOT_DONE");
    return;
  }

  g_pumpingInternetOk = true;
  tftTryEnqueueLine("CMD:PUMP_INTERNET 1");

  rtcRefreshDisplayCache();
  g_ntpTimeValid     = true;
  g_ntpSyncAttempted = true;
  Serial.println(F("[pump-ntp] waktu NTP valid, tampilkan jam"));
  tftTryEnqueueLine("CMD:CLOCK_TICK");

  // Boot screen: tampilkan "GET DATA TO SERVER" saat HTTP GET berlangsung
  tftTryEnqueueLine("CMD:BOOT_GETTING");

  // GET active command — ambil sessionId / patientId / patientName untuk dipakai
  // saat pengukuran selesai (dikirim via apiPostMeasurement).
  const bool getOk = apiFetchActiveCommand();
  if (getOk) {
    tftTryEnqueueLine("CMD:BOOT_GET_OK");
  } else {
    char failCmd[32];
    snprintf(failCmd, sizeof(failCmd), "CMD:BOOT_GET_FAIL:%d", g_apiLastGetHttpCode);
    tftTryEnqueueLine(failCmd);
  }
  vTaskDelay(pdMS_TO_TICKS(1500));

  // Boot screen selesai → pindah ke layar MENGUKUR
  tftTryEnqueueLine("CMD:BOOT_DONE");

  // Jangan matikan WiFi — pengukuran belum selesai, biarkan POST langsung kirim
  // tanpa harus connect ulang. WiFi akan dimatikan di pumpingPostMeasurementImpl
  // atau auto-mati via timeout di TaskPairingNet (API_WIFI_KEEPALIVE_MS).
  Serial.println(F("[pump-ntp] WiFi tetap nyala, tunggu pengukuran selesai"));
  s_pumpingWifiKeepaliveStartMs = millis();
  g_pumpingWifiKeepAlive = true;
}

// ============================================================
// MEASUREMENT POST
// Jika WiFi masih nyala dari fase NTP+GET → langsung POST tanpa reconnect.
// Jika WiFi sudah mati (timeout atau belum pernah connect) → reconnect dulu.
// ============================================================
void pumpingPostMeasurementImpl(void) {
  g_pumpingWifiKeepAlive = false;  // batalkan keepalive, setelah ini WiFi pasti mati

  if (g_postCanceled) {
    Serial.println(F("[meas] POST dibatalkan user — skip kirim"));
    g_pumpingWifiConnected = false;
    g_pumpingInternetOk    = false;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    tftTryEnqueueLine("CMD:PUMP_WIFI 0");
    tftTryEnqueueLine("CMD:PUMP_INTERNET 0");
    return;
  }

  if (!g_apiSessionValid) {
    Serial.println(F("[meas] dibatalkan: sesi aktif tidak diketahui"));
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return;
  }

  // Epoch tidak valid → NTP gagal → timestamp akan salah; batalkan POST
  // 1577836800 = 1 Jan 2020 UTC (batas minimal tahun yang masuk akal)
  if (g_measEpochTime < 1577836800UL || !g_ntpTimeValid) {
    Serial.println(F("[meas] dibatalkan: epochTime tidak valid (NTP belum sync)"));
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    tftTryEnqueueLine("CMD:API_POST_FAIL");
    return;
  }

  // --- Pakai WiFi yang sudah ada jika masih konek ---
  bool connected = (WiFi.status() == WL_CONNECTED);
  if (connected) {
    Serial.print(F("[meas] WiFi masih aktif, langsung POST ke "));
    Serial.println(WiFi.localIP());
  } else {
    // WiFi sudah mati (timeout keepalive atau belum pernah ada NTP) → reconnect
    if (g_wifiSlotCount == 0) {
      Serial.println(F("[meas] tidak ada slot WiFi"));
      return;
    }
    connected = wifiConnectFavoriteFirst(F("[meas] "), false, nullptr);
    if (!connected) {
      Serial.println(F("[meas] semua slot WiFi gagal"));
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      return;
    }
    Serial.print(F("[meas] Reconnect OK IP "));
    Serial.println(WiFi.localIP());
  }

  // --- Kirim measurement ---
  int      sbp     = g_measSbp;
  int      dbp     = g_measDbp;
  int      bpm     = g_measBpm;
  int      rot     = g_measRot;
  float    mapF    = g_measMap;
  uint8_t  mode    = g_measMode;
  // Baca baterai sekarang — ADC sudah stabil (pumping selesai + window 5s)
  float    batV    = adcReadBatteryVoltsAvg();
  g_measBatteryV   = batV;
  Serial.print(F("[meas] baterai = "));
  Serial.print(batV, 2);
  Serial.println(F(" V"));
  uint32_t epoch   = g_measEpochTime;

  bool ok = apiPostMeasurement(sbp, dbp, bpm, rot, mapF, mode, batV, epoch);
  Serial.println(ok ? F("[meas] POST sukses") : F("[meas] POST gagal"));
  g_postCancelWindowActive = false;
  if (ok) {
    g_postSucceeded = true;
  }
  // Perbarui ikon Cancel di layar hasil: ✓ jika berhasil, ✗ jika gagal
  tftTryEnqueueLine(ok ? "CMD:API_POST_OK" : "CMD:API_POST_FAIL");
  // --- Matikan WiFi setelah selesai ---
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println(F("[meas] WiFi dimatikan"));
}

// Dipanggil dari TaskPairingNet tiap loop — auto-matikan WiFi jika timeout keepalive
void pumpingWifiKeepaliveCheck(void) {
  if (!g_pumpingWifiKeepAlive) return;
  if ((millis() - s_pumpingWifiKeepaliveStartMs) >= API_WIFI_KEEPALIVE_MS) {
    g_pumpingWifiKeepAlive = false;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println(F("[pump-ntp] keepalive timeout — WiFi dimatikan"));
  }
}
