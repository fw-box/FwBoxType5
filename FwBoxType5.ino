//
// Copyright (c) 2020 Fw-Box (https://fw-box.com)
// Author: Hartman Hsieh
//
// Description :
//   None
//
// Connections :
//
// Required Library :
// arduino-cli lib install PubSubClient
// arduino-cli lib install U8g2
// arduino-cli lib install "Adafruit BMP085 Library"
// arduino-cli lib install BH1750
// arduino-cli lib install Sodaq_SHT2x
//

#include "FwBox.h"
#include <Sodaq_SHT2x.h>
#include <Adafruit_BMP085.h>
#include <BH1750.h> // Light Sensor (BH1750)
#include "FwBox_UnifiedLcd.h"
#include <U8g2lib.h>
//#include <WiFiUdp.h> // For NTP function
#include "FwBox_NtpTime.h"
#include "FwBox_TwWeather.h"
#include "FwBox_U8g2Widget.h"

#define DEVICE_TYPE 5
#define FIRMWARE_VERSION "1.1.4"

#define ANALOG_VALUE_DEBOUNCING 8

//
// Debug definitions
//
#define FW_BOX_DEBUG 0

#if FW_BOX_DEBUG == 1
  #define DBG_PRINT(VAL) Serial.print(VAL)
  #define DBG_PRINTLN(VAL) Serial.println(VAL)
  #define DBG_PRINTF(FORMAT, ARG) Serial.printf(FORMAT, ARG)
  #define DBG_PRINTF2(FORMAT, ARG1, ARG2) Serial.printf(FORMAT, ARG1, ARG2)
#else
  #define DBG_PRINT(VAL)
  #define DBG_PRINTLN(VAL)
  #define DBG_PRINTF(FORMAT, ARG)
  #define DBG_PRINTF2(FORMAT, ARG1, ARG2)
#endif

//
// Global variable
//
const char* WEEK_DAY_NAME[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"}; // Days Of The Week 

//
// LCD 1602
//
FwBox_UnifiedLcd* Lcd = 0;

//
// OLED 128x128
//
U8G2_SSD1327_MIDAS_128X128_1_HW_I2C* u8g2 = 0;

//
// Light Sensor (BH1750FVI)
//
BH1750 SensorLight;

//
// Sensor - BMP180
//
Adafruit_BMP085 SensorBmp;

bool SensorShtReady = false;
bool SensorLightReady = false;
bool SensorBmpReady = false;


//
// The sensor's values
//
float HumidityValue = 0.0;
float TemperatureValue = 0.0;
float LightValue = 0.0;
int PressureValue = 0;
String RemoteMessage = "RemoteMessage";

//
// The library for the Taiwan's weather.
//
FwBox_TwWeather TwWeather;
FwBox_WeatherResult WeatherResult;

String ValUnit[MAX_VALUE_COUNT];

unsigned long ReadingTime = 0;
unsigned long ReadingTimeWeather = 0;

int DisplayMode = 0;

String Paramaters[2];

void setup()
{
  Wire.begin();  // Join IIC bus for Light Sensor (BH1750).
  Serial.begin(9600);

  fbEarlyBegin(DEVICE_TYPE, FIRMWARE_VERSION);

  //
  // Set a default time, avoid system crashed in function - 'display'.
  //
  //setTime(6,0,0,1,1,2016);

  pinMode(LED_BUILTIN, OUTPUT);

  //
  // Initialize the LCD1602
  //
  Lcd = new FwBox_UnifiedLcd(16, 2);
  if (Lcd->begin() != 0) {
    //
    // LCD1602 doesn't exist, delete it.
    //
    delete Lcd;
    Lcd = 0;
#if DEBUG == 1
    DBG_PRINTLN("LCD1602 initialization failed.");
#endif // #if DEBUG == 1
  }

  //
  // Detect the I2C address of OLED.
  //
  Wire.beginTransmission(0x78>>1);
  if (Wire.endTransmission() == 0) { // Exist
    //
    // Initialize the OLED
    //
    u8g2 = new U8G2_SSD1327_MIDAS_128X128_1_HW_I2C(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);  /* Uno: A4=SDA, A5=SCL, add "u8g2.setBusClock(400000);" into setup() for speedup if possible */
    u8g2->begin();
    u8g2->enableUTF8Print();
    u8g2->setFont(u8g2_font_unifont_t_chinese1);  // use chinese2 for all the glyphs of "你好世界"
  }
  else {
#if DEBUG == 1
    DBG_PRINTLN("U8G2_SSD1327_MIDAS_128X128_1_HW_I2C is not found.");
#endif // #if DEBUG == 1
    u8g2 = 0;
  }

  //
  // Set the unit of the values before "display".
  //
  ValUnit[0] = "°C";
  ValUnit[1] = "%";
  ValUnit[2] = "Lux";
  ValUnit[3] = "Pa";

  //
  // Display the screen
  //
  display(analogRead(A0)); // Read 'A0' to change the display mode.

  //
  // Initialize the fw-box core
  //
  fbBegin(DEVICE_TYPE, FIRMWARE_VERSION);

  //
  // Setup MQTT subscribe callback
  //
  setRcvValueCallback(onReceiveValue);

  //
  // Scan SHT2X
  //
  Wire.beginTransmission(0x40);
  if (Wire.endTransmission() == 0) { // Exist
      SensorShtReady = true;
  }

  //
  // Initialize the Light Sensor
  //
  if (SensorLight.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    SensorLightReady = true;
#if DEBUG == 1
    DBG_PRINTLN("BH1750 Advanced begin");
#endif // #if DEBUG == 1
  }
  else {
#if DEBUG == 1
    DBG_PRINTLN("Error initialising BH1750");
#endif // #if DEBUG == 1
  }
  delay(1000);

  //
  // Initialize the BMP Sensor
  //
  if (SensorBmp.begin()) {
    SensorBmpReady = true;
  }
  else {
#if DEBUG == 1
    DBG_PRINTLN("Could not find a valid BMP180 sensor, check wiring!");
#endif // #if DEBUG == 1
  }
  delay(1000);

  //
  // Get the and unit of the values from fw-box server after "fbBegin".
  //
  for (int vi = 0; vi < 4; vi++) {
    if (FwBoxIns.getValUnit(vi).length() > 0)
      ValUnit[vi] = FwBoxIns.getValUnit(vi);
  }

  //
  // Init the library
  //

  //String ps[2];
  int ac = FwBoxIns.getParameterArray(Paramaters, 2);
  DBG_PRINTF("ParameterArray Count = %d\n", ac);
  DBG_PRINTLN(Paramaters[0]);
  DBG_PRINTLN(Paramaters[1]);
  if(Paramaters[0].length() <= 0) {
    //
    // Default location
    //
    Paramaters[0] = "新北市";
    Paramaters[1] = "板橋區";
  }
  DBG_PRINTLN(Paramaters[0]);
  DBG_PRINTLN(Paramaters[1]);
  TwWeather.begin("CWB-01A3B3EB-21FC-46FA-B5DF-D8178A1A8437", Paramaters[0], Paramaters[1]);

  //
  // Sync NTP time
  //
  FwBox_NtpTimeBegin();

  //WiFi.disconnect();

} // void setup()

void loop()
{
  if ((ReadingTime == 0) || ((millis() - ReadingTime) > 2000)) {
    //
    // Read sensors
    //
    readSensor();

    //
    // Check if any reads failed.
    //
    if (isnan(HumidityValue) || isnan(TemperatureValue)) {
    }
    else {
      //
      // Filter the wrong values.
      //
      if( (TemperatureValue > 1) &&
          (TemperatureValue < 70) && 
          (HumidityValue > 10) &&
          (HumidityValue < 95) ) {
  
        FwBoxIns.setValue(0, TemperatureValue);
        FwBoxIns.setValue(1, HumidityValue);
      }
    }

    if (LightValue > 0) {
      FwBoxIns.setValue(2, LightValue);
    }

    if (PressureValue > (101325 / 3)) { // 101325 Pa = 1 atm
      FwBoxIns.setValue(3, PressureValue);
    }

    ReadingTime = millis();
  } // END OF "if((ReadingTime == 0) || ((millis() - ReadingTime) > 2000))"

  if ((ReadingTimeWeather == 0) || ((millis() - ReadingTimeWeather) > 60*60*1000) || (WeatherResult.Wx1.length() <= 0)) {
    if ((WiFi.status() == WL_CONNECTED)) {
      WeatherResult = TwWeather.read(year(), month(), day(), hour(), minute());//TwWeather.read(0,0,0,0,0);
      if (WeatherResult.WxResult == true) {
        DBG_PRINTLN(WeatherResult.Wx1);
        DBG_PRINTLN(WeatherResult.Wx2);
        DBG_PRINTLN(WeatherResult.Wx3);
      }
      if (WeatherResult.TResult == true) {
        DBG_PRINTLN(WeatherResult.T1);
        DBG_PRINTLN(WeatherResult.T2);
        DBG_PRINTLN(WeatherResult.T3);
      }

      ReadingTimeWeather = millis();
    }
  }

  //
  // Display the screen
  //
  display(analogRead(A0)); // Read 'A0' to change the display mode.

  //
  // Run the handle
  //
  fbHandle();

} // END OF "void loop()"

void onReceiveValue(int valueIndex, String* payload)
{
  DBG_PRINT("onReceiveValue valueIndex = ");
  DBG_PRINTLN(valueIndex);
  DBG_PRINT("onReceiveValue *payload = ");
  DBG_PRINTLN(*payload);

  if (valueIndex == 4) { // LED
    payload->toUpperCase();
    if (payload->equals("ON") == true)
    {
      digitalWrite(LED_BUILTIN, LOW); // This value is reverse
      FwBoxIns.mqttPublish(valueIndex, "ON"); // Sync the status to MQTT server
    }
    else
    {
      digitalWrite(LED_BUILTIN, HIGH);
      FwBoxIns.mqttPublish(valueIndex, "OFF"); // Sync the status to MQTT server
    }
  }
  else if (valueIndex == 5) { // Remote Message
    RemoteMessage = *payload;
  }
}

void readSensor()
{
  if (SensorShtReady == true) {
    //
    // Read the temperature as Celsius (the default)
    // Calculate the average temperature of sensors - SHT and BMP.
    //
    TemperatureValue = (SHT2x.GetTemperature() + SensorBmp.readTemperature()) / 2;
    DBG_PRINTF2("Temperature : %f %s\n", TemperatureValue, ValUnit[0].c_str());

    //
    // Read the humidity(Unit:%)
    //
    HumidityValue = SHT2x.GetHumidity();
    DBG_PRINTF2("Humidity : %f %s\n", HumidityValue, ValUnit[1].c_str());
  }

  //
  // Read the Light level (Unit:Lux)
  //
  if (SensorLightReady == true) {
    LightValue = SensorLight.readLightLevel();
    if (LightValue > 0) {
      DBG_PRINTF2("Light : %f %s\n", LightValue, ValUnit[2].c_str());
    }
  }

  //
  // Read the Pressure (Unit:Pa)
  //
  if (SensorBmpReady == true) {
    PressureValue = SensorBmp.readPressure();
    DBG_PRINTF2("Pressure : %d %s\n", PressureValue, ValUnit[3].c_str());
  }
}

void display(int analogValue)
{
  //
  // Draw the LCD 1602
  //
  if (Lcd != 0) {
    //
    // Change the display mode according to the value of PIN - 'A0'.
    //
    DisplayMode = getDisplayMode(5, analogValue);
    //DBG_PRINTF("analogValue=%d\n", analogValue);
    //DBG_PRINTF("DisplayMode=%d\n", DisplayMode);

    switch (DisplayMode) {
      case 1:
        LcdDisplayType1();
        break;
      case 2:
        LcdDisplayType2();
        break;
      case 3:
        LcdDisplayType3();
        break;
      case 4:
        LcdDisplayType4();
        break;
      case 5:
        LcdDisplayType5();
        break;
      default:
        LcdDisplayType1();
        break;
    }
  }

  //
  // Draw the OLED
  //
  if (u8g2 != 0) {
    //
    // Change the display mode according to the value of PIN - 'A0'.
    //
    DisplayMode = getDisplayMode(4, analogValue);
    //DBG_PRINTF("analogValue=%d\n", analogValue);
    //DBG_PRINTF("DisplayMode=%d\n", DisplayMode);

    switch (DisplayMode) {
      case 1:
        OledDisplayType1();
        break;
      case 2:
        OledDisplayType2();
        break;
      case 3:
        OledDisplayType3();
        break;
      case 4:
        OledDisplayType4();
        break;
      default:
        OledDisplayType1();
        break;
    }
  }
}

void OledDisplayType1()
{
  u8g2->firstPage();
  do {
    u8g2->setFont(u8g2_font_unifont_t_chinese1);
    
    String line = "溫度 " + String(TemperatureValue, 2) + " " + ValUnit[0];
    u8g2->setCursor(TEXT_GAP, WORD_HEIGHT + TEXT_GAP + (LINE_HEIGHT * 0));
    u8g2->print(line);

    line = "濕度 " + String(HumidityValue, 2) + " " + ValUnit[1];
    u8g2->setCursor(TEXT_GAP, WORD_HEIGHT + TEXT_GAP + (LINE_HEIGHT * 1));
    u8g2->print(line);

    line = "亮度 " + String(LightValue, 0) + " " + ValUnit[2];
    u8g2->setCursor(TEXT_GAP, WORD_HEIGHT + TEXT_GAP + (LINE_HEIGHT * 2));
    u8g2->print(line);

    line = "氣壓 " + String(PressureValue) + " " + ValUnit[3];
    u8g2->setCursor(TEXT_GAP, WORD_HEIGHT + TEXT_GAP + (LINE_HEIGHT * 3));
    u8g2->print(line);

    u8g2->setCursor(TEXT_GAP, WORD_HEIGHT + TEXT_GAP + (LINE_HEIGHT * 4));
    u8g2->print(RemoteMessage);

    drawSmallIcons(u8g2, (WiFi.status() == WL_CONNECTED), (FwBoxIns.getServerStatus() == SERVER_STATUS_OK));
  }
  while (u8g2->nextPage());
}

void OledDisplayType2()
{
  u8g2->firstPage();
  do {
    drawPage128X128Wether(u8g2, &WeatherResult, (WiFi.status() == WL_CONNECTED), (FwBoxIns.getServerStatus() == SERVER_STATUS_OK));

    u8g2->setFont(u8g2_font_unifont_t_chinese1);
    String line = "室內 " + String(TemperatureValue, 1) + ValUnit[0];
    u8g2->setCursor(0, SMALL_ICON_BOTTOM - 17);
    u8g2->print(line);
    if(HumidityValue == 0)
      line = "...";
    else if(HumidityValue > 70)
      line = "潮溼";
    else if(HumidityValue < 60)
      line = "乾燥";
    else
      line = "舒適";
    u8g2->setCursor(0, SMALL_ICON_BOTTOM);
    u8g2->print(line);
  }
  while (u8g2->nextPage());
}

void OledDisplayType3()
{
  u8g2->firstPage();
  do {
    drawPage128X128Time(u8g2, &WeatherResult, (WiFi.status() == WL_CONNECTED), (FwBoxIns.getServerStatus() == SERVER_STATUS_OK));

    u8g2->setFont(u8g2_font_unifont_t_chinese1);
    String line = "室內 " + String(TemperatureValue, 1) + ValUnit[0];
    u8g2->setCursor(0, SMALL_ICON_BOTTOM - 17);
    u8g2->print(line);
    if(HumidityValue == 0)
      line = "...";
    else if(HumidityValue > 70)
      line = "潮溼";
    else if(HumidityValue < 60)
      line = "乾燥";
    else
      line = "舒適";
    u8g2->setCursor(0, SMALL_ICON_BOTTOM);
    u8g2->print(line);
  }
  while (u8g2->nextPage());
}

void OledDisplayType4()
{
  u8g2->firstPage();
  do {
    drawPage128X128Info(u8g2, FIRMWARE_VERSION, (WiFi.status() == WL_CONNECTED), (FwBoxIns.getServerStatus() == SERVER_STATUS_OK));
  }
  while (u8g2->nextPage());
}

//
// Display Date, Time, Humidity and Temperature
// 顯示日期，時間，溼度與溫度。
//
void LcdDisplayType1()
{
  //
  // Print YEAR-MONTH-DAY
  //
  Lcd->setCursor(0, 0);
  //Lcd->print(year());
  //Lcd->print("-");   
  PrintLcdDigits(month());
  Lcd->print("-");   
  PrintLcdDigits(day());
  Lcd->print(" ");
  Lcd->print(WEEK_DAY_NAME[weekday() - 1]);
  Lcd->print("  ");
  Lcd->setCursor(16 - 5, 0); // Align right
  Lcd->printf("%2.1f", TemperatureValue);
  Lcd->print("C");

  //
  // Print HOUR:MIN:SEC WEEK
  //
  Lcd->setCursor(0, 1);
  PrintLcdDigits(hour());
  Lcd->print(":");
  PrintLcdDigits(minute());
  Lcd->print(":");    
  PrintLcdDigits(second());
  Lcd->print("     ");
  Lcd->setCursor(16 - 3, 1); // Align right
  Lcd->printf("%2d", (int)HumidityValue);
  Lcd->print("%");
}

//
// Display Date and Time
// 顯示日期與時間。
//
void LcdDisplayType2()
{
  //
  // Print YEAR-MONTH-DAY
  //
  Lcd->setCursor(0, 0);
  Lcd->print("   ");
  Lcd->print(year());
  Lcd->print("-");   
  PrintLcdDigits(month());
  Lcd->print("-");   
  PrintLcdDigits(day());
  Lcd->print("   ");

  //
  // Print HOUR:MIN:SEC WEEK
  //
  Lcd->setCursor(0, 1);
  Lcd->print("  ");
  PrintLcdDigits(hour());
  Lcd->print(":");
  PrintLcdDigits(minute());
  Lcd->print(":");    
  PrintLcdDigits(second());
  Lcd->print(" ");
  Lcd->print(WEEK_DAY_NAME[weekday() - 1]);
  Lcd->print("  ");
}

//
// Display Humidity and Temperature
// 顯示溼度與溫度。
//
void LcdDisplayType3()
{
  Lcd->setCursor(0, 0);
  Lcd->print("Temp:      ");
  Lcd->print(TemperatureValue, 1);
  Lcd->print("C");
  Lcd->setCursor(0, 1);
  Lcd->print("Humidity:    ");
  Lcd->print(HumidityValue, 0);
  Lcd->print("%");
}

//
// Display the atmospheric pressure and the light level
// 顯示大氣壓力與環境亮度。
//
void LcdDisplayType4()
{
  //
  // Print the atmospheric pressure
  //
  Lcd->setCursor(0, 0);
  //Lcd->print("       ");
  Lcd->printf("%13d Pa", (int)PressureValue); // Align right

  Lcd->setCursor(0, 1);
  Lcd->print("Light:");
  Lcd->printf("%7d Lx", (int)LightValue); // Align right
}

//
// Display the information
//
void LcdDisplayType5()
{
  //
  // Print the device ID and the firmware version.
  //
  Lcd->setCursor(0, 0);
  Lcd->print("ID:");
  Lcd->print(FwBoxIns.getSimpleChipId());
  Lcd->print("     v");
  Lcd->print(FIRMWARE_VERSION);
  Lcd->print("    ");

  //
  // Display the local IP address.
  //
  Lcd->setCursor(0, 1);
  if (WiFi.status() == WL_CONNECTED) {
    String ip = WiFi.localIP().toString();
    int space_len = 16 - ip.length();
    Lcd->print(ip);

    //
    // Fill the char space at the end.
    //
    for (int i = 0; i < space_len; i++)
      Lcd->print(" ");
  }
  else {
    Lcd->print("                ");
  }
}

int getDisplayMode(int pageCount,int analogValue)
{
  int page_width = 1024 / pageCount;

  for (int i = 0; i < pageCount; i++) {
    if (i == 0) {
      if (analogValue < (page_width*(i+1))-ANALOG_VALUE_DEBOUNCING) { // The value - '5' is for debouncing.
        return i + 1;
      }
    }
    else if (i == (pageCount - 1)) {
      if (analogValue >= (page_width*i)+ANALOG_VALUE_DEBOUNCING) { // The value - '5' is for debouncing.
        return i + 1;
      }
    }
    else {
      if ((analogValue >= (page_width*i)+ANALOG_VALUE_DEBOUNCING) && (analogValue < (page_width*(i+1))-ANALOG_VALUE_DEBOUNCING)) { // The value - '5' is for debouncing.
        return i + 1;
      }
    }
  }

  return 1; // default page
}

void PrintLcdDigits(int digits)
{
  if (digits < 10)
    Lcd->print('0');
  Lcd->print(digits);
}
