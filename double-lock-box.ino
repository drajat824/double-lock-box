#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <time.h>
#include <TZ.h>
#include <FS.h>
#include <LittleFS.h>
#include <CertStoreBearSSL.h>
#include <ArduinoJson.h>
#include <Adafruit_Fingerprint.h>
#include <ESP8266HTTPClient.h>

// Menginisialisasi komunikasi serial untuk sensor sidik jari menggunakan SoftwareSerial di pin 13 (RX) dan 15 (TX).
SoftwareSerial mySerial(13, 15);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

uint8_t id = 0;           // Variabel untuk menyimpan ID sidik jari yang terdeteksi.
const int relay_dl = 12;  // Pin yang digunakan untuk mengendalikan relay brankas.
const int ledPin_merah = 9;
const int ledPin_hijau = 10;
const int buzzer = 4;
const int getar = 14;
bool ledState = LOW;

// Update these with values suitable for your network.
const char *ssid = "DLB94859";
const char *password = "12345678";

const char *mqtt_server = "395d0f154c95495889ba0205f5623f66.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;

const char *apiKey = "MzBlZGFkNTUtM2IyZS00ODA1LTg2N2MtZDZlNzVlNjc1MzAy";  // Ganti dengan kunci API OneSignal Anda
const char *appID = "49b0050a-99f4-4842-ba5b-db1b516fb6cb";

const char *mqtt_username = "test123";
const char *mqtt_password = "Test1234@";

//ID Perangkat & PIN
const char *id_perangkat_default = "87654321";
const char *pin_perangkat_default = "8765";

//Inisialisasi variabel untuk menghitung upaya fingerprint yang salah
int wrongAttempts = 0;
int maxWrongAttempts = 7;

bool isEnroll = false;  //apakah fingerprint digunakan untuk mendaftar

bool isFingerActive = true;        //apakah fingerprint aktiv
bool isVibrateActive = true;       //apakah getaran aktiv
bool isNotificationActive = true;  //apakah getaran aktiv

StaticJsonDocument<200> doc;

// A single, global CertStore which can be used by all connections.
// Needs to stay live the entire time any of the WiFiClientBearSSLs
// are present.
BearSSL::CertStore certStore;

BearSSL::WiFiClientSecure *bear = new BearSSL::WiFiClientSecure();

PubSubClient *client;
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (500)
char msg[MSG_BUFFER_SIZE];
int value = 0;

unsigned long previousMillis = 0;
const long interval = 3000;  // Waktu dalam milidetik (3 detik)

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  delay(500);
  // When opening the Serial Monitor, select 4800 Baud
  Serial.begin(115200);
  delay(500);

  pinMode(ledPin_merah, OUTPUT);
  pinMode(ledPin_hijau, OUTPUT);
  pinMode(buzzer, OUTPUT);
  pinMode(relay_dl, OUTPUT);
  pinMode(getar, INPUT);

  LittleFS.begin();
  setup_wifi();
  setDateTime();

  pinMode(LED_BUILTIN, OUTPUT);  // Initialize the LED_BUILTIN pin as an output
  delay(500);

  // you can use the insecure mode, when you want to avoid the certificates
  //espclient->setInsecure();

  int numCerts = certStore.initCertStore(LittleFS, PSTR("/certs.idx"), PSTR("/certs.ar"));
  Serial.printf("Number of CA certs read: %d\n", numCerts);
  if (numCerts == 0) {
    Serial.printf("No certs found. Did you run certs-from-mozilla.py and upload the LittleFS directory before running?\n");
    return;  // Can"t connect to anything w/o certs!
  }

  // Integrate the cert store with this connection
  bear->setCertStore(&certStore);

  client = new PubSubClient(*bear);

  client->setServer(mqtt_server, mqtt_port);
  client->setCallback(callback);

  // Menginisialisasi sensor sidik jari.
  finger.begin(57600);  // Kecepatan komunikasi dengan sensor sidik jari.
  delay(5);
  if (finger.verifyPassword()) {
    Serial.println("Found fingerprint sensor!");  // Sensor sidik jari berhasil terdeteksi.
  } else {
    Serial.println("Did not find fingerprint sensor :(");  // Sensor sidik jari tidak ditemukan.
    while (1) {
      delay(1);
    }
  }

  finger.getTemplateCount();  // Mendapatkan jumlah templat sidik jari yang tersimpan.
  Serial.print("Sensor contains ");
  Serial.print(finger.templateCount);
  Serial.println(" templates");

  delay(250);

  digitalWrite(ledPin_hijau, LOW);
  digitalWrite(ledPin_merah, HIGH);
  digitalWrite(relay_dl, HIGH);
}

void sendNotification() {
  HTTPClient https;
  //  WiFiClientSecure httpClient;
  // BearSSL::WiFiClientSecure espClient;
   bear->setInsecure();
  // delay(100);
  // Buat payload notifikasi JSON
  String json = "{\"app_id\":\"" + String(appID) + "\",\"included_segments\":[\"All\"],\"contents\":{\"en\":\"Perangkat anda sedang tidak aman!\"}}";

  https.begin(*bear, "https://onesignal.com/api/v1/notifications");
  https.addHeader("content-type", "application/json");
  https.addHeader("accept", "application/json");
  https.addHeader("Authorization", "Basic " + String(apiKey));

  int httpCode = https.POST(json);

  if (httpCode > 0) {
    String payload = https.getString();
    Serial.println("Notifikasi terkirim dengan kode: " + String(httpCode));
    Serial.println("Response: " + payload);
  } else {
    Serial.println("Gagal mengirim notifikasi dengan kode: " + String(httpCode));
    Serial.println("Error: " + https.errorToString(httpCode));
  }

  https.end();
}

void setDateTime() {
  // You can use your own timezone, but the exact time is not used at all.
  // Only the date is needed for validating the certificates.
  configTime(TZ_Asia_Jakarta, "pool.ntp.org", "time.nist.gov");

  Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(100);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println();

  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.printf("%s %s", tzname[0], asctime(&timeinfo));
}

void SendDeviceStatus() {
  delay(250);

  doc["type"] = "device-status";
  doc["gembok"] = digitalRead(relay_dl) == HIGH;  // True = Tertutup | False = Terbuka
  doc["finger"] = isFingerActive;
  doc["vibrate"] = isVibrateActive;
  doc["notification"] = isNotificationActive;

  String jsonString = "";
  serializeJson(doc, jsonString);

  client->publish("response-mobile", jsonString.c_str());
  Serial.println("Status Dikirim.");
}

void callback(char *topic, byte *payload, unsigned int length) {
  // Buat variabel String untuk menampung pesan JSON
  String message = "";

  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  //Mengurai pesan JSON
  DeserializationError error = deserializeJson(doc, message);

  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.print(message);
    Serial.println(error.f_str());
    return;
  }

  const char *id_perangkat_request_const = doc["id_perangkat"];
  const char *pin_perangkat_request_const = doc["pin_perangkat"];
  const char *command_request_const = doc["command"];

  String id_perangkat_request(id_perangkat_request_const);
  String pin_perangkat_request(pin_perangkat_request_const);
  String command_request(command_request_const);

  //Authentikasi

  if (command_request == "authentication") {
    if (id_perangkat_request == id_perangkat_default && pin_perangkat_request == pin_perangkat_default) {

      doc["type"] = "auth";
      doc["status"] = true;
      doc["message"] = "Berhasil masuk perangkat";

      String jsonString = "";
      serializeJson(doc, jsonString);

      client->publish("response-mobile", jsonString.c_str());

    } else {

      doc["type"] = "auth";
      doc["status"] = false;
      doc["message"] = "Perangkat tidak ditemukan, periksa ID dan Pin";

      String jsonString = "";
      serializeJson(doc, jsonString);

      client->publish("response-mobile", jsonString.c_str());
    }
  }

  //Rekam Sidik Jari

  if (command_request == "/rekam-sidik") {
    id++;
    isFingerActive = false;
    delay(250);
    isEnroll = true;

    Serial.print("Enrolling ID #");
    Serial.println(id);

    doc["type"] = "fingerprint-set-loading";
    doc["status"] = true;

    String jsonString = "";
    serializeJson(doc, jsonString);

    client->publish("response-mobile", jsonString.c_str());

    Serial.println("Ready to enroll a fingerprint!");

    if (id == 0) {  // ID #0 not allowed, try again!
      isFingerActive = true;
      delay(250);
      isEnroll = false;
      doc["type"] = "fingerprint-set";
      doc["status"] = false;
      doc["message"] = "Gagal mendaftarkan sidik jari dengan ID: ";

      String jsonString = "";
      serializeJson(doc, jsonString);

      client->publish("response-mobile", jsonString.c_str());
      return;
    }
  }

  //Perangkat

  if (command_request == "/matikan-alat") {
    Serial.println("Matikan alat.");

    //Mematikan
    isFingerActive = false;
    isVibrateActive = false;
    isNotificationActive = false;

    digitalWrite(ledPin_merah, LOW);
    digitalWrite(ledPin_hijau, LOW);
    digitalWrite(buzzer, LOW);
    digitalWrite(relay_dl, HIGH);

    SendDeviceStatus();
  }

  if (command_request == "/hidupkan-alat") {
    Serial.println("Hidupkan alat.");

    //Menghidupkan
    isFingerActive = true;
    isVibrateActive = true;
    isNotificationActive = true;

    SendDeviceStatus();
  }

  //Finger

  if (command_request == "/hidupkan-finger") {
    Serial.println("Hidupkan sensor finger.");

    //Menghidupkan
    isFingerActive = true;

    SendDeviceStatus();
  }

  if (command_request == "/matikan-finger") {
    Serial.println("Matikan sensor finger.");

    //Mematikan
    isFingerActive = false;

    SendDeviceStatus();
  }

  //Getar

  if (command_request == "/hidupkan-getar") {
    Serial.println("Hidupkan sensor getaran.");

    //Menghidupkan
    isVibrateActive = true;

    SendDeviceStatus();
  }

  if (command_request == "/matikan-getar") {
    Serial.println("Hidupkan sensor getaran.");

    //Mematikan
    isVibrateActive = false;

    SendDeviceStatus();
  }

  //Notifiaksi

  if (command_request == "/hidupkan-notifikasi") {
    Serial.println("Hidupkan notifikasi.");

    //Menghidupkan
    isNotificationActive = true;

    SendDeviceStatus();
  }

  if (command_request == "/matikan-notifikasi") {
    Serial.println("Matikan notifikasi.");

    //Mematikan
    isNotificationActive = false;

    SendDeviceStatus();
  }

  if (command_request == "/tutup-gembok") {
    Serial.println("Tutup gembok.");
    digitalWrite(ledPin_merah, HIGH);
    digitalWrite(ledPin_hijau, LOW);
    digitalWrite(relay_dl, HIGH);

    SendDeviceStatus();
  }

  //Buzzer
  if (command_request == "/matikan-buzzer") {
    Serial.println("Buzzer Dimatikan.");
    digitalWrite(buzzer, LOW);
  }

  if (command_request == "/status") {
    SendDeviceStatus();
  }

  if (command_request == "/delete-finger") {
    for (int id = 1; id <= 50; id++) {
      if (finger.deleteModel(id) == FINGERPRINT_OK) {
        Serial.print("Menghapus sidik jari ID #");
        Serial.println(id);
      } else {
        Serial.print("Gagal menghapus sidik jari ID #");
        Serial.println(id);
      }
    }
  }
}

void reconnect() {
  // Loop until we’re reconnected
  while (!client->connected()) {
    Serial.print("Attempting MQTT connection…");
    String clientId = "ESP8266Client - MyClient";
    // Attempt to connect
    // Insert your password
    if (client->connect(clientId.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("mqtt connected");
      client->subscribe("request-mobile");
    } else {
      Serial.print("failed, rc = ");
      Serial.print(client->state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

uint8_t getFingerprintID() {

  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return -1;

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return -1;

  p = finger.fingerFastSearch();
  if (p != FINGERPRINT_OK) {

    delay(250);

    digitalWrite(ledPin_hijau, LOW);   // Matikan LED hijau.
    digitalWrite(ledPin_merah, HIGH);  // Nyalakan LED merah.
    digitalWrite(relay_dl, HIGH);      // Tutup gembok
    SendDeviceStatus();

    wrongAttempts++;
    Serial.print("WrongAttemps: ");
    Serial.println(wrongAttempts);

    if (wrongAttempts >= maxWrongAttempts) {
      wrongAttempts = 0;
      digitalWrite(buzzer, HIGH);  // Nyalakan buzzer.
      Serial.println("Ada Penyusup!");

      if (isNotificationActive) {
        sendNotification();
        // doc["type"] = "notification";
        // doc["status"] = true;
        // doc["message"] = "Ada Penyusup!";

        // String jsonString = "";
        // serializeJson(doc, jsonString);

        // client->publish("response-mobile", jsonString.c_str());
      }
    }

    return -1;
  } else {
    wrongAttempts = 0;
    // found a match!
    Serial.print("Found ID #");
    Serial.print(finger.fingerID);

    delay(250);

    digitalWrite(relay_dl, LOW);       // Buka gembok
    digitalWrite(ledPin_hijau, HIGH);  // Nyalakan LED hijau.
    digitalWrite(buzzer, LOW);         //matikan buzzer
    digitalWrite(ledPin_merah, LOW);   // Matikan LED merah.
    SendDeviceStatus();
  }

  return finger.fingerID;
}

uint8_t getFingerprintEnroll() {
  int p = -1;  // Variabel status operasi
  Serial.print("Waiting for a valid finger to enroll as #");
  Serial.println(id);

  // Menunggu hingga sidik jari yang valid ditemukan
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    if (p == FINGERPRINT_OK) {
      Serial.println("Image taken");
    } else if (p == FINGERPRINT_NOFINGER) {
      Serial.println(".");
    } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
      Serial.println("Communication error");
    } else if (p == FINGERPRINT_IMAGEFAIL) {
      Serial.println("Imaging error");
    } else {
      Serial.println("Unknown error");
    }
  }

  // Sidik jari yang valid telah diambil

  p = finger.image2Tz(1);
  if (p == FINGERPRINT_OK) {
    Serial.println("Image converted");
  } else if (p == FINGERPRINT_IMAGEMESS) {
    Serial.println("Image too messy");
    return false;
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    return false;
  } else if (p == FINGERPRINT_FEATUREFAIL || p == FINGERPRINT_INVALIDIMAGE) {
    Serial.println("Could not find fingerprint features");
    return false;
  } else {
    Serial.println("Unknown error");
    return false;
  }

  Serial.println("Remove finger");
  p = 0;

  // Menunggu hingga jari diangkat
  while (p != FINGERPRINT_NOFINGER) {
    p = finger.getImage();
  }
  Serial.print("ID ");
  Serial.println(id);
  p = -1;

  Serial.print("ID ");
  Serial.println(id);
  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK) {
    isEnroll = false;
    Serial.println("Stored!");

    doc["type"] = "fingerprint-set";
    doc["status"] = true;

    String jsonString = "";
    serializeJson(doc, jsonString);

    client->publish("response-mobile", jsonString.c_str());

    delay(2000);

    isFingerActive = true;

    return true;
  } else {
    // Menangani kesalahan yang mungkin terjadi saat menyimpan model
    if (p == FINGERPRINT_PACKETRECIEVEERR) {
      Serial.println("Communication error");
    } else if (p == FINGERPRINT_BADLOCATION) {
      Serial.println("Could not store in that location");
    } else if (p == FINGERPRINT_FLASHERR) {
      Serial.println("Error writing to flash");
    } else {
      Serial.println("Unknown error");
    }
    return false;
  }
}


void getVibrate() {
  int sensorValue = digitalRead(getar);  // Baca status sensor getar.
  delay(250);

  Serial.print("Sensor Value: ");
  Serial.println(sensorValue);

  if (sensorValue > 0) {
    digitalWrite(buzzer, HIGH);        // Nyalakan buzzer.
    digitalWrite(ledPin_hijau, LOW);   // Matikan LED hijau.
    digitalWrite(ledPin_merah, HIGH);  // Nyalakan LED merah.
    digitalWrite(relay_dl, HIGH);      // Tutup gembok

    SendDeviceStatus();

    Serial.println("Ada Penyusup!");

    if (isNotificationActive) {
      sendNotification();
      // doc["type"] = "notification";
      // doc["status"] = true;
      // doc["message"] = "Ada Penyusup!";

      // String jsonString = "";
      // serializeJson(doc, jsonString);

      // client->publish("response-mobile", jsonString.c_str());
    }
  }
}

void loop() {

  Serial.print("STATUS GEMBOK: ");
  Serial.println(digitalRead(relay_dl) == HIGH);

  if (!client->connected()) {
    reconnect();
  }
  client->loop();

  Serial.print("isEnroll:");
  Serial.println(isEnroll);

  if (isEnroll) {
    while (!getFingerprintEnroll())
      ;
  } else {
    if (isFingerActive) {
      getFingerprintID();
    }
  }

  delay(50);  // Memberi jeda.

  if (isVibrateActive) {
    getVibrate();
  }
}
