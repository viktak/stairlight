#ifndef INCLUDES_H
#define INCLUDES_H

#include "version.h"

#include "../../../ActoSenso/Nodes/_common/debug.h"

#include "defines.h"
#include "../../_common/defines.h"

#include "enums.h"
#include "../../_common/enums.h"

#include "../../_common/variables.cpp"

#include <cstdlib>
#include <string>

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>

#include <pcf8574_esp.h>

#include <LittleFS.h>

#include <PubSubClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

#include <ArduinoJson.h>
#include <TimeLib.h>
#include <Time.h>
#include <Timezone.h>
#include "NTP.h"

#include "structs.h"
#include <TimeChangeRules.h>

#include "user_interface.h"

#endif