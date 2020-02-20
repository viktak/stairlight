struct config{
  char ssid[32];
  char password[32];

  signed char timeZone;

  char mqttServer[64];
  int mqttPort;

  int sunsetLightOffset;
  int sunriseLightOffset;

  unsigned long staircaseLightDelay;

};

struct sunData_t{
  time_t Sunrise;
  time_t Sunset;
};
