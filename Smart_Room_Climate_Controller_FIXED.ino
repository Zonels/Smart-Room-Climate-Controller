#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <EEPROM.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>

// ==================================================
// 1. WIFI DAN TELEGRAM
// Isi placeholder dengan kredensial Anda. Gunakan token Telegram BARU setelah merotasi token yang terekspos.
// ==================================================

const char* WIFI_SSID = "AKN Blitar";
const char* WIFI_PASS = "Kampus Juara #1";

// Ganti dengan token bot yang baru.
#define BOT_TOKEN "8110878968:AAHVfVsR-tIyO_eP2prmyagnfazy2SJToyM"
#define CHAT_ID   "995712826"

// ==================================================
// 2. PIN HARDWARE NODEMCU ESP8266
// ==================================================

#define DHTPIN      D5
#define DHTTYPE     DHT22

#define RELAY_FAN   D6
#define RELAY_DEHUM D7

// Relay aktif LOW:
// LOW  = relay ON
// HIGH = relay OFF
const bool RELAY_ACTIVE_LOW = true;

// ==================================================
// 3. OBJEK SENSOR DAN KOMUNIKASI
// ==================================================

DHT dht(DHTPIN, DHTTYPE);

LiquidCrystal_I2C lcd(0x27, 16, 2);

WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

// ==================================================
// 4. AMBANG HYSTERESIS
// ==================================================

const float TEMP_HIGH = 29.0;
const float TEMP_LOW  = 26.0;

const float HUM_HIGH = 65.0;
const float HUM_LOW  = 60.0;

// ==================================================
// 5. STATUS SISTEM DAN KALIBRASI
// ==================================================

bool fanState = false;
bool dehumState = false;

bool isAutoMode = true;
bool isOnline = false;
bool sensorValid = false;
// Setelah masuk AUTO, inisialisasi status dari sensor valid pertama.
bool autoInitializationPending = true;

// Nilai mentah langsung dari DHT22
float rawTemp = NAN;
float rawHum  = NAN;

// Nilai setelah dikoreksi dengan offset
float temp = NAN;
float hum  = NAN;

// Offset kalibrasi yang ditambahkan ke nilai mentah
float calTempOffset = 0.0;
float calHumOffset  = 0.0;

// Penyimpanan offset di EEPROM
const int EEPROM_SIZE = 512;
const uint32_t CALIBRATION_MAGIC = 0x44544832UL; // Penanda versi data kalibrasi DHT2

struct CalibrationData {
  uint32_t magic;
  float tempOffset;
  float humOffset;
};

// ==================================================
// 6. TIMER
// ==================================================

unsigned long waktuSelesaiCekBot = 0;
unsigned long waktuSelesaiDHT = 0;
unsigned long waktuCekWiFi = 0;
unsigned long waktuLCD = 0;

const unsigned long jedaCekBot = 2000;
const unsigned long jedaDHT = 2500;
const unsigned long jedaWiFi = 10000;
const unsigned long jedaLCD = 1000;

// ==================================================
// 7. FUNGSI PENYIMPANAN KALIBRASI
// ==================================================

void loadCalibration() {
  CalibrationData data;
  EEPROM.get(0, data);

  bool dataValid =
    data.magic == CALIBRATION_MAGIC &&
    isfinite(data.tempOffset) &&
    isfinite(data.humOffset) &&
    fabs(data.tempOffset) <= 20.0 &&
    fabs(data.humOffset) <= 50.0;

  if (dataValid) {
    calTempOffset = data.tempOffset;
    calHumOffset = data.humOffset;

    Serial.println("Kalibrasi dimuat dari EEPROM.");
  }
  else {
    calTempOffset = 0.0;
    calHumOffset = 0.0;

    Serial.println("Kalibrasi belum tersimpan; memakai offset 0.");
  }

  Serial.print("Offset suhu: ");
  Serial.println(calTempOffset, 2);

  Serial.print("Offset kelembapan: ");
  Serial.println(calHumOffset, 2);
}

void saveCalibration() {
  CalibrationData data;

  data.magic = CALIBRATION_MAGIC;
  data.tempOffset = calTempOffset;
  data.humOffset = calHumOffset;

  EEPROM.put(0, data);

  if (EEPROM.commit()) {
    Serial.println("Kalibrasi berhasil disimpan ke EEPROM.");
  }
  else {
    Serial.println("Gagal menyimpan kalibrasi ke EEPROM.");
  }
}

// ==================================================
// 8. KONTROL OUTPUT RELAY
// ==================================================

void updateRelay() {
  digitalWrite(
    RELAY_FAN,
    RELAY_ACTIVE_LOW
      ? (fanState ? LOW : HIGH)
      : (fanState ? HIGH : LOW)
  );

  digitalWrite(
    RELAY_DEHUM,
    RELAY_ACTIVE_LOW
      ? (dehumState ? LOW : HIGH)
      : (dehumState ? HIGH : LOW)
  );
}

// ==================================================
// 9. PENGIRIMAN TELEGRAM
// ==================================================

void sendTelegramMessage(String message) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Telegram tidak dikirim: Wi-Fi offline.");
    return;
  }

  if (String(BOT_TOKEN) == "ISI_TOKEN_BOT_TELEGRAM_BARU" ||
      String(BOT_TOKEN).length() < 20 ||
      String(CHAT_ID) == "ISI_CHAT_ID_TELEGRAM") {
    Serial.println("Token atau Chat ID Telegram belum dikonfigurasi.");
    return;
  }

  bool sent = bot.sendMessage(CHAT_ID, message, "");

  if (sent) {
    Serial.println("Pesan Telegram berhasil dikirim.");
  }
  else {
    Serial.println("Pengiriman Telegram gagal.");
  }
}

// ==================================================
// 10. KONTROL OTOMATIS
// ==================================================

void applyAutoControl(bool resetStatesInBand = false) {
  // Mode manual tidak boleh diambil alih sensor.
  if (!isAutoMode) {
    return;
  }

  // Jika /auto dipanggil saat sensor error, tunda inisialisasi
  // sampai data sensor valid berikutnya.
  if (resetStatesInBand) {
    autoInitializationPending = true;
  }

  // Jangan menggunakan data sensor tidak valid.
  if (!sensorValid || !isfinite(temp) || !isfinite(hum)) {
    return;
  }

  bool oldFanState = fanState;
  bool oldDehumState = dehumState;

  if (autoInitializationPending) {
    // Saat masuk AUTO (atau boot), status relay manual tidak diwarisi.
    // Dalam rentang hysteresis, default awal adalah OFF.
    fanState = (temp >= TEMP_HIGH);
    dehumState = (hum >= HUM_HIGH);
    autoInitializationPending = false;
  }
  else {
    // Kontrol kipas dengan hysteresis:
    // ON pada >= 29 C, OFF pada <= 26 C,
    // dan mempertahankan status saat berada di antaranya.
    if (temp >= TEMP_HIGH) {
      fanState = true;
    }
    else if (temp <= TEMP_LOW) {
      fanState = false;
    }

    // Kontrol dehumidifier dengan hysteresis:
    // ON pada >= 65%, OFF pada <= 60%.
    if (hum >= HUM_HIGH) {
      dehumState = true;
    }
    else if (hum <= HUM_LOW) {
      dehumState = false;
    }
  }

  updateRelay();

  if (oldFanState != fanState) {
    if (fanState) {
      sendTelegramMessage(
        "PERINGATAN SUHU TINGGI!\n"
        "Kipas otomatis ON.\n"
        "Suhu: " + String(temp, 1) + " C"
      );
    }
    else {
      sendTelegramMessage(
        "Suhu kembali rendah atau AUTO diinisialisasi.\n"
        "Kipas otomatis OFF.\n"
        "Suhu: " + String(temp, 1) + " C"
      );
    }
  }

  if (oldDehumState != dehumState) {
    String msg = "Status dehumidifier otomatis berubah: ";
    msg += dehumState ? "ON" : "OFF";
    msg += "\nKelembapan: ";
    msg += String(hum, 1);
    msg += " %";
    sendTelegramMessage(msg);
  }
}

// ==================================================
// 11. PEMBACAAN DAN KOREKSI SENSOR
// ==================================================

void readSensor() {
  float newRawTemp = dht.readTemperature();
  float newRawHum = dht.readHumidity();

  if (!isfinite(newRawTemp) || !isfinite(newRawHum) ||
      newRawTemp < -40.0 || newRawTemp > 80.0 ||
      newRawHum < 0.0 || newRawHum > 100.0) {
    sensorValid = false;
    Serial.println("ERROR: pembacaan DHT22 gagal atau di luar batas sensor.");
    return; // Relay tidak berubah saat sensor tidak valid.
  }

  rawTemp = newRawTemp;
  rawHum = newRawHum;

  // Koreksi software berdasarkan offset kalibrasi.
  float correctedTemp = rawTemp + calTempOffset;
  float correctedHum = rawHum + calHumOffset;

  // Tolak hasil koreksi yang tidak masuk akal, jangan digunakan untuk kontrol.
  if (!isfinite(correctedTemp) || !isfinite(correctedHum) ||
      correctedTemp < -40.0 || correctedTemp > 80.0 ||
      correctedHum < 0.0 || correctedHum > 100.0) {
    sensorValid = false;
    Serial.println("ERROR: hasil koreksi kalibrasi di luar batas wajar.");
    return; // Relay tidak berubah saat data terkoreksi tidak valid.
  }

  temp = correctedTemp;
  hum = correctedHum;
  sensorValid = true;

  Serial.print("Suhu mentah: ");
  Serial.print(rawTemp, 1);
  Serial.print(" C | suhu terkoreksi: ");
  Serial.print(temp, 1);

  Serial.print(" C | RH mentah: ");
  Serial.print(rawHum, 1);
  Serial.print(" % | RH terkoreksi: ");
  Serial.print(hum, 1);
  Serial.println(" %");

  applyAutoControl();
}

// ==================================================
// 12. LCD
// ==================================================

void updateLCD() {
  lcd.setCursor(0, 0);

  if (!sensorValid) {
    lcd.print("Sensor error    ");
  }
  else {
    char baris1[17];

    snprintf(
      baris1,
      sizeof(baris1),
      "T:%4.1fC H:%2.0f%%",
      temp,
      hum
    );

    lcd.print(baris1);

    int panjang = strlen(baris1);

    for (int i = panjang; i < 16; i++) {
      lcd.print(' ');
    }
  }

  lcd.setCursor(0, 1);

  String baris2 = "F:";
  baris2 += fanState ? "ON " : "OFF";

  baris2 += " D:";
  baris2 += dehumState ? "ON " : "OFF";

  baris2 += isAutoMode ? " A" : " M";

  lcd.print(baris2.substring(0, 16));

  for (int i = baris2.length(); i < 16; i++) {
    lcd.print(' ');
  }
}

// ==================================================
// 13. WIFI
// ==================================================

void connectWiFi() {
  Serial.println("Menghubungkan ke Wi-Fi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long mulai = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - mulai < 15000) {
    delay(250);
    Serial.print('.');
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    isOnline = true;

    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  }
  else {
    isOnline = false;
    Serial.println("Wi-Fi belum terhubung.");
  }
}

// ==================================================
// 14. PARSING PERINTAH KALIBRASI
// Format: /kalibrasi_suhu 28.0
// Angka adalah nilai dari alat referensi.
// ==================================================

bool parseCalibrationCommand(
  String text,
  const char* command,
  float &value
) {
  String prefix = String(command) + " ";

  if (!text.startsWith(prefix)) {
    return false;
  }

  String argument = text.substring(prefix.length());
  argument.trim();

  if (argument.length() == 0) {
    return false;
  }

  char* endPointer;
  float parsedValue = strtof(argument.c_str(), &endPointer);

  if (endPointer == argument.c_str() ||
      *endPointer != '\0' ||
      !isfinite(parsedValue)) {
    return false;
  }

  value = parsedValue;
  return true;
}

// ==================================================
// 15. PEMROSESAN PESAN TELEGRAM
// ==================================================

void prosesPesanMasuk(int jumlahPesan) {
  for (int i = 0; i < jumlahPesan; i++) {
    String chat_id = String(bot.messages[i].chat_id);

    // Batasi perintah ke chat ID yang diizinkan.
    if (chat_id != CHAT_ID) {
      continue;
    }

    String teksPesan = bot.messages[i].text;
    teksPesan.trim();

    // Telegram kadang menambahkan @nama_bot pada perintah di grup.
    int posisiAt = teksPesan.indexOf('@');
    int posisiSpasi = teksPesan.indexOf(' ');
    if (posisiAt > 0 && (posisiSpasi < 0 || posisiAt < posisiSpasi)) {
      teksPesan.remove(posisiAt, (posisiSpasi < 0 ? teksPesan.length() : posisiSpasi) - posisiAt);
    }

    float nilaiReferensi;

    // ------------------------------------------------
    // KALIBRASI SUHU
    // Contoh: /kalibrasi_suhu 28.0
    // ------------------------------------------------

    if (parseCalibrationCommand(
          teksPesan,
          "/kalibrasi_suhu",
          nilaiReferensi
        )) {
      if (!sensorValid) {
        bot.sendMessage(
          chat_id,
          "Kalibrasi gagal: pembacaan DHT22 belum valid. Coba lagi setelah sensor terbaca.",
          ""
        );
        continue;
      }

      if (nilaiReferensi < -40.0 || nilaiReferensi > 80.0) {
        bot.sendMessage(
          chat_id,
          "Nilai referensi suhu harus antara -40 sampai 80 C.",
          ""
        );
        continue;
      }

      float offsetBaru = nilaiReferensi - rawTemp;

      if (fabs(offsetBaru) > 20.0) {
        bot.sendMessage(
          chat_id,
          "Offset suhu terlalu besar (>20 C). Periksa nilai referensi dan sensor.",
          ""
        );
        continue;
      }

      calTempOffset = offsetBaru;
      temp = rawTemp + calTempOffset;

      saveCalibration();
      applyAutoControl();

      String msg = "Kalibrasi suhu diterapkan.\n";
      msg += "DHT22 mentah: " + String(rawTemp, 1) + " C\n";
      msg += "Nilai referensi: " + String(nilaiReferensi, 1) + " C\n";
      msg += "Offset baru: " + String(calTempOffset, 2) + " C\n";
      msg += "Suhu terkoreksi: " + String(temp, 1) + " C";

      bot.sendMessage(chat_id, msg, "");
    }

    // ------------------------------------------------
    // KALIBRASI KELEMBAPAN
    // Contoh: /kalibrasi_kelembapan 55.0
    // ------------------------------------------------

    else if (parseCalibrationCommand(
               teksPesan,
               "/kalibrasi_kelembapan",
               nilaiReferensi
             )) {
      if (!sensorValid) {
        bot.sendMessage(
          chat_id,
          "Kalibrasi gagal: pembacaan DHT22 belum valid. Coba lagi setelah sensor terbaca.",
          ""
        );
        continue;
      }

      if (nilaiReferensi < 0.0 || nilaiReferensi > 100.0) {
        bot.sendMessage(
          chat_id,
          "Nilai referensi kelembapan harus antara 0 sampai 100 persen.",
          ""
        );
        continue;
      }

      float offsetBaru = nilaiReferensi - rawHum;

      if (fabs(offsetBaru) > 50.0) {
        bot.sendMessage(
          chat_id,
          "Offset kelembapan terlalu besar (>50%). Periksa nilai referensi dan sensor.",
          ""
        );
        continue;
      }

      calHumOffset = offsetBaru;
      hum = rawHum + calHumOffset;

      saveCalibration();
      applyAutoControl();

      String msg = "Kalibrasi kelembapan diterapkan.\n";
      msg += "DHT22 mentah: " + String(rawHum, 1) + " %\n";
      msg += "Nilai referensi: " + String(nilaiReferensi, 1) + " %\n";
      msg += "Offset baru: " + String(calHumOffset, 2) + " %\n";
      msg += "Kelembapan terkoreksi: " + String(hum, 1) + " %";

      bot.sendMessage(chat_id, msg, "");
    }

    // ------------------------------------------------
    // RESET KALIBRASI
    // ------------------------------------------------

    else if (teksPesan == "/reset_kalibrasi") {
      calTempOffset = 0.0;
      calHumOffset = 0.0;

      if (sensorValid) {
        temp = rawTemp;
        hum = rawHum;
      }

      saveCalibration();
      applyAutoControl();

      bot.sendMessage(
        chat_id,
        "Offset kalibrasi suhu dan kelembapan direset ke 0 dan disimpan.",
        ""
      );
    }

    // ------------------------------------------------
    // START / HELP
    // ------------------------------------------------

    else if (teksPesan == "/start" ||
             teksPesan == "/help") {
      String msg = "SMART ROOM CLIMATE CONTROLLER\n\n";

      msg += "/auto - Mode otomatis\n";
      msg += "/manual - Mode manual\n";
      msg += "/cek - Cek status\n\n";

      msg += "/fan_on - Kipas ON\n";
      msg += "/fan_off - Kipas OFF\n";
      msg += "/dehum_on - Dehumidifier ON\n";
      msg += "/dehum_off - Dehumidifier OFF\n\n";

      msg += "/kalibrasi_suhu ANGKA\n";
      msg += "Contoh: /kalibrasi_suhu 28.0\n\n";

      msg += "/kalibrasi_kelembapan ANGKA\n";
      msg += "Contoh: /kalibrasi_kelembapan 55.0\n\n";

      msg += "/reset_kalibrasi - Reset offset";

      bot.sendMessage(chat_id, msg, "");
    }

    // ------------------------------------------------
    // MODE OTOMATIS
    // ------------------------------------------------

    else if (teksPesan == "/auto") {
      isAutoMode = true;
      applyAutoControl(true);

      String msg = "MODE OTOMATIS AKTIF [A]\n";

      if (sensorValid) {
        msg += "Suhu: " + String(temp, 1) + " C\n";
        msg += "Kelembapan: " + String(hum, 1) + " %\n";
        msg += "Kipas: ";
        msg += fanState ? "ON\n" : "OFF\n";

        msg += "Dehumidifier: ";
        msg += dehumState ? "ON" : "OFF";
      }
      else {
        msg += "Sensor belum valid.\n";
        msg += "Menunggu pembacaan sensor.";
      }

      bot.sendMessage(chat_id, msg, "");
    }

    // ------------------------------------------------
    // MODE MANUAL
    // ------------------------------------------------

    else if (teksPesan == "/manual") {
      isAutoMode = false;

      bot.sendMessage(
        chat_id,
        "MODE MANUAL AKTIF [M].\n"
        "Relay hanya berubah melalui perintah manual.",
        ""
      );
    }

    // ------------------------------------------------
    // STATUS SISTEM
    // ------------------------------------------------

    else if (teksPesan == "/cek") {
      String msg = "STATUS SISTEM\n";

      msg += "Mode: ";
      msg += isAutoMode ? "AUTO\n" : "MANUAL\n";

      msg += "Sensor: ";
      msg += sensorValid ? "OK\n" : "ERROR\n";

      if (sensorValid) {
        msg += "Suhu mentah: " + String(rawTemp, 1) + " C\n";
        msg += "Suhu terkoreksi: " + String(temp, 1) + " C\n";
        msg += "Offset suhu: " + String(calTempOffset, 2) + " C\n";

        msg += "RH mentah: " + String(rawHum, 1) + " %\n";
        msg += "RH terkoreksi: " + String(hum, 1) + " %\n";
        msg += "Offset RH: " + String(calHumOffset, 2) + " %\n";
      }

      msg += "Kipas: ";
      msg += fanState ? "ON\n" : "OFF\n";

      msg += "Dehumidifier: ";
      msg += dehumState ? "ON" : "OFF";

      bot.sendMessage(chat_id, msg, "");
    }

    // ------------------------------------------------
    // KIPAS MANUAL
    // ------------------------------------------------

    else if (teksPesan == "/fan_on") {
      isAutoMode = false;
      fanState = true;
      updateRelay();

      bot.sendMessage(
        chat_id,
        "Kipas ON.\nMode MANUAL aktif.\nGunakan /auto untuk kembali ke otomatis.",
        ""
      );
    }

    else if (teksPesan == "/fan_off") {
      isAutoMode = false;
      fanState = false;
      updateRelay();

      bot.sendMessage(
        chat_id,
        "Kipas OFF.\nMode MANUAL aktif.\nGunakan /auto untuk kembali ke otomatis.",
        ""
      );
    }

    // ------------------------------------------------
    // DEHUMIDIFIER MANUAL
    // ------------------------------------------------

    else if (teksPesan == "/dehum_on") {
      isAutoMode = false;
      dehumState = true;
      updateRelay();

      bot.sendMessage(
        chat_id,
        "Dehumidifier ON.\nMode MANUAL aktif.\nGunakan /auto untuk kembali ke otomatis.",
        ""
      );
    }

    else if (teksPesan == "/dehum_off") {
      isAutoMode = false;
      dehumState = false;
      updateRelay();

      bot.sendMessage(
        chat_id,
        "Dehumidifier OFF.\nMode MANUAL aktif.\nGunakan /auto untuk kembali ke otomatis.",
        ""
      );
    }

    else if (teksPesan.startsWith("/kalibrasi_suhu") ||
             teksPesan.startsWith("/kalibrasi_kelembapan")) {
      bot.sendMessage(
        chat_id,
        "Format kalibrasi:\n"
        "/kalibrasi_suhu 28.0\n"
        "/kalibrasi_kelembapan 55.0\n\n"
        "Angka yang dimasukkan adalah nilai alat referensi.",
        ""
      );
    }
  }
}

// ==================================================
// 16. SETUP
// ==================================================

void setup() {
  Serial.begin(115200);

  // Inisialisasi EEPROM dan baca offset tersimpan.
  EEPROM.begin(EEPROM_SIZE);
  loadCalibration();

  // Atur kondisi OFF sesuai polaritas relay sebelum output diaktifkan.
  digitalWrite(RELAY_FAN, RELAY_ACTIVE_LOW ? HIGH : LOW);
  digitalWrite(RELAY_DEHUM, RELAY_ACTIVE_LOW ? HIGH : LOW);
  pinMode(RELAY_FAN, OUTPUT);
  pinMode(RELAY_DEHUM, OUTPUT);

  fanState = false;
  dehumState = false;
  updateRelay();

  // LCD I2C NodeMCU: SDA = D2, SCL = D1.
  Wire.begin(D2, D1);

  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("Smart Room IoT");

  lcd.setCursor(0, 1);
  lcd.print("Starting...");

  dht.begin();

  // Catatan: setInsecure tidak memverifikasi sertifikat TLS.
  // Untuk penggunaan produksi, sebaiknya gunakan verifikasi CA.
  client.setInsecure();

  connectWiFi();

  lcd.clear();

  waktuSelesaiDHT = millis();
  waktuSelesaiCekBot = millis();
  waktuCekWiFi = millis();
  waktuLCD = millis();
}

// ==================================================
// 17. LOOP
// ==================================================

void loop() {
  unsigned long sekarang = millis();

  // Baca sensor setiap 2,5 detik.
  if (sekarang - waktuSelesaiDHT >= jedaDHT) {
    waktuSelesaiDHT = sekarang;
    readSensor();
  }

  // Perbarui LCD setiap 1 detik.
  if (sekarang - waktuLCD >= jedaLCD) {
    waktuLCD = sekarang;
    updateLCD();
  }

  // Periksa koneksi Wi-Fi.
  if (sekarang - waktuCekWiFi >= jedaWiFi) {
    waktuCekWiFi = sekarang;

    if (WiFi.status() == WL_CONNECTED) {
      if (!isOnline) {
        isOnline = true;

        sendTelegramMessage(
          "Smart Room Climate Controller terhubung kembali ke Wi-Fi."
        );
      }
    }
    else {
      isOnline = false;
      Serial.println("Wi-Fi terputus. Mencoba koneksi ulang...");
      WiFi.reconnect();
    }
  }

  // Periksa perintah Telegram.
  if (isOnline &&
      sekarang - waktuSelesaiCekBot >= jedaCekBot) {
    waktuSelesaiCekBot = sekarang;

    int jumlahPesan = bot.getUpdates(
      bot.last_message_received + 1
    );

    while (jumlahPesan > 0) {
      prosesPesanMasuk(jumlahPesan);

      jumlahPesan = bot.getUpdates(
        bot.last_message_received + 1
      );
    }
  }
}