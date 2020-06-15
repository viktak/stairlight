#define __debugSettings

//#define _use_local_sun_data
#define _use_local_staircase_timer

#include "includes.h"

typedef enum {
    Sunrise, Sunset
} sunRiseSunset;

//  Web server
ESP8266WebServer server(80);

//  Initialize Wifi
WiFiClient wclient;
PubSubClient PSclient(wclient);

//  Timers and their flags
os_timer_t heartbeatTimer;
os_timer_t sunDataTimer;
os_timer_t accessPointTimer;

//  I2C
PCF857x i2c_io(I2C_IO_ADDRESS, &Wire);

//  Other global variables
sunData_t sunData;
config appConfig;
bool isAccessPoint = false;
bool isAccessPointCreated = false;
TimeChangeRule *tcr;        // Pointer to the time change rule

unsigned long inputPattern;
unsigned long buttonPressedTime = 4294967295 - 3600000;  //  a high number is needed to prevent millis() overflow problem
enum CONNECTION_STATE connectionState;

//  Flags
bool needsHeartbeat = false;
bool needsSunData = false;
bool entranceLightState = false;
bool stairlightLastState = false;
bool ntpInitialized = false;

WiFiUDP Udp;

// Daylight savings time rules for Greece
TimeChangeRule myDST = {"MDT", Fourth, Sun, Mar, 2, DST_TIMEZONE_OFFSET * 60};
TimeChangeRule mySTD = {"MST", Fourth,  Sun, Oct, 2,  ST_TIMEZONE_OFFSET * 60};
Timezone myTZ(myDST, mySTD);

void LogEvent(int Category, int ID, String Title, String Data){
  if (PSclient.connected()){

    String msg = "{";

    msg += "\"Node\":" + (String)ESP.getChipId() + ",";
    msg += "\"Category\":" + (String)Category + ",";
    msg += "\"ID\":" + (String)ID + ",";
    msg += "\"Title\":\"" + Title + "\",";
    msg += "\"Data\":\"" + Data + "\"}";

    Serial.println(msg);

    PSclient.publish((MQTT_CUSTOMER + String("/") + MQTT_PROJECT + String("/") + appConfig.mqttTopic + "/log").c_str(), msg.c_str(), false);
  }
}

void SetRandomSeed(){
    uint32_t seed;

    // random works best with a seed that can use 31 bits
    // analogRead on a unconnected pin tends toward less than four bits
    seed = analogRead(0);
    delay(1);

    for (int shifts = 3; shifts < 31; shifts += 3)
    {
        seed ^= analogRead(0) << shifts;
        delay(1);
    }

    randomSeed(seed);
}

void accessPointTimerCallback(void *pArg) {
  ESP.reset();
}

void heartbeatTimerCallback(void *pArg) {
  needsHeartbeat = true;
}

void sunDataTimerCallback(void *pArg) {
  needsSunData = true;
}

bool loadSettings(config& data) {
  File configFile = LittleFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println("Failed to open config file");
    LogEvent(EVENTCATEGORIES::System, 1, "FS failure", "Failed to open config file.");
    return false;
  }

  size_t size = configFile.size();
  if (size > 1024) {
    Serial.println("Config file size is too large");
    LogEvent(EVENTCATEGORIES::System, 2, "FS failure", "Config file size is too large.");
    return false;
  }

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  // We don't use String here because ArduinoJson library requires the input
  // buffer to be mutable. If you don't use ArduinoJson, you may as well
  // use configFile.readString instead.
  configFile.readBytes(buf.get(), size);
  configFile.close();

  StaticJsonDocument<JSON_SETTINGS_SIZE> doc;
  DeserializationError error = deserializeJson(doc, buf.get());

  if (error) {
    Serial.println("Failed to parse config file");
    LogEvent(EVENTCATEGORIES::System, 3, "FS failure", "Failed to parse config file.");
    Serial.println(error.c_str());
    return false;
  }

  #ifdef __debugSettings
  serializeJsonPretty(doc,Serial);
  Serial.println();
  #endif

  if (doc["ssid"]){
    strcpy(appConfig.ssid, doc["ssid"]);
  }
  else
  {
    strcpy(appConfig.ssid, defaultSSID);
  }
  
  if (doc["password"]){
    strcpy(appConfig.password, doc["password"]);
  }
  else
  {
    strcpy(appConfig.password, DEFAULT_PASSWORD);
  }
  
  if (doc["mqttServer"]){
    strcpy(appConfig.mqttServer, doc["mqttServer"]);
  }
  else
  {
    strcpy(appConfig.mqttServer, DEFAULT_MQTT_SERVER);
  }
  
  if (doc["mqttPort"]){
    appConfig.mqttPort = doc["mqttPort"];
  }
  else
  {
    appConfig.mqttPort = DEFAULT_MQTT_PORT;
  }

    if (doc["mqttTopic"]){
    strcpy(appConfig.mqttTopic, doc["mqttTopic"]);
  }
  else
  {
    strcpy(appConfig.mqttTopic, defaultSSID);
  }
  
  if (doc["friendlyName"]){
    strcpy(appConfig.friendlyName, doc["friendlyName"]);
  }
  else
  {
    strcpy(appConfig.friendlyName, NODE_DEFAULT_FRIENDLY_NAME);
  }
  
  if (doc["timezone"]){
    appConfig.timeZone = doc["timezone"];
  }
  else
  {
    appConfig.timeZone = 0;
  }
  
  if (doc["heartbeatInterval"]){
    appConfig.heartbeatInterval = doc["heartbeatInterval"];
  }
  else
  {
    appConfig.heartbeatInterval = DEFAULT_HEARTBEAT_INTERVAL;
  }

  if (doc["staircaseLightDelay"]){
    appConfig.staircaseLightDelay = doc["staircaseLightDelay"];
  }
  else
  {
    appConfig.staircaseLightDelay = DEFAULT_STAIRCASE_LIGHT_DELAY;
  }

  if (doc["staircaseLightDelay"]){
    appConfig.sunriseLightOffset = doc["sunriseLightOffset"];
  }
  else
  {
    appConfig.sunriseLightOffset = DEFAULT_SUNRISE_LIGHT_OFFSET;
  }

  if (doc["staircaseLightDelay"]){
    appConfig.sunsetLightOffset = doc["sunsetLightOffset"];
  }
  else
  {
    appConfig.sunsetLightOffset = DEFAULT_SUNSET_LIGHT_OFFSET;
  }

  return true;
}

bool saveSettings() {
  StaticJsonDocument<1024> doc;

  doc["ssid"] = appConfig.ssid;
  doc["password"] = appConfig.password;

  doc["heartbeatInterval"] = appConfig.heartbeatInterval;

  doc["timezone"] = appConfig.timeZone;

  doc["mqttServer"] = appConfig.mqttServer;
  doc["mqttPort"] = appConfig.mqttPort;
  doc["mqttTopic"] = appConfig.mqttTopic;

  doc["friendlyName"] = appConfig.friendlyName;

  doc["staircaseLightDelay"] = appConfig.staircaseLightDelay;
  doc["sunriseLightOffset"] = appConfig.sunriseLightOffset;
  doc["sunsetLightOffset"] = appConfig.sunsetLightOffset;

  #ifdef __debugSettings
  serializeJsonPretty(doc,Serial);
  Serial.println();
  #endif

  File configFile = LittleFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    LogEvent(System, 4, "FS failure", "Failed to open config file for writing.");
    return false;
  }
  serializeJson(doc, configFile);
  configFile.close();

  return true;
}

void defaultSettings(){
  #ifdef __debugSettings
  strcpy(appConfig.ssid, DEBUG_WIFI_SSID);
  strcpy(appConfig.password, DEBUG_WIFI_PASSWORD);
  strcpy(appConfig.mqttServer, DEBUG_MQTT_SERVER);
  #else
  strcpy(appConfig.ssid, "ESP");
  strcpy(appConfig.password, "password");
  strcpy(appConfig.mqttServer, "test.mosquitto.org");
  #endif

  appConfig.mqttPort = DEFAULT_MQTT_PORT;
  strcpy(appConfig.mqttTopic, defaultSSID);

  appConfig.timeZone = 2;

  strcpy(appConfig.friendlyName, NODE_DEFAULT_FRIENDLY_NAME);
  appConfig.heartbeatInterval = DEFAULT_HEARTBEAT_INTERVAL;

  appConfig.staircaseLightDelay = DEFAULT_STAIRCASE_LIGHT_DELAY;
  appConfig.sunriseLightOffset = DEFAULT_SUNRISE_LIGHT_OFFSET;
  appConfig.sunsetLightOffset = DEFAULT_SUNSET_LIGHT_OFFSET;

  if (!saveSettings()) {
    Serial.println("Failed to save config");
  } else {
    Serial.println("Config saved");
  }
}

String DateTimeToString(time_t time){

  String myTime = "";
  char s[2];

  //  years
  itoa(year(time), s, DEC);
  myTime+= s;
  myTime+="-";


  //  months
  itoa(month(time), s, DEC);
  myTime+= s;
  myTime+="-";

  //  days
  itoa(day(time), s, DEC);
  myTime+= s;

  myTime+=" ";

  //  hours
  itoa(hour(time), s, DEC);
  myTime+= s;
  myTime+=":";

  //  minutes
  if(minute(time) <10)
    myTime+="0";

  itoa(minute(time), s, DEC);
  myTime+= s;
  myTime+=":";

  //  seconds
  if(second(time) <10)
    myTime+="0";

  itoa(second(time), s, DEC);
  myTime+= s;

  return myTime;
}

String TimeIntervalToString(time_t time){

  String myTime = "";
  char s[2];

  //  hours
  itoa((time/3600), s, DEC);
  myTime+= s;
  myTime+=":";

  //  minutes
  if(minute(time) <10)
    myTime+="0";

  itoa(minute(time), s, DEC);
  myTime+= s;
  myTime+=":";

  //  seconds
  if(second(time) <10)
    myTime+="0";

  itoa(second(time), s, DEC);
  myTime+= s;
  return myTime;
}

time_t CalculateSunData(time_t time, double_t latitude, double_t longitude, sunRiseSunset SunEvent){
  //  Using the algorithm found here:
  //  http://williams.best.vwh.net/sunrise_sunset_algorithm.htm

  double zenith = 90.83333333333333;
  double D2R = 3.1415926 / 180;
  double R2D = 180 / 3.1415926;

  //  1. first calculate the day of the year
  double N1 = floor(275 * month(time) / 9);
  double N2 = floor((month(time) + 9) / 12);
	double N3 = (1 + floor((year(time) - 4 * floor(year(time) / 4) + 2) / 3));
	double N = N1 - (N2 * N3) + day(time) - 30;

  //  2. convert the longitude to hour value and calculate an approximate time
  double lngHour = longitude / 15;

  double t;
  switch (SunEvent) {
    case Sunrise:
      t = N + ((6 - lngHour) / 24);
      break;
    case Sunset:
      t = N + ((18 - lngHour) / 24);
      break;
  }

  //  3. calculate the Sun's mean anomaly
  double M = (0.9856 * t) - 3.289;

  //  4. calculate the Sun's true longitude
  double L = M + (1.916 * sin(M * D2R)) + (0.020 * sin(2 * M * D2R)) + 282.634;
  if (L>360) L-=360;
  if (L<0) L+=360;

  //  5a. calculate the Sun's right ascension
  double RA = R2D * atan(0.91764 * tan(L * D2R));
  if (RA>360) RA-=360;
  if (RA<0) RA+=360;

  //  5b. right ascension value needs to be in the same quadrant as L
  double Lquadrant = (floor( L/90)) * 90;
  double RAquadrant = (floor(RA/90)) * 90;
  RA = RA + (Lquadrant - RAquadrant);

  //  5c. right ascension value needs to be converted into hours
  RA = RA / 15;

  //  6. calculate the Sun's declination
  double sinDec = 0.39782 * sin(L * D2R);
	double cosDec = cos(asin(sinDec));

  //  7a. calculate the Sun's local hour angle
  double cosH = (cos(zenith * D2R) - (sinDec * sin(latitude * D2R))) / (cosDec * cos(latitude * D2R));

  if (cosH >  1) return -1;
  if (cosH < -1) return -1;

  //  7b. finish calculating H and convert into hours
  double H;
  switch (SunEvent) {
    case Sunrise:
      H = 360 - R2D * acos(cosH);
      break;
    case Sunset:
      H = R2D * acos(cosH);
      break;
  }

  //  8. calculate local mean time of rising/setting
  double T = H + RA - (0.06571 * t) - 6.622;

  //  9. adjust back to UTC
  double UT = T - lngHour;
  if (UT<0) UT +=24;
  if (UT>24) UT -=24;

  //  10. convert UT value to local time zone of latitude/longitude
  double localT = UT + appConfig.timeZone;
  double num = localT;
  int hours = floor(localT);

  num-=floor(localT);
  num*=60;
  int minutes = floor(num);

  num-=floor(num);
  num*=60;

  int seconds = num;

  time_t lastMidnight = now() - 3600 * hour(time) - 60 * minute(time) - second(time);

  if ( myTZ.locIsDST(time) ){
    return lastMidnight + 3600 * (hours + 1) + 60 * minutes + seconds;
  } else{
    return lastMidnight + 3600 * hours + 60 * minutes + seconds;
  }

}

bool is_authenticated(){
  #ifdef __debugSettings
  return true;
  #endif
  if (server.hasHeader("Cookie")){
    String cookie = server.header("Cookie");
    if (cookie.indexOf("EspAuth=1") != -1) {
      LogEvent(EVENTCATEGORIES::Authentication, 1, "Success", "");
      return true;
    }
  }
  LogEvent(EVENTCATEGORIES::Authentication, 2, "Failure", "");
  return false;
}

void handleLogin(){
  String msg = "";
  if (server.hasHeader("Cookie")){
    String cookie = server.header("Cookie");
  }
  if (server.hasArg("DISCONNECT")){
    String header = "HTTP/1.1 301 OK\r\nSet-Cookie: EspAuth=0\r\nLocation: /login.html\r\nCache-Control: no-cache\r\n\r\n";
    server.sendContent(header);
    LogEvent(EVENTCATEGORIES::Login, 1, "Logout", "");
    return;
  }
  if (server.hasArg("username") && server.hasArg("password")){
    if (server.arg("username") == ADMIN_USERNAME &&  server.arg("password") == ADMIN_PASSWORD ){
      String header = "HTTP/1.1 301 OK\r\nSet-Cookie: EspAuth=1\r\nLocation: /status.html\r\nCache-Control: no-cache\r\n\r\n";
      server.sendContent(header);
      LogEvent(EVENTCATEGORIES::Login, 2, "Success", "User name: " + server.arg("username"));
      return;
    }
    msg = "<div class=\"alert alert-danger\"><strong>Error!</strong> Wrong user name and/or password specified.<a href=\"#\" class=\"close\" data-dismiss=\"alert\" aria-label=\"close\">&times;</a></div>";
    LogEvent(EVENTCATEGORIES::Login, 2, "Failure", "User name: " + server.arg("username") + " - Password: " + server.arg("password"));
  }

  File f = LittleFS.open("/pageheader.html", "r");
  String headerString;
  if (f.available()) headerString = f.readString();
  f.close();

  time_t localTime = myTZ.toLocal(now(), &tcr);

  f = LittleFS.open("/login.html", "r");

  String s, htmlString;

  while (f.available()){
    s = f.readStringUntil('\n');

    if (s.indexOf("%pageheader%")>-1) s.replace("%pageheader%", headerString);
    if (s.indexOf("%year%")>-1) s.replace("%year%", (String)year(localTime));
    if (s.indexOf("%alert%")>-1) s.replace("%alert%", msg);

    htmlString+=s;
  }
  f.close();
  server.send(200, "text/html", htmlString);
  LogEvent(PageHandler, 2, "Page served", "/");
}

void handleRoot() {
  LogEvent(EVENTCATEGORIES::PageHandler, 1, "Page requested", "/");

  if (!is_authenticated()){
    String header = "HTTP/1.1 301 OK\r\nLocation: /login.html\r\nCache-Control: no-cache\r\n\r\n";
    server.sendContent(header);
    return;
  }

  File f = LittleFS.open("/pageheader.html", "r");
  String headerString;
  if (f.available()) headerString = f.readString();
  f.close();

  time_t localTime = myTZ.toLocal(now(), &tcr);

  f = LittleFS.open("/index.html", "r");

  String FirmwareVersionString = String(FIRMWARE_VERSION) + " @ " + String(__TIME__) + " - " + String(__DATE__);

  String s, htmlString;

  while (f.available()){
    s = f.readStringUntil('\n');

    if (s.indexOf("%pageheader%")>-1) s.replace("%pageheader%", headerString);
    if (s.indexOf("%year%")>-1) s.replace("%year%", (String)year(localTime));
    if (s.indexOf("%espid%")>-1) s.replace("%espid%", (String)ESP.getChipId());
    if (s.indexOf("%hardwareid%")>-1) s.replace("%hardwareid%", HARDWARE_ID);
    if (s.indexOf("%hardwareversion%")>-1) s.replace("%hardwareversion%", HARDWARE_VERSION);
    if (s.indexOf("%softwareid%")>-1) s.replace("%softwareid%", SOFTWARE_ID);
    if (s.indexOf("%firmwareversion%")>-1) s.replace("%firmwareversion%", FirmwareVersionString);

    htmlString+=s;
  }
  f.close();
  server.send(200, "text/html", htmlString);
  LogEvent(EVENTCATEGORIES::PageHandler, 2, "Page served", "/");
}

void handleStatus() {

  LogEvent(EVENTCATEGORIES::PageHandler, 1, "Page requested", "status.html");
  if (!is_authenticated()){
     String header = "HTTP/1.1 301 OK\r\nLocation: /login.html\r\nCache-Control: no-cache\r\n\r\n";
     server.sendContent(header);
     return;
  }

  File f = LittleFS.open("/pageheader.html", "r");
  String headerString;
  if (f.available()) headerString = f.readString();
  f.close();

  time_t localTime = myTZ.toLocal(now(), &tcr);

  String s;

  f = LittleFS.open("/status.html", "r");

  String htmlString, ds18b20list;
 
  while (f.available()){
    s = f.readStringUntil('\n');

    //  System information
    if (s.indexOf("%pageheader%")>-1) s.replace("%pageheader%", headerString);
    if (s.indexOf("%year%")>-1) s.replace("%year%", (String)year(localTime));
    if (s.indexOf("%chipid%")>-1) s.replace("%chipid%", (String)ESP.getChipId());
    if (s.indexOf("%uptime%")>-1) s.replace("%uptime%", TimeIntervalToString(millis()/1000));
    if (s.indexOf("%currenttime%")>-1) s.replace("%currenttime%", DateTimeToString(localTime));
    if (s.indexOf("%lastresetreason%")>-1) s.replace("%lastresetreason%", ESP.getResetReason());
    if (s.indexOf("%flashchipsize%")>-1) s.replace("%flashchipsize%",String(ESP.getFlashChipSize()));
    if (s.indexOf("%flashchipspeed%")>-1) s.replace("%flashchipspeed%",String(ESP.getFlashChipSpeed()));
    if (s.indexOf("%freeheapsize%")>-1) s.replace("%freeheapsize%",String(ESP.getFreeHeap()));
    if (s.indexOf("%freesketchspace%")>-1) s.replace("%freesketchspace%",String(ESP.getFreeSketchSpace()));
    if (s.indexOf("%friendlyname%")>-1) s.replace("%friendlyname%",appConfig.friendlyName);
    if (s.indexOf("%mqtt-topic%")>-1) s.replace("%mqtt-topic%",appConfig.mqttTopic);

    //  Network settings
    switch (WiFi.getMode()) {
      case WIFI_AP:
        if (s.indexOf("%wifimode%")>-1) s.replace("%wifimode%", "Access Point");
        if (s.indexOf("%macaddress%")>-1) s.replace("%macaddress%",String(WiFi.softAPmacAddress()));
        if (s.indexOf("%networkaddress%")>-1) s.replace("%networkaddress%",WiFi.softAPIP().toString());
        if (s.indexOf("%ssid%")>-1) s.replace("%ssid%",String(WiFi.SSID()));
        if (s.indexOf("%subnetmask%")>-1) s.replace("%subnetmask%","n/a");
        if (s.indexOf("%gateway%")>-1) s.replace("%gateway%","n/a");
        break;
      case WIFI_STA:
        if (s.indexOf("%wifimode%")>-1) s.replace("%wifimode%", "Station");
        if (s.indexOf("%macaddress%")>-1) s.replace("%macaddress%",String(WiFi.macAddress()));
        if (s.indexOf("%networkaddress%")>-1) s.replace("%networkaddress%",WiFi.localIP().toString());
        if (s.indexOf("%ssid%")>-1) s.replace("%ssid%",String(WiFi.SSID()));
        if (s.indexOf("%subnetmask%")>-1) s.replace("%subnetmask%",WiFi.subnetMask().toString());
        if (s.indexOf("%gateway%")>-1) s.replace("%gateway%",WiFi.gatewayIP().toString());
        break;
      default:
        //  This should not happen...
        break;
    }

      htmlString+=s;
    }
    f.close();
  server.send(200, "text/html", htmlString);
  LogEvent(EVENTCATEGORIES::PageHandler, 2, "Page served", "status.html");
}

void handleStaircaseLightTimer() {
  LogEvent(EVENTCATEGORIES::PageHandler, 1, "Page requested", "staircaselighttimer.html");

  if (!is_authenticated()){
     String header = "HTTP/1.1 301 OK\r\nLocation: /login.html\r\nCache-Control: no-cache\r\n\r\n";
     server.sendContent(header);
     return;
   }

  if (server.method() == HTTP_POST){  //  POST
    if (server.hasArg("timerValue")){
      appConfig.staircaseLightDelay = server.arg("timerValue").toInt();
      LogEvent(EVENTCATEGORIES::StaircaselightDelay, 1, "New delay", server.arg("timerValue").c_str());
    }
    saveSettings();

    if (PSclient.connected()){

      const size_t capacity = JSON_OBJECT_SIZE(3) + JSON_OBJECT_SIZE(6) + 180;
      StaticJsonDocument<capacity> doc;

      doc["staircaseLightDelay"] = appConfig.staircaseLightDelay;

      #ifdef __debugSettings
      serializeJsonPretty(doc,Serial);
      Serial.println();
      #endif

      String myJsonString;

      serializeJson(doc, myJsonString);

      PSclient.publish((MQTT_CUSTOMER + String("/") + MQTT_PROJECT + "/" + ESP.getChipId() + "/settings/").c_str(), myJsonString.c_str(), false );
    }
  }

  File f = LittleFS.open("/pageheader.html", "r");
  String headerString;
  if (f.available()) headerString = f.readString();
  f.close();

  time_t localTime = myTZ.toLocal(now(), &tcr);

  f = LittleFS.open("/staircaselighttimer.html", "r");

  String s, htmlString, delaylist;

  delaylist = "";
  for (size_t i = 30; i < 151; i+=15) {
    delaylist+="<option";
    if (appConfig.staircaseLightDelay==i) delaylist+=" selected";
    delaylist+=" value=\"";
    delaylist+=(String)i;
    delaylist+="\">";
    delaylist+=(String)i;
    delaylist+=" seconds</option>";
    delaylist+="\n";
  }

  while (f.available()){
    s = f.readStringUntil('\n');

    if (s.indexOf("%pageheader%")>-1) s.replace("%pageheader%", headerString);
    if (s.indexOf("%year%")>-1) s.replace("%year%", (String)year(localTime));
    if (s.indexOf("%delaylist%")>-1) s.replace("%delaylist%", delaylist);
    htmlString+=s;
  }
  f.close();
  server.send(200, "text/html", htmlString);
  LogEvent(EVENTCATEGORIES::PageHandler, 2, "Page served", "staircaselighttimer.html");
}

void handleEntranceLight() {
  LogEvent(EVENTCATEGORIES::PageHandler, 1, "Page requested", "entrancelight.html");

  if (!is_authenticated()){
     String header = "HTTP/1.1 301 OK\r\nLocation: /login.html\r\nCache-Control: no-cache\r\n\r\n";
     server.sendContent(header);
     return;
   }

  if (server.method() == HTTP_POST){  //  POST
    for (int i = 0; i < server.args(); i++) {
      Serial.print(server.argName(i));
      Serial.print(": ");
      Serial.println(server.arg(i));
    }
    if (server.hasArg("sunriseOffset")){
      appConfig.sunriseLightOffset = server.arg("sunriseOffset").toInt();
      LogEvent(EVENTCATEGORIES::EntranceLight, 1, "New sunrise offset", server.arg("sunriseOffset").c_str());
    }
    if (server.hasArg("sunsetOffset")){
      appConfig.sunsetLightOffset = server.arg("sunsetOffset").toInt();
      LogEvent(EVENTCATEGORIES::EntranceLight, 1, "New sunset offset", server.arg("sunsetOffset").c_str());
    }
    saveSettings();
  }

  File f = LittleFS.open("/pageheader.html", "r");
  String headerString;
  if (f.available()) headerString = f.readString();
  f.close();

  time_t localTime = myTZ.toLocal(now(), &tcr);

  f = LittleFS.open("/entrancelight.html", "r");

  String s, htmlString, sunsetoffsetlist, sunriseoffsetlist;


  sunsetoffsetlist = "";
  for (int i = -60; i <= 60; i+=10) {
    sunsetoffsetlist+="<option";
    if (appConfig.sunsetLightOffset==i) sunsetoffsetlist+=" selected";
    sunsetoffsetlist+=" value=\"";
    sunsetoffsetlist+=(String)(i);
    sunsetoffsetlist+="\">";
    if(i==0) sunsetoffsetlist+=" exactly at sunset</option>";
      else{
        sunsetoffsetlist+=(String)(abs(i));
        if(i<0) sunsetoffsetlist+=" minutes before sunset</option>";
        else
        if(i>0) sunsetoffsetlist+=" minutes after sunset</option>";
      }

    sunsetoffsetlist+="\n";
  }

  sunriseoffsetlist = "";
  for (int i = -60; i <= 60; i+=10) {
    sunriseoffsetlist+="<option";
    if (appConfig.sunriseLightOffset==i) sunriseoffsetlist+=" selected";
    sunriseoffsetlist+=" value=\"";
    sunriseoffsetlist+=(String)i;
    sunriseoffsetlist+="\">";
    if(i==0) sunriseoffsetlist+=" exactly at sunrise</option>";
      else{
        sunriseoffsetlist+=(String)(abs(i));
        if(i<0) sunriseoffsetlist+=" minutes before sunrise</option>";
        else
        if(i>0) sunriseoffsetlist+=" minutes after sunrise</option>";
      }
    sunriseoffsetlist+="\n";
  }

  while (f.available()){
    s = f.readStringUntil('\n');

    if (s.indexOf("%pageheader%")>-1) s.replace("%pageheader%", headerString);
    if (s.indexOf("%year%")>-1) s.replace("%year%", (String)year(localTime));
    if (s.indexOf("%sunsetoffsetlist%")>-1) s.replace("%sunsetoffsetlist%", sunsetoffsetlist);
    if (s.indexOf("%sunriseoffsetlist%")>-1) s.replace("%sunriseoffsetlist%", sunriseoffsetlist);
    htmlString+=s;
  }
  f.close();
  server.send(200, "text/html", htmlString);
  LogEvent(EVENTCATEGORIES::PageHandler, 2, "Page served", "entrancelight.html");
}

void handleGeneralSettings() {
  LogEvent(EVENTCATEGORIES::PageHandler, 1, "Page requested", "generalsettings.html");

  if (!is_authenticated()){
     String header = "HTTP/1.1 301 OK\r\nLocation: /login.html\r\nCache-Control: no-cache\r\n\r\n";
     server.sendContent(header);
     return;
   }

  if (server.method() == HTTP_POST){  //  POST
    bool mqttDirty = false;

    if (server.hasArg("timezoneselector")){
      signed char oldTimeZone = appConfig.timeZone;
      appConfig.timeZone = atoi(server.arg("timezoneselector").c_str());

      adjustTime((appConfig.timeZone - oldTimeZone) * SECS_PER_HOUR);

      LogEvent(EVENTCATEGORIES::TimeZoneChange, 1, "New time zone", "UTC " + server.arg("timezoneselector"));
    }

    if (server.hasArg("friendlyname")){
      strcpy(appConfig.friendlyName, server.arg("friendlyname").c_str());
      LogEvent(EVENTCATEGORIES::FriendlyNameChange, 1, "New friendly name", appConfig.friendlyName);
    }

    if (server.hasArg("heartbeatinterval")){
      os_timer_disarm(&heartbeatTimer);
      appConfig.heartbeatInterval = server.arg("heartbeatinterval").toInt();
      LogEvent(EVENTCATEGORIES::HeartbeatIntervalChange, 1, "New Heartbeat interval", (String)appConfig.heartbeatInterval);
      os_timer_arm(&heartbeatTimer, appConfig.heartbeatInterval * 1000, true);
    }

    //  MQTT settings
    if (server.hasArg("mqttbroker")){
      if ((String)appConfig.mqttServer != server.arg("mqttbroker"))
        mqttDirty = true;
        sprintf(appConfig.mqttServer, "%s", server.arg("mqttbroker").c_str());
        LogEvent(EVENTCATEGORIES::MqttParamChange, 1, "New MQTT broker", appConfig.mqttServer);
    }

    if (server.hasArg("mqttport")){
      if (appConfig.mqttPort != atoi(server.arg("mqttport").c_str()))
        mqttDirty = true;
      appConfig.mqttPort = atoi(server.arg("mqttport").c_str());
      LogEvent(EVENTCATEGORIES::MqttParamChange, 2, "New MQTT port", server.arg("mqttport").c_str());
    }

    if (server.hasArg("mqtttopic")){
      if ((String)appConfig.mqttTopic != server.arg("mqtttopic"))
        mqttDirty = true;
        sprintf(appConfig.mqttTopic, "%s", server.arg("mqtttopic").c_str());
        LogEvent(EVENTCATEGORIES::MqttParamChange, 1, "New MQTT topic", appConfig.mqttTopic);
    }

    if (mqttDirty)
      PSclient.disconnect();

    saveSettings();
    ESP.reset();

  }

  File f = LittleFS.open("/pageheader.html", "r");
  String headerString;
  if (f.available()) headerString = f.readString();
  f.close();

  time_t localTime = myTZ.toLocal(now(), &tcr);

  f = LittleFS.open("/generalsettings.html", "r");

  String s, htmlString, timezoneslist;

  char ss[2];

  for (signed char i = -12; i < 15; i++) {
    itoa(i, ss, DEC);
    timezoneslist+="<option ";
    if (appConfig.timeZone == i){
      timezoneslist+= "selected ";
    }
    timezoneslist+= "value=\"";
    timezoneslist+=ss;
    timezoneslist+="\">UTC ";
    if (i>0){
      timezoneslist+="+";
    }
    if (i!=0){
      timezoneslist+=ss;
      timezoneslist+=":00";
    }
    timezoneslist+="</option>";
    timezoneslist+="\n";
  }

  while (f.available()){
    s = f.readStringUntil('\n');

    if (s.indexOf("%pageheader%")>-1) s.replace("%pageheader%", headerString);
    if (s.indexOf("%year%")>-1) s.replace("%year%", (String)year(localTime));
    if (s.indexOf("%mqtt-servername%")>-1) s.replace("%mqtt-servername%", appConfig.mqttServer);
    if (s.indexOf("%mqtt-port%")>-1) s.replace("%mqtt-port%", String(appConfig.mqttPort));
    if (s.indexOf("%mqtt-topic%")>-1) s.replace("%mqtt-topic%", appConfig.mqttTopic);
    if (s.indexOf("%timezoneslist%")>-1) s.replace("%timezoneslist%", timezoneslist);
    if (s.indexOf("%friendlyname%")>-1) s.replace("%friendlyname%", appConfig.friendlyName);
    if (s.indexOf("%heartbeatinterval%")>-1) s.replace("%heartbeatinterval%", (String)appConfig.heartbeatInterval);

    htmlString+=s;
  }
  f.close();
  server.send(200, "text/html", htmlString);

  LogEvent(EVENTCATEGORIES::PageHandler, 2, "Page served", "generalsettings.html");
}

void handleNetworkSettings() {
  LogEvent(EVENTCATEGORIES::PageHandler, 1, "Page requested", "networksettings.html");

  if (!is_authenticated()){
     String header = "HTTP/1.1 301 OK\r\nLocation: /login.html\r\nCache-Control: no-cache\r\n\r\n";
     server.sendContent(header);
     return;
   }

  if (server.method() == HTTP_POST){  //  POST
    if (server.hasArg("ssid")){
      strcpy(appConfig.ssid, server.arg("ssid").c_str());
      strcpy(appConfig.password, server.arg("password").c_str());
      saveSettings();

      isAccessPoint=false;
      connectionState = STATE_CHECK_WIFI_CONNECTION;
      WiFi.disconnect(false);

      ESP.reset();
    }
  }

  File f = LittleFS.open("/pageheader.html", "r");

  String headerString;

  if (f.available()) headerString = f.readString();
  f.close();

  time_t localTime = myTZ.toLocal(now(), &tcr);

  f = LittleFS.open("/networksettings.html", "r");
  String s, htmlString, wifiList;

  byte numberOfNetworks = WiFi.scanNetworks();
  for (size_t i = 0; i < numberOfNetworks; i++) {
    wifiList+="<div class=\"radio\"><label><input ";
    if (i==0) wifiList+="id=\"ssid\" ";

    wifiList+="type=\"radio\" name=\"ssid\" value=\"" + WiFi.SSID(i) + "\">" + WiFi.SSID(i) + "</label></div>";
  }

  while (f.available()){
    s = f.readStringUntil('\n');

    if (s.indexOf("%pageheader%")>-1) s.replace("%pageheader%", headerString);
    if (s.indexOf("%year%")>-1) s.replace("%year%", (String)year(localTime));
    if (s.indexOf("%wifilist%")>-1) s.replace("%wifilist%", wifiList);
      htmlString+=s;
    }
    f.close();
  server.send(200, "text/html", htmlString);

  LogEvent(EVENTCATEGORIES::PageHandler, 2, "Page served", "networksettings.html");
}

void handleTools() {
  LogEvent(EVENTCATEGORIES::PageHandler, 1, "Page requested", "tools.html");

  if (!is_authenticated()){
     String header = "HTTP/1.1 301 OK\r\nLocation: /login.html\r\nCache-Control: no-cache\r\n\r\n";
     server.sendContent(header);
     return;
   }

  if (server.method() == HTTP_POST){  //  POST

    if (server.hasArg("reset")){
      LogEvent(EVENTCATEGORIES::Reboot, 1, "Reset", "");
      defaultSettings();
      ESP.reset();
    }

    if (server.hasArg("restart")){
      LogEvent(EVENTCATEGORIES::Reboot, 2, "Restart", "");
      ESP.reset();
    }
  }

  File f = LittleFS.open("/pageheader.html", "r");
  String headerString;
  if (f.available()) headerString = f.readString();
  f.close();

  time_t localTime = myTZ.toLocal(now(), &tcr);

  f = LittleFS.open("/tools.html", "r");

  String s, htmlString;

  while (f.available()){
    s = f.readStringUntil('\n');

    if (s.indexOf("%pageheader%")>-1) s.replace("%pageheader%", headerString);
    if (s.indexOf("%year%")>-1) s.replace("%year%", (String)year(localTime));

      htmlString+=s;
    }
    f.close();
  server.send(200, "text/html", htmlString);

  LogEvent(EVENTCATEGORIES::PageHandler, 2, "Page served", "tools.html");
}

/*
    for (size_t i = 0; i < server.args(); i++) {
      Serial.print(server.argName(i));
      Serial.print(": ");
      Serial.println(server.arg(i));
    }
*/

void handleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void SendHeartbeat(){

    if (PSclient.connected()){

    time_t localTime = myTZ.toLocal(now(), &tcr);

    const size_t capacity = JSON_OBJECT_SIZE(3) + JSON_OBJECT_SIZE(6) + 180;
    StaticJsonDocument<capacity> doc;

    doc["Time"] = DateTimeToString(localTime);
    doc["Node"] = ESP.getChipId();
    doc["Freeheap"] = ESP.getFreeHeap();
    doc["FriendlyName"] = appConfig.friendlyName;
    doc["HeartbeatInterval"] = appConfig.heartbeatInterval;

    JsonObject wifiDetails = doc.createNestedObject("Wifi");
    wifiDetails["SSId"] = String(WiFi.SSID());
    wifiDetails["MACAddress"] = String(WiFi.macAddress());
    wifiDetails["IPAddress"] = WiFi.localIP().toString();

    #ifdef __debugSettings
    serializeJsonPretty(doc,Serial);
    Serial.println();
    #endif

    String myJsonString;

    serializeJson(doc, myJsonString);

    PSclient.publish((MQTT_CUSTOMER + String("/") + MQTT_PROJECT + "/" + appConfig.mqttTopic + "/HEARTBEAT").c_str(), myJsonString.c_str(), false);
  }

  needsHeartbeat = false;
}

void RefreshSunData(){

  time_t localTime = myTZ.toLocal(now(), &tcr);

  sunData.Sunrise = CalculateSunData(localTime, LATITUDE, LONGITUDE, Sunrise);
  sunData.Sunset  = CalculateSunData(localTime, LATITUDE, LONGITUDE, Sunset );

  localTime = myTZ.toLocal(sunData.Sunrise, &tcr);
  String sr = DateTimeToString(localTime);

  localTime = myTZ.toLocal(sunData.Sunset, &tcr);
  String ss = DateTimeToString(localTime);

  LogEvent(EVENTCATEGORIES::RefreshSunsetSunrise, 1, "Sun data calculated", "Sunrise: " + sr + " - Sunset: " + ss);
}

bool NeedsEntranceLight(){

  uint32_t mySunrise = (uint32_t)sunData.Sunrise;
  uint32_t mySunset = (uint32_t)sunData.Sunset;

  mySunrise+=appConfig.sunriseLightOffset * 60;
  mySunset+=appConfig.sunsetLightOffset * 60;

  //  Sunrise
  if ( hour() < hour(mySunrise) ) return true;
  if ( hour() == hour(mySunrise) ){
    if ( minute() < minute(mySunrise) ) return true;
    if ( minute() == minute(mySunrise) ){
      if ( second() < second(mySunrise) ) return true;
    }
  }

  //  Sunset
  if ( hour() < hour(mySunset) ) return false;
  if ( hour() == hour(mySunset) ){
    if ( minute() < minute(mySunset) ) return false;
    if ( minute() == minute(mySunset) ){
      if ( second() < second(mySunset) ) return false;
    }
  }

  return true;
}

void StartStaircaseLight(){
  buttonPressedTime = millis();
  LogEvent(EVENTCATEGORIES::StaircaseLight, 1, "Staircaselights", String(appConfig.staircaseLightDelay));
  if (PSclient.connected()){
    PSclient.publish((MQTT_CUSTOMER + String("/") + MQTT_PROJECT + String("/") + appConfig.mqttTopic + "/RESULT/POWER1").c_str(), "on", false );
  }
}

void ScanI2C(){
  byte error, address;
  int nDevices;

  Serial.println("\nScanning for I2C devices...");

  nDevices = 0;
  for(address = 1; address < 127; address++ )
  {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0)
    {
      Serial.print("Device ");
      Serial.print(nDevices);
      Serial.print(":\t0x");
      if (address<16)
        Serial.print("0");
      Serial.println(address,HEX);

      nDevices++;
    }
    else if (error==4)
    {
      Serial.print("Unknow error at address 0x");
      if (address<16)
        Serial.print("0");
      Serial.println(address,HEX);
    }
  }
  if (nDevices == 0)
    Serial.println("No I2C devices found.\n");

}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {

  Serial.print("Topic:\t\t");
  Serial.println(topic);

  Serial.print("Payload:\t");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  StaticJsonDocument<JSON_MQTT_COMMAND_SIZE> doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.println("Failed to parse incoming string.");
    Serial.println(error.c_str());
    for (size_t i = 0; i < 10; i++) {
      digitalWrite(CONNECTION_STATUS_LED_GPIO, !digitalRead(CONNECTION_STATUS_LED_GPIO));
      delay(50);
    }
    return;
  }
  else{
    //  It IS a JSON string

    #ifdef __debugSettings
    serializeJsonPretty(doc,Serial);
    Serial.println();
    #endif


    String sCommand;
    uint8_t iChannel;
    
    //  Relay0 - Entrance lights
    if (doc.containsKey("POWER0")){
      const char* myKey = doc["POWER0"];
      sCommand = myKey;
      sCommand.toUpperCase();
      iChannel = 0;
    }

    //  Relay1 - Staircase lights
    if (doc.containsKey("POWER1")){
      const char* myKey = doc["POWER1"];
      sCommand = myKey;
      sCommand.toUpperCase();
      iChannel = 1;
    }

    //  Relay2 - 
    if (doc.containsKey("POWER2")){
      const char* myKey = doc["POWER2"];
      sCommand = myKey;
      sCommand.toUpperCase();
      iChannel = 2;
    }

    //  Relay3 - 
    if (doc.containsKey("POWER3")){
      const char* myKey = doc["POWER3"];
      sCommand = myKey;
      sCommand.toUpperCase();
      iChannel = 3;
    }

    if ( sCommand == "ON" ){
      if ( iChannel == 1)
        StartStaircaseLight();
      i2c_io.write(iChannel, 0);
      if (PSclient.connected()){
        PSclient.publish((MQTT_CUSTOMER + String("/") + MQTT_PROJECT + String("/") + appConfig.mqttTopic + "/RESULT/POWER" + (String)iChannel).c_str(), "on", false );
      }
    }
    else if ( sCommand == "OFF" ){
      i2c_io.write(iChannel, 1);
      if (PSclient.connected()){
        PSclient.publish((MQTT_CUSTOMER + String("/") + MQTT_PROJECT + String("/") + appConfig.mqttTopic + "/RESULT/POWER" + (String)iChannel).c_str(), "off", false );
      }
    }

    //  reset
    if (doc.containsKey("reset")){
      LogEvent(EVENTCATEGORIES::MqttMsg, 1, "Reset", "");
      defaultSettings();
      ESP.reset();
    }

    //  restart
    if (doc.containsKey("restart")){
      LogEvent(EVENTCATEGORIES::MqttMsg, 2, "Restart", "");
      ESP.reset();
    }
  }

}
  

void setup() {
  delay(1); //  Needed for PlatformIO serial monitor
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  Serial.print("\n\n\n\rBooting node:     ");
  Serial.print(ESP.getChipId());
  Serial.println("...");

  String FirmwareVersionString = String(FIRMWARE_VERSION) + " @ " + String(__TIME__) + " - " + String(__DATE__);

  Serial.println("Hardware ID:      " + (String)HARDWARE_ID);
  Serial.println("Hardware version: " + (String)HARDWARE_VERSION);
  Serial.println("Software ID:      " + (String)SOFTWARE_ID);
  Serial.println("Software version: " + FirmwareVersionString);
  Serial.println();

  //  File system
  if (!LittleFS.begin()){
    Serial.println("Error: Failed to initialize the filesystem!");
  }

  if (!loadSettings(appConfig)) {
    Serial.println("Failed to load config, creating default settings...");
    defaultSettings();
  } else {
    Serial.println("Config loaded.");
  }

  sprintf(defaultSSID, "%s-%u", appConfig.mqttTopic, ESP.getChipId());
  WiFi.hostname(defaultSSID);
  
  //  GPIOs

  //  outputs
  pinMode(CONNECTION_STATUS_LED_GPIO, OUTPUT);
  digitalWrite(CONNECTION_STATUS_LED_GPIO, HIGH);

  //  I2C
  Wire.begin(SDA_GPIO, SCL_GPIO);
  ScanI2C();

  #ifdef __debugSettings

  for (size_t i = 0; i < 5; i++) {
    i2c_io.write8(0x00);
    delay(100);
    i2c_io.write8(0xff);
    delay(100);
  }

  #endif

  //  OTA
  ArduinoOTA.onStart([]() {
    Serial.println("OTA started.");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA finished.");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    if (progress % OTA_BLINKING_RATE == 0){
      if (digitalRead(CONNECTION_STATUS_LED_GPIO)==HIGH)
        digitalWrite(CONNECTION_STATUS_LED_GPIO, LOW);
        else
        digitalWrite(CONNECTION_STATUS_LED_GPIO, HIGH);
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Authentication failed.");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin failed.");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect failed.");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive failed.");
    else if (error == OTA_END_ERROR) Serial.println("End failed.");
  });

  ArduinoOTA.begin();

  Serial.println();

  server.on("/", handleRoot);
  server.on("/status.html", handleStatus);
  server.on("/generalsettings.html", handleGeneralSettings);
  server.on("/networksettings.html", handleNetworkSettings);
  server.on("/staircaselighttimer.html", handleStaircaseLightTimer);
  server.on("/entrancelight.html", handleEntranceLight);
  server.on("/tools.html", handleTools);
  server.on("/login.html", handleLogin);

  server.onNotFound(handleNotFound);

  //  Web server
  if (MDNS.begin("esp8266")) {
    Serial.println("MDNS responder started.");
  }

  //  Start HTTP (web) server
  server.begin();
  Serial.println("HTTP server started.");

  //  Authenticate HTTP requests
  const char * headerkeys[] = {"User-Agent","Cookie"} ;
  size_t headerkeyssize = sizeof(headerkeys)/sizeof(char*);
  server.collectHeaders(headerkeys, headerkeyssize );

  //  Timers
  os_timer_setfn(&heartbeatTimer, heartbeatTimerCallback, NULL);
  os_timer_arm(&heartbeatTimer, appConfig.heartbeatInterval * 1000, true);

  #ifdef _use_local_sun_data
  os_timer_setfn(&sunDataTimer, sunDataTimerCallback, NULL);
  os_timer_arm(&sunDataTimer, 60 * 60 * 1000, true);
  #endif

  //  Randomizer
  SetRandomSeed();

  // Set the initial connection state
  connectionState = STATE_CHECK_WIFI_CONNECTION;

}

void loop(){

  if (isAccessPoint){
    if (!isAccessPointCreated){
      Serial.print("Could not connect to ");
      Serial.print(appConfig.ssid);
      Serial.println("\r\nReverting to Access Point mode.");

      delay(500);

      WiFi.mode(WiFiMode::WIFI_AP);
      WiFi.softAP(defaultSSID, DEFAULT_PASSWORD);

      IPAddress myIP;
      myIP = WiFi.softAPIP();
      isAccessPointCreated = true;

      Serial.println("Access point created. Use the following information to connect to the ESP device, then follow the on-screen instructions to connect to a different wifi network:");

      Serial.print("SSID:\t\t\t");
      Serial.println(defaultSSID);

      Serial.print("Password:\t\t");
      Serial.println(DEFAULT_PASSWORD);

      Serial.print("Access point address:\t");
      Serial.println(myIP);

      Serial.println();
      Serial.println("Note: The device will reset in 5 minutes.");


      os_timer_setfn(&accessPointTimer, accessPointTimerCallback, NULL);
      os_timer_arm(&accessPointTimer, ACCESS_POINT_TIMEOUT, true);
      os_timer_disarm(&heartbeatTimer);
    }
    server.handleClient();
  }
  else{
    switch (connectionState) {

      // Check the WiFi connection
      case STATE_CHECK_WIFI_CONNECTION:

        // Are we connected ?
        if (WiFi.status() != WL_CONNECTED) {
          // Wifi is NOT connected
          digitalWrite(CONNECTION_STATUS_LED_GPIO, HIGH);
          connectionState = STATE_WIFI_CONNECT;
        } else  {
          // Wifi is connected so check Internet
          digitalWrite(CONNECTION_STATUS_LED_GPIO, LOW);
          connectionState = STATE_CHECK_INTERNET_CONNECTION;

          server.handleClient();
        }
        break;

      // No Wifi so attempt WiFi connection
      case STATE_WIFI_CONNECT:
        {
          // Indicate NTP no yet initialized
          ntpInitialized = false;

          digitalWrite(CONNECTION_STATUS_LED_GPIO, HIGH);
          Serial.printf("Trying to connect to WIFI network: %s", appConfig.ssid);

          // Set station mode
          WiFi.mode(WIFI_STA);

          // Start connection process
          WiFi.begin(appConfig.ssid, appConfig.password);

          // Initialize iteration counter
          uint8_t attempt = 0;

          while ((WiFi.status() != WL_CONNECTED) && (attempt++ < WIFI_CONNECTION_TIMEOUT)) {
            digitalWrite(CONNECTION_STATUS_LED_GPIO, LOW);
            Serial.print(".");
            delay(50);
            digitalWrite(CONNECTION_STATUS_LED_GPIO, HIGH);
            delay(950);
          }
          if (attempt >= WIFI_CONNECTION_TIMEOUT) {
            Serial.println();
            Serial.println("Could not connect to WiFi");
            delay(100);

            isAccessPoint=true;

            break;
          }
          digitalWrite(CONNECTION_STATUS_LED_GPIO, LOW);
          Serial.println(" Success!");
          Serial.print("IP address: ");
          Serial.println(WiFi.localIP());
          connectionState = STATE_CHECK_INTERNET_CONNECTION;
        }
        break;

      case STATE_CHECK_INTERNET_CONNECTION:

        // Do we have a connection to the Internet ?
        if (checkInternetConnection()) {
          // We have an Internet connection

          if (!ntpInitialized) {
            // We are connected to the Internet for the first time so set NTP provider
            initNTP();

            ntpInitialized = true;
            needsSunData = true;
            Serial.println("Connected to the Internet.");
          }

          connectionState = STATE_INTERNET_CONNECTED;
        } else  {
          connectionState = STATE_CHECK_WIFI_CONNECTION;
        }
        break;

      case STATE_INTERNET_CONNECTED:

        ArduinoOTA.handle();

        if (!PSclient.connected()) {
          PSclient.setServer(appConfig.mqttServer, appConfig.mqttPort);
          if (PSclient.connect(defaultSSID, (MQTT_CUSTOMER + String("/") + MQTT_PROJECT + String("/") + appConfig.mqttTopic + "/STATE").c_str(), 0, true, "offline" )){
            PSclient.setCallback(mqtt_callback);

            PSclient.subscribe((MQTT_CUSTOMER + String("/") + MQTT_PROJECT + String("/") + appConfig.mqttTopic + "/cmnd").c_str(), 0);

            PSclient.publish((MQTT_CUSTOMER + String("/") + MQTT_PROJECT + String("/") + appConfig.mqttTopic + "/STATE").c_str(), "online", true);
            LogEvent(EVENTCATEGORIES::Conn, 1, "Node online", WiFi.localIP().toString());
          }
        }


#ifdef _use_local_sun_data
        if (needsSunData){
          RefreshSunData();
          needsSunData = false;
        }

        //  External ligths
        if (NeedsEntranceLight()) {
          if (!entranceLightState){
            LogEvent(EVENTCATEGORIES::EntranceLight, 2, "Lights", "on");
            if (PSclient.connected()){
              PSclient.publish(MQTT::Publish(MQTT_CUSTOMER + String("/") + MQTT_PROJECT + String("/") + String(ESP.getChipId()) + "/POWER0", "on" ).set_qos(0));
            }
          i2c_io.write(ENTRANCELIGHT_RELAY, 1);
          entranceLightState = true;
          }
        }
        else{
          if (entranceLightState){
            LogEvent(EVENTCATEGORIES::EntranceLight, 3, "Lights", "off");
            if (PSclient.connected()){
              PSclient.publish(MQTT::Publish(MQTT_CUSTOMER + String("/") + MQTT_PROJECT + String("/") + String(ESP.getChipId()) + "/POWER0", "off" ).set_qos(0));
            }
          }
          i2c_io.write(ENTRANCELIGHT_RELAY, 0);
          entranceLightState = false;
        }
#endif

        inputPattern = i2c_io.read8();
        //Serial.println(inputPattern, BIN);

        //  Staircase lights

        if ( (inputPattern & INPUT_MASK_1) == 0 ){
          if (millis() - buttonPressedTime > BUTTON_DEBOUNCE_DELAY){
            i2c_io.write(1, 0);
            StartStaircaseLight();
          }
        }

        if ( millis() - buttonPressedTime < appConfig.staircaseLightDelay * 1000 ){
          i2c_io.write(STAIRCASELIGHT_RELAY, 0);
          stairlightLastState = true;
        }
        else{
          i2c_io.write(STAIRCASELIGHT_RELAY, 1);
          if ( stairlightLastState ){
            LogEvent(EVENTCATEGORIES::StaircaseLight, 2, "Staircaselights", "0");
            if (PSclient.connected()){
              PSclient.publish((MQTT_CUSTOMER + String("/") + MQTT_PROJECT + String("/") + appConfig.mqttTopic + "/RESULT/POWER1").c_str(), "off", false );
            }
          }
          stairlightLastState = false;
        }

        if (PSclient.connected()){
          PSclient.loop();
        }

        if (needsHeartbeat){
          SendHeartbeat();
          needsHeartbeat = false;
        }

        // Set next connection state
        connectionState = STATE_CHECK_WIFI_CONNECTION;
        break;
      }

  }
}
