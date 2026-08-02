#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <BH1750.h>
#include <DHT.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <deque>
#include <time.h>

//inisialisasi LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);
void clearLine(int line) {
  lcd.setCursor(0, line);
  lcd.print("                ");
}
//inisialisasi LightSensor
BH1750 lightMeter(0x23);
float luxValue;
//inisialisasi TempSensor
DHT dht(32, DHT22);
float humidValue;
float tempValue;
//inisialisasi GasSensor
const int mq135Pin = 34;
float RLOAD = 10; 
float RZERO = 11.52; //R0 paling aman 
float PARA = 4100;//15200;
float PARB = 1.71;//2.7690;
float ppmValue;
//inisialisasi timer
unsigned long delayGetData = 0;
//inisialisasi array prediksi
std::deque<String> labelHistory = {"07:00", "07:30", "08:00", "08:30", "09:00"}; //data dummy
std::deque<float> tempHistory = {26, 27, 28, 30, 31};
std::deque<float> humidHistory = {85, 82, 80, 75, 70};
std::deque<float> co2History = {400, 410, 405, 415, 420};
std::deque<float> lightHistory = {500, 1500, 4000, 8000, 11000};
//inisialisasi Wifi SSID
const char* SSID = "HUAWEI";
const char* Password = "aXV5AAgg";
IPAddress ip;
//inisialisasi WebServer
AsyncWebServer server(80);
//inisialisasi networktime
const char* ntpServer = "0.id.pool.ntp.org";
const long  gmtOffset_sec = 25200;
const int   daylightOffset_sec = 0;
int lastSavedHour = -1;
int lastSavedMinute = -1;
String CurrentTime() { //fungsi ambil waktu
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return "";
  }
  char timeStr[6];  // HH:MM
  strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
  return String(timeStr);
}
void saveHistory() { //fungsi save histori 30 menit
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo)) {
    return;
  }
  if ((timeinfo.tm_min == 0 || timeinfo.tm_min == 30) && (timeinfo.tm_hour != lastSavedHour || timeinfo.tm_min != lastSavedMinute)) {
    lastSavedHour = timeinfo.tm_hour;
    lastSavedMinute = timeinfo.tm_min;

    if (labelHistory.size() >= 10) {
      labelHistory.pop_front();
      tempHistory.pop_front();
      humidHistory.pop_front();
      co2History.pop_front();
      lightHistory.pop_front();
    }

    labelHistory.push_back(CurrentTime());
    tempHistory.push_back(tempValue);
    humidHistory.push_back(humidValue);
    co2History.push_back(ppmValue);
    lightHistory.push_back(luxValue);

    Serial.println("History saved");
  }
}
//fungsi baca sensor DHT
float getDhtTemp(){
  return dht.readTemperature() - 2; //-4
}
float getDhtHumid(){
  return dht.readHumidity(); //+4 +8
}

float getResistance(int adcValue) { //fungsi ambil resistansi sensor MQ135
 if(adcValue <= 0){
    return NAN;
  }
  float voltage = (adcValue / 4095.0) * 3.3;
  return ((3.3 - voltage)/voltage) * RLOAD; 
}

int getAdc(){ //ambil adc mq135
  return analogRead(mq135Pin);
}
float calibrateR0() { // fungsi kalibrasi RZERO
  int samples = 60;
  float resistanceSum = 0;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Calibrating...");
  for (int i = samples; i > 0; i--) {
    int adcValue = getAdc();
    float resistance = getResistance(adcValue);
    resistanceSum += resistance;
    Serial.printf("Resistance: %.2f\n", resistance);
    lcd.setCursor(0, 1);
    lcd.print(i);
    if(i > 1){
      lcd.print(" seconds");
    } else{
      lcd.print(" second");
    }
    lcd.print(" left     ");
    delay(1000);
  }

  float resistanceAverage = resistanceSum / samples;
  float r0Value = resistanceAverage / 3.6;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("R0 Done:");
  lcd.setCursor(0, 1);
  lcd.print(r0Value, 2);

  delay(2000);
  return r0Value;
}

//fungsi WebServer
void setupRoutes() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/style.css", "text/css");
  });

  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request){
    

    String json = "{";
    json += "\"temperature\":" + String(tempValue, 2) + ",";
    json += "\"humidity\":" + String(humidValue, 2) + ",";
    json += "\"co2\":" + String(ppmValue) + ",";
    json += "\"light\":" + String(luxValue) + ",";
    json += "\"temp-history\":[";
    for (size_t i = 0; i < tempHistory.size(); i++) {
      json += String(tempHistory[i], 2);
      if (i < tempHistory.size() - 1) json += ",";
    }
    json += "],";
    json += "\"humid-history\":[";
    for (size_t i = 0; i < humidHistory.size(); i++) {
      json += String(humidHistory[i], 2);
      if (i < humidHistory.size() - 1) json += ",";
    }
    json += "],";
    json += "\"co2-history\":[";
    for (size_t i = 0; i < co2History.size(); i++) {
      json += String(co2History[i], 2);
      if (i < co2History.size() - 1) json += ",";}
    json += "],";
    json += "\"light-history\":[";
    for (size_t i = 0; i < lightHistory.size(); i++) {
      json += String(lightHistory[i], 2);
      if (i < lightHistory.size() - 1) json += ",";}
    json += "],";
    json += "\"label-history\":[";
    for (size_t i = 0; i < labelHistory.size(); i++) {
      json += "\"" + labelHistory[i] + "\"";
      if (i < labelHistory.size() - 1) json += ",";
    }
    json += "]";
    json += "}";

    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
  });
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  lcd.init();
  lcd.backlight();
  Wire.begin(21, 22); //inisialisasi I2C
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  if (!LittleFS.begin()) {
  Serial.println("LittleFS gagal mount");
  return;
  }

  lcd.setCursor(0, 0);
  lcd.print("Booting Up!");
  delay(2000);

  RZERO = calibrateR0();
  delay(2000); 

  lcd.clear();
  lcd.setCursor(0, 0);
  WiFi.begin(SSID, Password);
  lcd.print("Connecting Wifi");
  while (WiFi.status() != WL_CONNECTED) {
    lcd.setCursor(0, 1);
    lcd.print(".");
    delay(500);
    lcd.print(".");
    delay(500);
    lcd.print(".");
    delay(500);
    clearLine(1);
  }
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Wifi Connected!");
  lcd.setCursor(0, 1);
  ip = WiFi.localIP();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  lcd.print(ip);
  Serial.printf("R0: %.2f\n", RZERO);
  Serial.printf("IP Address: %s\n", ip.toString().c_str());
  setupRoutes();
  server.begin();
}

void loop() {
  if (millis() - delayGetData >= 2000) {
    delayGetData = millis();
    //fungsi DHT baca temp dan humid
    humidValue = getDhtHumid();
    tempValue = getDhtTemp();
    Serial.print("Humidity: ");
    Serial.print(humidValue);
    Serial.print("%  Temprature: ");
    Serial.print(tempValue);
    Serial.print("°C  CO2: ");
    //fungsi MQ135 baca CO2
    int adcValue = getAdc();
    float resistance = getResistance(adcValue);
    float ratio = resistance / RZERO;
    ppmValue = PARA * pow(ratio, -PARB);
    Serial.print(ppmValue);
    Serial.print("PPM  Resistance: ");
    Serial.print(resistance);
    Serial.print("ohm  Ratio: ");
    Serial.print(ratio);
    Serial.print("  Cahaya: ");
    //fungsi BH1750 baca cahaya
    luxValue = lightMeter.readLightLevel();
    Serial.print(luxValue);
    Serial.println("");
  }
  saveHistory();
}
