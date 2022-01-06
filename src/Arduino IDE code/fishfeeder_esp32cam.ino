#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <EEPROM.h>
#include <ESP32Servo.h>
#include "esp_camera.h"
#include "esp_pm.h"
#include "esp_http_server.h"
#include <ArduinoJson.h>
#include "driver/rtc_io.h"
#include <driver/adc.h>
#include "soc/soc.h"             
#include "soc/rtc_cntl_reg.h" 
#include <UniversalTelegramBot.h>
#include <GyverButton.h>
#define APSSID "fishfeeder" //Имя точки доступа
#define APPSK  "XXXXXXXX" //Пароль точки доступа
#define BOT_TOKEN "xxxxxxxx:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" //Токен телеграм бота
#define FLASH_LED_PIN 4 // Пин вспышки

/* Переменные пинов подключения камеры */
#define CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22
#define ARR_SIZE(a) (sizeof(a) / sizeof(a[0])) // Лямбда возвращающая размер массива
camera_config_t camera_example_config; // Инициализация структуры настроек, воизбежании ошибок с функциями
struct tm timeinfo;
const byte inPins = 14; // Кнопка сброса настроек WIFI
const char *myHostname = APSSID; // hostname устройства
const char *softAP_ssid = APSSID; //Имя точки доступа
const char *softAP_password = APPSK; //Пароль точки доступа
int buttonHoldTime = 5000; //5 секунд зажатие кнопки для сброса настроек
unsigned long pressedtime = 0; // таймер нажатой кнопки
unsigned long scndtimer = 0;
byte wl_delay_counter = 0;
bool exist_cred = false; // если натройки существуют
char ssid[32] = ""; //инициализация переменных под чтение данных из EEPROM
char password[32] = ""; //инициализация переменных под чтение данных из EEPROM
const byte DNS_PORT = 53; // DNS порт
IPAddress apIP(8, 8, 4, 4); // IP - адресс точки доступа
IPAddress netMsk(255, 255, 255, 0); // Маска подсети точки доступа
Servo feedservo; //объявляем объект feedservo из класса Servo
WebServer server(80); //Объект сервер из класса вэб-сервер
DNSServer dnsServer; // Объект днс-сервера
/* Служебные переменные WIFI */
boolean connect;
unsigned long lastConnectTry = 0;
unsigned int status = WL_IDLE_STATUS;
char scheduled_time[16] ="";
char feedsize_f_for_eeprom[16] = "";
const unsigned long BOT_MTBS = 1000; // Интервал между сканированиями сообщений у бота в ms (1000ms = 1 sec)
unsigned long feeder_tmr,flash_tmr,cam_tmr;
unsigned long bot_lasttime; // Дата последней проверки сообщений у бота
WiFiClientSecure secured_client; //Объект SSL соединения
UniversalTelegramBot bot(BOT_TOKEN, secured_client); //Объект телеграм бота
bool flashState = LOW,wflag=false,woflag=false,fflag=false,foflag=false,cflag=false,coflag=false,schedule_feed_state=false,dataAvailable = false,flash_state_enabled=false, schedule_exist=false,ap_mode_flag=false; // переменные состояний
camera_fb_t *fb = NULL; // Очищаем буффер камеры
String previous_message,my_message,lastFeedTime,scheduled_time_str; // Переменные планировщика кормления
int chat_allowed_ids[] = {1111111111,2222222222,33333333333}; // Разрешенные чаты пользователей для работы с ботом (получите у бота @IDBot)
String chat_id_to_response = "333333333333"; // Chatid для отправки сообщений в режиме запланированного кормления
int scheduled_time_arr[3] = {0,0}, feed_sizes[] = {130,135,140,145,150,155,160,165,170,175,180}, feedsize_f=5; // Служебные переменные для кормления, массив с градусами для серво а индекс это размер порции
bool isMoreDataAvailable();
byte *getNextBuffer();
int getNextBufferLen();

/* Служебные функции */
/* ----------------------------------------------------------- */
/** Проверка являеться ли строка IP адерсом? */
boolean isIp(String str) {
  for (size_t i = 0; i < str.length(); i++) {
    int c = str.charAt(i);
    if (c != '.' && (c < '0' || c > '9')) {
      return false;
    }
  }
  return true;
}
/** конвертация IP в строку */
String toStringIp(IPAddress ip) {
  String res = "";
  for (int i = 0; i < 3; i++) {
    res += String((ip >> (8 * i)) & 0xFF) + ".";
  }
  res += String(((ip >> 8 * 3)) & 0xFF);
  return res;
}
/** Чтение имя WLAN сети и пароля из памяти EEPROM */
void loadCredentials() {
  EEPROM.begin(512);
  char ok[2 + 1];
  EEPROM.get(0, ssid);
  EEPROM.get(0 + sizeof(ssid), password);
  EEPROM.get(0 + sizeof(ssid) + sizeof(password),scheduled_time);
  EEPROM.get(0 + sizeof(ssid) + sizeof(password) + sizeof(scheduled_time), feedsize_f_for_eeprom);
  EEPROM.get(0 + sizeof(ssid) + sizeof(password) + sizeof(scheduled_time) + sizeof(feedsize_f_for_eeprom), ok);
  EEPROM.end();
  if (String(ok) != String("OK")) {
    ssid[0] = 0;
    password[0] = 0;
  }
//  Serial.println("Recovered credentials:");
//  Serial.println(ssid);
//  Serial.println(strlen(password) > 0 ? "********" : "<no password>");
  if (strlen(password) > 0) {
    exist_cred = true;
    }
  if(strlen(scheduled_time) > 0) {
        scheduled_time_arr[0] = getValue(scheduled_time, ':', 0).toInt();
        scheduled_time_arr[1] = getValue(scheduled_time, ':', 1).toInt();
        scheduled_time_str = scheduled_time;
//        Serial.print("Recovered schedule: ");
//        Serial.println(scheduled_time);
    }
  if(strlen(feedsize_f_for_eeprom) > 0) { 
    feedsize_f = atoi(feedsize_f_for_eeprom); //Serial.print("Recovered feedsize: "); Serial.println(feedsize_f);
    }
}
/** Сохранение WLAN имя/пароля в EEPROM */
void saveCredentials() {
  EEPROM.begin(512);
  char ok[2 + 1] = "OK";
  EEPROM.put(0, ssid);
  EEPROM.put(0 + sizeof(ssid), password);
  EEPROM.put(0 + sizeof(ssid) + sizeof(password),scheduled_time);
  EEPROM.put(0 + sizeof(ssid) + sizeof(password) + sizeof(scheduled_time), feedsize_f_for_eeprom);
  EEPROM.put(0 + sizeof(ssid) + sizeof(password) + sizeof(scheduled_time) + sizeof(feedsize_f_for_eeprom), ok);
  EEPROM.commit();
  EEPROM.end();
}
/** Конфигурация и инициализация камеры  */
extern "C" { esp_err_t camera_enable_out_clock(camera_config_t *config); void camera_disable_out_clock(); }

esp_err_t my_camera_init() {
  //power up the camera if PWDN pin is defined
  if (PWDN_GPIO_NUM != -1) {
    gpio_pad_select_gpio(PWDN_GPIO_NUM);
    gpio_set_direction((gpio_num_t)PWDN_GPIO_NUM, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)PWDN_GPIO_NUM, 0);
 }
  esp_err_t err = esp_camera_init(&camera_example_config);
  if (err != ESP_OK) {
//    Serial.printf("Camera init failed with error 0x%x\n", err);
    return err;
  }
//  Serial.printf("Camera Init OK");
  return ESP_OK;
}

/** Функции обработки страниц вэб сервера */
const String csstyle = F("<style type=\"text/css\"> .div_stl{ width:80%;border:0px;border-radius:0.5rem; box-shadow: rgba(99, 99, 99, 0.3) -4px 4px 3px,rgba(99, 99, 99, 0.5) -1px 1px 2px,inset 0px 0px 1px rgba(0,0,0,0.3); } .btn_stl{border:0;border-radius:0.5rem; background: linear-gradient(to top left, #45609D 0%, #7A91C4 100%);color:#fff;line-height:2.4rem;font-size:1.2rem;width:70%;} .btn_stl:hover{background: linear-gradient(to top left, #699D45 0%, #8DC47A 100%);} </style>"); //CSS 

boolean captivePortal() {
  if (!isIp(server.hostHeader()) && server.hostHeader() != (String(myHostname) + ".local")) {
//    Serial.print("Host header: ");
//    Serial.println(String(myHostname));
//    Serial.println("Request redirected to captive portal");
    server.sendHeader("Location", String("http://") + toStringIp(server.client().localIP()), true);
    server.send(302, "text/plain", "");   // Empty content inhibits Content-length header so we have to close the socket ourselves.
    server.client().stop(); // Stop is needed because we sent no content length
    return true;
  }
  return false;
}
void handleRoot() {
  if (captivePortal()) { // If caprive portal redirect instead of displaying the page.
    return;
  }
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");

  String Page;
  Page += String(F("<!DOCTYPE html><html><head>")) + csstyle + String(F("<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>Настройки ")) + myHostname + F("</title>");
  Page += String(F("</head><body><center><div style=\"div_stl\"><h1>Кормилка рыбок</h1>"));
  if (server.client().localIP() == apIP) {
    Page += String(F("<p>Вы подключились к: ")) + softAP_ssid + F("</p>");
  } else {
    Page += String(F("<p>Вы подключились через сеть: ")) + ssid + F("</p>");
  }
  Page += String(F(
            "<br />  <a href=\"/wifi\" target=\"_\"><input type=\"button\" value=\"Настройки\" class=\"btn_stl\"><br />Powered by:<br />marinaursu & IBishopI</font></div></center></body></html>" ));
  server.send(200, "text/html", Page);
  server.client().stop();
}
void handleWifi() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");

  String Page;
  Page += String(F("<!DOCTYPE html><html><head>")) + csstyle + String(F("<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"></head><body><center><div class=\"div_stl\"><br /><h1>Настройка WiFi</h1>"));
  if (server.client().localIP() == apIP) {
    Page += String(F("<p>Вы подключены к: ")) + softAP_ssid + F("</p>");
  } else {
    Page += String(F("<p>Вы подключены через сеть: ")) + ssid + F("</p>");
  }
  Page +=  String(F(
             "\r\n<br />"
             "<p>Выберите свою сеть из списка:</p>"));
//  Serial.println("WLAN scan start");
  int n = WiFi.scanNetworks();
//  Serial.println("WLAN scan done");
  Page += String(F(
            "\r\n<form method='POST' action='wifisave'>"
          "<select name='n' style=\"width:70%;background: transparent;padding:5px;font-size:16px;border:1px solid #ccc;height:34px;margin-bottom:5px;\">"));
    if (n > 0) {
    for (int i = 0; i < n; i++) {
      Page += String(F("\r\n<option value=\"")) + WiFi.SSID(i) + F("\">") +  WiFi.SSID(i) + ((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? F(" [open ") : F(" [wpa ")) + F(" ") + WiFi.RSSI(i) + F("dbm]</option>");
    }
  } else {
    Page += F("<option value=\"none\">(!)Нет доступных сетей</option>");
  }        
  Page += String(F(   "</select><br /><input type='password' placeholder=' пароль' name='p' style=\"width:70%;background: transparent;padding:0px;font-size:16px;border:1px solid #ccc;height:34px;margin-bottom:5px;\"/><br /><input type='submit' value='Подключится' class='btn_stl'/></form><br /><a href='/'><-назад</a></div></center></body></html>"));

  server.send(200, "text/html", Page);
  server.client().stop(); // Stop is needed because we sent no content length
}
void handleWifiSave() {
//  Serial.println("wifi save");
  server.arg("n").toCharArray(ssid, sizeof(ssid) - 1);
  server.arg("p").toCharArray(password, sizeof(password) - 1);
  server.sendHeader("Location", "wifi", true);
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send(302, "text/plain", "");    // Empty content inhibits Content-length header so we have to close the socket ourselves.
  server.client().stop(); // Stop is needed because we sent no content length
  saveCredentials();
  delay(1000);
  ESP.restart();
}

/** Функция обработки 404 ошибки **/
void handleNotFound() {
  if (captivePortal()) { // If caprive portal redirect instead of displaying the error page.
    return;
  }
  String message = F("File Not Found\n\n");
  message += F("URI: ");
  message += server.uri();
  message += F("\nMethod: ");
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += F("\nArguments: ");
  message += server.args();
  message += F("\n");

  for (uint8_t i = 0; i < server.args(); i++) {
    message += String(F(" ")) + server.argName(i) + F(": ") + server.arg(i) + F("\n");
  }
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send(404, "text/plain", message);
}


/** Служебные функции */
void buttonPressed() {
  if (pressedtime == 0) {
  pressedtime = millis();
  }
  if (pressedtime != 0) {
  scndtimer = millis();
  } else {
  scndtimer = 0;  
  }
while ( ( scndtimer - pressedtime ) > buttonHoldTime ){
//Serial.println("Pressed for: ");
//Serial.println(( scndtimer - pressedtime ));
digitalWrite(FLASH_LED_PIN, HIGH);
delay(30);
digitalWrite(FLASH_LED_PIN, LOW);
EEPROM.begin(512);
delay(100);
for ( byte i = 0; i < 512; i++ ){
EEPROM.put(i, 0);
delay(25);
EEPROM.commit();
//Serial.println( i );
if ( i == 255 ) {
break; 
}}
EEPROM.end();
pressedtime = 0;
ESP.restart();
break;
}}

String printLocalTime(const char* mask)
{
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
 //   Serial.println("Failed to obtain time");
    return "NTP unavailable";
  }
  char timeStringBuff[50]; 
  strftime(timeStringBuff, sizeof(timeStringBuff), mask, &timeinfo);
  return timeStringBuff;
}

bool isMoreDataAvailable()
{
  if (dataAvailable)
  {
    dataAvailable = false;
    return true;
  }
  else
  {
    return false;
  }
}

byte *getNextBuffer()
{
  if (fb)
  {
    return fb->buf;
  }
  else
  {
    return nullptr;
  }
}

int getNextBufferLen()
{
  if (fb)
  {
    return fb->len;
  }
  else
  {
    return 0;
  }
}

void WIFI_AP_mode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(softAP_ssid, softAP_password);
//  Serial.println(WiFi.softAPConfig(apIP, apIP, netMsk)? "Configuring Soft AP" : "Error in Configuration");    
  WiFi.softAPIP();
  delay(500); //без задержки пустой IP
//  Serial.print("AP IP address: ");
//  Serial.println(WiFi.softAPIP());
  ap_mode_flag=true;
  }

 /** Функция подключения к wifi и получения времени с NTP (+ установка сертификата telegram) **/
void connectWifi() {
  if( exist_cred  ) {
    ap_mode_flag=false;
 // Serial.println("Connecting as wifi client...");
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int connRes = WiFi.waitForConnectResult();
    while (WiFi.status() != WL_CONNECTED)
  {
  //  Serial.print(".");
    delay(500);
  }
  secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
//    Serial.print("\nWiFi connected. IP address: ");
//  Serial.println(WiFi.localIP());
//    Serial.print("Retrieving time: ");
  const char* ntpServer = "pool.ntp.org";
  const long  gmtOffset_sec = 7200;
  const int   daylightOffset_sec = 3600;
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  time_t now = time(nullptr);
  while (now < 24 * 3600)
  {
//    Serial.print(".");
    delay(100);
    now = time(nullptr);
  }
//  Serial.println(now);
//  Serial.println(printLocalTime("%B %d %H:%M:%S"));
  }
}

bool check_allow(String ch_check) {
  for(int ch = 0; ch < ARR_SIZE(chat_allowed_ids); ch++) {
    if(chat_allowed_ids[ch] == ch_check.toInt()) {
      return true;
      }
    }
  return false;
  }

String getValue(String data, char separator, int index)
{
    int found = 0;
    int strIndex[] = { 0, -1 };
    int maxIndex = data.length() - 1;

    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i+1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void feed_fish(int feeder_pos, String fchat_id) {
feedservo.attach(15);
feedservo.write(feeder_pos);
wflag=true;
lastFeedTime = printLocalTime("%B %d %H:%M:%S");
bot.sendMessage(fchat_id, "Кормушка 1: покормила!", "Markdown");
}

void detacher() {
  if(wflag) {
    if(!woflag) {
      feeder_tmr = millis();
      woflag=!woflag;
      } else {
        if((millis() - feeder_tmr) >= 1000) {
          feedservo.write(110);
          } else if((millis() - feeder_tmr) >= 1800) {
          Serial.print("Servo detached!");
          feedservo.detach();
          woflag=!woflag;
          wflag=!wflag;
          camera_enable_out_clock(&camera_example_config);
            }
        }
    }
}

void flash_disabler() {
  if(fflag) {
    if(!foflag) {
      flash_tmr = millis();
      foflag=!foflag;
      }
    else {
      if((millis() - flash_tmr) >= 1000) {
           flashState = !flashState;
           digitalWrite(FLASH_LED_PIN, flashState);
           Serial.print("Flash state: "); Serial.println(flashState);
           fflag=!fflag;
           foflag=!foflag;
        } 
     }
  }
}

/** Функция проверки сообщений и реакции на них telegram бота */
void handleNewMessages(int numNewMessages)
{
//  Serial.println("handleNewMessages");
//  Serial.println(String(numNewMessages));
  for (int i = 0; i < numNewMessages; i++)
  {
    String chat_id = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;
//    Serial.println(chat_id);
    if(check_allow(chat_id)) { 
    String from_name = bot.messages[i].from_name;
    if (from_name == "")
      from_name = "Guest";

    if (text == "/flash")
    {
      previous_message = text;
      flash_state_enabled = !flash_state_enabled;
    }

    if (text == "/photo")
    {
      previous_message = text;
      if(feedservo.attached()) { Serial.println("attached"); } else { Serial.println("detached"); }
      if(flash_state_enabled) {
      fflag=true;
      flashState = !flashState;
      digitalWrite(FLASH_LED_PIN, flashState);
      }
      fb = NULL;
      // Take Picture with Camera
      fb = esp_camera_fb_get();
      //camera_capture();
      delay(50);
//      Serial.printf("Took a photo %d x %d with len: %d\n", fb->width, fb->height, fb->len);
      if (!fb)
      {
//        Serial.println("Camera capture failed");
        bot.sendMessage(chat_id, "Camera capture failed", "");
        return;
      }
      dataAvailable = true;
//      Serial.println("Sending");
      bot.sendPhotoByBinary(chat_id, "image/jpeg", fb->len,
                            isMoreDataAvailable, nullptr,
                            getNextBuffer, getNextBufferLen);

//      Serial.println("done!");

      esp_camera_fb_return(fb);
    }
    if (text == "/feed")
    {
      previous_message = text;
      fb = NULL;
      camera_disable_out_clock();
      feed_fish(feed_sizes[feedsize_f], chat_id);
    }
    if (text == "/feedsize")
    {
      previous_message = text;
      my_message = "Установите порцию от 0 до 10 (где 10 максимум)";
      bot.sendMessage(chat_id, my_message, "Markdown");
    }
     if (text == "/schedule")
    {
      previous_message = text;
      my_message = "Установите ежедневное время кормления в формате HH24:MM\nНапример: 22:00";
      bot.sendMessage(chat_id, my_message, "Markdown");
    }
    if (text == "/status")
    {
      previous_message = text;
      my_message = "Кормушка 1:\nIP-адрес(у роутер): ";
      my_message += WiFi.localIP().toString().c_str();
      my_message += "\nВремя посл.кормления: ";
      my_message += lastFeedTime;
      my_message += "\nВспышка: ";
      my_message += flash_state_enabled == false ? "Выключена\n" : "Включена\n";
      my_message += "Запланировонно: ";
      my_message += scheduled_time_str != NULL ? scheduled_time_str : "Не запланировано";
      my_message += "\nРазмер порции: ";
      my_message += feedsize_f;
      my_message += "\n";
      bot.sendMessage(chat_id, my_message, "Markdown");
    }
    if (text == "/start")
    {
      previous_message = text;
      String welcome = "Кормушка для рыб к Вашим услугам.\n\n";
      welcome += "/photo : Сделает снимок\n";
      welcome += "/flash : Включить/выключить вспышку\n";
      welcome += "/feed : Покормить рыбку\n";
      welcome += "/schedule : запланировать кормёжку\n";
      welcome += "/feedsize : Установить порцию\n";
      welcome += "/status : Вывести текущую информацию\n";
      welcome += "/wobble : Устранение засора\n";
      
      bot.sendMessage(chat_id, welcome, "Markdown");
    }
    else {
      if(previous_message == "/schedule") {
        scheduled_time_arr[0] = getValue(text, ':', 0).toInt();
        scheduled_time_arr[1] = getValue(text, ':', 1).toInt();
      
        if(scheduled_time_arr[0] != 0 && scheduled_time_arr[1] != 0) {
        scheduled_time_str = text;
          if(sizeof(text) <= 12) {           
        text.toCharArray(scheduled_time, sizeof(scheduled_time) - 1); } }
        schedule_exist = true;
        saveCredentials();
        }
      if(previous_message == "/feedsize") {
        feedsize_f = text.toInt();
        text.toCharArray(feedsize_f_for_eeprom, sizeof(feedsize_f_for_eeprom) - 1);
//        Serial.print("Storing: ");
//        Serial.print(feedsize_f_for_eeprom);
//        
        saveCredentials();
        }
      }
    } else {bot.sendMessage(chat_id, "У Вас нет прав для пользования ботом!", "Markdown");} 
  }
}

void drop_quality() { // Drop quality to 320x240 due of Telegram API issue
    sensor_t *s = esp_camera_sensor_get();
  if (s->id.PID == OV3660_PID)
  {
    s->set_vflip(s, 1);       //flip it back
    s->set_brightness(s, 2);  //up the blightness just a bit
    s->set_saturation(s, -2); //lower the saturation
  }
  //drop down frame size for higher initial frame rate
  s->set_vflip(s, 1);       //flip it back
  s->set_brightness(s, 2);  //up the blightness just a bit
  s->set_saturation(s, -2); //lower the saturation
  s->set_framesize(s, FRAMESIZE_QVGA);
  }


/** Настройки основной программы */
void setup()
{
//  Serial.begin(115200);
//  Serial.println();
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  camera_example_config.ledc_channel = LEDC_CHANNEL_0;
  camera_example_config.ledc_timer = LEDC_TIMER_0;
  camera_example_config.pin_d0 = Y2_GPIO_NUM;
  camera_example_config.pin_d1 = Y3_GPIO_NUM;
  camera_example_config.pin_d2 = Y4_GPIO_NUM;
  camera_example_config.pin_d3 = Y5_GPIO_NUM;
  camera_example_config.pin_d4 = Y6_GPIO_NUM;
  camera_example_config.pin_d5 = Y7_GPIO_NUM;
  camera_example_config.pin_d6 = Y8_GPIO_NUM;
  camera_example_config.pin_d7 = Y9_GPIO_NUM;
  camera_example_config.pin_xclk = XCLK_GPIO_NUM;
  camera_example_config.pin_pclk = PCLK_GPIO_NUM;
  camera_example_config.pin_vsync = VSYNC_GPIO_NUM;
  camera_example_config.pin_href = HREF_GPIO_NUM;
  camera_example_config.pin_sscb_sda = SIOD_GPIO_NUM;
  camera_example_config.pin_sscb_scl = SIOC_GPIO_NUM;
  camera_example_config.pin_pwdn = PWDN_GPIO_NUM;
  camera_example_config.pin_reset = RESET_GPIO_NUM;
  camera_example_config.xclk_freq_hz = 20000000;
  camera_example_config.pixel_format = PIXFORMAT_JPEG;
  camera_example_config.frame_size = FRAMESIZE_VGA; //FRAMESIZE_UXGA; // FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
  camera_example_config.jpeg_quality = 12;
  camera_example_config.fb_count = 1;
  
my_camera_init();
drop_quality();
camera_disable_out_clock();
delay(100);
camera_enable_out_clock(&camera_example_config);
  pinMode(inPins, INPUT); 
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, flashState); //defaults to low

  loadCredentials(); // Загрузка WLAN имя/пароля из EEPROM
  if( exist_cred > 0) { connectWifi(); ap_mode_flag=false; } else { WIFI_AP_mode(); }

  bot.longPoll = 1; // Бот ожидает 1 секунду на новое сообщение

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);
          if (!MDNS.begin(myHostname)) {
//          Serial.println("Error setting up MDNS responder!");
         bool f = false;
        } else {
//          Serial.println("mDNS responder started");
          // Add service to MDNS-SD
          MDNS.addService("http", "tcp", 80);
        }

  server.on("/", handleRoot);
  server.on("/wifi", handleWifi);
  server.on("/wifisave", handleWifiSave);
  server.on("/generate_204", handleRoot);  //Android captive portal. Maybe not needed. Might be handled by notFound handler.
  server.on("/fwlink", handleRoot);  //Microsoft captive portal. Maybe not needed. Might be handled by notFound handler.
  server.onNotFound(handleNotFound);
  server.begin(); // Web server start
//  Serial.println("HTTP server started");
//  Serial.println(WiFi.status());
}

/** Основной цикл программы */
void loop()
{ 
  detacher();
  flash_disabler();
  dnsServer.processNextRequest();
  server.handleClient();
  if(!ap_mode_flag) {
  if(getLocalTime(&timeinfo)){
  if(!schedule_feed_state && printLocalTime("%H").toInt() == scheduled_time_arr[0] && printLocalTime("%M").toInt() == scheduled_time_arr[1]) {
   schedule_feed_state=true;
   camera_disable_out_clock(); 
   feed_fish(feed_sizes[feedsize_f],chat_id_to_response);
    } else if(schedule_feed_state && printLocalTime("%M").toInt() != scheduled_time_arr[1]) { schedule_feed_state=false; }
  }
  if (digitalRead(inPins) == HIGH ) {
    buttonPressed();
    } else {
   pressedtime = 0;
  }
  if(WiFi.status() != WL_CONNECTED) { connectWifi(); } else {

  if (millis() - bot_lasttime > BOT_MTBS)
  {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages)
    {
   //   Serial.println("got response");
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    detacher();
    flash_disabler();
    bot_lasttime = millis();
   }
  }
 }
}
