/*
  SKETCH UTAMA — Tensimeter IoT + TFT ESP32-C3

  Isi folder (satu project Arduino):
  1. File .ino ini      — setup, task FreeRTOS, variabel bersama
  2. config.h           — pin & konstanta
  3. ADC.ino            — baca tegangan baterai
  4. RTC.ino            — DS3231 tanggal & jam
  5. TFT_Display.ino    — gambar layar
  6. BP_Parser.ino      — rangkai baris UART & decode HEX
  7. WebServer.ino      — hotspot pairing + HTTP + trigger OTA
  8. web.h              — HTML halaman pairing (PROGMEM / index_html)
  9. OTA_Function.ino   — unduh & flash firmware dari Firebase
 10. ota_web.h           — HTML halaman update firmware

  Kabel tensimeter: TX alat → GPIO3 (BP_RX), RX alat ← GPIO21 (BP_TX), GND sama.
  Di Arduino IDE: "USB CDC On Boot: Enabled" supaya Serial Monitor jalan.

  Kenapa pakai FreeRTOS (beberapa task)?
  - Task UART baca tensimeter terus (jangan tertunda).
  - Task TFT gambar lewat SPI (kalau digabung loop(), RX bisa kehilangan data).
  - loop() kosong — pola sama Referensi/Program_Kecepatan_Arah_Angin_V1.3_AddListFile:
    setup() buat xTaskCreatePinnedToCore, loop() {}, badan task di bawah loop().

  Boot: cuplik Serial1 → logo → cek teks "start test" / "bpt mode" / "mem mode"
       atau tunggu sampai BOOT_MEASUREMENT_SCAN_MS.

  Semua #include <...> / "..." diletakkan di file ini saja (seperti referensi ToaPP).
*/

#include "config.h"
#include "web.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "logo_mamacare.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <climits>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBoldOblique12pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/Org_01.h>
#include "Petme8x8.h"
#include <Preferences.h>

#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>

void pairingWebStartImpl(void);
void pairingWebStopImpl(void);
void wifiConnectImpl(void);
void historySessionBegin(void);
void historySessionEnd(void);
void historyWifiNtpSyncRequest(void);
void historyWifiNtpSyncImpl(void);
void pumpingSessionBegin(void);
void pumpingWifiNtpSyncRequest(void);
void pumpingWifiNtpSyncImpl(void);
void pumpingPostMeasurementImpl(void);
void pumpingCancelPostAndWifiOff(void);
void pumpingMeasurementResultBegin(void);
void pumpingWifiKeepaliveCheck(void);
void apiInitDeviceId(void);
void pumpingPostMeasurementRequest(int sbp, int dbp, int bpm, int rot,
                                   float mapMmHg, uint8_t mode);
bool rtcInit(void);
bool rtcIsOk(void);
bool rtcIsTimeValid(void);
uint32_t rtcDriftSecondsFromTm(const struct tm* ti);
int rtcCurrentSecond(void);
void rtcRefreshDisplayCache(void);
const char* rtcGetDisplayDate(void);
const char* rtcGetDisplayTime(void);
bool rtcSetFromTm(const struct tm* ti);
void onSessionIdChanged(const char* newSessionId);
void loadDeviceNameFromPrefs(void);
void saveDeviceNameToPrefs(const char* name);
extern char g_apiDeviceName[DEVICE_NAME_MAX_LEN];
extern float g_apiSavedMap;
extern bool  g_apiSavedMapValid;
extern int   g_apiDbpMiringKiri;
extern bool  g_apiDbpMiringKiriValid;
float adcReadBatteryVoltsAvg(void);
void adcCalPrefsBegin(void);
void loadAdcCalFromPrefs(void);

void TaskRtc(void* pvParameters);
void updateFirmware(void);
void vOtaTryStaReconnect(void);
void vOtaSessionAbort(void);

//========= OTA (pola Program_CP_Logger) =========//
volatile bool Update_OTA = false;
volatile bool bOtaDownloading = false;
unsigned long ulOtaSessionStartMs = 0;
unsigned long ulOtaFirmwareWindowStartMs = 0;
unsigned long ulOtaNextStaAttemptMs = 0;
unsigned long ulOtaLast1sMs = 0;
const char* firmwareURL = "https://firebasestorage.googleapis.com/v0/b/otastorage-503b9.appspot.com/o/Tensi_IoT.bin?alt=media";
const char* ota_sta_ssid = OTA_STA_SSID;
const char* ota_sta_pass = OTA_STA_PASS;

//========= Flag NTP: set oleh pumpingWifiNtpSyncImpl setelah sync selesai =========//
// g_ntpSyncAttempted : true setelah NTP dicoba (berhasil ATAU gagal)
//   → TaskRtc mulai kirim CMD:CLOCK_TICK (tampilkan waktu / placeholder)
// g_ntpTimeValid     : true hanya jika NTP berhasil dan waktu sudah valid
//   → epochTime POST dianggap sah; pumpingPostMeasurementImpl cek ini
volatile bool g_ntpSyncAttempted = false;
volatile bool g_ntpTimeValid     = false;

//========= Kredensial WiFi — hingga 5 slot (0=lama, count-1=terbaru) =========//
typedef struct {
  char ssid[WIFI_SSID_MAX_LEN];
  char pass[WIFI_PASS_MAX_LEN];
} WifiSlot;

WifiSlot   g_wifiSlots[WIFI_MAX_SLOTS];
uint8_t    g_wifiSlotCount = 0;

char g_wifiSSID[WIFI_SSID_MAX_LEN];
char g_wifiPassword[WIFI_PASS_MAX_LEN];

static Preferences s_wifiPrefs;
static bool        s_wifiPrefsOk = false;

static void wifiSlotClear(WifiSlot* s) {
  if (!s) return;
  s->ssid[0] = '\0';
  s->pass[0] = '\0';
}

static void wifiSlotCopy(WifiSlot* dst, const char* ssid, const char* pass) {
  if (!dst || !ssid || !pass) return;
  strncpy(dst->ssid, ssid, WIFI_SSID_MAX_LEN - 1);
  strncpy(dst->pass, pass, WIFI_PASS_MAX_LEN - 1);
  dst->ssid[WIFI_SSID_MAX_LEN - 1] = '\0';
  dst->pass[WIFI_PASS_MAX_LEN - 1] = '\0';
}

static void prefsWifiBegin(void) {
  s_wifiPrefsOk = s_wifiPrefs.begin(PREFS_NAMESPACE_WIFI, false);
  if (!s_wifiPrefsOk) {
    Serial.println(F("[prefs-wifi] namespace wifi gagal dibuka"));
  }
}

static void wifiRemoveLegacyPrefKeys(void) {
  if (!s_wifiPrefsOk) return;
  for (uint8_t i = WIFI_MAX_SLOTS; i < 5u; i++) {
    char keySsid[8];
    char keyPass[8];
    snprintf(keySsid, sizeof(keySsid), "s%u", (unsigned)i);
    snprintf(keyPass, sizeof(keyPass), "p%u", (unsigned)i);
    s_wifiPrefs.remove(keySsid);
    s_wifiPrefs.remove(keyPass);
  }
}

static void persistWifiSlotsToPrefs(void) {
  if (!s_wifiPrefsOk) return;
  s_wifiPrefs.putUChar(PREFS_KEY_WIFI_COUNT, g_wifiSlotCount);
  for (uint8_t i = 0; i < WIFI_MAX_SLOTS; i++) {
    char keySsid[8];
    char keyPass[8];
    snprintf(keySsid, sizeof(keySsid), "s%u", (unsigned)i);
    snprintf(keyPass, sizeof(keyPass), "p%u", (unsigned)i);
    if (i < g_wifiSlotCount && g_wifiSlots[i].ssid[0]) {
      s_wifiPrefs.putString(keySsid, g_wifiSlots[i].ssid);
      s_wifiPrefs.putString(keyPass, g_wifiSlots[i].pass);
    } else {
      s_wifiPrefs.remove(keySsid);
      s_wifiPrefs.remove(keyPass);
    }
  }
  wifiRemoveLegacyPrefKeys();
  s_wifiPrefs.putString(PREFS_KEY_WIFI_SSID, g_wifiSlotCount ? g_wifiSlots[g_wifiSlotCount - 1].ssid : "");
  s_wifiPrefs.putString(PREFS_KEY_WIFI_PASS, g_wifiSlotCount ? g_wifiSlots[g_wifiSlotCount - 1].pass : "");
}

void wifiSyncLatestGlobals(void) {
  g_wifiSSID[0]     = '\0';
  g_wifiPassword[0] = '\0';
  if (g_wifiSlotCount == 0) return;
  const WifiSlot* latest = &g_wifiSlots[g_wifiSlotCount - 1];
  strncpy(g_wifiSSID, latest->ssid, WIFI_SSID_MAX_LEN - 1);
  strncpy(g_wifiPassword, latest->pass, WIFI_PASS_MAX_LEN - 1);
  g_wifiSSID[WIFI_SSID_MAX_LEN - 1]         = '\0';
  g_wifiPassword[WIFI_PASS_MAX_LEN - 1] = '\0';
}

void wifiLogAllSlots(void) {
  Serial.print(F("[prefs-wifi] slot count="));
  Serial.println(g_wifiSlotCount);
  for (uint8_t i = 0; i < g_wifiSlotCount; i++) {
    Serial.print(F("  ["));
    Serial.print(i);
    Serial.print(F("] "));
    Serial.print(g_wifiSlots[i].ssid);
    if (i == g_wifiSlotCount - 1) {
      Serial.println(F(" (terbaru)"));
    } else {
      Serial.println();
    }
  }
  if (g_wifiSlotCount == 0) {
    Serial.println(F("  (kosong)"));
  }
}

static void migrateLegacyWifiPrefsIfNeeded(void) {
  if (!s_wifiPrefsOk || g_wifiSlotCount > 0) return;
  String legacySsid = s_wifiPrefs.getString(PREFS_KEY_WIFI_SSID, "");
  if (legacySsid.length() == 0) return;
  String legacyPass = s_wifiPrefs.getString(PREFS_KEY_WIFI_PASS, "");
  wifiSlotCopy(&g_wifiSlots[0], legacySsid.c_str(), legacyPass.c_str());
  g_wifiSlotCount = 1;
  persistWifiSlotsToPrefs();
  Serial.println(F("[prefs-wifi] migrasi format lama -> slot 0"));
}

void loadWifiSlotsFromPrefs(void) {
  g_wifiSlotCount = 0;
  for (uint8_t i = 0; i < WIFI_MAX_SLOTS; i++) {
    wifiSlotClear(&g_wifiSlots[i]);
  }
  if (!s_wifiPrefsOk) {
    wifiSyncLatestGlobals();
    return;
  }

  const uint8_t storedCount = s_wifiPrefs.getUChar(PREFS_KEY_WIFI_COUNT, 0);
  bool needsTrim = false;

  if (storedCount > 0) {
    if (storedCount > WIFI_MAX_SLOTS) {
      needsTrim = true;
      WifiSlot tmp[5];
      uint8_t  tmpCount = 0;
      const uint8_t readMax = (storedCount > 5u) ? 5u : storedCount;
      for (uint8_t i = 0; i < readMax; i++) {
        char keySsid[8];
        char keyPass[8];
        snprintf(keySsid, sizeof(keySsid), "s%u", (unsigned)i);
        snprintf(keyPass, sizeof(keyPass), "p%u", (unsigned)i);
        String ssid = s_wifiPrefs.getString(keySsid, "");
        String pass = s_wifiPrefs.getString(keyPass, "");
        if (ssid.length() == 0) continue;
        wifiSlotCopy(&tmp[tmpCount], ssid.c_str(), pass.c_str());
        tmpCount++;
      }
      const uint8_t startIdx =
        (tmpCount > WIFI_MAX_SLOTS) ? (uint8_t)(tmpCount - WIFI_MAX_SLOTS) : 0;
      for (uint8_t i = startIdx; i < tmpCount; i++) {
        g_wifiSlots[g_wifiSlotCount] = tmp[i];
        g_wifiSlotCount++;
      }
    } else {
      for (uint8_t i = 0; i < storedCount; i++) {
        char keySsid[8];
        char keyPass[8];
        snprintf(keySsid, sizeof(keySsid), "s%u", (unsigned)i);
        snprintf(keyPass, sizeof(keyPass), "p%u", (unsigned)i);
        String ssid = s_wifiPrefs.getString(keySsid, "");
        String pass = s_wifiPrefs.getString(keyPass, "");
        if (ssid.length() == 0) continue;
        wifiSlotCopy(&g_wifiSlots[g_wifiSlotCount], ssid.c_str(), pass.c_str());
        g_wifiSlotCount++;
      }
    }
  }

  migrateLegacyWifiPrefsIfNeeded();

  if (needsTrim) {
    persistWifiSlotsToPrefs();
    Serial.print(F("[prefs-wifi] migrasi: disimpan "));
    Serial.print(WIFI_MAX_SLOTS);
    Serial.println(F(" slot terbaru"));
  }

  wifiSyncLatestGlobals();
  wifiLogAllSlots();
}

void loadWifiCredsFromPrefs(void) {
  loadWifiSlotsFromPrefs();
}

void addWifiSlotToPrefs(const char* ssid, const char* pass) {
  if (!s_wifiPrefsOk || !ssid || !pass || ssid[0] == '\0') return;

  int dupIdx = -1;
  for (uint8_t i = 0; i < g_wifiSlotCount; i++) {
    if (strcmp(g_wifiSlots[i].ssid, ssid) == 0) {
      dupIdx = (int)i;
      break;
    }
  }

  WifiSlot entry;
  wifiSlotCopy(&entry, ssid, pass);

  if (dupIdx >= 0) {
    for (uint8_t i = (uint8_t)dupIdx; i + 1 < g_wifiSlotCount; i++) {
      g_wifiSlots[i] = g_wifiSlots[i + 1];
    }
    g_wifiSlotCount--;
    Serial.print(F("[prefs-wifi] SSID duplikat dipindah ke terbaru: "));
    Serial.println(ssid);
  }

  if (g_wifiSlotCount >= WIFI_MAX_SLOTS) {
    for (uint8_t i = 0; i + 1 < WIFI_MAX_SLOTS; i++) {
      g_wifiSlots[i] = g_wifiSlots[i + 1];
    }
    g_wifiSlotCount = WIFI_MAX_SLOTS - 1;
    Serial.println(F("[prefs-wifi] FIFO: slot tertua dihapus"));
  }

  g_wifiSlots[g_wifiSlotCount] = entry;
  g_wifiSlotCount++;

  persistWifiSlotsToPrefs();
  wifiSyncLatestGlobals();
  Serial.print(F("[prefs-wifi] Disimpan SSID: "));
  Serial.println(ssid);
  wifiLogAllSlots();
}

void saveWifiCredsToPrefs(const char* ssid, const char* pass) {
  addWifiSlotToPrefs(ssid, pass);
}

void deleteWifiSlotFromPrefs(uint8_t slotIndex) {
  if (!s_wifiPrefsOk || slotIndex >= g_wifiSlotCount) return;

  Serial.print(F("[prefs-wifi] Hapus slot "));
  Serial.print(slotIndex);
  Serial.print(F(": "));
  Serial.println(g_wifiSlots[slotIndex].ssid);

  for (uint8_t i = slotIndex; i + 1 < g_wifiSlotCount; i++) {
    g_wifiSlots[i] = g_wifiSlots[i + 1];
  }
  g_wifiSlotCount--;
  wifiSlotClear(&g_wifiSlots[g_wifiSlotCount]);

  persistWifiSlotsToPrefs();
  wifiSyncLatestGlobals();
  wifiLogAllSlots();
}

//========= Flag untuk TaskPairingNet (start / stop hotspot + koneksi WiFi) =========//
// Task pairing cek flag ini setiap 200 ms — lebih mudah dibaca daripada Queue.
volatile bool flagMulaiPairing = false;
volatile bool flagHentikanPairing = false;
volatile bool flagHubungkanWifi = false;

// HISTORY: WiFi STA + NTP sekali saat MEMORI 1 (background, tanpa kunci daya)
volatile bool flagHistoryNtpSync = false;
volatile bool g_historyWifiConnected = false;

// PUMPING: WiFi + NTP sekali saat boot/start test (background)
volatile bool flagPumpingNtpSync = false;
volatile bool g_pumpingWifiConnected = false;
volatile bool g_pumpingInternetOk = false;

// POST measurement (background, dipicu setelah tftShowResultNew)
volatile bool      flagPostMeasurement = false;
volatile bool      g_postCancelWindowActive = false;
volatile uint32_t  g_postCancelWindowEndMs  = 0;
volatile bool      g_postCanceled           = false;
volatile bool      g_postSucceeded          = false;
volatile int       g_measSbp           = 0;
volatile int       g_measDbp           = 0;
volatile int       g_measBpm           = 0;
volatile int       g_measRot           = 0;
volatile float     g_measMap           = 0.0f;
volatile uint8_t   g_measMode          = 0;
volatile float     g_measBatteryV      = 0.0f;
volatile uint32_t  g_measEpochTime     = 0;

// Dipanggil dari TFT_Display setelah tftShowResultNew (hasil pengukuran final)
void pumpingPostMeasurementRequest(int sbp, int dbp, int bpm, int rot,
                                   float mapMmHg, uint8_t mode) {
  if (sbp <= 0 || dbp <= 0 || bpm <= 0) {
    Serial.println(F("[meas] data tidak valid, skip kirim"));
    return;
  }
  if (g_postCanceled || g_postSucceeded) {
    Serial.println(F("[meas] POST sudah dibatalkan/terkirim, lewati jadwal ulang"));
    return;
  }

  // MAP yang dikirim: -1 = null. MAP hanya diukur saat duduk, lalu dipakai
  // ulang untuk miring_kiri & terlentang.
  float mapToSend;

  // Posisi duduk (0): pakai MAP hasil ukur saat ini.
  // ROT belum ada nilainya → kirim null.
  if (mode == 0) {
    mapToSend = mapMmHg;
    rot = INT32_MIN;  // null di JSON
  } else {
    // miring_kiri & terlentang: map mengikuti nilai dari GET web.
    mapToSend = g_apiSavedMapValid ? g_apiSavedMap : -1.0f;
  }

  // Posisi miring_kiri (1): ROT untuk posisi ini dikirim null.
  if (mode == 1) {
    rot = INT32_MIN;  // null di JSON
  }

  // Posisi terlentang (2): hitung ROT = DBP_terlentang - DBP_miring_kiri.
  // Nilai DBP miring_kiri didapat dari GET web.
  if (mode == 2) {
    if (g_apiDbpMiringKiriValid) {
      rot = dbp - g_apiDbpMiringKiri;
      Serial.print(F("[meas] ROT = "));
      Serial.print(dbp);
      Serial.print(F(" - "));
      Serial.print(g_apiDbpMiringKiri);
      Serial.print(F(" = "));
      Serial.println(rot);
    } else {
      Serial.println(F("[meas] ROT: dbpMiringKiri belum tersimpan, kirim null"));
      rot = INT32_MIN;  // null di JSON
    }
  }

  g_measSbp         = sbp;
  g_measDbp         = dbp;
  g_measBpm         = bpm;
  g_measRot         = rot;
  g_measMap         = mapToSend;
  g_measMode        = mode;
  // Pembacaan baterai ditunda sampai pumpingPostMeasurementImpl() — saat ADC
  // sudah stabil (jauh dari aktivitas pompa).
  g_measBatteryV    = 0.0f;
  // Epoch dari system time (sudah di-sync NTP saat boot pumping).
  g_measEpochTime   = (uint32_t)time(NULL);
  g_postCancelWindowEndMs   = millis() + POST_CANCEL_WINDOW_MS;
  g_postCancelWindowActive  = true;
  Serial.println(F("[meas] cancel window 5s dimulai"));
}

void pairingWebRequestStart(void) {
  flagMulaiPairing = true;
}

void pairingWebRequestStop(void) {
  flagHentikanPairing = true;
}

//========= Objek TFT (SPI) =========//
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

//========= Mode pengukuran, menu memori, flag layar =========//
volatile uint8_t selectedMode = 0;  // 0=Duduk  1=Tidur samping kiri  2=Terlentang
volatile bool isPumping = false;
volatile bool showingResult = false;  // true = layar hasil (tombol mode dikunci)

volatile uint8_t memMenuSel = MEM_MENU_MATIKAN;
volatile bool showingMemPairingMenu = false;

volatile bool g_historyWaitScreen = false;
volatile bool g_pairingCountdownActive = false;
volatile uint32_t g_pairingCountdownEndMs = 0;

// HIGH-logic power-lock juga saat mencoba STA setelah /save (jangan matikan MOSFET lebih dulu)
volatile bool g_wifiStaConnectHoldActive = false;

//========= Kunci daya (POWER_LOCK_PIN) =========//
// HIGH = pegang nyala (pairing, history tunggu, menu tertentu)
void powerLockUpdate(void) {
  bool hitungMundurPairing = g_pairingCountdownActive;
  bool tungguTombolHistory = g_historyWaitScreen;
  bool menuPilihPairing = (memMenuSel == MEM_MENU_PAIRING);
  bool menuPilihHistory = (memMenuSel == MEM_MENU_HISTORY);
  bool menuPerluKunci =
    showingMemPairingMenu && (menuPilihPairing || menuPilihHistory);

  bool tahanDaya =
    hitungMundurPairing || tungguTombolHistory || menuPerluKunci ||
    g_wifiStaConnectHoldActive || Update_OTA || bOtaDownloading;

  if (tahanDaya) {
    digitalWrite(POWER_LOCK_PIN, HIGH);
  } else {
    digitalWrite(POWER_LOCK_PIN, LOW);
  }
}

void pairingCountdownStart(void) {
  g_pairingCountdownEndMs = millis() + PAIRING_COUNTDOWN_MS;
  g_pairingCountdownActive = true;
  powerLockUpdate();
}

void pairingCountdownStop(void) {
  g_pairingCountdownActive = false;
  powerLockUpdate();
}

void calSessionShutdown(void) {
  pairingCountdownStop();
  digitalWrite(POWER_LOCK_PIN, LOW);
  Serial.println(F("[cal] matikan alat — power lock OFF"));
}

static Preferences s_selModePrefs;
static bool s_selModePrefsOk = false;

static void prefsSelModeBegin(void) {
  s_selModePrefsOk = s_selModePrefs.begin(PREFS_NAMESPACE_MODE, false);
  if (!s_selModePrefsOk) {
    Serial.println(F("[prefs] namespace mode gagal dibuka"));
  }
}

//========= deviceName — judul menu MODE (dari GET /devices/{id}) =========//
void loadDeviceNameFromPrefs(void) {
  strncpy(g_apiDeviceName, DEVICE_NAME_DEFAULT, DEVICE_NAME_MAX_LEN - 1);
  g_apiDeviceName[DEVICE_NAME_MAX_LEN - 1] = '\0';
  if (!s_selModePrefsOk) {
    return;
  }
  String s = s_selModePrefs.getString(PREFS_KEY_DEVICE_NAME, "");
  if (s.length() == 0) {
    return;
  }
  strncpy(g_apiDeviceName, s.c_str(), DEVICE_NAME_MAX_LEN - 1);
  g_apiDeviceName[DEVICE_NAME_MAX_LEN - 1] = '\0';
  Serial.print(F("[prefs] deviceName="));
  Serial.println(g_apiDeviceName);
}

void saveDeviceNameToPrefs(const char* name) {
  if (!name || name[0] == '\0') {
    return;
  }
  if (strcmp(g_apiDeviceName, name) == 0) {
    Serial.println(F("[prefs] deviceName tidak berubah, lewati simpan"));
    return;
  }
  strncpy(g_apiDeviceName, name, DEVICE_NAME_MAX_LEN - 1);
  g_apiDeviceName[DEVICE_NAME_MAX_LEN - 1] = '\0';
  if (!s_selModePrefsOk) {
    return;
  }
  s_selModePrefs.putString(PREFS_KEY_DEVICE_NAME, g_apiDeviceName);
  Serial.print(F("[prefs] simpan deviceName="));
  Serial.println(g_apiDeviceName);
}

//========= currentSessionId — reset posisi ke Duduk jika sesi berubah =========//
static void loadStoredSessionId(char* buf, size_t len) {
  if (!buf || len == 0) {
    return;
  }
  buf[0] = '\0';
  if (!s_selModePrefsOk) {
    return;
  }
  // Pakai getString agar match dengan putString (PT_STR); getBytes (PT_BLOB)
  // tidak kompatibel dan akan selalu mengembalikan 0 byte.
  String s = s_selModePrefs.getString(PREFS_KEY_SESSION_ID, "");
  strncpy(buf, s.c_str(), len - 1);
  buf[len - 1] = '\0';
}

static void saveStoredSessionId(const char* id) {
  if (!s_selModePrefsOk || !id) {
    return;
  }
  s_selModePrefs.putString(PREFS_KEY_SESSION_ID, id);
}

void onSessionIdChanged(const char* newSessionId) {
  if (!newSessionId || newSessionId[0] == '\0') {
    return;
  }
  static char stored[SESSION_ID_MAX_LEN];
  loadStoredSessionId(stored, sizeof(stored));
  if (strcmp(stored, newSessionId) != 0) {
    Serial.print(F("[prefs] sessionId baru: "));
    Serial.println(newSessionId);
    saveStoredSessionId(newSessionId);
  }
}

//========= Log teks di TFT + hasil & memori =========//
char tftLog[TFT_LOG_LINES][TFT_LOG_COLS];
uint8_t tftLogHead;
uint8_t tftLogCount;
int16_t tftNextY;
bool tftMemModeCapture;
bool tftResultCapture;
int resultSys, resultDia, resultPulse, resultMap, resultRot;

//========= Angka riwayat dari tensimeter (memori) =========//
int memSys, memDia, memPulse, memMap;
char memTimestamp[28];

//========= Buffer 1 baris teks UART =========//
char lineBuf[192];
size_t lineLen;

//========= Parser paket HEX "err:0 ..." dari tensimeter =========//
bool b_read;
bool b_discard;
bool packet_ready;
uint8_t hdr_stage;
int i, j;
char final_buff[64];
int hexSys, hexDias, hexBPM;

//========= Antrean RTOS: task UART/tombol → task TFT (SPI tidak mengganggu RX) ==========
static QueueHandle_t tftLineQueue;

bool tftTryEnqueueLine(const char* line) {
  if (!tftLineQueue || !line) return false;
  char buf[TFT_CMD_LINE_MAX];
  strncpy(buf, line, TFT_CMD_LINE_MAX - 1);
  buf[TFT_CMD_LINE_MAX - 1] = '\0';
  if (xQueueSend(tftLineQueue, buf, 0) == pdTRUE) return true;
  Serial.println(F("[TFT] queue penuh, baris dibuang"));
  return false;
}

//========= Init ADC, tombol, SPI TFT, logo ==========
static void tftBacklightSet(bool on) {
  digitalWrite(TFT_LED_PIN, on ? LOW : HIGH);
}

static void setupAdcPinsTftAndShowLogo(void) {
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  analogSetPinAttenuation(ADC_PIN, ADC_11db);
  pinMode(ADC_PIN, INPUT);
  pinMode(TEST_DIG_PIN, INPUT_PULLUP);
  pinMode(POWER_LOCK_PIN, OUTPUT);
  digitalWrite(POWER_LOCK_PIN, LOW);

  pinMode(TFT_LED_PIN, OUTPUT);
  tftBacklightSet(false);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, -1);  // SCLK=GPIO4, MOSI=GPIO6
  tft.initR(INITR_BLACKTAB);
  tft.setSPISpeed(40000000);  // 40 MHz
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);
  tftLogHead = 0;
  tftLogCount = 0;
  tftNextY = 0;

  tft.drawRGBBitmap(0, 0, logo_mamacare, LOGO_W, LOGO_H);
  tftBacklightSet(true);
}

//========= Saat boot: cek teks dari alat (3 flag biasa, tidak pakai struct/pointer) =========//
bool bootModePumping = false;  // ketemu "start test" / "startBPTest"
bool bootModeBptIdle = false;  // ketemu "bpt mode"
bool bootModeMemory = false;   // ketemu "mem mode"

char bootLine[192];  // buffer 1 baris teks saat boot
int bootLineLen = 0;

// Proses 1 karakter dari UART, susun baris, cek pola teks yang dikenal
void vCekKarakterBootUart(char c) {
  if (c == '\r') {
    return;
  }
  if (c == '\n') {
    bootLine[bootLineLen] = '\0';
    if (bootLineLen > 0) {
      Serial.println(bootLine);
      if (strstr(bootLine, "start test") || strstr(bootLine, "startBPTest")) {
        bootModePumping = true;
      }
      if (strstr(bootLine, "bpt mode")) {
        bootModeBptIdle = true;
      }
      if (strstr(bootLine, "mem mode")) {
        bootModeMemory = true;
      }
    }
    bootLineLen = 0;
    return;
  }
  if (bootLineLen < 191) {
    bootLine[bootLineLen++] = c;
  }
}

//========= setup() =========//
void setup() {
  // Turunkan CPU ke CPU_FREQ_MHZ (default 80) — hemat daya; harus dipanggil
  // SEBELUM Serial.begin/Serial1.begin agar peripheral pakai clock baru.
  // WiFi tetap bisa jalan di 80 MHz pada ESP32-C3.
  setCpuFrequencyMhz(CPU_FREQ_MHZ);

  // Urutan penting: Serial1 dulu (buffer besar), baru Serial USB
  Serial1.setRxBufferSize(1024);
  Serial1.begin(UART_BAUD, SERIAL_8N1, BP_RX, BP_TX);

  Serial.begin(115200);

  Serial.print(F("[boot] CPU freq = "));
  Serial.print(getCpuFrequencyMhz());
  Serial.println(F(" MHz"));

  setupAdcPinsTftAndShowLogo();

  prefsSelModeBegin();
  adcCalPrefsBegin();
  loadAdcCalFromPrefs();
  loadDeviceNameFromPrefs();

  prefsWifiBegin();
  loadWifiSlotsFromPrefs();
  apiInitDeviceId();
  // Simpan byte yang sudah masuk sebelum TFT/SPI jalan
  uint8_t bootUartSnap[BOOT_SERIAL_SNAPSHOT_MAX];
  size_t bootUartSnapLen = 0;
  while (Serial1.available() && bootUartSnapLen < BOOT_SERIAL_SNAPSHOT_MAX) {
    bootUartSnap[bootUartSnapLen++] = (uint8_t)Serial1.read();
  }

  rtcInit();  // internal RTC — waktu valid setelah NTP sync




  unsigned long scanDeadline = millis() + BOOT_MEASUREMENT_SCAN_MS;

  // Cari teks: start test | startBPTest | bpt mode | mem mode (langsung stop jika ketemu)
  bool sudahKetemu = false;
  for (size_t i = 0; i < bootUartSnapLen && !sudahKetemu; i++) {
    vCekKarakterBootUart((char)bootUartSnap[i]);
    sudahKetemu = bootModePumping || bootModeBptIdle || bootModeMemory;
  }

  while (!sudahKetemu && millis() < scanDeadline) {
    while (Serial1.available() && !sudahKetemu) {
      vCekKarakterBootUart((char)Serial1.read());
      sudahKetemu = bootModePumping || bootModeBptIdle || bootModeMemory;
    }
    delay(1);
  }

  memSys = memDia = memPulse = memMap = 0;
  memTimestamp[0] = '\0';

  tft.fillScreen(ST77XX_BLACK);
  tftNextY = 0;

  tftResultCapture = false;

  if (bootModePumping) {
    isPumping = true;
    tftMemModeCapture = false;
    pumpingSessionBegin();
    tftDrawBootConnectScreen();   // boot info: WiFi → Connected → GET → menu
    pumpingWifiNtpSyncRequest();
    Serial.println(F("[BOOT] pengukuran aktif (start test / startBPTest)"));
  } else if (bootModeBptIdle) {
    isPumping = false;
    tftMemModeCapture = true;
    tftDrawSelectorScreen(false);
    Serial.println(F("[BOOT] mode pengukuran (bpt mode)"));
  } else if (bootModeMemory) {
    isPumping = false;
    tftMemModeCapture = true;
    tftDrawMemoryOrPairingScreen();
    Serial.println(F("[BOOT] mem mode -> memori/pairing + capture"));
  } else {
    isPumping = false;
    tftMemModeCapture = false;
    tftDrawMemoryOrPairingScreen();
    Serial.println(F("[BOOT] default memori/pairing"));
  }

  powerLockUpdate();
  rtcRefreshDisplayCache();
  Serial.flush();
  Serial.println(F("[RTOS] antrean TFT + start tasks"));
  Serial.flush();

  tftLineQueue = xQueueCreate(TFT_CMD_QUEUE_DEPTH, TFT_CMD_LINE_MAX);
  if (tftLineQueue == NULL) {
    Serial.println(F("FATAL: tftLineQueue gagal dibuat"));
    for (;;) { delay(1000); }
  }

  /* ----- FreeRTOS: pola Program_Kecepatan_Arah_Angin (setup hanya buat task) ----- */
  xTaskCreatePinnedToCore(
    TaskTft,   /* Task function. */
    "TftDraw", /* name of task. */
    8192,      /* Stack size of task */
    NULL,      /* parameter of the task */
    2,         /* priority of the task */
    NULL,      /* Task handle to keep track of created task */
    0);        /* pin task to core */

  xTaskCreatePinnedToCore(
    TaskBpUartParser, /* Task function. */
    "BpUartParser",   /* name of task. */
    8192,             /* Stack size of task */
    NULL,             /* parameter of the task */
    3,                /* priority of the task */
    NULL,             /* Task handle to keep track of created task */
    0);               /* pin task to core */

  xTaskCreatePinnedToCore(
    TaskGpioDiag, /* Task function. */
    "GpioDiag",   /* name of task. */
    2048,         /* Stack size of task */
    NULL,         /* parameter of the task */
    1,            /* priority of the task */
    NULL,         /* Task handle to keep track of created task */
    0);           /* pin task to core */

  xTaskCreatePinnedToCore(
    TaskPairingCountdown, /* Task function. */
    "PairCnt",            /* name of task. */
    2048,                 /* Stack size of task */
    NULL,                 /* parameter of the task */
    1,                    /* priority of the task */
    NULL,                 /* Task handle to keep track of created task */
    0);                   /* pin task to core */

  xTaskCreatePinnedToCore(
    TaskPairingNet, /* Task function. */
    "PairNet",      /* name of task. */
    10240,          /* Stack size of task */
    NULL,           /* parameter of the task */
    1,              /* priority of the task */
    NULL,           /* Task handle to keep track of created task */
    0);             /* pin task to core */

  xTaskCreatePinnedToCore(
    TaskRtc,   /* Task function. */
    "RtcTick", /* name of task. */
    2048,      /* Stack size of task */
    NULL,      /* parameter of the task */
    1,         /* priority of the task */
    NULL,      /* Task handle to keep track of created task */
    0);        /* pin task to core */
}

void loop() {
}

//====================== TASK FREERTOS (setelah loop, seperti ref. Angin) ======================//
/*
  Task TFT ↔ UART/tombol: antrean FreeRTOS (tftLineQueue).
  Task lain: flag volatile bool.
*/

void TaskTft(void* pvParameters) {
  (void)pvParameters;
  char line[TFT_CMD_LINE_MAX];
  for (;;) {
    if (xQueueReceive(tftLineQueue, line, portMAX_DELAY) == pdTRUE) {
      tftFilterSerialLine(line);
    }
  }
}

void TaskBpUartParser(void* pvParameters) {
  (void)pvParameters;
  TickType_t xLastWakeTime;
  const TickType_t xFrequency = pdMS_TO_TICKS(1); /* jeda 1 ms antar putaran baca UART */
  xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    /* Selama OTA: jangan baca Serial1 tensimeter agar unduh/flash tidak terganggu */
    if (Update_OTA || bOtaDownloading) {
      vTaskDelayUntil(&xLastWakeTime, xFrequency);
      continue;
    }

    while (Serial1.available()) {
      char c = Serial1.read();

      if (!b_read) {
        appendLineChar(c);
        /* Mencari awalan "err:0" — satu langkah per huruf */
        if (hdr_stage == 0 && c == 'e') {
          hdr_stage = 1;
        } else if (hdr_stage == 1 && c == 'r') {
          hdr_stage = 2;
        } else if (hdr_stage == 2 && c == 'r') {
          hdr_stage = 3;
        } else if (hdr_stage == 3 && c == ':') {
          hdr_stage = 4;
        } else if (hdr_stage == 4 && c == '0') {
          b_read = true;
          hdr_stage = 0;
          i = 0;
          j = 0;
          b_discard = false;
        } else {
          hdr_stage = (c == 'e') ? 1 : 0;
        }
      } else {
        /* Sudah ketemu "err:0": lewati 30 byte, lalu ambil 11 byte ke final_buff */
        if (!b_discard) {
          i++;
          if (i == 30) {
            b_discard = true;
          }
        } else if (j < 11) {
          final_buff[j++] = c;
          if (j == 11) {
            packet_ready = true;
            b_read = false;
            i = 0;
            j = 0;
            b_discard = false;
          }
        }
      }
    }

    if (packet_ready) {
      packet_ready = false;
      decodePacket();
    }

    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

void TaskGpioDiag(void* pvParameters) {
  (void)pvParameters;
  TickType_t xLastWakeTime;
  const TickType_t xFrequency = pdMS_TO_TICKS(50); /* cek tombol tiap 50 ms */
  xLastWakeTime = xTaskGetTickCount();
  bool lastBtn = HIGH;
  for (;;) {
    bool btn = (bool)digitalRead(TEST_DIG_PIN);
    if (lastBtn == HIGH && btn == LOW) {
      if (g_pairingCountdownActive || g_historyWaitScreen || g_wifiStaConnectHoldActive) {
        Serial.println(F("[BTN] diabaikan (pairing/WiFi/history tunggu)"));
      } else if (showingResult && g_postCancelWindowActive && !g_postSucceeded) {
        pumpingCancelPostAndWifiOff();
        Serial.println(F("[BTN] POST dibatalkan oleh user"));
      } else if (!showingResult) {
        if (showingMemPairingMenu) {
          memMenuSel = (uint8_t)((memMenuSel + 1) % MEM_MENU_NUM_OPTS);
          powerLockUpdate();
          tftTryEnqueueLine("CMD:ARROW_ONLY");
          Serial.print(F("[BTN] mem menu -> "));
          Serial.println(memMenuSel);
        } else {
          Serial.println(F("[BTN] mode manual dinonaktifkan (ikut GET web)"));
        }
      } else {
        Serial.println(F("[BTN] diabaikan - hasil sedang ditampilkan"));
      }
    }
    lastBtn = btn;
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

void TaskPairingCountdown(void* pvParameters) {
  (void)pvParameters;
  TickType_t xLastWakeTime;
  const TickType_t xFrequency = pdMS_TO_TICKS(200); /* cek tiap 200 ms */
  xLastWakeTime = xTaskGetTickCount();

  int detikTerakhir = -1;  // -1 = belum pernah tampil; cukup -1 sudah jelas

  for (;;) {
    if (g_pairingCountdownActive) {
      // Hitung sisa waktu countdown pairing dalam milidetik (ms)
      long sisaMs;
      sisaMs = (long)(g_pairingCountdownEndMs - millis());

      // Konversi sisa waktu menjadi detik
      int sisaDetik;
      if (sisaMs <= 0) {
        sisaDetik = 0;
      } else {
        sisaDetik = (int)(sisaMs / 1000);
      }

      // Tampilkan angka hanya kalau berubah (tidak perlu gambar ulang tiap 200 ms)
      if (sisaDetik != detikTerakhir) {
        detikTerakhir = sisaDetik;
        char cmd[40];
        snprintf(cmd, sizeof(cmd), "CMD:PAIRING_TICK %d", sisaDetik);
        tftTryEnqueueLine(cmd);
      }

      // Waktu habis → hentikan, beritahu layar
      if (sisaMs <= 0) {
        g_pairingCountdownActive = false;
        powerLockUpdate();
        tftTryEnqueueLine("CMD:PAIRING_TIMEOUT");
        detikTerakhir = -1;
      }
    }

    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

void TaskPairingNet(void* pvParameters) {
  (void)pvParameters;
  TickType_t xLastWakeTime;
  const TickType_t xFrequency = pdMS_TO_TICKS(200); /* cek flag pairing tiap 200 ms */
  xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    if (Update_OTA) {
      const uint32_t nowMs = millis();
      if (nowMs - ulOtaLast1sMs >= 1000UL) {
        ulOtaLast1sMs = nowMs;
        powerLockUpdate();
        if (!WiFi.isConnected()) {
          Serial.println(F("[OTA] Menunggu WiFi STA (hotspot)..."));
          if ((long)(nowMs - ulOtaNextStaAttemptMs) >= 0) {
            ulOtaNextStaAttemptMs = nowMs + OTA_STA_RETRY_MS;
            Serial.println(F("[OTA] Try To Reconnect (STA)..."));
            vOtaTryStaReconnect();
          }
          if (nowMs - ulOtaSessionStartMs > OTA_SESSION_TIMEOUT_MS) {
            Serial.println(F("[OTA] Timeout 2 menit — batal."));
            vOtaSessionAbort();
          }
        } else if (Update_OTA) {
          Serial.println(F("[OTA] updateFirmware"));
          updateFirmware();
        }
      }
      vTaskDelayUntil(&xLastWakeTime, xFrequency);
      continue;
    }

    if (flagMulaiPairing) {
      flagMulaiPairing = false;
      pairingWebStartImpl();
    }
    if (flagHentikanPairing) {
      flagHentikanPairing = false;
      pairingWebStopImpl();
    }
    if (flagHubungkanWifi) {
      flagHubungkanWifi = false;
      wifiConnectImpl();
    }
    if (flagHistoryNtpSync) {
      flagHistoryNtpSync = false;
      historyWifiNtpSyncImpl();
    }
    if (flagPumpingNtpSync) {
      flagPumpingNtpSync = false;
      pumpingWifiNtpSyncImpl();
    }
    if (g_postCancelWindowActive &&
        (long)(millis() - g_postCancelWindowEndMs) >= 0) {
      g_postCancelWindowActive = false;
      if (!g_postCanceled && !g_postSucceeded) {
        flagPostMeasurement = true;
        Serial.println(F("[meas] cancel window habis -> POST"));
      } else {
        Serial.println(F("[meas] window habis dengan canceled flag - skip POST"));
      }
    }
    if (flagPostMeasurement) {
      flagPostMeasurement = false;
      pumpingPostMeasurementImpl();
    }
    pumpingWifiKeepaliveCheck();
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

void TaskRtc(void* pvParameters) {
  (void)pvParameters;
  int lastSec = -1;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(250));
    // Jangan tick sebelum NTP dicoba: area jam tetap kosong (bukan placeholder)
    if (!isPumping || showingResult || !g_ntpSyncAttempted) {
      lastSec = -1;
      continue;
    }
    int sec = rtcCurrentSecond();
    if (sec != lastSec) {
      lastSec = sec;
      rtcRefreshDisplayCache();
      tftTryEnqueueLine("CMD:CLOCK_TICK");
    }
  }
}
