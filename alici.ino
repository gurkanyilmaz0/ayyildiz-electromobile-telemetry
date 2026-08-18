#define SERIAL_PC_BAUD   9600   // Bilgisayara (Python Arayüzüne) USB Serial Hızı
#define LORA_BAUD        9600   // LoRa Modülü Haberleşme Hızı

// Arduino Mega Donanımsal Serial1 (Pin 19: RX1, Pin 18: TX1)
#define LORA_SERIAL      Serial1

// LoRa E22 Mod Pinleri (Normal Şeffaf Mod için M0=LOW, M1=LOW)
#define LORA_M0          4
#define LORA_M1          5

void setup() {
  Serial.begin(SERIAL_PC_BAUD);
  LORA_SERIAL.begin(LORA_BAUD);

  pinMode(LORA_M0, OUTPUT);
  pinMode(LORA_M1, OUTPUT);
  digitalWrite(LORA_M0, LOW);
  digitalWrite(LORA_M1, LOW);

  Serial.println(F("[YER ISTASYONU ALICI HAZIR - MEGA]"));
}

void loop() {
  // 1. ARAÇTAN GELEN TELEMETRİ: LoRa -> Bilgisayar (Python)
  while (LORA_SERIAL.available()) {
    char c = (char)LORA_SERIAL.read();
    Serial.write(c);
  }

  // 2. PYTHON'DAN GELEN ACK: Bilgisayar -> LoRa -> Araç
  while (Serial.available()) {
    char c = (char)Serial.read();
    LORA_SERIAL.write(c);
  }
}
