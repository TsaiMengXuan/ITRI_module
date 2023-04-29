#ifndef MODBUS_CONFIG_H
#define MODBUS_CONFIG_H

// Server IP and Port
// Testing Area IP(DAQ): 192.168.200.102
// WS1560 IP:192.168.200.103
// Remote Port: 2004
// podcast port: 65535 
// Access password: FAMA
// PPML_5 pwd: 02750963
// 0X0012 current power consumption


#define SERVER_IP                           "192.168.50.186"
#define SERVER_PORT                         502

// Coil and Register Addresses
#define DO0_COIL_ADDRESS                    16
#define DO1_COIL_ADDRESS                    17
#define AI0_REGISTER_ADDRESS                0
#define AI_AVERAGE_VALUE                    8
#define AI0_ENABLE_ADDRESS                  220
#define RESET_HISTORICAL_MAX_AI0_VALUE      100
#define RESET_HISTORICAL_MAX_AVERAGE_VALUE  108
#define RESET_HISTORICAL_MIN_AI0_VALUE      110
#define RESET_HISTORICAL_MIN_AVERAGE_VALUE  118
#define AI0_HIGH_ALARM_FLAG                 130
#define AI_AVERAGE_HIGH_ALARM_VALUE         138
#define AI0_LOW_ALARM_FLAG                  140
#define AI_AVERAGE_LOW_ALARM_FLAG           148
#define OMRON_EFFECTIVE_POWER               26
#define OMRON_INTEGRATED_EFFECTIVE_ENERGY   512
#define REGISTER_COUNTS                     8
#define COIL_COUNTS                         8
#define TEN_VOLTAGE_RANGE                   10.0
#define FIVE_VOLTAGE_RANGE                  5.0
#define ONE_VOLTAGE_RANGE                   1.0
#define FIVE_HUNDRED_MILLIVOLTS_RANGE       0.5
#define HUNDRED_FIFTY_MILIVOLTS_RANGE       0.15

// Choose Input Range
// float TEN_VOLTAGE_RANGE[8] = {10.0, 10.0, 10.0, 10.0, 10.0, 10.0, 10.0, 10.0};
// float FIVE_VOLTAGE_RANGE[8] = {5.0, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0};
// float ONE_VOLTAGE_RANGE[8] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
// float FIVE_HUNDRED_MILLIVOLTS_RANGE[8] = {0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5};
// float HUNDRED_FIFTY_MILIVOLTS_RANGE[8] = {0.15, 0.15, 0.15, 0.15, 0.15, 0.15, 0.15, 0.15};

#endif //MODBUS_CONFIG_H
