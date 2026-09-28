/*
  ADC.ino — tegangan baterai (ADC_PIN / GPIO0 = ADC1_CH0)
  Rumus kalibrasi: Volt = M * nilai_raw + C  (M/C dari Preferences)
*/

#include <Preferences.h>

static float s_adcCalM = ADC_CAL_M_DEFAULT;
static float s_adcCalC = ADC_CAL_C_DEFAULT;

static Preferences s_adcCalPrefs;
static bool s_adcCalPrefsOk = false;

static float teganganDariNilaiAdc(int raw) {
  return s_adcCalM * (float)raw + s_adcCalC;
}

void adcCalPrefsBegin(void) {
  s_adcCalPrefsOk = s_adcCalPrefs.begin(PREFS_NAMESPACE_MODE, false);
  if (!s_adcCalPrefsOk) {
    Serial.println(F("[prefs-adc] namespace gagal dibuka"));
  }
}

void loadAdcCalFromPrefs(void) {
  if (!s_adcCalPrefsOk) {
    return;
  }
  s_adcCalM = s_adcCalPrefs.getFloat(PREFS_KEY_ADC_CAL_M, ADC_CAL_M_DEFAULT);
  s_adcCalC = s_adcCalPrefs.getFloat(PREFS_KEY_ADC_CAL_C, ADC_CAL_C_DEFAULT);
  Serial.print(F("[prefs-adc] M="));
  Serial.print(s_adcCalM, 10);
  Serial.print(F(" C="));
  Serial.println(s_adcCalC, 10);
}

void saveAdcCalToPrefs(float m, float c) {
  s_adcCalM = m;
  s_adcCalC = c;
  if (!s_adcCalPrefsOk) {
    return;
  }
  s_adcCalPrefs.putFloat(PREFS_KEY_ADC_CAL_M, m);
  s_adcCalPrefs.putFloat(PREFS_KEY_ADC_CAL_C, c);
  Serial.print(F("[prefs-adc] simpan M="));
  Serial.print(m, 10);
  Serial.print(F(" C="));
  Serial.println(c, 10);
}

float adcGetCalM(void) {
  return s_adcCalM;
}

float adcGetCalC(void) {
  return s_adcCalC;
}

float adcVoltsFromRaw(int raw) {
  const float v = teganganDariNilaiAdc(raw);
  return (v < 0.0f) ? 0.0f : v;
}

//========= Rata-rata 8x (ada jeda) — cocok dipanggil dari setup() =========//
float adcReadBatteryVoltsSetup() {
  uint32_t jumlah = 0;
  for (int n = 0; n < 8; n++) {
    jumlah += (uint32_t)analogRead(ADC_PIN);
    delay(5);
  }
  return teganganDariNilaiAdc((int)(jumlah / 8u));
}

//========= Baca cepat 1x (tanpa delay) — boleh dari task RTOS =========//
float adcReadBatteryVolts() {
  return teganganDariNilaiAdc(analogRead(ADC_PIN));
}

//========= Rata-rata 8x — untuk kirim API (stabil, tanpa delay panjang) =========//
int adcReadBatteryRawAvg(void) {
  uint32_t jumlah = 0;
  for (int n = 0; n < 8; n++) {
    jumlah += (uint32_t)analogRead(ADC_PIN);
  }
  return (int)(jumlah / 8u);
}

// Tegangan terkalibrasi (V) dari rata-rata ADC — dikirim ke API (satuan volt)
float adcReadBatteryVoltsAvg(void) {
  return adcVoltsFromRaw(adcReadBatteryRawAvg());
}
