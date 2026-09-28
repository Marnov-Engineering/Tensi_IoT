/*
  BP_Parser.ino — baca UART tensimeter
  1. appendLineChar : susun 1 baris teks sampai Enter
  2. decodePacket   : ubah 11 karakter HEX di final_buff jadi SYS / DIA / nadi
*/

bool tftTryEnqueueLine(const char* line);

//========= Satu digit HEX jadi angka 0 s/d 15 =========//
// Contoh: '3' -> 3, 'A' -> 10, 'F' -> 15
static int angkaDariSatuHurufHex(char ch) {
  if (ch > '9') {
    return ch - '7';  // A..F
  }
  return ch - '0';    // 0..9
}

//========= Tambah 1 karakter ke baris teks (Serial1) =========//
void appendLineChar(char c) {
  if (c == '\r') {
    return;
  }

  if (c == '\n') {
    lineBuf[lineLen] = '\0';
    if (lineLen > 0) {
      Serial.println(lineBuf);
      tftTryEnqueueLine(lineBuf);
    }
    lineLen = 0;
    return;
  }

  if (lineLen < sizeof(lineBuf) - 1) {
    lineBuf[lineLen++] = c;
  } else {
    lineLen = 0;
  }
}

//========= Isi final_buff sudah lengkap → hitung SYS, DIA, MAP, BPM =========//
// Format 11 karakter contoh: "A4 6E 90 72" (ada spasi di posisi tetap)
void decodePacket() {
  hexSys  = angkaDariSatuHurufHex(final_buff[0]) * 16 + angkaDariSatuHurufHex(final_buff[1]);
  hexDias = angkaDariSatuHurufHex(final_buff[3]) * 16 + angkaDariSatuHurufHex(final_buff[4]);
  hexBPM  = angkaDariSatuHurufHex(final_buff[9]) * 16 + angkaDariSatuHurufHex(final_buff[10]);

  char line[32];
  snprintf(line, sizeof(line), "SYS %d", hexSys);
  tftTryEnqueueLine(line);
  snprintf(line, sizeof(line), "DIA %d", hexDias);
  tftTryEnqueueLine(line);
  snprintf(line, sizeof(line), "PULSE %d", hexBPM);
  tftTryEnqueueLine(line);

  Serial.print(F("SYS "));   Serial.println(hexSys);
  Serial.print(F("DIA "));   Serial.println(hexDias);
  Serial.print(F("PULSE ")); Serial.println(hexBPM);
  Serial.print(F("MAP "));   Serial.println((hexSys + 2 * hexDias) / 3);
}
