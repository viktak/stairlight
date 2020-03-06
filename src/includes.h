#include "defines.h"
#include "../../_common/defines.h"

#include "enums.h"
#include "../../_common/enums.h"

#include <cstdlib>
#include <string>

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>

#include <Wire.h>
#include <pcf8574_esp.h>

#define FS_NO_GLOBALS   //  This is needed for FS.h and SD.h together
#include <FS.h>

#include <PubSubClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

#include <ArduinoJson.h>
#include <TimeLib.h>
#include <Time.h>
#include <Timezone.h>
#include "NTP.h"

#include "structs.h"

#include "user_interface.h"