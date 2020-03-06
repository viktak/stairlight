struct config{
  char ssid[32];
  char password[32];

  char friendlyName[30];
  uint heartbeatInterval;

  signed char timeZone;

  char mqttServer[64];
  int mqttPort;
  char mqttTopic[32];

  bool dst;

  int sunsetLightOffset;
  int sunriseLightOffset;

  unsigned long staircaseLightDelay;

};

struct sunData_t{
  time_t Sunrise;
  time_t Sunset;
};
