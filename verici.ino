#include <math.h>
#include <string.h>
#include <stdio.h>
#include <SPI.h>
#include <mcp_can.h>
#include <SD.h>
#include "config.h"

MCP_CAN CAN(CAN_CS_PIN);

const float R_REF = 1000.0;
const float R_0 = 10000.0;
const float T_0 = 298.15;
const float B_VALUE = 3950;

struct SicaklikVerisi {
  float sicaklik_sensor1;
  float sicaklik_sensor2;
  float sicaklik_sensor3;
  float sicaklik_max;
  bool sicaklik_hata;
};

struct BmsVerisi {
  float voltaj;
  float akim;
  int soc;
  bool veri_hata;
  unsigned long sonSocZamani;
  unsigned long sonHucreZamani;
  unsigned long sonKapasiteZamani;
  unsigned long kalanKapasite_mAh;
  bool kapasite_gecerli;
  int sarjDurumu;
  unsigned int maxHucreVoltaj_mV;
  unsigned int minHucreVoltaj_mV;
  bool hucre_gecerli;
};

struct TelemetriPaketi {
  unsigned long seq;
  unsigned long zaman;
  float hiz;
  float tmax;
  float voltaj;
  float akim;
  int soc;
  float kalanEnerji;
  float izolasyon;
  unsigned int maxHucre;
  unsigned int minHucre;
  ActiveStatus durum;
};

const unsigned long BMS_REQ_SOC_V_I   = 0x18900140;
const unsigned long BMS_RESP_SOC_V_I  = 0x18904001;
const unsigned long BMS_REQ_HUCRE     = 0x18910140;
const unsigned long BMS_RESP_HUCRE    = 0x18914001;
const unsigned long BMS_REQ_KAPASITE  = 0x18930140;
const unsigned long BMS_RESP_KAPASITE = 0x18934001;

BmsVerisi sonBmsVerisi = {0, 0, 0, true, 0, 0, 0, 0, false, 0, 0, 0, false};

unsigned long sonGonderimZamani = 0;
unsigned long sonBmsIstekZamani = 0;
unsigned long sonNextionZamani = 0;

const unsigned long NEXTION_REFRESH_DELAY = 250;
const unsigned long BMS_POLL_DELAY        = 1000; // BMS her 1 saniyede güncellenir
const unsigned long ACK_TIMEOUT_MS        = 350;

bool sdHazir = false;
bool canHazir = false;
char dosyaAdi[13];

// 60 saniyelik kural için 30 paket (30 x 2 sn = 60 sn)
const int KUYRUK_KAPASITESI = 30;
TelemetriPaketi kuyruk[KUYRUK_KAPASITESI];
int kuyrukBas = 0;
int kuyrukSon = 0;
int kuyrukAdet = 0;
unsigned long paketSayaci = 0;

unsigned long sonBasariliAckZamani = 0;

void kuyrugaEkle(const TelemetriPaketi &p) {
  if (kuyrukAdet < KUYRUK_KAPASITESI) {
    kuyruk[kuyrukSon] = p;
    kuyrukSon = (kuyrukSon + 1) % KUYRUK_KAPASITESI;
    kuyrukAdet++;
    Serial.print(F("[KUYRUK] Paket eklendi. No: "));
    Serial.println(p.seq);
  } else {
    kuyrukBas = (kuyrukBas + 1) % KUYRUK_KAPASITESI;
    kuyruk[kuyrukSon] = p;
    kuyrukSon = (kuyrukSon + 1) % KUYRUK_KAPASITESI;
    Serial.println(F("[UYARI - 60s DOLDU] En eski paket silindi."));
  }
}

float hizKmhOku() {
  return HIZ_KMH_SABIT;
}

float izolasyonDirenciOku() {
  return IZOLASYON_DIRENC_KOHM_SABIT;
}

float kalanEnerjiHesapla() {
  if (sonBmsVerisi.kapasite_gecerli && sonBmsVerisi.voltaj > 0.0) {
    return (sonBmsVerisi.kalanKapasite_mAh / 1000.0) * sonBmsVerisi.voltaj;
  } else {
    return (BATARYA_PAKET_KAPASITE_WH * sonBmsVerisi.soc) / 100.0;
  }
}

ActiveStatus sistemDurumuHesapla(const SicaklikVerisi &sicaklik, float hizKmh, bool telemetriKopuk) {
  bool bmsSocTimeout = (millis() - sonBmsVerisi.sonSocZamani > 4000);

  if (!canHazir || sicaklik.sicaklik_hata || sonBmsVerisi.veri_hata || bmsSocTimeout || telemetriKopuk) {
    return STATE_ERROR;
  }
  if (sicaklik.sicaklik_max >= SICAKLIK_MAX || hizKmh >= HIZ_KRITIK_KMH) {
    return STATE_CRITICAL;
  }
  if (sonBmsVerisi.sarjDurumu == 1) {
    return STATE_CHARGING;
  }
  if (sonBmsVerisi.sarjDurumu == 2 || hizKmh > 0.5) {
    return STATE_RUNNING;
  }
  return STATE_IDLE;
}

const char* durumMetnineCevir(ActiveStatus durum, bool telemetriKopuk) {
  if (telemetriKopuk) return "BAGLANTI KOPUK (>60s)";

  switch (durum) {
    case STATE_IDLE:     return "SISTEM NORMAL";
    case STATE_RUNNING:  return "SURUS / DESARJ";
    case STATE_CHARGING: return "SARJ OLUYOR";
    case STATE_CRITICAL: return "KRITIK DURUM!";
    case STATE_ERROR:    return "SENSOR/BMS HATASI";
    default:             return "BILINMEYEN";
  }
}

void tarihStringOlustur(char* buffer) {
  const char* aylar[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  const char* d = __DATE__;

  char ayStr[4] = {d[0], d[1], d[2], '\0'};
  int gun = ((d[4] == ' ') ? 0 : (d[4] - '0')) * 10 + (d[5] - '0');

  int ay = 1;
  for (int i = 0; i < 12; i++) {
    if (strncmp(ayStr, aylar[i], 3) == 0) {
      ay = i + 1;
      break;
    }
  }
  sprintf(buffer, "%02d%02d", gun, ay);
}

bool dosyaAdiOlustur() {
  char tarih[5];
  tarihStringOlustur(tarih);

  for (int n = 1; n <= 999; n++) {
    sprintf(dosyaAdi, "%s-%d.CSV", tarih, n);
    if (!SD.exists(dosyaAdi)) {
      return true;
    }
  }
  return false;
}

float ntcOku(int pin) {
  int analogVal = analogRead(pin);

  if (analogVal > 0 && analogVal < 1023) {
    float vOut = analogVal * (5.0 / 1023.0);
    float rNtc = R_REF * ((5.0 / vOut) - 1.0);
    float tempK = 1.0 / ((1.0 / T_0) + (1.0 / B_VALUE) * log(rNtc / R_0));
    return tempK - 273.15;
  } else {
    return -127.0;
  }
}

SicaklikVerisi sicaklikOku() {
  SicaklikVerisi v;
  v.sicaklik_sensor1 = ntcOku(SICAKLIK_DS1);
  v.sicaklik_sensor2 = ntcOku(SICAKLIK_DS2);
  v.sicaklik_sensor3 = ntcOku(SICAKLIK_DS3);

  v.sicaklik_hata = false;
  v.sicaklik_max = -127.0;

  if (v.sicaklik_sensor1 != -127.0) {
    if (v.sicaklik_sensor1 > v.sicaklik_max) v.sicaklik_max = v.sicaklik_sensor1;
  } else v.sicaklik_hata = true;

  if (v.sicaklik_sensor2 != -127.0) {
    if (v.sicaklik_sensor2 > v.sicaklik_max) v.sicaklik_max = v.sicaklik_sensor2;
  } else v.sicaklik_hata = true;

  if (v.sicaklik_sensor3 != -127.0) {
    if (v.sicaklik_sensor3 > v.sicaklik_max) v.sicaklik_max = v.sicaklik_sensor3;
  } else v.sicaklik_hata = true;

  return v;
}

void bmsTumIstekleriGonder() {
  byte req[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  CAN.sendMsgBuf(BMS_REQ_SOC_V_I, 1, 8, req);
  delay(15);
  CAN.sendMsgBuf(BMS_REQ_KAPASITE, 1, 8, req);
  delay(15);
  CAN.sendMsgBuf(BMS_REQ_HUCRE, 1, 8, req);
}

void bmsCanKontrolEt() {
  while (!digitalRead(CAN_INT_PIN)) {
    unsigned long rxId;
    byte len = 0;
    byte buf[8];
    CAN.readMsgBuf(&rxId, &len, buf);

    unsigned long temizId = rxId & 0x1FFFFFFF;

    if (temizId == BMS_RESP_SOC_V_I) {
      sonBmsVerisi.voltaj       = ((buf[0] << 8) | buf[1]) / 10.0;
      sonBmsVerisi.akim         = (((buf[4] << 8) | buf[5]) - 30000) / 10.0;
      sonBmsVerisi.soc          = ((buf[6] << 8) | buf[7]) / 10;
      sonBmsVerisi.veri_hata    = false;
      sonBmsVerisi.sonSocZamani = millis();
    } else if (temizId == BMS_RESP_KAPASITE) {
      sonBmsVerisi.sarjDurumu        = buf[0];
      sonBmsVerisi.kalanKapasite_mAh = ((unsigned long)buf[4] << 24) | ((unsigned long)buf[5] << 16) |
                                       ((unsigned long)buf[6] << 8)  | buf[7];
      sonBmsVerisi.kapasite_gecerli  = true;
      sonBmsVerisi.sonKapasiteZamani = millis();
    } else if (temizId == BMS_RESP_HUCRE) {
      sonBmsVerisi.maxHucreVoltaj_mV = ((unsigned int)buf[0] << 8) | buf[1];
      sonBmsVerisi.minHucreVoltaj_mV = ((unsigned int)buf[3] << 8) | buf[4];
      sonBmsVerisi.hucre_gecerli     = true;
      sonBmsVerisi.sonHucreZamani    = millis();
    }
  }
}

void nextionKomutBitir() {
  HMI_SERIAL.write(0xFF);
  HMI_SERIAL.write(0xFF);
  HMI_SERIAL.write(0xFF);
}

void nextionSayiGonder(const char* obj, long deger) {
  HMI_SERIAL.print(obj);
  HMI_SERIAL.print(F(".val="));
  HMI_SERIAL.print(deger);
  nextionKomutBitir();
}

void nextionMetinGonder(const char* obj, const char* metin) {
  HMI_SERIAL.print(obj);
  HMI_SERIAL.print(F(".txt=\""));
  HMI_SERIAL.print(metin);
  HMI_SERIAL.print(F("\""));
  nextionKomutBitir();
}

// SD Kart formatı resmi şartname standardında (Bölüm 3 Madde 9.2-f)
void csvSatiriYaz(unsigned long zaman, float hizKmh, float tmax, float v, float kalanEnerjiWh) {
  if (!sdHazir) return;

  File csvDosya = SD.open(dosyaAdi, FILE_WRITE);
  if (!csvDosya) return;

  csvDosya.print(zaman);
  csvDosya.print(F(";"));
  csvDosya.print(hizKmh, 1);
  csvDosya.print(F(";"));
  csvDosya.print(tmax, 1);
  csvDosya.print(F(";"));
  csvDosya.print(v, 1);
  csvDosya.print(F(";"));
  csvDosya.println(kalanEnerjiWh, 1);

  csvDosya.close();
}

void nextionGuncelle(const SicaklikVerisi &sicaklik, float hizKmh,
                      float kalanEnerjiWh,
                      ActiveStatus durum, bool telemetriKopuk) {
  // 1. HIZ: Nextion içindeki tm1 timer'ı h0 üzerinden ibre z0 ve n0'ı günceller
  long hizDegeri = (long)round(hizKmh);
  nextionSayiGonder("h0", hizDegeri);

  // 2. SICAKLIK VE TOPLAM VOLTAJ (n1 >= 55 olunca tm0 ile p1 ikonu otomatik yanıp söner)
  nextionSayiGonder("n1", (long)round(sicaklik.sicaklik_max));
  nextionSayiGonder("n2", (long)round(sonBmsVerisi.voltaj));
  
  // 3. AKIM (vvs=2 için Akım * 100)
  nextionSayiGonder("x2", (long)round(sonBmsVerisi.akim * 100.0)); 
  
  // 4. BATARYA DOLULUK YÜZDESİ VE BARI (j0)
  char socBuf[8];
  snprintf(socBuf, sizeof(socBuf), "%%%d", sonBmsVerisi.soc);
  nextionMetinGonder("t1", socBuf);
  nextionSayiGonder("j0", sonBmsVerisi.soc); // Mavi barı doldurur

  // 5. MİN / MAKS HÜCRE GERİLİMLERİ (vvs=2 için 10'a bölerek V formatı)
  if (sonBmsVerisi.hucre_gecerli) {
    nextionSayiGonder("x0", (long)round(sonBmsVerisi.maxHucreVoltaj_mV / 10.0));
    nextionSayiGonder("x1", (long)round(sonBmsVerisi.minHucreVoltaj_mV / 10.0));
  }

  // 6. KALAN ENERJİ (Wh) VE DURUM METNİ
  nextionSayiGonder("n5", (long)round(kalanEnerjiWh));
  nextionMetinGonder("t0", durumMetnineCevir(durum, telemetriKopuk));
}

bool gonderVeAckBekle(const char* mesaj, unsigned long seq, unsigned long timeoutMs) {
  while (LORA_SERIAL_P.available()) {
    LORA_SERIAL_P.read();
  }

  LORA_SERIAL_P.println(mesaj);

  unsigned long baslangic = millis();
  char ackBuf[32];
  int ackIdx = 0;

  while (millis() - baslangic < timeoutMs) {
    bmsCanKontrolEt(); 

    while (LORA_SERIAL_P.available()) {
      char c = (char)LORA_SERIAL_P.read();
      if (c == '\n') {
        ackBuf[ackIdx] = '\0';
        if (strncmp(ackBuf, "ACK:", 4) == 0) {
          unsigned long gelenSeq = strtoul(ackBuf + 4, NULL, 10);
          if (gelenSeq == seq) {
            return true;
          }
        }
        ackIdx = 0;
      } else if (c != '\r') {
        if (ackIdx < (int)(sizeof(ackBuf) - 1)) {
          ackBuf[ackIdx++] = c;
        }
      }
    }
  }
  return false;
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  LORA_SERIAL_P.begin(LORA_SERIAL);
  HMI_SERIAL.begin(HMI_BAUD);

  pinMode(ROLE_PIN, OUTPUT);
  pinMode(ALARM_PIN, OUTPUT);
  pinMode(LORA_M0, OUTPUT);
  pinMode(LORA_M1, OUTPUT);

  digitalWrite(LORA_M0, LOW);
  digitalWrite(LORA_M1, LOW);
  digitalWrite(ALARM_PIN, LOW);
  digitalWrite(ROLE_PIN, HIGH);

  pinMode(CAN_INT_PIN, INPUT_PULLUP);

  sonBasariliAckZamani = millis();

  // SD Kart Başlatma (SD_CS = Pin 49)
  if (!SD.begin(SD_CS)) {
    Serial.println(F("SD kart baslatilamadi!"));
    sdHazir = false;
  } else if (!dosyaAdiOlustur()) {
    sdHazir = false;
  } else {
    File f = SD.open(dosyaAdi, FILE_WRITE);
    if (f) {
      f.println(F("zaman_ms;hiz_kmh;T_bat_C;V_bat_C;kalan_enerji_Wh")); //
      f.close();
      sdHazir = true;
      Serial.print(F("CSV dosyasi olusturuldu: "));
      Serial.println(dosyaAdi);
    } else {
      sdHazir = false;
    }
  }

  if (CAN.begin(MCP_ANY, CAN_500KBPS, MCP_16MHZ) == CAN_OK) {
    Serial.println(F("MCP2515 CAN init OK"));
    CAN.setMode(MCP_NORMAL);
    canHazir = true;
    bmsTumIstekleriGonder();
  } else {
    Serial.println(F("MCP2515 CAN init FAIL"));
    canHazir = false;
  }
}

void loop() {
  SicaklikVerisi sicaklik = sicaklikOku();

  // 55°C üstünde alarm daima aktiftir (70°C aşılsa bile yardımcı hattan çalmaya devam eder)
  if (sicaklik.sicaklik_max >= ALARM_MAX) {
    digitalWrite(ALARM_PIN, HIGH);
  } else {
    digitalWrite(ALARM_PIN, LOW);
  }

  // 70°C Aşırı Sıcaklık Koruması: Ana kontaktör açılır ve enerji kesilir (Bölüm 2 Madde 9.2-i)
  if (sicaklik.sicaklik_max >= SICAKLIK_MAX) {
    digitalWrite(ROLE_PIN, LOW);
  }

  bmsCanKontrolEt();

  // BMS Veri Sorgulama Asenkron Timer'ı (Her 1 saniyede bir taze veri istenir)
  if (millis() - sonBmsIstekZamani >= BMS_POLL_DELAY) {
    sonBmsIstekZamani = millis();
    bmsTumIstekleriGonder();
  }

  float hizKmh = hizKmhOku();
  float kalanEnerjiWh = kalanEnerjiHesapla();
  float izolasyonDirenci = izolasyonDirenciOku();
  bool telemetriKopuk = (millis() - sonBasariliAckZamani >= ((unsigned long)LOST_MAX * 1000UL));
  ActiveStatus mevcutDurum = sistemDurumuHesapla(sicaklik, hizKmh, telemetriKopuk);

  // Nextion Ekranını Yenile (250 ms)
  if (millis() - sonNextionZamani >= NEXTION_REFRESH_DELAY) {
    sonNextionZamani = millis();
    nextionGuncelle(sicaklik, hizKmh, kalanEnerjiWh, mevcutDurum, telemetriKopuk);
  }

  // Telemetri Gönderim Döngüsü (SEND_DELAY = 2000 ms)
  if (millis() - sonGonderimZamani >= SEND_DELAY) {
    sonGonderimZamani = millis();

    if (telemetriKopuk) {
      Serial.print(F("[UYARI - LOST_MAX] Telemetri baglantisi "));
      Serial.print(LOST_MAX);
      Serial.println(F(" saniyedir tamamen kesik!"));
    }

    paketSayaci++;

    char mesaj[160];
    char tmaxStr[10], vStr[10], iStr[10], spdStr[10], eStr[10], isoStr[10];
    dtostrf(sicaklik.sicaklik_max, 4, 1, tmaxStr);
    dtostrf(sonBmsVerisi.voltaj, 4, 1, vStr);
    dtostrf(sonBmsVerisi.akim, 4, 1, iStr);
    dtostrf(hizKmh, 4, 1, spdStr);
    dtostrf(kalanEnerjiWh, 5, 1, eStr);
    dtostrf(izolasyonDirenci, 5, 0, isoStr);

    snprintf(mesaj, sizeof(mesaj),
             "TYPE:LIVE,SEQ:%lu,TIME:%lu,SPD:%s,Tmax:%s,V:%s,I:%s,SOC:%d,E:%s,ISO:%s,Hmax:%u,Hmin:%u",
             paketSayaci, millis(), spdStr, tmaxStr, vStr, iStr, sonBmsVerisi.soc, eStr, isoStr,
             sonBmsVerisi.maxHucreVoltaj_mV, sonBmsVerisi.minHucreVoltaj_mV);

    Serial.print(F("[GONDERILIYOR - CANLI] "));
    Serial.println(mesaj);

    bool ackGeldi = gonderVeAckBekle(mesaj, paketSayaci, ACK_TIMEOUT_MS);

    if (ackGeldi) {
      sonBasariliAckZamani = millis();

      Serial.print(F("[ACK ALINDI] Canli paket onaylandi. No: "));
      Serial.println(paketSayaci);

      if (kuyrukAdet > 0) {
        TelemetriPaketi eskiPaket = kuyruk[kuyrukBas];
        delay(50); 

        char tekrarMesaj[160];
        char eTmax[10], eV[10], eI[10], eSpd[10], eE[10], eIso[10];
        dtostrf(eskiPaket.tmax, 4, 1, eTmax);
        dtostrf(eskiPaket.voltaj, 4, 1, eV);
        dtostrf(eskiPaket.akim, 4, 1, eI);
        dtostrf(eskiPaket.hiz, 4, 1, eSpd);
        dtostrf(eskiPaket.kalanEnerji, 5, 1, eE);
        dtostrf(eskiPaket.izolasyon, 5, 0, eIso);

        snprintf(tekrarMesaj, sizeof(tekrarMesaj),
                 "TYPE:RETRY,SEQ:%lu,TIME:%lu,SPD:%s,Tmax:%s,V:%s,I:%s,SOC:%d,E:%s,ISO:%s,Hmax:%u,Hmin:%u",
                 eskiPaket.seq, eskiPaket.zaman, eSpd, eTmax, eV, eI, eskiPaket.soc, eE, eIso,
                 eskiPaket.maxHucre, eskiPaket.minHucre);

        Serial.print(F("[GONDERILIYOR - TEKRAR] "));
        Serial.println(tekrarMesaj);

        bool retryAck = gonderVeAckBekle(tekrarMesaj, eskiPaket.seq, ACK_TIMEOUT_MS);

        if (retryAck) {
          sonBasariliAckZamani = millis();
          Serial.print(F("[ACK ALINDI - TEKRAR] Gecmis paket onaylandi. No: "));
          Serial.println(eskiPaket.seq);
          kuyrukBas = (kuyrukBas + 1) % KUYRUK_KAPASITESI;
          kuyrukAdet--;
        } else {
          Serial.println(F("[KAYIP - TEKRAR] Gecmis paket iletilemedi, kuyrukta bekliyor."));
        }
      }
    } else {
      // YALNIZCA KOPMA ANINDA: SD karta ve kuyruğa kaydedilir (Madde 9.2-e)
      Serial.print(F("[KOPMA] ACK gelmedi! Paket SD karta ve kuyruga kaydediliyor: "));
      Serial.println(paketSayaci);

      TelemetriPaketi kayipPaket;
      kayipPaket.seq = paketSayaci;
      kayipPaket.zaman = millis();
      kayipPaket.hiz = hizKmh;
      kayipPaket.tmax = sicaklik.sicaklik_max;
      kayipPaket.voltaj = sonBmsVerisi.voltaj;
      kayipPaket.akim = sonBmsVerisi.akim;
      kayipPaket.soc = sonBmsVerisi.soc;
      kayipPaket.kalanEnerji = kalanEnerjiWh;
      kayipPaket.izolasyon = izolasyonDirenci;
      kayipPaket.maxHucre = sonBmsVerisi.maxHucreVoltaj_mV;
      kayipPaket.minHucre = sonBmsVerisi.minHucreVoltaj_mV;
      kayipPaket.durum = mevcutDurum;
      kuyrugaEkle(kayipPaket);

      csvSatiriYaz(millis(), hizKmh, sicaklik.sicaklik_max, sonBmsVerisi.voltaj, kalanEnerjiWh);
    }
  }
}
