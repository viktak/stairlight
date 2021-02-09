#ifndef DEFINES_H
#define DEFINES_H

#define MQTT_CUSTOMER "viktak"
#define MQTT_PROJECT  "spiti"

#define HARDWARE_ID "NeoPixel & I2C"
#define HARDWARE_VERSION "1.0"
#define SOFTWARE_ID "Stairlight Express"

#define DEBUG_SPEED 921600

#define JSON_SETTINGS_SIZE (JSON_OBJECT_SIZE(11) + 270)
#define JSON_MQTT_COMMAND_SIZE 300

#define CONTROL_COMMAND_JSON_SIZE 200

#define CONNECTION_STATUS_LED_GPIO 0

#define SDA_GPIO 13
#define SCL_GPIO 14

#define ENTRANCELIGHT_RELAY 0
#define STAIRCASELIGHT_RELAY 1

//#define I2C_IO_ADDRESS 0x27
#define I2C_IO_ADDRESS 0x3F

//  Input masks
#define INPUT_MASK_0  0b10000000
#define INPUT_MASK_1  0b01000000
#define INPUT_MASK_2  0b00100000
#define INPUT_MASK_3  0b00010000

//  Default values
#define DEFAULT_STAIRCASE_LIGHT_DELAY 60
#define DEFAULT_SUNRISE_LIGHT_OFFSET 0
#define DEFAULT_SUNSET_LIGHT_OFFSET 0

#endif