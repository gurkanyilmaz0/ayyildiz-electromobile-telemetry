
# TEKNOFEST / TÜBİTAK Elektromobil - Telemetri ve Araç Kontrol Sistemi (AYYILDIZ TEAM)

Bu depo, TÜBİTAK Uluslararası Elektrikli Araç Yarışları (Elektromobil) şartnamesine tam uyumlu olarak geliştirilmiş kablosuz telemetri, BMS CAN-Bus entegrasyonu, SD kart veri kurtarma kuyruğu, Nextion HMI sürücü kokpit paneli ve Python yer istasyonu yazılımlarını içermektedir.

---

## 📌 Proje Mimarisi ve Dosyalar

* **`verici.ino` (Araç İçi Telemetri & VCU):** Arduino Mega 2560 üzerinde çalışır. Daly Smart BMS'ten CAN-Bus üzerinden batarya verilerini okur, 3 noktalı NTC sıcaklık denetimi yapar, 55°C alarm ve 70°C acil kontaktör kesme korumalarını yönetir. Nextion sürücü ekranını günceller ve LoRa üzerinden yer istasyonuna 2 saniyede bir doğrulamalı (ACK) telemetri verisi yollar.
* **`config.h`:** Tüm donanım pinleri, SPI CS tanımları (SD CS: Pin 49, CAN CS: Pin 53), güvenlik eşik değerleri (55°C / 70°C) ve telemetri sabitlerini barındıran merkezi konfigürasyon başlığıdır.
* **`alici.ino` (Yer İstasyonu Alıcı Köprüsü):** Pist kenarındaki Arduino Mega alıcı modülüdür. LoRa üzerinden gelen paketleri yer istasyonu bilgisayarına şeffaf olarak aktarır ve araca anlık ACK (doğrulama) paketleri yollar.
* **`izleme.py` (Yer İstasyonu Python GUI):** Yer istasyonunda gelen telemetri paketlerini gerçek zamanlı görselleştiren, grafikleyen ve şartnamenin zorunlu kıldığı formatta otomatik `.csv` log kaydı tutan arayüz yazılımıdır.

---

## 🚀 Temel Özellikler ve Şartname Uyumluluğu

* **Çift Yönlü Doğrulama (ACK & Sinyal Kopma Algılama):** Araç verici kodu, yer istasyonundan ACK gelmediği anda sinyalin koptuğunu algılar.
* **60 Saniyelik Dairesel Kuyruk & SD Yedekleme:** Şartname gereği, iletişim koptuğunda canlı paketler 30 paketlik (60 sn) dairesel kuyruğa ve yerel SD karta kaydedilir; iletişim tekrar sağlandığında geçmiş paketler yer istasyonuna sırayla geri yüklenir.
* **Daly BMS CAN-Bus Entegrasyonu:** Asenkron sorgulama mekanizmasıyla toplam voltaj, anlık akım, SOC, en yüksek/en düşük hücre gerilimleri ve kalan kapasite milisaniye hassasiyetinde çekilir.
* **Sürücü Gösterge Paneli (Nextion HMI):** 250 ms tazeleme hızıyla analog hız kadranı ibresi, batarya barı, hücre gerilimleri ve flaşörlü sıcaklık alarm ikonunu yönetir.
* **Katı Güvenlik Standartları:** $T_{\max} \ge 55^\circ\text{C}$ olduğunda kesintisiz sesli/görsel ikaz; $T_{\max} \ge 70^\circ\text{C}$ olduğunda ana batarya kontaktörünü açarak enerjiyi anında kesme koruması.

---

## 🛠️ Donanım Bağlantı Tablosu (Arduino Mega 2560)

| Fonksiyon / Modül | Arduino Mega Pini | Açıklama |
| :--- | :--- | :--- |
| **CAN Modülü (MCP2515)** | Pin 53 (CS), Pin 21 (INT) | SPI Veriyolu & Kesme |
| **SD Kart Modülü** | Pin 49 (CS) | SPI Veriyolu (Çakışma Önlenmiş) |
| **LoRa E22 Modülü** | Serial1 (Pin 19 RX, Pin 18 TX) | UART 9600 Baud, Pin 4 (M0), Pin 5 (M1) |
| **Nextion HMI Ekran** | Serial2 (Pin 17 RX, Pin 16 TX) | UART 9600 Baud |
| **Sıcaklık Sensörleri (NTC)** | A0 (DS1), A1 (DS2), A2 (DS3) | Analog Girişler (10k Referans) |
| **Kontaktör / Röle Kontrol** | Pin 7 | Aktif LOW Güç Kesme |
| **Sesli / Işıklı Alarm** | Pin 6 | 55°C Üstü İkaz Çıkışı |

## 📡 Yer İstasyonu Alıcı Bağlantısı (Arduino Mega 2560)

Yer istasyonunda paket kaybını ve gecikmeyi önlemek amacıyla donanımsal UART (`Serial1`) üzerinden **Arduino Mega 2560** kullanılmaktadır:

| LoRa Modülü (E22) Pini | Arduino Mega Pini | Açıklama |
| :--- | :--- | :--- |
| **VCC** | 5V | Güç Beslemesi |
| **GND** | GND | Ortak Toprak |
| **TXD** | Pin 19 (RX1) | LoRa'dan Mega'ya Veri Girişi |
| **RXD** | Pin 18 (TX1) | Mega'dan LoRa'ya ACK Çıkışı |
| **M0 / M1** | Pin 4 / Pin 5 (veya GND) | Normal Çalışma Modu (LOW) |
| **USB Portu** | Serial (Pin 0/1 USB) | Bilgisayara ve Python GUI'ye Şeffaf Köprü |
