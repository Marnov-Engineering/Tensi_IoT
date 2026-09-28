/*
  RTC.ino — RTC internal ESP32 (via time.h / SNTP)
  DS3231 dihapus; waktu di-set oleh NTP saat pumpingWifiNtpSyncImpl.

  Sebelum NTP berhasil  : rtcIsTimeValid() = false, cache = "--/--/--" "--:--:--"
  Setelah NTP berhasil  : rtcIsTimeValid() = true,  cache = tanggal & jam WIB aktual
*/

#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include "config.h"

static char s_rtcDateBuf[12] = "--/--/--";
static char s_rtcTimeBuf[12] = "--:--:--";

//--------------------------------------------------------------------
// rtcIsOk — internal RTC selalu tersedia
//--------------------------------------------------------------------
bool rtcIsOk(void) {
  return true;
}

//--------------------------------------------------------------------
// rtcIsTimeValid — periksa apakah time(NULL) sudah di-set oleh NTP
//   Sebelum NTP: time(NULL) ~ 0 → tahun 1970 → tidak valid
//   Setelah NTP: tahun 2026+ → valid
//--------------------------------------------------------------------
bool rtcIsTimeValid(void) {
  time_t now = time(NULL);
  if (now < 1000000L) return false;   // nilainya terlalu kecil, pasti belum di-set
  struct tm ti;
  localtime_r(&now, &ti);
  const int y = ti.tm_year + 1900;
  return (y >= NTP_VALID_YEAR_MIN && y <= NTP_VALID_YEAR_MAX);
}

//--------------------------------------------------------------------
// rtcRefreshDisplayCache — perbarui string tanggal/jam
//   Jika belum valid, isi dengan placeholder
//--------------------------------------------------------------------
void rtcRefreshDisplayCache(void) {
  if (!rtcIsTimeValid()) {
    strncpy(s_rtcDateBuf, "--/--/--", sizeof(s_rtcDateBuf));
    strncpy(s_rtcTimeBuf, "--:--:--", sizeof(s_rtcTimeBuf));
    return;
  }
  time_t now = time(NULL);
  struct tm ti;
  localtime_r(&now, &ti);
  snprintf(s_rtcDateBuf, sizeof(s_rtcDateBuf), "%02d/%02d/%02d",
           ti.tm_mday, ti.tm_mon + 1, ti.tm_year % 100);
  snprintf(s_rtcTimeBuf, sizeof(s_rtcTimeBuf), "%02d:%02d:%02d",
           ti.tm_hour, ti.tm_min, ti.tm_sec);
}

const char* rtcGetDisplayDate(void) { return s_rtcDateBuf; }
const char* rtcGetDisplayTime(void) { return s_rtcTimeBuf; }

//--------------------------------------------------------------------
// rtcInit — tidak ada hardware I2C; set cache ke placeholder, return true
//--------------------------------------------------------------------
bool rtcInit(void) {
  strncpy(s_rtcDateBuf, "--/--/--", sizeof(s_rtcDateBuf));
  strncpy(s_rtcTimeBuf, "--:--:--", sizeof(s_rtcTimeBuf));
  return true;
}

//--------------------------------------------------------------------
// rtcCurrentSecond — detik dari system clock (berubah tiap 1 detik)
//   Sebelum NTP: time(NULL) ~ 0 → % 60 = 0, tidak berubah → TaskRtc diam
//   Setelah NTP: berubah setiap detik → TaskRtc kirim CMD:CLOCK_TICK
//--------------------------------------------------------------------
int rtcCurrentSecond(void) {
  return (int)(time(NULL) % 60);
}

//--------------------------------------------------------------------
// rtcDriftSecondsFromTm — selisih antara struct tm dari NTP vs time(NULL)
//   Dipakai untuk log drift saat NTP sync (bukan update DS3231 lagi)
//--------------------------------------------------------------------
uint32_t rtcDriftSecondsFromTm(const struct tm* ti) {
  if (!ti) return UINT32_MAX;
  // Karena configTime() sudah mengatur system clock dari NTP,
  // drift antara ti (NTP) dan time(NULL) (system) mendekati nol.
  time_t sysEpoch = time(NULL);
  // Estimasi NTP epoch: jam + menit + detik ke detik-dalam-hari saja
  // (mktime tanpa TZ yang benar bisa meleset; pakai selisih jam:menit:detik)
  struct tm tiLocal = *ti;
  time_t ntpEpoch = mktime(&tiLocal);
  int32_t diff = (int32_t)(ntpEpoch - sysEpoch);
  return (uint32_t)((diff < 0) ? -diff : diff);
}

//--------------------------------------------------------------------
// rtcSetFromTm — dengan internal RTC, configTime()+getLocalTime() sudah
//   mengatur system clock. Fungsi ini menjadi cadangan eksplisit via
//   settimeofday() — berguna jika NTP tidak dipakai via configTime.
//--------------------------------------------------------------------
bool rtcSetFromTm(const struct tm* ti) {
  if (!ti) return false;
  struct tm t = *ti;
  time_t epoch = mktime(&t);
  if (epoch < 0) return false;
  struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
  bool ok = (settimeofday(&tv, NULL) == 0);
  if (ok) {
    rtcRefreshDisplayCache();
  }
  return ok;
}
