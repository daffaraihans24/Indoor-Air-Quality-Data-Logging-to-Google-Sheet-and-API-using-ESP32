#include <Arduino.h>
#include <AverageValue.h>
#include <DHT.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <ESP_Google_Sheet_Client.h>
#include <GS_SDHelper.h>

//WiFi Declare
String wifiSSID = "YOUR SSID";
String wifiPassword = "YOUR SSID PASSWORD";


//NTP Configuration
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 6*3600;  //WIB Time, Indonesia
const int daylightOffset_sec = 3600;

// Google Project ID
#define PROJECT_ID "YOUR PROJECT ID"

// Service Account's client email
#define CLIENT_EMAIL "your-service-account-client email.iam.gserviceaccount.com"

// Service Account's private key
const char PRIVATE_KEY[] PROGMEM =  "-----BEGIN PRIVATE KEY----------END PRIVATE KEY-----";

// The ID of the spreadsheet where you'll publish the data
const char spreadsheetId[] = "Your Spreadsheet ID";

// Pin Definitions
#define MQ135PIN 34
#define DHTPIN 13  
#define DHTTYPE DHT11
#define SHARP_LED_PIN 4   
#define SHARP_VO_PIN 36 

// LED Indicator
#define WIFI_LED_YELLOW 5 // Wifi is Connected
#define WIFI_LED_RED 18   // WiFi Disconnect
#define DATA_LED_BLUE 19  // Send Data Success
#define DATA_LED_RED 21   // Send Data Failed 
unsigned long previousMillis = 0;


// Sensor Definition and Var
DHT dht(DHTPIN, DHTTYPE);
float humidity;
float temperature;

// Air Quality Variables
const int Rload = 20000;                      
const float rO_CO2 = 21000;                   
const float rO_CO = 2100;
const double CO2_ppm_base = 427;                   
const double CO_ppm_base = 0.3;                   
const float CO2_a = 111.33571680223955;       
const float CO2_b = -2.87179516064990; 
const float CO_a = 572.2866651396; 
const float CO_b = -3.9387598706; 

float CO2_maxppm = 0;
float CO2_minppm = 0;
float CO_maxppm = 0;
float CO_minppm = 0;

// Average Values
const long MAX_VALUES_NUM = 10;         
AverageValue<float> averageValue_CO(MAX_VALUES_NUM);
AverageValue<float> averageValue_CO2(MAX_VALUES_NUM);

// Sharp GP2Y1010AU0F Variables
int samplingTime = 280;
int deltaTime = 40;
int sleepTime = 9680;

float voMeasured = 0;
float calcVoltage = 0;
float dustDensity = 0;

// Google Sheets Client
void tokenStatusCallback(TokenInfo info);

// Function Declarations
void readHum_Temp();
void readAirQuality();
void readDust();
void calculateMinMaxPPM_AirQuality();
void connectWifi();
void syncLocalTime();
void postHTTP();
void postToGoogleSheets();
void calibrateSensors();
void AutoReconnectWiFi();
String getFormattedDate();


// Calibration before sending data and sending data
const unsigned long calibrationTime = 90000; // 1.30 minutes
const unsigned long dataInterval = 20000;    // 20 second 
const unsigned long sensorInterval = 600;     // Reading sensor time
unsigned long startCalibrationMillis = 0;
bool isCalibrating = true;                   



void setup() {
  pinMode(WIFI_LED_YELLOW, OUTPUT);
  pinMode(WIFI_LED_RED, OUTPUT);
  pinMode(DATA_LED_BLUE, OUTPUT);
  pinMode(DATA_LED_RED, OUTPUT);
  pinMode(SHARP_LED_PIN, OUTPUT);

  Serial.begin(115200);
  connectWifi();
  syncLocalTime();
 
  pinMode(MQ135PIN, INPUT);
  dht.begin();
  calculateMinMaxPPM_AirQuality();
  
  GSheet.begin(CLIENT_EMAIL, PROJECT_ID, PRIVATE_KEY);
  GSheet.setTokenCallback(tokenStatusCallback);
  GSheet.setPrerefreshSeconds(10 * 60);

  startCalibrationMillis = millis();
}

void loop() {
  unsigned long currentMillis = millis();
  AutoReconnectWiFi();
  if (isCalibrating) {
    calibrateSensors();
  } else {
    // Sending data every 20s 
    if (currentMillis - previousMillis >= dataInterval) {
      previousMillis = currentMillis;
      readHum_Temp();
      readAirQuality();
      readDust();
      Serial.println("Sending data sensors...");
      postToGoogleSheets();
      postHTTP();
    }
  }
}

void calibrateSensors() {
  unsigned long currentMillis = millis();
  if (currentMillis - startCalibrationMillis < calibrationTime) {
    if (currentMillis - previousMillis >= sensorInterval) {
      previousMillis = currentMillis;
      readHum_Temp();
      readAirQuality();
      readDust();
      digitalWrite(DATA_LED_BLUE, HIGH);
      delay(250);
      digitalWrite(DATA_LED_BLUE, LOW);
      digitalWrite(DATA_LED_RED, HIGH);
      delay(250);
      digitalWrite(DATA_LED_RED, LOW);
      Serial.println("Calibrating sensors...");
    }
  } else {
    // Calibration done
    isCalibrating = false;
    previousMillis = currentMillis;
    Serial.println("Calibration sensors done. Starting to sending data ...");
  }
}

void tokenStatusCallback(TokenInfo info){
    if (info.status == token_status_error){
        GSheet.printf("Token info: type = %s, status = %s\n", GSheet.getTokenType(info).c_str(), GSheet.getTokenStatus(info).c_str());
        GSheet.printf("Token error: %s\n", GSheet.getTokenError(info).c_str());
    }
    else{
        GSheet.printf("Token info: type = %s, status = %s\n", GSheet.getTokenType(info).c_str(), GSheet.getTokenStatus(info).c_str());
    }
}

void postToGoogleSheets() {
  Serial.println("Post to Google Sheets...");

  FirebaseJson response;
  FirebaseJson valueRange;

  // Get timestamp
  String formattedDate = getFormattedDate();

  valueRange.add("majorDimension", "COLUMNS");
  valueRange.set("values/[0]/[0]", formattedDate);
  valueRange.set("values/[1]/[0]", humidity);
  valueRange.set("values/[2]/[0]", temperature);
  valueRange.set("values/[3]/[0]", averageValue_CO2.average());
  valueRange.set("values/[4]/[0]", averageValue_CO.average());
  valueRange.set("values/[5]/[0]", dustDensity);

  // For Google Sheet API ref doc, go to https://developers.google.com/sheets/api/reference/rest/v4/spreadsheets.values/append
  // Append values to the spreadsheet
  bool success = GSheet.values.append(&response /* returned response */, spreadsheetId /* spreadsheet Id to append */, "Sheet1!A1" /* range to append */, &valueRange /* data range to append */);
  if (success){
    response.toString(Serial, true);
    valueRange.clear();
    Serial.println("Post Method Success!");
    // digitalWrite(DATA_LED_BLUE, HIGH);
    // delay(500);
    // digitalWrite(DATA_LED_BLUE, LOW);
    // digitalWrite(DATA_LED_RED, LOW);
  }
  else{
    Serial.println(GSheet.errorReason());
    Serial.println("Post Method Failed!");
    // digitalWrite(DATA_LED_RED, HIGH);
  }
}

void postHTTP() {
  Serial.println("Posting...");
  String url = "YOUR API URL";
  HTTPClient http;
  String response;

  StaticJsonDocument<1000> buff;
  String jsonParams;
  String formattedDate = getFormattedDate();

  
  buff["date"] = formattedDate;
  buff["humidity"] = String(humidity, 2);
  buff["temperature"] = String(temperature, 2);
  buff["co2"] = String(averageValue_CO2.average(), 2);
  buff["co"] = String(averageValue_CO.average(), 2);
  buff["pm25"] = String(dustDensity, 2);

  serializeJson(buff, jsonParams);
  Serial.print("JSON Data: ");
  Serial.println(jsonParams);  // Debug JSON data

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  int statusCode = http.POST(jsonParams);
  response = http.getString();

  Serial.print("HTTP Status Code: ");
  Serial.println(statusCode);
  Serial.print("HTTP Response: ");
  Serial.println(response);

  if (statusCode == 200) {
    Serial.println("Post Method Success!");
    digitalWrite(DATA_LED_BLUE, HIGH);
    delay(500);
    digitalWrite(DATA_LED_BLUE, LOW);
    digitalWrite(DATA_LED_RED, LOW);
  } else {
    Serial.println("Post Method Failed!");
    digitalWrite(DATA_LED_RED, HIGH);
  }

  http.end();
}

void connectWifi() {
  Serial.print("Connecting to WiFi...");
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
  unsigned long wifiConnectStart = millis();

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    digitalWrite(WIFI_LED_YELLOW, HIGH);
    delay(50);
    digitalWrite(WIFI_LED_YELLOW, LOW);
    delay(50);
    if (millis() - wifiConnectStart >= 10000) { 
      Serial.println("\nWiFi Connection Failed");
      digitalWrite(WIFI_LED_RED, HIGH);
      return;
    }
  }
  Serial.println("\nWiFi Connected");
  digitalWrite(WIFI_LED_YELLOW, HIGH);
  digitalWrite(WIFI_LED_RED, LOW);
  Serial.println(WiFi.localIP());
}

void AutoReconnectWiFi(){
  if (WiFi.status()!=WL_CONNECTED){
    Serial.println("Wifi Disconnect");
    connectWifi();
  }
}

void syncLocalTime() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

String getFormattedDate() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Failed to obtain time";
  }
  char buffer[80];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

void readHum_Temp() {
  humidity = dht.readHumidity();
  temperature = dht.readTemperature();
  
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println(F("Failed to read from DHT sensor!"));
    return;
  }
  
  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.println("%");
  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println("°C");
}

void readAirQuality() {
  float adcRaw = analogRead(MQ135PIN);
  float rS = ((4095 * Rload) / adcRaw) - Rload;
  
  float CO2_rSrO = rS / rO_CO2;
  float CO_rSrO = (rS / rO_CO) / 10;
  
  float CO2_ppm = CO2_a * pow((float)rS / (float)rO_CO2, CO2_b);
  averageValue_CO2.push(CO2_ppm);
  Serial.print("CO2 : ");
  Serial.print(averageValue_CO2.average(), 2); 
  Serial.println(" ppm");
  
  float CO_ppm = CO_a * pow((float)rS / (float)rO_CO, CO_b);
  averageValue_CO.push(CO_ppm);
  Serial.print("CO : ");
  Serial.print(averageValue_CO.average(), 2); 
  Serial.println(" ppm");
}

void readDust() {
  digitalWrite(SHARP_LED_PIN, LOW); // power on the LED
  delayMicroseconds(samplingTime);

  voMeasured = analogRead(SHARP_VO_PIN); // read the dust value

  delayMicroseconds(deltaTime);
  digitalWrite(SHARP_LED_PIN, HIGH); // turn the LED off
  delayMicroseconds(sleepTime);

  calcVoltage = voMeasured * (3.3 / 4095.0);
  dustDensity = 242.48 * calcVoltage - 0.1;

  // Ensure dustDensity is not negative
  dustDensity = max(dustDensity, 0.0f);
  
  Serial.print("PM2.5 Density: ");
  Serial.println(dustDensity); 
  Serial.println(" ug/m3");
}

void calculateMinMaxPPM_AirQuality() {
  // min[Rs/Ro] = (max[ppm]/a)^(1/b)
  // max[Rs/Ro] = (min[ppm]/a)^(1/b)
  
  // Calculate min and max PPM values for CO2
  CO2_minppm = pow((1000 / CO2_a), 1 / CO2_b);
  CO2_maxppm = pow((10 / CO2_a), 1 / CO2_b);

  // Calculate min and max PPM values for CO
  CO_minppm = pow((1000 / CO_a), 1 / CO_b);
  CO_maxppm = pow((10 / CO_a), 1 / CO_b);
}

