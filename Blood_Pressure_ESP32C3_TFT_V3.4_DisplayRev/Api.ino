/*
  Api.ino — HTTP API client
  1. apiFetchActiveCommand : GET /deviceApi/{deviceId}
  2. apiPostMeasurement    : POST /deviceApi/{deviceId}

  - WiFi sudah terhubung saat fungsi ini dipanggil.
  - Parser JSON sederhana (flat JSON + Firestore REST).
*/

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <climits>
#include <string.h>
#include "config.h"

extern volatile bool g_pumpingWifiConnected;
extern volatile uint8_t selectedMode;
void onSessionIdChanged(const char* newSessionId);
void saveDeviceNameToPrefs(const char* name);
bool tftTryEnqueueLine(const char* line);

//========= Globals sesi aktif (terisi setelah GET sukses) =========//
char g_apiDeviceId[DEVICE_ID_LEN]            = "";
char g_apiSessionId[SESSION_ID_MAX_LEN]      = "";
char g_apiPatientId[PATIENT_ID_MAX_LEN]      = "";
char g_apiPatientName[PATIENT_NAME_MAX_LEN]  = "";
char g_apiDeviceName[DEVICE_NAME_MAX_LEN]    = DEVICE_NAME_DEFAULT;
char g_apiSessionStatus[16]                  = "";  // "active" / "" dll
char g_apiCurrentPosition[20]                = "duduk";
int  g_apiLastGetHttpCode                    = 0;    // kode HTTP terakhir dari GET
float g_apiSavedMap                          = 0.0f;
bool g_apiSavedMapValid                      = false;
int g_apiDbpMiringKiri                       = 0;
bool g_apiDbpMiringKiriValid                 = false;
volatile bool g_apiSessionValid              = false;

void apiInitDeviceId(void) {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(g_apiDeviceId, sizeof(g_apiDeviceId),
           "%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.print(F("[api] deviceId="));
  Serial.println(g_apiDeviceId);
}

//========= Parser JSON =========//
// Ambil nilai string setelah "key": "..."  (flat JSON)
static bool jsonExtractString(const char* json, const char* key,
                              char* outBuf, size_t outBufSize) {
  if (!json || !key || !outBuf || outBufSize == 0) return false;
  outBuf[0] = '\0';
  char pattern[80];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char* k = strstr(json, pattern);
  if (!k) return false;
  k += strlen(pattern);
  while (*k && (*k == ' ' || *k == ':' || *k == '\t')) k++;
  if (*k != '\"') return false;
  k++;
  size_t i = 0;
  while (*k && *k != '\"' && i + 1 < outBufSize) outBuf[i++] = *k++;
  outBuf[i] = '\0';
  return (i > 0);
}

static bool jsonExtractBool(const char* json, const char* key, bool* outVal) {
  if (!json || !key || !outVal) return false;
  char pattern[80];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char* k = strstr(json, pattern);
  if (!k) return false;
  k += strlen(pattern);
  while (*k && (*k == ' ' || *k == ':' || *k == '\t')) k++;
  if (strncmp(k, "true",  4) == 0) { *outVal = true;  return true; }
  if (strncmp(k, "false", 5) == 0) { *outVal = false; return true; }
  return false;
}

static bool jsonExtractFloat(const char* json, const char* key, float* outVal) {
  if (!json || !key || !outVal) return false;
  char pattern[80];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char* k = strstr(json, pattern);
  if (!k) return false;
  k += strlen(pattern);
  while (*k && (*k == ' ' || *k == ':' || *k == '\t')) k++;
  char* endPtr = nullptr;
  double v = strtod(k, &endPtr);
  if (endPtr == k) return false;
  *outVal = (float)v;
  return true;
}

// Cek field Firestore bernilai null: "fieldName": { "nullValue": null }
static bool jsonExtractFirestoreIsNull(const char* json, const char* fieldName) {
  if (!json || !fieldName) return false;
  char pattern[80];
  snprintf(pattern, sizeof(pattern), "\"%s\"", fieldName);
  const char* k = strstr(json, pattern);
  if (!k) return false;
  k += strlen(pattern);
  const char* nv = strstr(k, "\"nullValue\"");
  return (nv && (nv - k) <= 200);
}

// Ambil nilai dari format Firestore REST:
// "fieldName": { "stringValue": "nilai" }
static bool jsonExtractFirestoreString(const char* json, const char* fieldName,
                                       char* outBuf, size_t outBufSize) {
  if (!json || !fieldName || !outBuf || outBufSize == 0) return false;
  outBuf[0] = '\0';
  // Cari "fieldName"
  char pattern[80];
  snprintf(pattern, sizeof(pattern), "\"%s\"", fieldName);
  const char* k = strstr(json, pattern);
  if (!k) return false;
  k += strlen(pattern);
  // Cari "stringValue" setelah fieldName (dalam ~200 char ke depan)
  const char* sv = strstr(k, "\"stringValue\"");
  if (!sv || (sv - k) > 200) return false;
  sv += strlen("\"stringValue\"");
  while (*sv && (*sv == ' ' || *sv == ':' || *sv == '\t')) sv++;
  if (*sv != '"') return false;
  sv++;
  size_t i = 0;
  while (*sv && *sv != '"' && i + 1 < outBufSize) outBuf[i++] = *sv++;
  outBuf[i] = '\0';
  return (i > 0);
}

static bool jsonExtractFirestoreDouble(const char* json, const char* fieldName,
                                       float* outVal) {
  if (!json || !fieldName || !outVal) return false;
  char pattern[80];
  snprintf(pattern, sizeof(pattern), "\"%s\"", fieldName);
  const char* k = strstr(json, pattern);
  if (!k) return false;
  k += strlen(pattern);

  const char* dv = strstr(k, "\"doubleValue\"");
  if (!dv || (dv - k) > 200) return false;
  dv += strlen("\"doubleValue\"");
  while (*dv && (*dv == ' ' || *dv == ':' || *dv == '\t')) dv++;
  char* endPtr = nullptr;
  double v = strtod(dv, &endPtr);
  if (endPtr == dv) return false;
  *outVal = (float)v;
  return true;
}

static bool jsonExtractFirestoreInteger(const char* json, const char* fieldName,
                                        int* outVal) {
  if (!json || !fieldName || !outVal) return false;
  char pattern[80];
  snprintf(pattern, sizeof(pattern), "\"%s\"", fieldName);
  const char* k = strstr(json, pattern);
  if (!k) return false;
  k += strlen(pattern);

  const char* iv = strstr(k, "\"integerValue\"");
  if (!iv || (iv - k) > 200) return false;
  iv += strlen("\"integerValue\"");
  while (*iv && (*iv == ' ' || *iv == ':' || *iv == '\t')) iv++;
  if (*iv == '"') iv++;
  char* endPtr = nullptr;
  long v = strtol(iv, &endPtr, 10);
  if (endPtr == iv) return false;
  *outVal = (int)v;
  return true;
}

// Ambil string field — coba flat JSON dulu, lalu format Firestore REST
static bool jsonExtractFieldString(const char* json, const char* fieldName,
                                   char* outBuf, size_t outBufSize) {
  if (jsonExtractString(json, fieldName, outBuf, outBufSize)) {
    return true;
  }
  return jsonExtractFirestoreString(json, fieldName, outBuf, outBufSize);
}

static void apiSerialPrintGetParseResult(const char* deviceName,
                                         const char* status,
                                         const char* sessionId,
                                         bool sessionIdNull,
                                         const char* patientId,
                                         const char* patientName,
                                         const char* sessionStatus,
                                         bool sessionValid) {
  Serial.println();
  Serial.println(F("[api] ===== HASIL PARSING GET ====="));
  Serial.print(F("  deviceName         = "));
  if (deviceName[0]) Serial.println(deviceName); else Serial.println(F("(kosong)"));
  Serial.print(F("  status             = "));
  if (status[0]) Serial.println(status); else Serial.println(F("(kosong)"));
  Serial.print(F("  currentSessionId   = "));
  if (sessionIdNull) Serial.println(F("(null)"));
  else if (sessionId[0]) Serial.println(sessionId);
  else Serial.println(F("(kosong)"));
  Serial.print(F("  currentPatientId   = "));
  if (patientId[0]) Serial.println(patientId); else Serial.println(F("(kosong)"));
  Serial.print(F("  currentPatientName = "));
  if (patientName[0]) Serial.println(patientName); else Serial.println(F("(kosong)"));
  Serial.print(F("  sessionStatus      = "));
  if (sessionStatus[0]) Serial.println(sessionStatus); else Serial.println(F("(kosong)"));
  Serial.print(F("  sessionValid       = "));
  Serial.println(sessionValid ? F("true") : F("false"));
  Serial.println(F("[api] ============================="));
  Serial.println();
}

// Baca body HTTP ke buffer. contentLen < 0 = panjang tidak diketahui (chunked):
// berhenti setelah idle singkat, jangan tunggu sampai buffer penuh.
static size_t httpReadBodyToBuf(WiFiClient* stream, int contentLen,
                                char* buf, size_t bufSize,
                                uint32_t timeoutMs) {
  if (!stream || !buf || bufSize < 2) return 0;

  size_t nRead = 0;
  const unsigned long deadline = millis() + timeoutMs;
  unsigned long idleSince = 0;

  while (millis() < deadline && nRead + 1 < bufSize) {
    const int avail = stream->available();
    if (avail > 0) {
      const size_t space = bufSize - 1 - nRead;
      const size_t chunk = ((size_t)avail < space) ? (size_t)avail : space;
      for (size_t i = 0; i < chunk; i++) {
        buf[nRead++] = (char)stream->read();
      }
      idleSince = 0;
      if (contentLen > 0 && nRead >= (size_t)contentLen) {
        break;
      }
    } else {
      if (contentLen > 0 && nRead >= (size_t)contentLen) {
        break;
      }
      if (contentLen <= 0 && nRead > 0) {
        if (idleSince == 0) {
          idleSince = millis();
        } else if (millis() - idleSince >= 250UL) {
          break;
        }
      }
      if (!stream->connected() && nRead > 0) {
        break;
      }
      delay(1);
    }
  }

  buf[nRead] = '\0';
  return nRead;
}

//========= GET device state =========//
// Return true jika device sedang in_use (ada sesi aktif) dan currentSessionId+currentPatientId terisi.
bool apiFetchActiveCommand(void) {
  g_apiSessionValid       = false;
  g_apiSessionId[0]       = '\0';
  g_apiPatientId[0]       = '\0';
  g_apiPatientName[0]     = '\0';
  g_apiSessionStatus[0]   = '\0';
  strncpy(g_apiCurrentPosition, "duduk", sizeof(g_apiCurrentPosition) - 1);
  g_apiCurrentPosition[sizeof(g_apiCurrentPosition) - 1] = '\0';
  g_apiSavedMap = 0.0f;
  g_apiSavedMapValid = false;
  g_apiDbpMiringKiri = 0;
  g_apiDbpMiringKiriValid = false;

  g_apiLastGetHttpCode = 0;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[api] GET dibatalkan: WiFi belum terhubung"));
    g_apiLastGetHttpCode = -1;
    return false;
  }
  if (g_apiDeviceId[0] == '\0') {
    apiInitDeviceId();
  }

  // URL: base + path — tidak ada String, semua di stack/static
  static char getUrl[300];
  snprintf(getUrl, sizeof(getUrl),
           API_BASE_URL API_GET_ACTIVE_CMD_PATH, g_apiDeviceId);

  Serial.print(F("[api] GET "));
  Serial.println(getUrl);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(API_HTTP_TIMEOUT_MS / 1000u);

  HTTPClient https;
  https.setTimeout(API_HTTP_TIMEOUT_MS);
  if (!https.begin(client, getUrl)) {
    Serial.println(F("[api] begin() gagal"));
    g_apiLastGetHttpCode = -2;
    return false;
  }

  int code = https.GET();
  g_apiLastGetHttpCode = code;
  Serial.print(F("[api] GET code="));
  Serial.println(code);

  bool ok = false;
  if (code >= 200 && code < 300) {
    // Baca respons ke buffer statis — hindari heap String
    static char bodyBuf[2048];
    const int contentLen = https.getSize();
    WiFiClient* stream = https.getStreamPtr();
    const size_t nRead = stream
      ? httpReadBodyToBuf(stream, contentLen, bodyBuf, sizeof(bodyBuf),
                          API_HTTP_TIMEOUT_MS)
      : 0;
    Serial.print(F("[api] body bytes="));
    Serial.println(nRead);

    Serial.println(F("[api] --- body start ---"));
    Serial.println(bodyBuf);
    Serial.println(F("[api] --- body end ---"));

    // Response Firestore REST atau flat JSON
    char deviceNameStr[DEVICE_NAME_MAX_LEN] = "";
    char statusStr[20] = "";
    bool sessionIdNull = jsonExtractFirestoreIsNull(bodyBuf, "currentSessionId");

    jsonExtractFieldString(bodyBuf, "deviceName", deviceNameStr, sizeof(deviceNameStr));
    jsonExtractFieldString(bodyBuf, "status", statusStr, sizeof(statusStr));
    jsonExtractFieldString(bodyBuf, "currentSessionId",
                           g_apiSessionId, sizeof(g_apiSessionId));
    jsonExtractFieldString(bodyBuf, "currentPatientId",
                           g_apiPatientId, sizeof(g_apiPatientId));
    jsonExtractFieldString(bodyBuf, "currentPatientName",
                           g_apiPatientName, sizeof(g_apiPatientName));
    jsonExtractFieldString(bodyBuf, "sessionStatus",
                           g_apiSessionStatus, sizeof(g_apiSessionStatus));
    jsonExtractFieldString(bodyBuf, "currentPosition",
                           g_apiCurrentPosition, sizeof(g_apiCurrentPosition));

    float parsedMap = 0.0f;
    int   parsedMapInt = 0;
    if (jsonExtractFloat(bodyBuf, "map", &parsedMap) ||
        jsonExtractFirestoreDouble(bodyBuf, "map", &parsedMap) ||
        (jsonExtractFirestoreInteger(bodyBuf, "map", &parsedMapInt) &&
         (parsedMap = (float)parsedMapInt, true))) {
      g_apiSavedMap = parsedMap;
      g_apiSavedMapValid = true;
      Serial.print(F("[api] MAP dari GET = "));
      Serial.println(parsedMap, 2);
    } else {
      Serial.println(F("[api] MAP tidak ditemukan di response GET"));
    }

    int parsedDbpMiring = 0;
    float parsedDbpMiringF = 0.0f;
    if (jsonExtractFirestoreInteger(bodyBuf, "miringDbp", &parsedDbpMiring) ||
        (jsonExtractFloat(bodyBuf, "miringDbp", &parsedDbpMiringF) &&
         (parsedDbpMiring = (int)parsedDbpMiringF, true))) {
      g_apiDbpMiringKiri = parsedDbpMiring;
      g_apiDbpMiringKiriValid = true;
      Serial.print(F("[api] DBP miring dari GET = "));
      Serial.println(parsedDbpMiring);
    } else {
      Serial.println(F("[api] miringDbp tidak ditemukan di response GET"));
    }

    if (deviceNameStr[0] != '\0') {
      saveDeviceNameToPrefs(deviceNameStr);
    }

    if (strcmp(statusStr, "in_use") == 0 &&
        g_apiSessionId[0] && g_apiPatientId[0]) {
      if (strcmp(g_apiCurrentPosition, "miring_kiri") == 0) {
        selectedMode = 1;
      } else if (strcmp(g_apiCurrentPosition, "terlentang") == 0) {
        selectedMode = 2;
      } else {
        selectedMode = 0;
      }
      g_apiSessionValid = true;
      onSessionIdChanged(g_apiSessionId);
      ok = true;
    }

    apiSerialPrintGetParseResult(deviceNameStr, statusStr,
                                 g_apiSessionId, sessionIdNull,
                                 g_apiPatientId, g_apiPatientName,
                                 g_apiSessionStatus, g_apiSessionValid);

    if (ok) {
      Serial.println(F("[api] sesi aktif OK — siap kirim pengukuran"));
    } else if (strcmp(statusStr, "in_use") != 0) {
      Serial.println(F("[api] status bukan in_use — tidak ada sesi aktif"));
    } else {
      Serial.println(F("[api] field currentSessionId/currentPatientId kosong"));
    }
  } else if (code > 0) {
    Serial.println(https.getString());
  }

  https.end();
  client.stop();   // tutup koneksi TCP sebelum lanjut
  Serial.println(F("[api] GET selesai, koneksi ditutup"));
  return ok;
}

//========= POST measurement =========//
static const char* apiPositionStringFromMode(uint8_t mode) {
  if (mode == 1) return "miring_kiri";   // positionSequence[1]
  if (mode == 2) return "terlentang";    // positionSequence[2]
  return "duduk";                         // positionSequence[0]
}

bool apiPostMeasurement(int sbp, int dbp, int bpm, int rot, float mapMmHg,
                        uint8_t mode, float batteryV, uint32_t epochTime) {
  if (!g_apiSessionValid) {
    Serial.println(F("[api] POST dilewati: tidak ada sesi aktif"));
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[api] POST dibatalkan: WiFi belum terhubung"));
    return false;
  }
  if (g_apiDeviceId[0] == '\0') {
    apiInitDeviceId();
  }

  const char* position = apiPositionStringFromMode(mode);

  // URL: {BASE}/{deviceId}
  static char putUrl[300];
  snprintf(putUrl, sizeof(putUrl), API_BASE_URL API_PUT_MEAS_PATH, g_apiDeviceId);

  // Payload Firestore REST format: {"fields":{"key":{"valueType":"val"},...}}
  // integerValue dikirim sebagai string, doubleValue sebagai angka, nullValue:null
  // rot == INT32_MIN adalah sentinel → kirim null
  // mapMmHg < 0 adalah sentinel → kirim null. MAP diukur saat duduk lalu
  // dipakai ulang untuk miring_kiri & terlentang (nilai >= 0 = valid).
  static char payloadBuf[768];
  const bool rotValid = (rot != INT32_MIN);
  const bool mapValid = (mapMmHg >= 0.0f);

  if (rotValid && mapValid) {
    // duduk dengan ROT dari hardware (jarang, duduk biasanya rot null)
    snprintf(payloadBuf, sizeof(payloadBuf),
      "{\"fields\":{"
        "\"patientId\":{\"stringValue\":\"%s\"},"
        "\"sessionId\":{\"stringValue\":\"%s\"},"
        "\"deviceId\":{\"stringValue\":\"%s\"},"
        "\"position\":{\"stringValue\":\"%s\"},"
        "\"bpm\":{\"integerValue\":\"%d\"},"
        "\"dbp\":{\"integerValue\":\"%d\"},"
        "\"sbp\":{\"integerValue\":\"%d\"},"
        "\"rot\":{\"integerValue\":\"%d\"},"
        "\"map\":{\"doubleValue\":%.1f},"
        "\"batteryRaw\":{\"doubleValue\":%.2f},"
        "\"epochTime\":{\"integerValue\":\"%lu\"},"
        "\"VersionNumber\":{\"stringValue\":\"%s\"}"
      "}}",
      g_apiPatientId, g_apiSessionId, g_apiDeviceId, position,
      bpm, dbp, sbp, rot,
      (double)mapMmHg, (double)batteryV, (unsigned long)epochTime,
      API_VERSION_NUMBER);
  } else if (rotValid && !mapValid) {
    // terlentang: ada ROT, map null
    snprintf(payloadBuf, sizeof(payloadBuf),
      "{\"fields\":{"
        "\"patientId\":{\"stringValue\":\"%s\"},"
        "\"sessionId\":{\"stringValue\":\"%s\"},"
        "\"deviceId\":{\"stringValue\":\"%s\"},"
        "\"position\":{\"stringValue\":\"%s\"},"
        "\"bpm\":{\"integerValue\":\"%d\"},"
        "\"dbp\":{\"integerValue\":\"%d\"},"
        "\"sbp\":{\"integerValue\":\"%d\"},"
        "\"rot\":{\"integerValue\":\"%d\"},"
        "\"map\":{\"nullValue\":null},"
        "\"batteryRaw\":{\"doubleValue\":%.2f},"
        "\"epochTime\":{\"integerValue\":\"%lu\"},"
        "\"VersionNumber\":{\"stringValue\":\"%s\"}"
      "}}",
      g_apiPatientId, g_apiSessionId, g_apiDeviceId, position,
      bpm, dbp, sbp, rot,
      (double)batteryV, (unsigned long)epochTime,
      API_VERSION_NUMBER);
  } else if (!rotValid && mapValid) {
    // duduk: rot null, map ada
    snprintf(payloadBuf, sizeof(payloadBuf),
      "{\"fields\":{"
        "\"patientId\":{\"stringValue\":\"%s\"},"
        "\"sessionId\":{\"stringValue\":\"%s\"},"
        "\"deviceId\":{\"stringValue\":\"%s\"},"
        "\"position\":{\"stringValue\":\"%s\"},"
        "\"bpm\":{\"integerValue\":\"%d\"},"
        "\"dbp\":{\"integerValue\":\"%d\"},"
        "\"sbp\":{\"integerValue\":\"%d\"},"
        "\"rot\":{\"nullValue\":null},"
        "\"map\":{\"doubleValue\":%.1f},"
        "\"batteryRaw\":{\"doubleValue\":%.2f},"
        "\"epochTime\":{\"integerValue\":\"%lu\"},"
        "\"VersionNumber\":{\"stringValue\":\"%s\"}"
      "}}",
      g_apiPatientId, g_apiSessionId, g_apiDeviceId, position,
      bpm, dbp, sbp,
      (double)mapMmHg, (double)batteryV, (unsigned long)epochTime,
      API_VERSION_NUMBER);
  } else {
    // miring_kiri: rot null, map null
    snprintf(payloadBuf, sizeof(payloadBuf),
      "{\"fields\":{"
        "\"patientId\":{\"stringValue\":\"%s\"},"
        "\"sessionId\":{\"stringValue\":\"%s\"},"
        "\"deviceId\":{\"stringValue\":\"%s\"},"
        "\"position\":{\"stringValue\":\"%s\"},"
        "\"bpm\":{\"integerValue\":\"%d\"},"
        "\"dbp\":{\"integerValue\":\"%d\"},"
        "\"sbp\":{\"integerValue\":\"%d\"},"
        "\"rot\":{\"nullValue\":null},"
        "\"map\":{\"nullValue\":null},"
        "\"batteryRaw\":{\"doubleValue\":%.2f},"
        "\"epochTime\":{\"integerValue\":\"%lu\"},"
        "\"VersionNumber\":{\"stringValue\":\"%s\"}"
      "}}",
      g_apiPatientId, g_apiSessionId, g_apiDeviceId, position,
      bpm, dbp, sbp,
      (double)batteryV, (unsigned long)epochTime,
      API_VERSION_NUMBER);
  }

  Serial.print(F("[api] "));
  Serial.print(F(API_PUT_MEAS_METHOD));
  Serial.print(F(" "));
  Serial.println(putUrl);
  Serial.print(F("[api] payload: "));
  Serial.println(payloadBuf);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(API_HTTP_TIMEOUT_MS / 1000u);

  HTTPClient https;
  https.setTimeout(API_HTTP_TIMEOUT_MS);
  if (!https.begin(client, putUrl)) {
    Serial.println(F("[api] begin() gagal"));
    return false;
  }
  https.addHeader("Content-Type", "application/json");

  // sendRequest() mendukung semua method (POST/PUT/PATCH) tanpa String
  int code = https.sendRequest(API_PUT_MEAS_METHOD,
                               (uint8_t*)payloadBuf,
                               strlen(payloadBuf));

  Serial.print(F("[api] resp code="));
  Serial.println(code);
  if (code > 0) {
    Serial.println(https.getString());
  }
  https.end();
  return (code >= 200 && code < 300);
}
