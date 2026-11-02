# 🌡️ Smart Room Climate Controller (ESP8266 + Telegram IoT)

Sistem pemantau dan pengendali iklim ruangan otomatis berbasis ESP8266. Memantau suhu dan kelembapan udara secara *real-time*, mengontrol kipas dan *dehumidifier*, serta menyediakan akses pemantauan dan kontrol jarak jauh via **Telegram Bot**.

---

## 🚀 Keunggulan Utama

* **Akses Luar Jaringan (Cloud via Telegram):** Perangkat dapat dipantau dan dikontrol dari mana saja di luar rumah tanpa memerlukan *port forwarding* atau IP publik.
* **TETAP BEKERJA SAAT INTERNET MATI (Offline Mode):** Fungsi utama kendali otomatis, logika histeresis, pembacaan sensor DHT22, dan pembaruan layar LCD berjalan secara lokal di ESP8266. Jika Wi-Fi/Internet terputus, alat tetap menjaga iklim ruangan secara normal.
* **Perlindungan Beban (Histeresis):** Mencegah sakelar relay terputus-sambung secara cepat (*chattering*) pada titik ambang batas untuk memperpanjang umur kipas dan *dehumidifier*.
* **Kalibrasi Sensor via Chat & EEPROM:** Koreksi akurasi pembacaan suhu/kelembapan dapat dilakukan langsung melalui pesan Telegram (`/kalibrasi_suhu` & `/kalibrasi_kelembapan`) dan tersimpan secara permanen di memori EEPROM.
* **Hemat Pin (I2C Backpack):** Menggunakan modul ekspander PCF8574 sehingga layar LCD 1602A hanya memerlukan 2 pin komunikasi (SDA & SCL).
* **Kendali Ganda (Auto & Manual):** Mendukung mode otomatis berdasarkan sensor dan mode kontrol manual dari jauh.

---

## 🛠️ Komponen & Peralatan

### Hardware
* **ESP8266 (NodeMCU v3 / Amica)** – Mikrokontroler utama & Wi-Fi Gateway
* **Sensor DHT22 (AM2302)** – Sensor suhu dan kelembapan presisi tinggi
* **Modul Relay 2-Channel (Active LOW)** – Sakelar daya kipas dan *dehumidifier*
* **LCD 1602A (16x2 Character Display)** – Tampilan indikator fisik lokal
* **PCF8574 I2C Module / Backpack** – Converter I2C untuk LCD 1602A
* **Breadboard & Kabel Jumper** – Rangkaian uji coba (Male-to-Male / Male-to-Female)
* **Kabel Micro USB** – Catu daya & jalur pemograman ESP8266

### Software & Library
* **Arduino IDE** (v1.8.x / v2.x)
* **Aplikasi Telegram** (Untuk pemantauan HP)
* **Library Arduino:**
  * `ESP8266WiFi.h`
  * `WiFiClientSecure.h`
  * `UniversalTelegramBot.h`
  * `ArduinoJson.h`
  * `Wire.h`
  * `LiquidCrystal_I2C.h`
  * `DHT.h`
  * `EEPROM.h`

---

## 🔌 Skema Wiring Pinout

| Komponen | Pin Modul | Pin NodeMCU ESP8266 | Keterangan |
| :--- | :--- | :--- | :--- |
| **Sensor DHT22** | DATA / OUT | `D5` (GPIO14) | Pembacaan Suhu & RH |
| **Modul Relay** | IN1 (Fan) | `D6` (GPIO12) | Kendali Kipas (Active LOW) |
| **Modul Relay** | IN2 (Dehum) | `D7` (GPIO13) | Kendali Dehumidifier (Active LOW) |
| **LCD I2C (PCF8574)**| SDA | `D2` (GPIO4) | Data I2C |
| **LCD I2C (PCF8574)**| SCL | `D1` (GPIO5) | Clock I2C |
| **Daya (VCC/GND)** | VCC / GND | `3V3` / `VV (5V)` / `GND` | Sesuaikan tegangan modul |

---

## ⚙️ Logika Kontrol Otomatis (Histeresis)

| Beban | Pin Relay | Kondisi ON | Kondisi OFF |
| :--- | :--- | :--- | :--- |
| **Kipas (Fan)** | D6 | Suhu $\ge 29.0^\circ\text{C}$ | Suhu $\le 26.0^\circ\text{C}$ |
| **Dehumidifier** | D7 | Kelembapan $\ge 65.0\%$ | Kelembapan $\le 60.0\%$ |

---

## 🤖 Perintah Bot Telegram

| Perintah | Fungsi |
| :--- | :--- |
| `/start` / `/help` | Menampilkan daftar perintah yang tersedia |
| `/auto` | Mengaktifkan Mode Otomatis (dikontrol sensor) |
| `/manual` | Mengaktifkan Mode Manual |
| `/cek` | Menampilkan status suhu, kelembapan, offset, dan relay saat ini |
| `/fan_on` / `/fan_off` | Menyala/mematikan kipas (otomatis beralih ke Mode Manual) |
| `/dehum_on` / `/dehum_off` | Menyala/mematikan dehumidifier (otomatis beralih ke Mode Manual) |
| `/kalibrasi_suhu [NILAI]` | Mengatur suhu referensi (Contoh: `/kalibrasi_suhu 28.0`) |
| `/kalibrasi_kelembapan [NILAI]` | Mengatur kelembapan referensi (Contoh: `/kalibrasi_kelembapan 55.0`) |
| `/reset_kalibrasi` | Mengembalikan nilai offset kalibrasi ke `0.0` |

---

## 🚀 Cara Menggunakan

1. **Clone Repositori:**
   ```bash
   git clone [https://github.com/username/smart-room-climate-controller.git](https://github.com/username/smart-room-climate-controller.git)
