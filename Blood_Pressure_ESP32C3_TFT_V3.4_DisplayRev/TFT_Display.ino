/*
  TFT_Display.ino — gambar semua tampilan ST7735
  1. Layar pilih mode / mengukur / memori & pairing
  2. Panah, ikon, teks hasil tensi (SYS/DIA/dll.)
  Variabel tft, mode, dan flag tombol ada di file utama (.ino).
  Include library hanya di sketch utama.
*/

extern volatile bool g_historyWaitScreen;
extern volatile bool g_pairingCountdownActive;
extern volatile bool g_wifiStaConnectHoldActive;
extern volatile bool g_historyWifiConnected;
extern volatile bool g_pumpingWifiConnected;
extern volatile bool g_pumpingInternetOk;
extern volatile bool g_ntpSyncAttempted;
extern volatile bool g_apiSessionValid;
extern volatile uint8_t selectedMode;
extern volatile bool  g_postCancelWindowActive;
extern volatile bool  g_postCanceled;
extern volatile bool  g_postSucceeded;
void                 pumpingMeasurementResultBegin(void);
extern float         g_apiSavedMap;
extern bool          g_apiSavedMapValid;
extern int           g_apiDbpMiringKiri;
extern bool          g_apiDbpMiringKiriValid;
extern bool          tftMemModeCapture;
extern char          g_apiDeviceId[];
extern char          g_apiDeviceName[];
extern char          g_apiPatientName[];
void                 apiInitDeviceId(void);
void                 powerLockUpdate(void);
void                 pairingCountdownStart(void);
void                 pairingCountdownStop(void);
void                 pairingWebRequestStart(void);
void                 pairingWebRequestStop(void);
void                 historySessionBegin(void);
void                 historySessionEnd(void);
void                 historyWifiNtpSyncRequest(void);
void                 pumpingSessionBegin(void);
void                 pumpingWifiNtpSyncRequest(void);
void                 pumpingPostMeasurementRequest(int sbp, int dbp, int bpm,
                                                  int rot, float mapMmHg,
                                                  uint8_t mode);
const char*          rtcGetDisplayDate(void);
const char*          rtcGetDisplayTime(void);
float                adcReadBatteryVolts(void);

// Ikon status (layar pumping / Lopaka)
static const unsigned char PROGMEM image_network_www_bits[] = {
  0x03, 0xc0, 0x0d, 0xb0, 0x32, 0x4c, 0x24, 0x24, 0x44, 0x22, 0x7f, 0xfe,
  0x88, 0x11, 0x88, 0x11, 0x88, 0x11, 0x88, 0x11, 0x7f, 0xfe, 0x44, 0x22,
  0x24, 0x24, 0x32, 0x4c, 0x0d, 0xb0, 0x03, 0xc0
};
static const unsigned char PROGMEM image_wifi_full_bits[] = {
  0x01, 0xf0, 0x00, 0x07, 0xfc, 0x00, 0x1e, 0x0f, 0x00, 0x39, 0xf3, 0x80,
  0x77, 0xfd, 0xc0, 0xef, 0x1e, 0xe0, 0x5c, 0xe7, 0x40, 0x3b, 0xfb, 0x80,
  0x17, 0x1d, 0x00, 0x0e, 0xee, 0x00, 0x05, 0xf4, 0x00, 0x03, 0xb8, 0x00,
  0x01, 0x50, 0x00, 0x00, 0xe0, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00
};

// Panah menu MODE mem/pairing (18×15 px)
static const unsigned char PROGMEM image_Release_arrow_bits[] = {
  0x00, 0x60, 0x00, 0x00, 0x50, 0x00, 0x00, 0x48, 0x00, 0x00, 0x44, 0x00, 0x7f, 0xd2, 0x00,
  0x80, 0x19, 0x00, 0xbf, 0xfc, 0x80, 0xbf, 0xfe, 0x40, 0xbf, 0xfc, 0x80, 0x80, 0x19, 0x00,
  0x7f, 0xd2, 0x00, 0x00, 0x44, 0x00, 0x00, 0x48, 0x00, 0x00, 0x50, 0x00, 0x00, 0x60, 0x00
};

// Hasil POST API — centang hijau 14×16 px
// Kanan-atas = puncak stroke kanan; kiri-tengah = awal stroke kiri; bawah-tengah = ujung bawah
static const unsigned char PROGMEM image_checked_bits[] = {
  0x00, 0x04,  // row  0: col 13
  0x00, 0x0C,  // row  1: col 12-13
  0x00, 0x1C,  // row  2: col 11-13
  0x00, 0x18,  // row  3: col 11-12
  0x00, 0x30,  // row  4: col 10-11
  0x00, 0x60,  // row  5: col 9-10
  0x00, 0x60,  // row  6: col 9-10
  0x80, 0xC0,  // row  7: col 0 | col 8-9
  0xC0, 0x80,  // row  8: col 0-1 | col 8
  0xC1, 0x80,  // row  9: col 0-1, 7 | col 8
  0x63, 0x00,  // row 10: col 1-2, 6-7
  0x36, 0x00,  // row 11: col 2-3, 5-6
  0x36, 0x00,  // row 12: col 2-3, 5-6
  0x1C, 0x00,  // row 13: col 3-5
  0x08, 0x00,  // row 14: col 4
  0x08, 0x00,  // row 15: col 4
};

// Hasil POST API — silang merah 11×16 px
static const unsigned char PROGMEM image_crossed_bits[] = {
  0x80, 0x20,  // row  0: col 0, col 10
  0xC0, 0x60,  // row  1: col 0-1, col 9-10
  0xC0, 0x60,  // row  2
  0x60, 0xC0,  // row  3: col 1-2, col 8-9
  0x31, 0x80,  // row  4: col 2-3, col 7-8
  0x31, 0x80,  // row  5
  0x1B, 0x00,  // row  6: col 3-4, col 6-7
  0x0E, 0x00,  // row  7: col 4-6  (centre X)
  0x0E, 0x00,  // row  8
  0x0E, 0x00,  // row  9
  0x1B, 0x00,  // row 10: col 3-4, col 6-7
  0x1B, 0x00,  // row 11
  0x31, 0x80,  // row 12: col 2-3, col 7-8
  0x60, 0xC0,  // row 13: col 1-2, col 8-9
  0x60, 0xC0,  // row 14
  0xC0, 0x60,  // row 15: col 0-1, col 9-10
};

// Panah bawah — layar tunggu HISTORY (14×15 px)
static const unsigned char PROGMEM image_ArrowDownFilled_bits[] = {
  0x1f, 0xe0, 0x10, 0x20, 0x17, 0xa0, 0x16, 0xa0, 0x15, 0xa0, 0x16, 0xa0, 0x15, 0xa0,
  0xf6, 0xbc, 0x85, 0x84, 0x5f, 0xe8, 0x2f, 0xd0, 0x17, 0xa0, 0x0b, 0x40, 0x04, 0x80, 0x03, 0x00
};

// Layar pumping — palet desain Lopaka (RGB565)
static const uint16_t PUMP_CLR_WHITE   = 0xFFFF;
static const uint16_t PUMP_CLR_ORANGE  = 0xFD61;
static const uint16_t PUMP_CLR_BLUE    = 0x015E;

/* Detik terakhir yang sudah digambar; di-reset saat tftDrawPairingReadyScreen full redraw. */
static int16_t s_pairingCdLastPaintedSec = -1;

// Detik berlalu saat percobaan STA (0 … WIFI_CONNECT_TIMEOUT_MS/1000)
static int16_t s_wifiConnLastElapsedSec = -1;
// Nomor percobaan STA terakhir yang sudah digambar (1 … WIFI_CONNECT_ATTEMPTS)
static int16_t s_wifiConnLastAttempt = -1;

// true saat layar boot-connect (WiFi/NTP progress) sedang ditampilkan
static bool   s_bootScreenActive  = false;
// State teks dinamis boot screen — untuk teknik erase-by-overwrite (timpa warna latar)
static char   s_bootSsidLast[18]  = "";   // SSID terakhir yang ditulis
static int8_t s_bootWifiLast      = -1;   // -1=belum, 0=gagal, 1=berhasil
static int8_t s_bootGetLast       = -1;   // -1=belum, 0=gagal, 1=berhasil

// Indeks record memori — naik otomatis tiap "read Rec result:" (history baru)
static int memRecIndex = 0;
// Nama pasien dibatasi karena ruang layar sempit (mis. 8 karakter).
#define PATIENT_NAME_DISPLAY_MAX 8

// Tampilkan nama pasien (dari currentPatientName/g_apiPatientName) rata tengah dalam kotak.
// Jika belum dapat nama dari server → kosong (tidak menggambar apa pun).
static void tftPaintPatientName(int16_t boxX, int16_t y, int16_t boxW) {
  if (g_apiPatientName[0] == '\0') {
    return;  // belum dapat nama → kosongkan
  }
  char nameBuf[PATIENT_NAME_DISPLAY_MAX + 1];
  strncpy(nameBuf, g_apiPatientName, PATIENT_NAME_DISPLAY_MAX);
  nameBuf[PATIENT_NAME_DISPLAY_MAX] = '\0';
  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(PUMP_CLR_BLUE);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(nameBuf, 0, 0, &x1, &y1, &w, &h);
  int16_t cx = boxX + (boxW - (int16_t)w) / 2;
  tft.setCursor(cx, y);
  tft.print(nameBuf);
}

// Angka hasil pumping — rata kanan dalam kotak oranye kanan.
static void tftPrintPumpingValueRight(int16_t boxX, int16_t boxW, int16_t baselineY,
                                      const char* text) {
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int16_t cx = boxX + boxW - (int16_t)w - 3;
  if (cx < boxX) {
    cx = boxX;
  }
  tft.setCursor(cx, baselineY);
  tft.print(text);
}

// Label tiap mode pengukuran
static const char* tftModeLabel(uint8_t mode) {
  if (mode == 1) return "Tidur Samping Kiri";
  if (mode == 2) return "Terlentang";
  return "Duduk";
}

// Singkatan mode — header layar hasil (sama seperti selector pumping)
static const char* tftModeAbbrev(uint8_t mode) {
  if (mode == 1) return "TSK";
  if (mode == 2) return "TLG";
  return "DDK";
}

// MAP = DBP + 1/3 * (SBP - DBP) — hanya dihitung & disimpan saat posisi Duduk
static float tftCalcMapMmHg(int sbp, int dbp) {
  return (float)dbp + (1.0f / 3.0f) * ((float)sbp - (float)dbp);
}

static void tftPaintMapValue(float mapMmHg, bool show) {
  tft.fillRoundRect(1, 31, 79, 35, 8, PUMP_CLR_ORANGE);
  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(PUMP_CLR_BLUE);
  tft.setCursor(32, 35);
  tft.print("MAP");
  if (!show) {
    return;
  }
  char buf[16];
  tft.setFont(&FreeSansBoldOblique12pt7b);
  tft.setTextColor(PUMP_CLR_BLUE);
  snprintf(buf, sizeof(buf), "%.1f", mapMmHg);
  tft.setCursor(9, 61);
  tft.print(buf);
  tft.setFont(NULL);
  tft.setTextSize(1);
}

// Panel kiri bawah — tampilan fix per posisi (tidak tampilkan prefs lama yang belum di-update)
static void tftPaintRotDtDmPanel(int dia, uint8_t mode) {
  int dtVal = 0;
  int dmVal = 0;
  bool showRot = false;
  bool showDt  = false;
  bool showDm  = false;
  int  rotVal  = 0;

  if (mode == 0) {
    // Duduk: ROT kosong, DT=0, DM=0
    showDt = false;
    showDm = false;
  } else if (mode == 1) {
    // TSK: ROT kosong, DT=0, DM=DBP baru
    showDt = false;
    if (dia > 0) {
      dmVal  = dia;
      showDm = true;
    }
  } else {
    // TLG: DT=DBP baru, DM dari TSK, ROT=DT-DM
    if (dia > 0) {
      dtVal  = dia;
      showDt = true;
    }
    if (g_apiDbpMiringKiriValid) {
      dmVal  = g_apiDbpMiringKiri;
      showDm = true;
      if (showDt) {
        rotVal  = dia - g_apiDbpMiringKiri;
        showRot = true;
      }
    }
  }

  tft.fillRoundRect(1, 68, 79, 41, 8, PUMP_CLR_ORANGE);
  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(PUMP_CLR_BLUE);
  tft.setCursor(11, 73);
  tft.print("ROT");

  char buf[20];
  if (showRot) {
    tft.setFont(&FreeSansBoldOblique12pt7b);
    snprintf(buf, sizeof(buf), "%d", rotVal);
    // Kiri panel (dekat label ROT) — angka negatif tidak terdorong ke kanan
    tft.setCursor(8, 101);
    tft.print(buf);
    tft.setFont(NULL);
    tft.setTextSize(1);
  }

  tft.setCursor(50, 81);
  snprintf(buf, sizeof(buf), "DT:%d", showDt ? dtVal : 0);
  tft.print(buf);
  tft.setCursor(50, 93);
  snprintf(buf, sizeof(buf), "DM:%d", showDm ? dmVal : 0);
  tft.print(buf);
}

// ============================================================
// Layar 1 — Selector mode pengukuran
// showPumping=true : layar Lopaka (MENGUKUR + posisi + singkatan; GPIO mengganti teks)
// ============================================================
// --- Layar selector saat pumping (desain Lopaka; tombol GPIO mengganti teks + singkatan) ---
static void tftPaintPumpingPositionCenter(void) {
  tft.fillRoundRect(9, 49, 111, 59, 8, PUMP_CLR_ORANGE);
  tft.fillRoundRect(84, 111, 44, 28, 8, PUMP_CLR_ORANGE);
  // GET gagal / sesi belum valid: kotak posisi kosong (jam & MENGUKUR tetap tampil).
  if (!g_apiSessionValid) {
    return;
  }

  tft.setTextColor(PUMP_CLR_BLUE);
  tft.setTextWrap(false);
  tft.setFont(&FreeMonoBold9pt7b);
  uint8_t m = selectedMode;
  if (m == 0) {
    tft.setCursor(37, 82);
    tft.print("DUDUK");
  } else if (m == 1) {
    tft.setCursor(37, 66);
    tft.print("TIDUR");
    tft.setCursor(25, 82);
    tft.print("SAMPING");
    tft.setCursor(42, 97);
    tft.print("KIRI");
  } else {
    tft.setCursor(45, 72);
    tft.print("TER");
    tft.setCursor(22, 90);
    tft.print("LENTANG");
  }

  tft.setFont(&FreeSansBold9pt7b);
  tft.setTextColor(PUMP_CLR_BLUE);
  tft.setCursor(88, 131);
  if (m == 0) {
    tft.print("DDK");
  } else if (m == 1) {
    tft.print("TSK");
  } else {
    tft.print("TLG");
  }
}

static void tftPaintPumpingWifiIcon(bool show) {
  tft.fillRect(105, 142, 19, 16, PUMP_CLR_WHITE);
  if (show) {
    tft.drawBitmap(105, 142, image_wifi_full_bits, 19, 16, PUMP_CLR_BLUE);
  }
}

static void tftPaintPumpingInternetIcon(bool show) {
  tft.fillRect(87, 142, 16, 16, PUMP_CLR_WHITE);
  if (show) {
    tft.drawBitmap(87, 142, image_network_www_bits, 16, 16, PUMP_CLR_BLUE);
  }
}

static void tftPaintPumpingStatusIcons(void) {
  tftPaintPumpingWifiIcon(g_pumpingWifiConnected);
  tftPaintPumpingInternetIcon(g_pumpingInternetOk);
}

// ============================================================
// Layar Boot-Connect — tampil saat power-on bootModePumping
// Urutan: logo → layar ini (WiFi progress) → menu pumping
// ============================================================

// Helper: cetak teks rata-tengah dalam kotak info (x=8, w=112)
static void tftBootPrintCentered(const char* text, int16_t y) {
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int16_t cx = 8 + (int16_t)((112 - (int16_t)w) / 2);
  if (cx < 10) cx = 10;
  tft.setCursor(cx, y);
  tft.print(text);
}

// ============================================================
// Bitmap baterai 24×16 px — 7 level (0% .. 100%)
// Referensi: full = 3.0V, habis = 2.4V
// ============================================================
static const unsigned char PROGMEM image_battery_0_bits[]    = {0x00,0x00,0x00,0x0f,0xff,0xfe,0x10,0x00,0x01,0x10,0x40,0x41,0x70,0x20,0x81,0x80,0x11,0x01,0x80,0x0a,0x01,0x80,0x04,0x01,0x80,0x0a,0x01,0x80,0x11,0x01,0x70,0x20,0x81,0x10,0x40,0x41,0x10,0x00,0x01,0x0f,0xff,0xfe,0x00,0x00,0x00,0x00,0x00,0x00};
static const unsigned char PROGMEM image_battery_17_bits[]   = {0x00,0x00,0x00,0x0f,0xff,0xfe,0x10,0x00,0x01,0x10,0x00,0x0d,0x70,0x00,0x0d,0x80,0x00,0x0d,0x80,0x00,0x0d,0x80,0x00,0x0d,0x80,0x00,0x0d,0x80,0x00,0x0d,0x70,0x00,0x0d,0x10,0x00,0x0d,0x10,0x00,0x01,0x0f,0xff,0xfe,0x00,0x00,0x00,0x00,0x00,0x00};
static const unsigned char PROGMEM image_battery_33_bits[]   = {0x00,0x00,0x00,0x0f,0xff,0xfe,0x10,0x00,0x01,0x10,0x00,0x6d,0x70,0x00,0x6d,0x80,0x00,0x6d,0x80,0x00,0x6d,0x80,0x00,0x6d,0x80,0x00,0x6d,0x80,0x00,0x6d,0x70,0x00,0x6d,0x10,0x00,0x6d,0x10,0x00,0x01,0x0f,0xff,0xfe,0x00,0x00,0x00,0x00,0x00,0x00};
static const unsigned char PROGMEM image_battery_50_bits[]   = {0x00,0x00,0x00,0x0f,0xff,0xfe,0x10,0x00,0x01,0x10,0x03,0x6d,0x70,0x03,0x6d,0x80,0x03,0x6d,0x80,0x03,0x6d,0x80,0x03,0x6d,0x80,0x03,0x6d,0x80,0x03,0x6d,0x70,0x03,0x6d,0x10,0x03,0x6d,0x10,0x00,0x01,0x0f,0xff,0xfe,0x00,0x00,0x00,0x00,0x00,0x00};
static const unsigned char PROGMEM image_battery_67_bits[]   = {0x00,0x00,0x00,0x0f,0xff,0xfe,0x10,0x00,0x01,0x10,0x1b,0x6d,0x70,0x1b,0x6d,0x80,0x1b,0x6d,0x80,0x1b,0x6d,0x80,0x1b,0x6d,0x80,0x1b,0x6d,0x80,0x1b,0x6d,0x70,0x1b,0x6d,0x10,0x1b,0x6d,0x10,0x00,0x01,0x0f,0xff,0xfe,0x00,0x00,0x00,0x00,0x00,0x00};
static const unsigned char PROGMEM image_battery_83_bits[]   = {0x00,0x00,0x00,0x0f,0xff,0xfe,0x10,0x00,0x01,0x10,0xdb,0x6d,0x70,0xdb,0x6d,0x80,0xdb,0x6d,0x80,0xdb,0x6d,0x80,0xdb,0x6d,0x80,0xdb,0x6d,0x80,0xdb,0x6d,0x70,0xdb,0x6d,0x10,0xdb,0x6d,0x10,0x00,0x01,0x0f,0xff,0xfe,0x00,0x00,0x00,0x00,0x00,0x00};
static const unsigned char PROGMEM image_battery_full_bits[] = {0x00,0x00,0x00,0x7f,0xff,0xf0,0x80,0x00,0x08,0xb6,0xdb,0x68,0xb6,0xdb,0x6e,0xb6,0xdb,0x61,0xb6,0xdb,0x61,0xb6,0xdb,0x61,0xb6,0xdb,0x61,0xb6,0xdb,0x61,0xb6,0xdb,0x6e,0xb6,0xdb,0x68,0x80,0x00,0x08,0x7f,0xff,0xf0,0x00,0x00,0x00,0x00,0x00,0x00};

// Pilih bitmap baterai berdasarkan tegangan (3.0V=full, 2.4V=habis)
static const unsigned char* batteryBitmapForVoltage(float v) {
  if (v >= 2.95f) return image_battery_full_bits;
  if (v >= 2.85f) return image_battery_83_bits;
  if (v >= 2.75f) return image_battery_67_bits;
  if (v >= 2.65f) return image_battery_50_bits;
  if (v >= 2.55f) return image_battery_33_bits;
  if (v >= 2.45f) return image_battery_17_bits;
  return image_battery_0_bits;
}

// Gambar bar baterai bawah (ikon + tegangan) — boot connect & pumping selector
static void tftPaintBatteryBar(void) {
  tft.fillRoundRect(5, 141, 71, 18, 8, PUMP_CLR_BLUE);
  float batV = adcReadBatteryVolts();
  tft.drawBitmap(13, 142, batteryBitmapForVoltage(batV), 24, 16, PUMP_CLR_ORANGE);
  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(PUMP_CLR_ORANGE);
  char batBuf[8];
  snprintf(batBuf, sizeof(batBuf), "%.1fV", (double)batV);
  tft.setCursor(44, 146);
  tft.print(batBuf);
}

// Layar awal boot: kotak putih + header + kotak info + "Connecting WiFi"
void tftDrawBootConnectScreen(void) {
  s_bootScreenActive    = true;
  showingResult         = false;
  showingMemPairingMenu = false;
  tftNextY              = tft.height();

  tft.fillScreen(PUMP_CLR_WHITE);
  tft.setTextWrap(false);

  // Header "MENGUKUR"
  tft.fillRoundRect(8, 2, 112, 23, 8, PUMP_CLR_ORANGE);
  tft.setFont(&FreeMonoBold9pt7b);
  tft.setTextColor(PUMP_CLR_BLUE);
  tft.setCursor(22, 18);
  tft.print("MENGUKUR");

  // Kotak info — teks awal "MENGHUBUNGKAN" + "WIFI"
  tft.fillRoundRect(8, 31, 112, 104, 8, PUMP_CLR_ORANGE);
  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(PUMP_CLR_BLUE);
  tftBootPrintCentered("MENGHUBUNGKAN", 35);
  tftBootPrintCentered("WIFI", 47);

  // Reset state teks dinamis
  s_bootSsidLast[0]   = '\0';
  s_bootWifiLast      = -1;
  s_bootGetLast       = -1;

  // Bar baterai kiri bawah + tombol OFF kanan bawah
  tftPaintBatteryBar();
  tft.fillRoundRect(90, 141, 34, 18, 8, PUMP_CLR_BLUE);
  drawPetme(tft, 95, 148, "OFF", PUMP_CLR_ORANGE);
}

// Helper: cetak teks di y tertentu dengan warna tertentu (font kecil, no-wrap)
static void tftBootPrintColored(const char* text, int16_t y, uint16_t color) {
  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(color);
  tftBootPrintCentered(text, y);
}

// SSID (y=64) — timpa lama dengan orange lalu tulis baru dengan biru
static void tftBootPaintSsid(const char* ssid) {
  if (!s_bootScreenActive) return;
  tft.setTextWrap(false);
  if (s_bootSsidLast[0] != '\0') {
    tftBootPrintColored(s_bootSsidLast, 64, PUMP_CLR_ORANGE);
  }
  strncpy(s_bootSsidLast, ssid ? ssid : "", 17);
  s_bootSsidLast[17] = '\0';
  tftBootPrintColored(s_bootSsidLast, 64, PUMP_CLR_BLUE);
}

// Status WiFi (y=82) — "TERHUBUNG" / "TDK TERHUBUNG"
static void tftBootPaintWifiResult(bool connected) {
  if (!s_bootScreenActive) return;
  tft.setTextWrap(false);
  if (s_bootWifiLast == 0) tftBootPrintColored("TDK TERHUBUNG", 82, PUMP_CLR_ORANGE);
  else if (s_bootWifiLast == 1) tftBootPrintColored("TERHUBUNG", 82, PUMP_CLR_ORANGE);
  s_bootWifiLast = connected ? 1 : 0;
  tftBootPrintColored(connected ? "TERHUBUNG" : "TDK TERHUBUNG", 82, PUMP_CLR_BLUE);
}

// Status GET — statis "AMBIL DATA PASIEN" (y=101)
static void tftBootPaintGetStatus(void) {
  if (!s_bootScreenActive) return;
  tftBootPrintColored("AMBIL DATA PASIEN", 101, PUMP_CLR_BLUE);
}

// Hasil GET (y=117) — "BERHASIL" / "GAGAL = {code}"
static char s_bootGetFailText[20] = "GAGAL";  // teks gagal terakhir (dengan kode)

static void tftBootPaintGetResult(bool success, int httpCode = 0) {
  if (!s_bootScreenActive) return;
  tft.setTextWrap(false);
  // Hapus teks lama
  if (s_bootGetLast == 0) tftBootPrintColored(s_bootGetFailText, 117, PUMP_CLR_ORANGE);
  else if (s_bootGetLast == 1) tftBootPrintColored("BERHASIL", 117, PUMP_CLR_ORANGE);
  s_bootGetLast = success ? 1 : 0;
  if (success) {
    tftBootPrintColored("BERHASIL", 117, PUMP_CLR_BLUE);
  } else {
    if (httpCode != 0) {
      snprintf(s_bootGetFailText, sizeof(s_bootGetFailText), "GAGAL = %d", httpCode);
    } else {
      strncpy(s_bootGetFailText, "GAGAL", sizeof(s_bootGetFailText));
    }
    tftBootPrintColored(s_bootGetFailText, 117, PUMP_CLR_BLUE);
  }
}

// Ganti tombol Cancel dengan ikon hasil POST API (✓ hijau atau ✗ merah).
// Dipanggil via CMD:API_POST_OK / CMD:API_POST_FAIL dari TaskPairingNet.
static void tftPaintApiResultIcon(bool success) {
  // Hapus area tombol Cancel (x=1 y=141 w=60 h=18)
  tft.fillRoundRect(1, 141, 60, 18, 8, PUMP_CLR_WHITE);
  if (success) {
    // centang hijau — 14×16, tengah area tombol
    tft.drawBitmap(24, 142, image_checked_bits, 14, 16, 0x0CE3);
  } else {
    // silang merah — 11×16, tengah area tombol
    tft.drawBitmap(25, 142, image_crossed_bits, 11, 16, 0xF800);
  }
}

static void tftPaintPumpingDateTime(void) {
  tft.fillRoundRect(8, 28, 112, 18, 8, PUMP_CLR_ORANGE);
  tft.setTextColor(PUMP_CLR_BLUE);
  tft.setTextWrap(false);
  tft.setFont(&Org_01);
  tft.setCursor(19, 38);
  tft.print(rtcGetDisplayDate());
  tft.setCursor(75, 38);
  tft.print(rtcGetDisplayTime());
}

static void tftDrawPumpingSelectorScreen(void) {
  tft.fillScreen(PUMP_CLR_WHITE);
  tft.setTextWrap(false);

  tft.fillRoundRect(9, 49, 111, 59, 8, PUMP_CLR_ORANGE);
  tft.fillRoundRect(1, 111, 80, 28, 8, PUMP_CLR_ORANGE);
  tft.fillRoundRect(84, 111, 44, 28, 8, PUMP_CLR_ORANGE);
  tft.fillRoundRect(7, 2, 113, 23, 8, PUMP_CLR_ORANGE);
  tft.fillRoundRect(8, 28, 112, 18, 8, PUMP_CLR_ORANGE);

  tft.setTextColor(PUMP_CLR_BLUE);
  tft.setFont(&FreeMonoBold9pt7b);
  tft.setCursor(22, 18);
  tft.print("MENGUKUR");

  // Jam/tanggal TIDAK ditampilkan di sini: area oranye kosong dulu.
  // Akan diisi oleh CMD:CLOCK_TICK setelah NTP berhasil (waktu aktual)
  // atau jika NTP gagal (placeholder --:-- / --/--/--).

  tftPaintPumpingPositionCenter();

  tftPaintPatientName(1, 121, 80);

  tftPaintPumpingStatusIcons();

  tftPaintBatteryBar();
}

void tftDrawSelectorScreen(bool showPumping) {
  g_historyWaitScreen = false;
  historySessionEnd();
  pairingCountdownStop();
  pairingWebRequestStop();

  showingResult         = false;
  showingMemPairingMenu = false;

  if (showPumping) {
    tftDrawPumpingSelectorScreen();
    tftNextY = tft.height();
    return;
  }

  tftMemModeCapture = true;
  tftDrawMemoryOrPairingScreen();
  tftNextY = tft.height();
}

// ============================================================
// Layar 2 — Hasil pengukuran
// MAP: Duduk → hitung + simpan Preferences; TSK/TLG → tampilkan MAP tersimpan
// ============================================================
void tftShowResultNew(int sys, int dia, int pulse, int map, int rot, const char* ts) {
  (void)map;
  (void)ts;

  showingResult         = true;
  showingMemPairingMenu = false;
  tft.fillScreen(PUMP_CLR_WHITE);
  tft.setTextWrap(false);

  float mapF;
  bool  showMap;
  if (selectedMode == 0) {
    mapF    = tftCalcMapMmHg(sys, dia);
    showMap = true;
  } else {
    // TSK / TLG: MAP mengikuti nilai terbaru dari GET web.
    mapF    = g_apiSavedMapValid ? g_apiSavedMap : 0.0f;
    showMap = g_apiSavedMapValid;
  }

  tft.fillRoundRect(2, 111, 80, 28, 8, PUMP_CLR_ORANGE);
  tft.fillRoundRect(83, 8, 46, 30, 8, PUMP_CLR_ORANGE);
  tft.fillRoundRect(83, 59, 45, 28, 8, PUMP_CLR_ORANGE);
  tft.fillRoundRect(84, 111, 44, 28, 8, PUMP_CLR_ORANGE);
  tft.fillRoundRect(1, 2, 78, 27, 8, PUMP_CLR_ORANGE);

  tft.setTextColor(PUMP_CLR_BLUE);

  char buf[16];

  tft.setFont(&FreeSansBoldOblique12pt7b);
  snprintf(buf, sizeof(buf), "%d", sys);
  tftPrintPumpingValueRight(83, 46, 30, buf);
  snprintf(buf, sizeof(buf), "%d", dia);
  tftPrintPumpingValueRight(83, 45, 80, buf);
  snprintf(buf, sizeof(buf), "%d", pulse);
  tftPrintPumpingValueRight(84, 44, 133, buf);

  (void)rot;
  tftPaintMapValue(mapF, showMap);
  tftPaintRotDtDmPanel(dia, selectedMode);

  tftPaintPatientName(2, 122, 80);

  tftPaintPumpingStatusIcons();

  tft.fillRoundRect(1, 141, 60, 18, 8, PUMP_CLR_BLUE);
  drawPetme(tft, 7, 147, "Cancel", PUMP_CLR_ORANGE);

  tft.setTextColor(PUMP_CLR_BLUE);
  tft.setFont(&FreeSansBold12pt7b);
  if (g_apiSessionValid) {
    const char* abbr = tftModeAbbrev(selectedMode);
    int16_t hx = 14;
    if (selectedMode == 1 || selectedMode == 2) {
      hx = 12;
    }
    tft.setCursor(hx, 23);
    tft.print(abbr);
  }

  tftNextY = tft.height();

  // Trigger kirim hasil ke server (jalan di TaskPairingNet — tidak blocking).
  // Untuk payload API gunakan MAP fresh dari sys/dia, bukan saved MAP
  // (saved MAP khusus untuk display TSK/TLG).
  pumpingPostMeasurementRequest(sys, dia, pulse, rot,
                                tftCalcMapMmHg(sys, dia), selectedMode);
}

// ============================================================
// Layar — MODE mem / pairing / history (Lopaka), GPIO10 menggeser panah 3 opsi (tidak ke NVS)
// Kotak oranye menu (atas) terpisah dari kotak MAC (bawah, dekat tombol Pilih/Ok).
static const int16_t kMemMenuBoxY = 28;
static const int16_t kMemMenuBoxH = 82;
static const int16_t kMemMacBoxY  = 116;
static const int16_t kMemMacBoxH  = 22;
// Tiga opsi simetris — geser sedikit ke bawah agar center di kotak menu
static const int16_t kMemMenuTextY[MEM_MENU_NUM_OPTS] = { 51, 75, 99 };
static const int16_t kMemMenuArrowY[MEM_MENU_NUM_OPTS] = { 39, 63, 87 };

static void tftPaintMemMenuArrowOnly(void) {
  for (uint8_t k = 0; k < MEM_MENU_NUM_OPTS; k++) {
    tft.fillRect(13, kMemMenuArrowY[k], 18, 15, PUMP_CLR_ORANGE);
  }
  tft.drawBitmap(13, kMemMenuArrowY[memMenuSel], image_Release_arrow_bits, 18, 15, PUMP_CLR_BLUE);
}

static void tftPaintMemMenuTitle(void) {
  tft.setFont(&FreeMonoBold9pt7b);
  tft.setTextColor(PUMP_CLR_BLUE);
  const char* title =
    (g_apiDeviceName[0] != '\0') ? g_apiDeviceName : DEVICE_NAME_DEFAULT;
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  int16_t cx = 8 + (112 - (int16_t)w) / 2;
  int16_t cy = 2 + (23 + h) / 2;
  tft.setCursor(cx, cy);
  tft.print(title);
}

// MAC address + tegangan baterai — rata tengah horizontal & vertikal dalam kotak oranye.
// Format: "8856A66D403C-2.7"  (1 desimal, 16 char × 6px = 96px dalam kotak 112px)
static void tftPaintMemMenuMacAddress(void) {
  if (g_apiDeviceId[0] == '\0') {
    apiInitDeviceId();
  }
  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(PUMP_CLR_BLUE);

  float batV = adcReadBatteryVolts();
  char combined[28];
  snprintf(combined, sizeof(combined), "%s-%.1f", g_apiDeviceId, (double)batV);

  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(combined, 0, 0, &x1, &y1, &w, &h);

  const int16_t boxX = 8;
  const int16_t boxW = 112;
  const int16_t cx = boxX + (boxW - (int16_t)w) / 2 - x1;
  const int16_t cy = kMemMacBoxY + (kMemMacBoxH - (int16_t)h) / 2 - y1;

  tft.setCursor(cx, cy);
  tft.print(combined);
}

void tftDrawMemoryOrPairingScreen(void) {
  pairingCountdownStop();
  pairingWebRequestStop();
  historySessionEnd();
  g_historyWaitScreen = false;

  showingResult         = false;
  isPumping             = false;
  showingMemPairingMenu   = true;
  memMenuSel            = MEM_MENU_MATIKAN;

  tft.fillScreen(PUMP_CLR_WHITE);
  tft.setTextWrap(false);

  tft.fillRoundRect(8, 2, 112, 23, 8, PUMP_CLR_ORANGE);
  tft.fillRoundRect(8, kMemMenuBoxY, 112, kMemMenuBoxH, 8, PUMP_CLR_ORANGE);
  tft.fillRoundRect(8, kMemMacBoxY, 112, kMemMacBoxH, 8, PUMP_CLR_ORANGE);
  tft.fillRoundRect(75, 140, 51, 18, 8, PUMP_CLR_BLUE);
  tft.fillRoundRect(2, 141, 51, 18, 8, PUMP_CLR_BLUE);

  tft.setTextColor(PUMP_CLR_BLUE);
  tft.setFont(&FreeMonoBold9pt7b);
  tftPaintMemMenuTitle();

  tft.setCursor(36, kMemMenuTextY[0]);
  tft.print("MATIKAN");
  tft.setCursor(36, kMemMenuTextY[1]);
  tft.print("PAIRING");
  tft.setCursor(36, kMemMenuTextY[2]);
  tft.print("HISTORY");

  tft.drawBitmap(13, kMemMenuArrowY[memMenuSel], image_Release_arrow_bits, 18, 15, PUMP_CLR_BLUE);

  // MAC address alat (uppercase tanpa ':') — teks kecil di bawah menu
  tftPaintMemMenuMacAddress();

  drawPetme(tft, 7, 147, "Pilih", PUMP_CLR_ORANGE);
  drawPetme(tft, 92, 147, "Ok", PUMP_CLR_ORANGE);

  tftNextY = tft.height();
}

static void tftPaintPairingCountdownSeconds(int sec) {
  const int maxSec = (int)(PAIRING_COUNTDOWN_MS / 1000u);
  if (sec < 0) {
    sec = 0;
  }
  if (sec > maxSec) {
    sec = maxSec;
  }

  if (sec == s_pairingCdLastPaintedSec) {
    return;
  }
  s_pairingCdLastPaintedSec = sec;

  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setTextWrap(false);
  /* Foreground + background: gambar ulang hanya area teks, tanpa fillRect blok. */
  tft.setTextColor(PUMP_CLR_BLUE, PUMP_CLR_ORANGE);
  char buf[12];
  snprintf(buf, sizeof(buf), "%3d d", sec);
  tft.setCursor(44, 124);
  tft.print(buf);
}

static int16_t petmeTextWidth(const char* label) {
  return (int16_t)strlen(label) * 8;
}

// Tombol bawah biru — lebar menyesuaikan panjang teks PetMe 8x8
static void tftPaintBottomBtnLeft(int16_t x, int16_t y, const char* label, uint16_t textColor) {
  const int16_t padH  = 7;
  const int16_t btnH  = 18;
  const int16_t textW = petmeTextWidth(label);
  const int16_t btnW  = textW + padH * 2;
  tft.fillRoundRect(x, y, btnW, btnH, 8, PUMP_CLR_BLUE);
  drawPetme(tft, x + (btnW - textW) / 2, y + (btnH - 8) / 2 + 1, label, textColor);
}

static void tftPaintBottomBtnRight(int16_t marginR, int16_t y, const char* label, uint16_t textColor) {
  const int16_t padH  = 7;
  const int16_t btnH  = 18;
  const int16_t textW = petmeTextWidth(label);
  const int16_t btnW  = textW + padH * 2;
  const int16_t x     = (int16_t)tft.width() - marginR - btnW;
  tft.fillRoundRect(x, y, btnW, btnH, 8, PUMP_CLR_BLUE);
  drawPetme(tft, x + (btnW - textW) / 2, y + (btnH - 8) / 2 + 1, label, textColor);
}

/* Layar hotspot pairing + countdown 60 d. */
static void tftRedrawPairingApInstructionScreen(void) {
  tft.fillScreen(PUMP_CLR_WHITE);
  tft.setTextWrap(false);

  tft.fillRoundRect(7, 94, 112, 44, 8, PUMP_CLR_ORANGE);
  tft.fillRoundRect(8, 31, 112, 60, 8, PUMP_CLR_ORANGE);
  tft.fillRoundRect(8, 2, 112, 23, 8, PUMP_CLR_ORANGE);
  tftPaintBottomBtnLeft(2, 141, "Mem", PUMP_CLR_ORANGE);
  tftPaintBottomBtnRight(2, 141, "Pump", PUMP_CLR_ORANGE);

  tft.setTextColor(PUMP_CLR_BLUE);
  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setCursor(17, 34);
  tft.print("Silahkan masukan");

  drawPetme(tft, 20, 80, "192.168.4.1", PUMP_CLR_BLUE);

  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setCursor(32, 45);
  tft.print("SSID & PASS");
  tft.setCursor(50, 62);
  tft.print(PAIRING_AP_SSID);

  tft.drawBitmap(27, 57, image_wifi_full_bits, 19, 16, PUMP_CLR_BLUE);

  tft.setCursor(22, 100);
  tft.print("Alat Akan Mati");

  tft.setFont(&FreeMonoBold9pt7b);
  tft.setTextColor(PUMP_CLR_BLUE);
  tft.setCursor(26, 18);
  tft.print("PAIRING");

  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(PUMP_CLR_BLUE);
  tft.setCursor(31, 111);
  tft.print("Dalam Waktu");

  tftPaintPairingCountdownSeconds((int)(PAIRING_COUNTDOWN_MS / 1000u));

  tftNextY = tft.height();
}

void tftDrawPairingReadyScreen(void) {
  showingResult         = false;
  showingMemPairingMenu = false;
  g_historyWaitScreen   = false;

  s_pairingCdLastPaintedSec = -1;

  tftRedrawPairingApInstructionScreen();

  pairingWebRequestStart();
  pairingCountdownStart();
}

// ============================================================
// Layar OTA — "Alat sedang download program"
// Ditampilkan saat tombol Update Firmware ditekan + selama unduh berlangsung.
// Dipanggil via CMD:OTA_DOWNLOADING dari TaskPairingNet/route /updateServer.
// ============================================================
void tftDrawOtaDownloadingScreen(void) {
  pairingCountdownStop();
  pairingWebRequestStop();
  historySessionEnd();
  g_historyWaitScreen   = false;
  showingResult         = false;
  showingMemPairingMenu = false;
  isPumping             = false;

  tft.fillScreen(PUMP_CLR_WHITE);
  tft.setTextWrap(false);

  // Header oranye — judul rata tengah
  tft.fillRoundRect(8, 2, 112, 23, 8, PUMP_CLR_ORANGE);
  tft.setFont(&FreeSansBold9pt7b);
  tft.setTextColor(PUMP_CLR_BLUE);
  tftBootPrintCentered("OTA", 18);

  // Panel konten oranye
  tft.fillRoundRect(8, 31, 112, 104, 8, PUMP_CLR_ORANGE);
  tft.drawBitmap(54, 40, image_wifi_full_bits, 19, 16, PUMP_CLR_BLUE);

  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(PUMP_CLR_BLUE);
  tftBootPrintCentered("Alat sedang", 68);
  tftBootPrintCentered("download program", 80);
  tftBootPrintCentered("Mohon jangan", 100);
  tftBootPrintCentered("matikan perangkat", 112);

  // Footer tombol biru — rata tengah
  {
    const char* label = "OTA UPDATE";
    const int16_t padH  = 7;
    const int16_t btnH  = 18;
    const int16_t textW = petmeTextWidth(label);
    const int16_t btnW  = textW + padH * 2;
    const int16_t btnX  = (int16_t)((tft.width() - btnW) / 2);
    const int16_t btnY  = 141;
    tft.fillRoundRect(btnX, btnY, btnW, btnH, 8, PUMP_CLR_BLUE);
    drawPetme(tft, btnX + (btnW - textW) / 2, btnY + (btnH - 8) / 2 + 1, label, PUMP_CLR_ORANGE);
  }

  tftNextY = tft.height();
}

void tftDrawHistoryWaitScreen(void) {
  pairingCountdownStop();
  pairingWebRequestStop();
  historySessionEnd();

  showingResult         = false;
  showingMemPairingMenu = false;
  g_historyWaitScreen   = true;
  tftMemModeCapture     = true;
  memSys = memDia = memPulse = memMap = 0;
  memTimestamp[0]       = '\0';
  memRecIndex           = 0;
  historySessionBegin();

  tft.fillScreen(PUMP_CLR_WHITE);
  tft.setTextWrap(false);

  tft.fillRoundRect(8, 31, 112, 104, 8, PUMP_CLR_ORANGE);
  tft.fillRoundRect(8, 2, 112, 23, 8, PUMP_CLR_ORANGE);
  tft.fillRoundRect(2, 141, 57, 18, 8, PUMP_CLR_BLUE);

  drawPetme(tft, 7, 147, "MEMORY", PUMP_CLR_ORANGE);

  tft.setTextColor(PUMP_CLR_BLUE);
  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setCursor(38, 46);
  tft.print("SILAHKAN");
  tft.setCursor(25, 60);
  tft.print("TEKAN KEMBALI");

  tft.setFont(&FreeMonoBold9pt7b);
  tft.setCursor(31, 92);
  tft.print("MEMORY");

  tft.drawBitmap(22, 115, image_ArrowDownFilled_bits, 14, 15, PUMP_CLR_BLUE);

  tft.setCursor(26, 18);
  tft.print("HISTORY");

  tftNextY = tft.height();
  powerLockUpdate();
}

// ============================================================
// Layar error pengukuran (desain Lopaka — end test err != 0)
// ============================================================
void tftDrawMeasurementErrorScreen(int errCode) {
  showingResult         = false;
  showingMemPairingMenu = false;
  g_historyWaitScreen   = false;

  tft.fillScreen(PUMP_CLR_WHITE);
  tft.setTextWrap(false);

  tft.fillRoundRect(8, 31, 112, 104, 8, PUMP_CLR_ORANGE);
  tft.fillRoundRect(8, 2, 112, 23, 8, PUMP_CLR_ORANGE);
  tft.fillRoundRect(90, 141, 34, 18, 8, PUMP_CLR_BLUE);
  tft.fillRoundRect(5, 141, 54, 18, 8, PUMP_CLR_BLUE);

  drawPetme(tft, 8, 148, "MEMORY", PUMP_CLR_ORANGE);
  drawPetme(tft, 95, 148, "OFF", PUMP_CLR_ORANGE);

  tft.setTextColor(PUMP_CLR_BLUE);
  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setCursor(38, 43);
  tft.print("SILAHKAN");
  tft.setCursor(28, 55);
  tft.print("COBA KEMBALI");

  char buf[16];
  tft.setFont(&FreeMonoBold9pt7b);
  snprintf(buf, sizeof(buf), "ERROR %d", errCode);
  tft.setCursor(25, 92);
  tft.print(buf);

  tft.drawBitmap(96, 117, image_ArrowDownFilled_bits, 14, 15, PUMP_CLR_BLUE);

  tft.setFont(&FreeMonoBold9pt7b);
  tft.setCursor(20, 18);
  tft.print("MENGUKUR");

  tftNextY = tft.height();
}

// ============================================================
// Layar 4 — Riwayat memori (desain Lopaka drawScreen_3: putih/oranye/biru)
// ============================================================
static void tftPaintMemoryWifiIcon(bool show) {
  tft.fillRect(67, 143, 19, 16, PUMP_CLR_WHITE);
  if (show) {
    tft.drawBitmap(67, 143, image_wifi_full_bits, 19, 16, PUMP_CLR_BLUE);
  }
}

void tftShowMemoryResult(int sys, int dia, int pulse, int map, const char* ts) {
  (void)map;
  (void)ts;

  g_historyWaitScreen = false;
  powerLockUpdate();

  showingResult         = true;
  showingMemPairingMenu = false;
  tft.fillScreen(PUMP_CLR_WHITE);
  tft.setTextWrap(false);

  tftPaintMapValue(0.0f, false);
  tft.fillRoundRect(1, 68, 79, 41, 8, PUMP_CLR_ORANGE);
  tft.fillRoundRect(2, 111, 80, 28, 8, PUMP_CLR_ORANGE);
  tft.fillRoundRect(83, 8, 44, 30, 8, PUMP_CLR_ORANGE);
  tft.fillRoundRect(83, 59, 44, 28, 8, PUMP_CLR_ORANGE);
  tft.fillRoundRect(84, 111, 43, 28, 8, PUMP_CLR_ORANGE);
  tft.fillRoundRect(1, 2, 78, 27, 8, PUMP_CLR_ORANGE);

  tft.setTextColor(PUMP_CLR_BLUE);

  char buf[16];

  tft.setFont(&FreeSansBoldOblique12pt7b);
  snprintf(buf, sizeof(buf), "%d", sys);
  tft.setCursor(83, 30);
  tft.print(buf);
  snprintf(buf, sizeof(buf), "%d", dia);
  tft.setCursor(83, 80);
  tft.print(buf);
  snprintf(buf, sizeof(buf), "%d", pulse);
  tft.setCursor(83, 133);
  tft.print(buf);

  snprintf(buf, sizeof(buf), "%d", 0);
  tft.setCursor(0, 101);
  tft.print(buf);

  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(PUMP_CLR_BLUE);
  tft.setCursor(11, 73);
  tft.print("ROT");
  tft.setCursor(41, 81);
  tft.print("DT:0");
  tft.setCursor(41, 93);
  tft.print("DM:0");

  tft.setCursor(17, 122);
  snprintf(buf, sizeof(buf), "MEMORI %d", memRecIndex);
  tft.print(buf);

  tftPaintMemoryWifiIcon(g_historyWifiConnected);

  tft.fillRoundRect(1, 141, 60, 18, 8, PUMP_CLR_BLUE);
  drawPetme(tft, 14, 148, "NEXT", PUMP_CLR_ORANGE);

  tft.fillRoundRect(93, 141, 34, 18, 8, PUMP_CLR_BLUE);
  drawPetme(tft, 99, 148, "OFF", PUMP_CLR_ORANGE);

  tft.setTextColor(PUMP_CLR_BLUE);
  tft.setFont(&FreeSansBold12pt7b);
  tft.setCursor(12, 23);
  tft.print("MEM");

  if (memRecIndex == 1) {
    historyWifiNtpSyncRequest();
  }

  tftNextY = tft.height();
}

// ============================================================
// Helper lama — tetap ada untuk kompatibilitas kode lain
// ============================================================
void tftDrawBatteryLine(float vCal) {
  int mVcal = (int)(vCal * 1000.0f + 0.5f);
  char buf[40];
  tft.setTextColor(ST77XX_YELLOW);
  tft.setTextSize(1);
  tft.setTextWrap(false);
  tft.setCursor(0, tftNextY);
  snprintf(buf, sizeof(buf), "Baterai %.2f V (%d mV)", vCal, mVcal);
  tft.print(buf);
  tftNextY += TFT_LINE_H;
}

void tftResetDisplay() {
  g_historyWaitScreen = false;
  historySessionEnd();
  pairingCountdownStop();
  pairingWebRequestStop();

  tft.fillScreen(ST77XX_BLACK);
  tftNextY              = 0;
  tftLogHead            = 0;
  tftLogCount           = 0;
  tftMemModeCapture     = false;
  tftResultCapture      = false;
  showingMemPairingMenu = false;
}

static void tftPaintStrip(int16_t y, const char* s) {
  tft.fillRect(0, y, tft.width(), TFT_LINE_H, ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(1);
  tft.setTextWrap(false);
  tft.setCursor(0, y);
  tft.print(s);
}

static void tftReflowFromBuffer() {
  tft.fillScreen(ST77XX_BLACK);
  tftNextY = 0;
  const int16_t maxFit = tft.height() / TFT_LINE_H;
  uint8_t n   = (tftLogCount > maxFit) ? (uint8_t)maxFit : tftLogCount;
  uint8_t idx = (tftLogHead + TFT_LOG_LINES - n) % TFT_LOG_LINES;

  tft.setFont(NULL);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(1);
  tft.setTextWrap(false);
  for (uint8_t k = 0; k < n; k++) {
    tft.setCursor(0, tftNextY);
    tft.print(tftLog[idx]);
    tftNextY += TFT_LINE_H;
    idx = (idx + 1) % TFT_LOG_LINES;
  }
}

void tftAppendLine(const char* s) {
  if (!s) return;
  strncpy(tftLog[tftLogHead], s, TFT_LOG_COLS - 1);
  tftLog[tftLogHead][TFT_LOG_COLS - 1] = '\0';
  tftLogHead = (tftLogHead + 1) % TFT_LOG_LINES;
  if (tftLogCount < TFT_LOG_LINES) tftLogCount++;

  if (tftNextY + TFT_LINE_H > tft.height()) {
    tftReflowFromBuffer();
    return;
  }
  tft.setFont(NULL);
  tftPaintStrip(tftNextY, s);
  tftNextY += TFT_LINE_H;
}

// ============================================================
// Update ringan setelah tombol — menu MODE mem: panah bitmap; pumping: posisi; idle: panah 7×5
// ============================================================
void tftUpdateSelectorArrow() {
  if (showingMemPairingMenu) {
    tftPaintMemMenuArrowOnly();
    return;
  }
  if (isPumping) {
    tftPaintPumpingPositionCenter();
  }
}

// ============================================================
// Status koneksi WiFi — area tengah + countdown (y 31–138)
// Dipanggil saat pairing screen aktif setelah form disubmit.
// ============================================================
static void tftPaintWifiConnAttempt(int attempt);
static void tftPaintWifiConnElapsedSec(int secElapsed);

static void tftPaintWifiConnecting(void) {
  tft.fillRoundRect(7, 31, 114, 107, 8, PUMP_CLR_ORANGE);
  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(PUMP_CLR_BLUE);
  tft.drawBitmap(55, 45, image_wifi_full_bits, 19, 16, PUMP_CLR_BLUE);
  tft.setCursor(20, 70);
  tft.print("Menghubungkan ke");
  tft.setCursor(36, 82);
  tft.print("WiFi...");

  s_wifiConnLastAttempt = -1;
  s_wifiConnLastElapsedSec = -1;
  tftPaintWifiConnAttempt(1);
  tftPaintWifiConnElapsedSec(0);
}

static void tftPaintWifiConnAttempt(int attempt) {
  if (attempt < 1) {
    attempt = 1;
  }
  int maxAttempt = (int)WIFI_CONNECT_ATTEMPTS;
  if (attempt > maxAttempt) {
    attempt = maxAttempt;
  }
  if (attempt == s_wifiConnLastAttempt) {
    return;
  }
  s_wifiConnLastAttempt = (int16_t)attempt;

  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setTextWrap(false);
  tft.setTextColor(PUMP_CLR_BLUE, PUMP_CLR_ORANGE);
  char buf[20];
  snprintf(buf, sizeof(buf), "Percobaan %d/%d", attempt, maxAttempt);
  tft.setCursor(14, 100);
  tft.print(buf);
}

static void tftPaintWifiConnElapsedSec(int secElapsed) {
  if (secElapsed < 0) {
    secElapsed = 0;
  }
  int maxSec = (int)(WIFI_CONNECT_TIMEOUT_MS / 1000u);
  if (secElapsed > maxSec) {
    secElapsed = maxSec;
  }
  if (secElapsed == s_wifiConnLastElapsedSec) {
    return;
  }
  s_wifiConnLastElapsedSec = (int16_t)secElapsed;

  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setTextWrap(false);
  tft.setTextColor(PUMP_CLR_BLUE, PUMP_CLR_ORANGE);
  char buf[20];
  snprintf(buf, sizeof(buf), "%d / %d d", secElapsed, maxSec);
  tft.setCursor(28, 112);
  tft.print(buf);
}

static void tftPaintWifiConnected(const char* ip) {
  tft.fillRoundRect(7, 31, 114, 107, 8, PUMP_CLR_ORANGE);
  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(PUMP_CLR_BLUE);
  tft.setCursor(46, 44);
  tft.print("WiFi OK!");
  tft.setCursor(36, 60);
  tft.print("Terhubung!");
  tft.setCursor(27, 76);
  tft.print("Alamat IP:");
  tft.setCursor(17, 90);
  tft.setTextSize(1);
  tft.print(ip);
}

static void tftPaintWifiFailed(void) {
  tft.fillRoundRect(7, 31, 114, 107, 8, PUMP_CLR_ORANGE);
  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(PUMP_CLR_BLUE);
  tft.setCursor(27, 48);
  tft.print("WiFi Gagal!");
  tft.setCursor(16, 65);
  tft.print("Periksa SSID dan");
  tft.setCursor(22, 77);
  tft.print("password Anda.");
  tft.setCursor(10, 94);
  tft.print("Buka 192.168.4.1");
  tft.setCursor(21, 106);
  tft.print("untuk coba lagi.");
}

// ============================================================
// Filter baris serial dari tensimeter → aksi layar
// Dipanggil dari TaskTft (konteks aman untuk SPI/TFT).
// ============================================================
void tftFilterSerialLine(const char* line) {
  if (!line || !line[0]) return;

  // Perintah internal: tombol ditekan — menu mem: panah; pumping: blok posisi; idle: panah hitam
  if (strcmp(line, "CMD:ARROW_ONLY") == 0) {
    tftUpdateSelectorArrow();
    return;
  }

  if (strcmp(line, "CMD:CLOCK_TICK") == 0) {
    if (isPumping && !showingResult && !s_bootScreenActive) {
      tftPaintPumpingDateTime();
    }
    return;
  }

  if (strncmp(line, "CMD:HISTORY_WIFI ", 17) == 0) {
    const bool show = (line[17] == '1');
    g_historyWifiConnected = show;
    if (showingResult && tftMemModeCapture) {
      tftPaintMemoryWifiIcon(show);
    }
    return;
  }

  if (strncmp(line, "CMD:PUMP_WIFI ", 14) == 0) {
    g_pumpingWifiConnected = (line[14] == '1');
    if (!s_bootScreenActive) {
      if (isPumping && !showingResult) {
        tftPaintPumpingWifiIcon(g_pumpingWifiConnected);
      } else if (showingResult && !tftMemModeCapture) {
        tftPaintPumpingWifiIcon(g_pumpingWifiConnected);
      }
    }
    return;
  }

  if (strncmp(line, "CMD:PUMP_INTERNET ", 18) == 0) {
    g_pumpingInternetOk = (line[18] == '1');
    if (!s_bootScreenActive) {
      if (isPumping && !showingResult) {
        tftPaintPumpingInternetIcon(g_pumpingInternetOk);
      } else if (showingResult && !tftMemModeCapture) {
        tftPaintPumpingInternetIcon(g_pumpingInternetOk);
      }
    }
    return;
  }

  if (strncmp(line, "CMD:PAIRING_TICK ", 17) == 0) {
    int s = atoi(line + 17);
    tftPaintPairingCountdownSeconds(s);
    return;
  }

  if (strcmp(line, "CMD:PAIRING_TIMEOUT") == 0 ||
      strcmp(line, "CMD:PAIRING_PREFLIGHT_SLEEP") == 0) {
    pairingWebRequestStop();
    pairingCountdownStop();
    tft.fillScreen(ST77XX_BLACK);
    tftNextY = tft.height();
    return;
  }

  if (strcmp(line, "CMD:PAIRING_AP_READY") == 0) {
    s_pairingCdLastPaintedSec = -1;
    tftRedrawPairingApInstructionScreen();
    return;
  }

  if (strcmp(line, "CMD:OTA_DOWNLOADING") == 0) {
    tftDrawOtaDownloadingScreen();
    return;
  }

  // Perintah internal: full redraw selector (dipicu perubahan state pumping, dll.)
  if (strcmp(line, "CMD:SELECTOR_REDRAW") == 0) {
    tftDrawSelectorScreen(isPumping);
    return;
  }

  // Status koneksi WiFi setelah form SSID/password disubmit
  if (strcmp(line, "CMD:WIFI_CONNECTING") == 0) {
    tftPaintWifiConnecting();
    return;
  }

  if (strncmp(line, "CMD:WIFI_CONN_ATTEMPT ", 22) == 0) {
    int a = atoi(line + 22);
    tftPaintWifiConnAttempt(a);
    s_wifiConnLastElapsedSec = -1;
    tftPaintWifiConnElapsedSec(0);
    return;
  }

  if (strncmp(line, "CMD:WIFI_CONN_TICK ", 19) == 0) {
    int s = atoi(line + 19);
    tftPaintWifiConnElapsedSec(s);
    return;
  }

  if (strncmp(line, "CMD:WIFI_OK ", 12) == 0) {
    tftPaintWifiConnected(line + 12);
    return;
  }

  if (strcmp(line, "CMD:WIFI_FAIL") == 0) {
    tftPaintWifiFailed();
    return;
  }

  if (strcmp(line, "CMD:API_POST_OK") == 0) {
    if (showingResult && !tftMemModeCapture && !g_postCanceled) {
      tftPaintApiResultIcon(true);
    }
    return;
  }

  if (strcmp(line, "CMD:API_POST_FAIL") == 0) {
    if (showingResult && !tftMemModeCapture && !g_postCanceled) {
      tftPaintApiResultIcon(false);
    }
    return;
  }

  if (strcmp(line, "CMD:POST_CANCELED") == 0) {
    if (showingResult && !tftMemModeCapture && !g_postSucceeded) {
      tftPaintApiResultIcon(false);
    }
    return;
  }

  // Power down: submenu MODE mem — MATIKAN tetap mati; PAIRING/HISTORY layar khusus
  if (strstr(line, "MSG_POWER_DOWN")) {
    historySessionEnd();
    isPumping = false;
    if (showingMemPairingMenu) {
      if (memMenuSel == MEM_MENU_MATIKAN) {
        tftDrawSelectorScreen(false);
        return;
      }
      if (memMenuSel == MEM_MENU_PAIRING) {
        tftDrawPairingReadyScreen();
        return;
      }
      if (memMenuSel == MEM_MENU_HISTORY) {
        tftDrawHistoryWaitScreen();
        return;
      }
    }
    tftDrawSelectorScreen(false);
    return;
  }

  if (strstr(line, "bpt mode ,0")) {
    isPumping = false;
    tftDrawSelectorScreen(false);
    return;
  }

  // Mode memori — tangkap field; tampilkan layar MODE jika belum di menu ini (juga saat UART pertama)
  if (strstr(line, "mem mode")) {
    if (isPumping || tftResultCapture || (showingResult && !tftMemModeCapture)) {
      Serial.println(F("[TFT] mem mode diabaikan - sedang pumping/hasil"));
      if (showingResult && !tftMemModeCapture && !g_postSucceeded) {
        tftPaintApiResultIcon(false);
      }
      return;
    }
    tftMemModeCapture = true;
    memSys = memDia = memPulse = memMap = 0;
    memTimestamp[0] = '\0';
    if (!showingMemPairingMenu && !g_historyWaitScreen) {
      tftDrawMemoryOrPairingScreen();
    }
    return;
  }

  // Tangkap field riwayat memori; render kartu hasil saat record_time diterima.
  // tftMemModeCapture TIDAK di-reset di sini — tetap aktif untuk record berikutnya.
  // Reset hanya terjadi saat start_test / power_down di bagian atas fungsi ini.
  if (tftMemModeCapture) {
    const char* p;
    // Awal record baru → reset akumulator
    if (strstr(line, "read Rec result:")) {
      memSys = memDia = memPulse = memMap = 0;
      memTimestamp[0] = '\0';
      memRecIndex++;
      return;
    }
    if ((p = strstr(line, "sys =")))   { memSys   = atoi(p + 5); return; }
    if ((p = strstr(line, "dia =")))   { memDia   = atoi(p + 5); return; }
    if ((p = strstr(line, "pulse ="))) { memPulse = atoi(p + 7); return; }
    if ((p = strstr(line, "mean ="))) {
      int v = atoi(p + 6);
      memMap = (v != 0) ? v : (memSys + 2 * memDia) / 3;
      return;
    }
    if ((p = strstr(line, "record_time:"))) {
      p += strlen("record_time:");
      while (*p == ' ') p++;
      strncpy(memTimestamp, p, sizeof(memTimestamp) - 1);
      memTimestamp[sizeof(memTimestamp) - 1] = '\0';
      if (memMap == 0) memMap = (memSys + 2 * memDia) / 3;
      tftShowMemoryResult(memSys, memDia, memPulse, memMap, memTimestamp);
      return;
    }
    return;
  }

  // Tangkap hasil setelah pengukuran sukses (err:0)
  if (tftResultCapture) {
    #define ISHEX(c) (((c)>='0'&&(c)<='9')||((c)>='A'&&(c)<='F')||((c)>='a'&&(c)<='f'))
    #define HEXVAL(c) ((c)>='A' ? ((c)>='a' ? (c)-'a'+10 : (c)-'A'+10) : (c)-'0')

    // Paket hex: "SY DI MA PU ..." — byte 0=SYS, 1=DIA, 2=ROT(byte3 paket), 3=PULSE
    if (strlen(line) >= 11 &&
        ISHEX(line[0]) && ISHEX(line[1]) && line[2] == ' ' &&
        ISHEX(line[3]) && ISHEX(line[4]) && line[5] == ' ' &&
        ISHEX(line[6]) && ISHEX(line[7]) && line[8] == ' ' &&
        ISHEX(line[9]) && ISHEX(line[10])) {
      resultSys   = (HEXVAL(line[0]) << 4) | HEXVAL(line[1]);
      resultDia   = (HEXVAL(line[3]) << 4) | HEXVAL(line[4]);
      resultRot   = (HEXVAL(line[6]) << 4) | HEXVAL(line[7]);
      resultPulse = (HEXVAL(line[9]) << 4) | HEXVAL(line[10]);
      tftResultCapture = false;
      char ts[28] = {0};
      tftShowResultNew(resultSys, resultDia, resultPulse, 0, resultRot, ts);
      return;
    }

    #undef ISHEX
    #undef HEXVAL

    // Fallback teks "SYS xxx", "DIA xxx", "PULSE xxx"
    const char* p;
    if ((p = strstr(line, "SYS "))   && *(p+4) >= '0' && *(p+4) <= '9') { resultSys   = atoi(p+4); return; }
    if ((p = strstr(line, "DIA "))   && *(p+4) >= '0' && *(p+4) <= '9') { resultDia   = atoi(p+4); return; }
    if ((p = strstr(line, "PULSE ")) && *(p+6) >= '0' && *(p+6) <= '9') {
      resultPulse = atoi(p+6);
      tftResultCapture = false;
      char ts[28] = {0};
      tftShowResultNew(resultSys, resultDia, resultPulse, 0, resultRot, ts);
      return;
    }
    if (strstr(line, "measuring process happy ending")) {
      tftResultCapture = false;
      char ts[28] = {0};
      tftShowResultNew(resultSys, resultDia, resultPulse, 0, resultRot, ts);
      return;
    }
    return;
  }

  // Boot-connect screen — update bertahap saat WiFi/NTP/GET berjalan
  if (strncmp(line, "CMD:BOOT_SSID ", 14) == 0) {
    tftBootPaintSsid(line + 14);
    return;
  }
  if (strcmp(line, "CMD:BOOT_WIFI_OK") == 0) {
    tftBootPaintWifiResult(true);
    return;
  }
  if (strcmp(line, "CMD:BOOT_WIFI_FAIL") == 0) {
    tftBootPaintWifiResult(false);
    return;
  }
  if (strcmp(line, "CMD:BOOT_GETTING") == 0) {
    tftBootPaintGetStatus();
    return;
  }
  if (strcmp(line, "CMD:BOOT_GET_OK") == 0) {
    tftBootPaintGetResult(true);
    return;
  }
  if (strncmp(line, "CMD:BOOT_GET_FAIL", 17) == 0) {
    // Format: "CMD:BOOT_GET_FAIL:{code}" — parse kode jika ada
    int httpCode = 0;
    if (line[17] == ':') {
      httpCode = atoi(line + 18);
    }
    tftBootPaintGetResult(false, httpCode);
    return;
  }
  if (strcmp(line, "CMD:BOOT_DONE") == 0) {
    s_bootScreenActive = false;
    tftDrawSelectorScreen(true);
    // Gambar jam langsung setelah posisi — keduanya tampil bersamaan, tidak ada jeda
    if (g_ntpSyncAttempted) {
      tftPaintPumpingDateTime();
    }
    return;
  }

  // Tombol start ditekan → mulai pumping, tampilkan boot-connect screen lalu selector
  if (strstr(line, "start test") || strstr(line, "startBPTest")) {
    historySessionEnd();
    isPumping         = true;
    tftMemModeCapture = false;
    tftResultCapture  = false;
    g_historyWaitScreen = false;
    pairingCountdownStop();
    pairingWebRequestStop();
    pumpingSessionBegin();
    tftDrawBootConnectScreen();
    pumpingWifiNtpSyncRequest();
    return;
  }

  // Akhir tes — cek kode error
  static const char kErrTag[] = "end test,err:";
  const char* errPos = strstr(line, kErrTag);
  if (errPos) {
    isPumping         = false;
    tftMemModeCapture = false;
    int errCode = atoi(errPos + sizeof(kErrTag) - 1);
    if (errCode == 0) {
      pumpingMeasurementResultBegin();
      tftResultCapture = true;
      resultSys = resultDia = resultPulse = resultMap = resultRot = 0;
    } else {
      tftResultCapture = false;
      tftDrawMeasurementErrorScreen(errCode);
    }
  }
}
