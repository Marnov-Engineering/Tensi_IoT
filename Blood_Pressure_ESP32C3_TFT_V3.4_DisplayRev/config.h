#pragma once

/*
  config.h — semua pin dan angka konstanta di satu tempat
  Board: ESP32-C3, layar ST7735, Serial1 ke tensimeter
*/

//========= Layar TFT ST7735 (SPI) =========//
// MOSI=6  SCLK=4  DC=5  RST=1  CS=-1 (tidak dipakai)
#define TFT_CS    -1
#define TFT_RST    1
#define TFT_DC     5
#define TFT_MOSI   6
#define TFT_SCLK   4

// Backlight TFT — active-LOW: LOW = nyala, HIGH = mati
// Pakai GPIO20 (bukan GPIO0): LED biasanya ter-wire ke pin ini di PCB.
// GPIO0 = ADC1_CH0 → lebih aman untuk baca baterai, bukan output LED.
#define TFT_LED_PIN            2
#define TFT_LED_BOOT_DELAY_MS  2000u

//========= UART tensimeter (Serial1) =========//
// GPIO3 = RX (← TX alat)   GPIO21 = TX (→ RX alat)
#define BP_RX     3
#define BP_TX     21
#define UART_BAUD 115200

// Cuplik byte UART sebelum init TFT (SPI) supaya data awal tidak hilang
#define BOOT_SERIAL_SNAPSHOT_MAX  512
// Setelah logo: cari teks boot paling lama selama ini (ms)
#define BOOT_MEASUREMENT_SCAN_MS  4000

//========= Tombol / kunci daya =========//
#define TEST_DIG_PIN       10
#define TEST_DIG_PRINT_MS  500

// MOSFET tahan daya: HIGH = pegang tegangan, LOW = lepas (lihat powerLockUpdate)
#define POWER_LOCK_PIN       7
#define PAIRING_COUNTDOWN_MS 120000u

// Menu MODE di layar memori (nilai memMenuSel), ubah pakai tombol GPIO10
#define MEM_MENU_MATIKAN   0u
#define MEM_MENU_PAIRING   1u
#define MEM_MENU_HISTORY   2u
#define MEM_MENU_NUM_OPTS  3u

//========= ADC baterai =========//
// GPIO0 = ADC1_CH0 — kalibrasi tegangan sudah diset untuk pin ini.
// JANGAN pindah ke GPIO2: GPIO2 adalah strapping pin (di-reset chip baca level pin ini).
#define ADC_PIN  0

// Default kalibrasi (jika belum ada di Preferences)
#define ADC_CAL_M_DEFAULT  0.0009429925f
#define ADC_CAL_C_DEFAULT  0.1425085f
#define PREFS_KEY_ADC_CAL_M  "adcCalM"
#define PREFS_KEY_ADC_CAL_C  "adcCalC"

//========= RTC — pakai internal ESP32 (via SNTP/time.h); DS3231 tidak dipakai =========//
// Pin I2C di bawah dicatat sebagai referensi PCB; Wire.h tidak diinisialisasi.
#define RTC_I2C_SDA  8
#define RTC_I2C_SCL  9

//========= Log teks di TFT =========//
#define TFT_LOG_LINES  16
#define TFT_LOG_COLS   42
#define TFT_LINE_H     10

// Antrean perintah TFT (RTOS): panjang & ukuran baris yang di-forward ke task TFT
#define TFT_CMD_QUEUE_DEPTH  16
#define TFT_CMD_LINE_MAX     192

//========= Preferences (flash) =========//
#define PREFS_NAMESPACE_MODE  "mamacare"
// currentSessionId terakhir — bandingkan saat GET device state
#define PREFS_KEY_SESSION_ID  "sessionId"
#define PREFS_KEY_DEVICE_NAME "deviceName"

// Nama hotspot saat pairing (terlihat di HP)
#define PAIRING_AP_SSID "MamaCare"

//========= Preferences WiFi credentials (hingga 5 slot, FIFO) =========//
#define PREFS_NAMESPACE_WIFI  "wifi"
#define PREFS_KEY_WIFI_SSID   "ssid"   // format lama — migrasi ke slot
#define PREFS_KEY_WIFI_PASS   "pass"
#define PREFS_KEY_WIFI_COUNT  "count"
#define WIFI_MAX_SLOTS        5u
#define WIFI_CONNECT_ATTEMPTS 5u

// Batas panjang SSID dan password WiFi (termasuk null terminator)
#define WIFI_SSID_MAX_LEN  64
#define WIFI_PASS_MAX_LEN  64

// Timeout percobaan koneksi WiFi STA per SSID (ms)
#define WIFI_CONNECT_TIMEOUT_MS  3000u

// NTP — sinkron RTC saat mode HISTORY (MEMORI 1), zona WIB (GMT+7)
#define NTP_GMT_OFFSET_SEC       (7 * 3600)
#define NTP_DAYLIGHT_OFFSET_SEC    0
#define NTP_SERVER                 "pool.ntp.org"
#define NTP_FETCH_TIMEOUT_MS       10000u
// Selisih RTC vs NTP (detik) minimal sebelum RTC di-update saat boot pumping
#define NTP_RTC_MAX_DRIFT_SEC      10u
#define NTP_VALID_YEAR_MIN         2020
#define NTP_VALID_YEAR_MAX         2040

// Setelah form /save: tunjukkan IP lalu lepas power-lock (tidur).
#define PAIRING_PREFLIGHT_SUCCESS_HOLD_MS  10000u
// Setelah gagal STA: tampilkan pesan "WiFi gagal" lalu matikan alat.
#define PAIRING_PREFLIGHT_FAIL_HOLD_MS     3000u

//========= API Server — Cloud Functions =========//
#define API_BASE_URL  "https://asia-east1-mamacare-3ae58.cloudfunctions.net/deviceApi"

// GET device state — endpoint: GET /deviceApi/{deviceId}
// Response Firestore REST: {"fields":{"deviceName":{"stringValue":"..."},...}}
// atau flat JSON: {"deviceId":"...","status":"in_use",...}
#define API_GET_ACTIVE_CMD_PATH   "/%s"

// POST measurement — endpoint: POST /deviceApi/{deviceId}
#define API_PUT_MEAS_PATH         "/%s"
#define API_PUT_MEAS_METHOD       "POST"

// Timeout HTTP API (ms)
#define API_HTTP_TIMEOUT_MS       15000u
// Percobaan re-koneksi WiFi STA jika WiFi sudah mati saat POST
#define API_WIFI_CONNECT_ATTEMPTS 1u
// Batas waktu WiFi tetap nyala setelah NTP+GET sebelum auto-mati (ms)
// Pengukuran biasanya selesai < 3 menit; 5 menit = aman
#define API_WIFI_KEEPALIVE_MS     300000UL

// Jeda Cancel sebelum POST API ke server (ms)
#define POST_CANCEL_WINDOW_MS     5000u

// Batas panjang string sesi/pasien
#define SESSION_ID_MAX_LEN        96
#define PATIENT_ID_MAX_LEN        64
#define PATIENT_NAME_MAX_LEN      64
#define DEVICE_ID_LEN             13   // MAC 12 huruf + null
#define DEVICE_NAME_MAX_LEN       32
#define DEVICE_NAME_DEFAULT       "-"

// Versi protokol API measurement (dikirim saat POST)
#define API_VERSION_NUMBER    "3"

//========= Versi firmware (tampil di web OTA) =========//
#define FIRMWARE_VERSION  "V3.4.2_DisplayRev"

//========= CPU clock ESP32-C3 (MHz) =========//
// Pilihan valid ESP32-C3: 160 (default), 80, 40 (WiFi minimal 80).
// Turunkan ke 80 untuk hemat daya saat WiFi aktif; ke 40 saat WiFi mati.
#define CPU_FREQ_MHZ  160u

//========= OTA — hotspot unduh firmware (pola CP Logger) =========//
#define OTA_STA_SSID           "UpdateFirmware"
#define OTA_STA_PASS           "12345678"
#define OTA_SESSION_TIMEOUT_MS 120000UL
#define OTA_FIRMWARE_RETRY_MS  120000UL
#define OTA_STA_RETRY_MS       10000UL

