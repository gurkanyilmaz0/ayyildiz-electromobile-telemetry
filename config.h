#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define SERIAL_BAUD     9600
#define LORA_SERIAL     9600
#define HMI_BAUD        9600

#define LORA_SERIAL_P   Serial1
#define HMI_SERIAL      Serial2

#define ROLE_PIN        7
#define ALARM_PIN       6

#define LORA_M0         4
#define LORA_M1         5

#define CAN_CS_PIN      53
#define CAN_INT_PIN     21
#define SD_CS           10

#define SICAKLIK_DS1    A0
#define SICAKLIK_DS2    A1
#define SICAKLIK_DS3    A2

#define ALARM_MAX       55.0
#define SICAKLIK_MAX    70.0
#define HIZ_KRITIK_KMH  60.0

#define SEND_DELAY      2000
#define LOST_MAX        60

#define BATARYA_PAKET_KAPASITE_WH 4800.0

#define HIZ_KMH_SABIT                 0.0
#define IZOLASYON_DIRENC_KOHM_SABIT   9999.0

enum ActiveStatus : uint8_t {
  STATE_IDLE      = 0,
  STATE_RUNNING   = 1,
  STATE_CHARGING  = 2,
  STATE_CRITICAL  = 3,
  STATE_ERROR     = 4
};

#endif
