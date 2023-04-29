#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <modbus/modbus.h>
#include <unistd.h>
#include "modbus_config.h"
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>

#define BUFFER_SIZE     50

// 建立連線
int create_connection(modbus_t *ctx);
// 斷開連線
void disconnection(modbus_t *ctx);
// 建立通訊封包
modbus_t *create_TCP_context(const char *server_ip, int port, int slave_addr);
// read_value_by_coil: 讀取 coil 數值
int read_value_by_coil(modbus_t *ctx, int coil_addr, int number_of_bits, uint8_t *coil_values);
// read_analog_inputs_voltage: 讀取類比輸入數值並轉換成電壓值
float read_analog_inputs_voltage(modbus_t *ctx, int reg_addr, int number_of_registers, float volt_range);
// read_value_by_register： 讀取記憶體位址數值
uint16_t read_value_by_register(modbus_t *ctx, int reg_addr, int number_of_registers);
// set_value_to_coil: 寫入 coil 數值
int set_value_to_coil(modbus_t *ctx, int coil_addr, int state);   
// set_input_channel_enable_status: 啟用/禁用單一類比通道
int set_input_channel_enable_status(modbus_t *ctx, int reg_addr, uint16_t enable);
// convert_to_voltage: 將數值轉換成電壓值
float convert_to_voltage(uint16_t register_value, float max_voltage);
void write_log(const char *message);
void hex_to_str(const unsigned char *hex_array, size_t hex_array_len, char *str);

void hex_to_str(const unsigned char *hex_array, size_t hex_array_len, char *str) {
    for (size_t i = 0; i < hex_array_len; i++) {
        sprintf(str + (i * 2), "%04X", hex_array[i]);
    }
    str[hex_array_len * 2] = '\0';
}

/*Main Program*/
int main(int argc, char *argv[]) {
    uint8_t *coil_values;
    modbus_t *ctx = create_TCP_context(SERVER_IP, SERVER_PORT, 1);
    create_connection(ctx);
    // Test: Battery monitor

    // printf("Following are Analog Input Channel voltage!\n");
    // read_analog_inputs_voltage(ctx, AI0_REGISTER_ADDRESS, REGISTER_COUNTS, TEN_VOLTAGE_RANGE);

    // printf("Following are Analog Input Channel voltage!\n");
    // read_analog_inputs_voltage(ctx, OMRON_EFFECTIVE_POWER, 1, TEN_VOLTAGE_RANGE);

    // printf("Following are Analog Input Channel voltage!\n");
    // read_analog_inputs_voltage(ctx, OMRON_INTEGRATED_EFFECTIVE_ENERGY, 1, TEN_VOLTAGE_RANGE);
    //read_value_by_register(ctx, 3, 1);
    
    // int i;
    // for(i=0;i<1024;i+=2)
    // {
    //     read_value_by_register(ctx, i, 2);
    // }
    // 

    while (true)
    {
        int record = read_value_by_register(ctx, 0x0200, 2);
        
        if (record != EXIT_FAILURE) {
            char log_message[20];
            snprintf(log_message, sizeof(log_message), "0x%.4x", record);
            write_log(log_message);
        }
        usleep(1000*1000);
    }
    
    // collect data
    // convert formula kwH / wH 
    // 

    // read_value_by_register(ctx, 512, 1);
    // Test: DAQ's Digital Output
    // printf("Following are Digital Output Coil Values!\n");
    // read_value_by_coil(ctx, DO0_COIL_ADDRESS, 1, coil_values);  

    // printf("Following instruction is going to set coil value!\n");
    // set_value_to_coil(ctx, DO0_COIL_ADDRESS, 0);

    // printf("Following instruction is goint to set Analog Input Channel 'Enable/Disable' status!\n");
    // printf("1 -> Enable, 0 -> Disable!\n");
    // set_input_channel_enable_status(ctx, AI0_ENABLE_ADDRESS, 1);

    disconnection(ctx);
    return 0;
}

// [Create TCP Context] API
// parameter need to add set_slave_id()
// check modbus_new_tcp connect successful
modbus_t *create_TCP_context(const char *server_ip, int port, int slave_addr){
    modbus_t *ctx = modbus_new_tcp(server_ip, port);
    if (ctx == NULL) {
        fprintf(stderr, "Failed to create Modbus TCP context\n");
        return NULL;
    }else {
        int rc = modbus_set_slave(ctx, slave_addr);
        if (rc == -1) {
            fprintf(stderr, "Slave device address illegal: %s\n", modbus_strerror(errno));
            return NULL;
        }else {        
            fprintf(stderr, "Create Modbus TCP context sucessfully!\n");
            return ctx;
        }
    }
}

// [Create Connection between Modbus and Network] API
int create_connection(modbus_t *ctx){
    if (modbus_connect(ctx) == -1) {
            fprintf(stderr, "Failed to connect to Modbus server\n");
            modbus_free(ctx);
            return -1;
        }else{
            fprintf(stderr, "Connect to Modbus server!\n");
            return 0;
        }
}

// [Disconnect Modbus] API
void disconnection(modbus_t *ctx){
    modbus_close(ctx);
    modbus_free(ctx);
}

// [Read Digital Output] API
int read_value_by_coil(modbus_t *ctx, int coil_addr, int number_of_bits, uint8_t *coil_values){
    // Memory Allocation
    coil_values = (uint8_t*)malloc(number_of_bits * sizeof(uint8_t));
    if (coil_values == NULL){
        fprintf(stderr, "Memory Allocation error!\n");
        return EXIT_FAILURE;
    }
    // Read Value
    // 如果讀值失敗，e.g. number_of_bits = 4，只有讀到 3 個，應該怎麼做處裡 !? 讀值為全有還是全無 (survey)
    // 回傳部分結果並顯示哪些部分異常
    // 與學長討論如果沒有讀到 coil value 應該怎麼處理
    int rc = modbus_read_bits(ctx, coil_addr, number_of_bits, coil_values);

    if (rc == -1) {
        fprintf(stderr, "Failed to read coil values: %s\n", modbus_strerror(errno));
        free(coil_values);
        return EXIT_FAILURE;
    } else {
        if (rc != number_of_bits) {
            printf("Failed to read %d coil values\n", number_of_bits - rc);
        }
        for (int i = 0; i < number_of_bits; i++) {
            printf("Coil %d value: %d\n", coil_addr, coil_values[i]);
        }
    }
    return EXIT_SUCCESS;
}

// 讀取暫存器的原始數值
// 更改 return register value
// 參數增加一個記憶體位址，把讀到結果存進這個位址中，回傳值為true/false
// Error about memory allocation 
// Free
uint16_t read_value_by_register(modbus_t *ctx, int reg_addr, int number_of_registers){
    uint16_t *register_values = (uint16_t*)malloc(number_of_registers * sizeof(uint16_t));
    if (register_values == NULL){
        fprintf(stderr, "Memory Allocation error!\n");
        return EXIT_FAILURE;
    }
    int rc = modbus_read_registers(ctx, reg_addr, number_of_registers, register_values);
    if (rc == -1) {
        fprintf(stderr, "Failed to read analog input values: %s\n", modbus_strerror(errno));
        free(register_values);
        return EXIT_FAILURE;
    } else {
        // Indicate Analog Input register Values
        // for (int i = 0; i < number_of_registers; i++) {
        //     printf("%.4d ",register_values[i]);
        // }
        printf("\n");
        for (int i = 0; i < number_of_registers; i++) {
            printf("%.4x ",register_values[i]);
        }
        
        printf("\n");
        printf("\n");
        return register_values[1];
        free(register_values);

        // return EXIT_SUCCESS;
    }
}

// Read Analog Input API
// Reference: modbus' context, start address, 
//                      number of registers want to read, choose voltage range
// start address means when gets register values, save the value on PC from START_ADDRESS
// read_analog_inputs_voltage(ctx, AI0_REGISTER_ADDRESS, 4, START_ADDRESS, TEN_VOLTAGE_RANGE)
float read_analog_inputs_voltage(modbus_t *ctx, int reg_addr, int number_of_registers, float volt_range){
    // Memory Allocation
    uint16_t *ai_register_values = (uint16_t*)malloc(number_of_registers * sizeof(uint16_t));
    float *voltage_values = (float *)malloc(number_of_registers * sizeof(float));
    
    if (ai_register_values == NULL || voltage_values == NULL){
        fprintf(stderr, "Memory Allocation error!\n");
        free(ai_register_values);
        free(voltage_values);
        return EXIT_FAILURE;
    }
    
    // Read Value
    int rc = modbus_read_input_registers(ctx, reg_addr, number_of_registers, ai_register_values);
    if (rc == -1) {
        fprintf(stderr, "Failed to read analog input values: %s\n", modbus_strerror(errno));
        free(ai_register_values);
        free(voltage_values);
        return EXIT_FAILURE;
    } else {
        // Indicate Analog Input register Values and Convert to Voltage
        for (int i = 0; i < number_of_registers; i++) {
            printf("Channel Address %d [Analog input %d]: %u\n", reg_addr, i, ai_register_values[i]);
            float voltage = convert_to_voltage(ai_register_values[i], volt_range);
            voltage_values[i] = voltage;
            printf("Channel Address %d [Analog input %d]: %.4f\n", reg_addr, i, voltage);
        }
    }
    return *voltage_values;
}


// [Set Digital Output] API
// argument:"state" can only be 0 or 1
// adjust output context and compare read value choose to write or not
int set_value_to_coil(modbus_t *ctx, int coil_addr, int state){
    uint8_t *coil_values;
    coil_values = (uint8_t *)malloc(sizeof(uint8_t));
    modbus_read_bits(ctx, coil_addr, 1, coil_values);
    int integer_coil_values = (int)*coil_values;
    if (integer_coil_values != state)
    {
    // Write the state to the digital output coil
    int rc = modbus_write_bit(ctx, coil_addr, state);
        if (rc == -1) {
            fprintf(stderr, "Failed to write coil: %s\n", modbus_strerror(errno));
            return EXIT_FAILURE;
        } else {
            printf("Coil address %d state set to %d\n", coil_addr, state);
            return EXIT_SUCCESS;
        }        
    }
    printf("Coil address %d had set to %d already!\n", coil_addr, state);
    return EXIT_SUCCESS;
}

// write register value
// return 1 -> True / return 0-> False / return -1 -> Illegal Address
int set_value_to_register(modbus_t *ctx, int reg_addr, const uint16_t *value){
    uint16_t *reg_value;
    reg_value = (uint16_t *)malloc(sizeof(uint16_t));
    int rc = modbus_read_registers(ctx, reg_addr, 1, reg_value);
    if (rc == -1) {
        fprintf(stderr, "The register address cannot be written: %s\n", modbus_strerror(errno));
        free(reg_value);
        return EXIT_FAILURE;
    }
    if (*reg_value != *value)
    {
        // Write the state to the digital output coil
        rc = modbus_write_register(ctx, reg_addr, *value);
        if (rc == -1) {
            fprintf(stderr, "Failed to write register: %s\n", modbus_strerror(errno));
            free(reg_value);
            return EXIT_FAILURE;
        } else {
            printf("Register address %d value had set to %d\n", reg_addr, *value);
            free(reg_value);
            return EXIT_SUCCESS;
        }        
    }
    printf("Register address %d value had set to %d already!\n", reg_addr, *value);
    free(reg_value);
    return EXIT_SUCCESS;
}


// Set analog input channel enable/disable status
// set_input_channel_enable_status
// 調整API，將要寫入的值與讀到的值做比較，如果一樣，不動作，反之寫入
int set_input_channel_enable_status(modbus_t *ctx, int reg_addr, uint16_t enable) {
    uint16_t *status;
    status = (uint16_t *)malloc(sizeof(uint16_t));
    int rc = modbus_read_registers(ctx, reg_addr, 1, status);
    if (rc == -1) {
        fprintf(stderr, "The register address cannot be set enable/disable status: %s\n", modbus_strerror(errno));
        free(status);
        return EXIT_FAILURE;
    } if (*status != enable) {
        rc = modbus_write_register(ctx, reg_addr, enable);
        if (rc == -1) {
            fprintf(stderr, "Failed to set input channel enable status: %s\n", modbus_strerror(errno));
            free(status);
            return EXIT_FAILURE;
        } else {
            printf("The Analog Input Channel %d enable/disable status had set to %d\n", reg_addr-220, enable);
            free(status);
            return EXIT_SUCCESS;
        }
    }
    printf("The Analog Input Channel %d enable/disable status had set to %d already!\n", reg_addr-220, enable);
    free(status);
    return EXIT_SUCCESS;
}


// Convert Formula
// Convert Register Value into Analog Value
// The actual analog input voltage value needs to be divided by 32767
// add into modbus_config.h
float convert_to_voltage(uint16_t register_value, float max_voltage) {
    const float max_register_value = 32767.0;
    // *max_voltage
    float voltage_value =  ((float)register_value / max_register_value);
    return voltage_value;
}


void write_log(const char *message) {
    FILE *file;
    time_t current_time;
    struct tm *time_info;
    char time_str[100];

    file = fopen("log.txt", "a");

    if (file == NULL) {
        printf("無法打開log文件。\n");
        return;
    }

    time(&current_time);
    time_info = localtime(&current_time);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", time_info);

    fprintf(file, "[%s]: %s\n", time_str, message);
    fclose(file);
}

// Experiment 1: 

typedef struct {
    uint16_t data[BUFFER_SIZE][10]; // Assuming 10 registers
    int start;
    int end;
} CircularBuffer;

void buffer_init(CircularBuffer *buffer) {
    buffer->start = 0;
    buffer->end = 0;
}

int buffer_is_full(CircularBuffer *buffer) {
    return (buffer->end + 1) % BUFFER_SIZE == buffer->start;
}

void buffer_push(CircularBuffer *buffer, uint16_t *data, int size) {
    if (buffer_is_full(buffer)) {
        buffer->start = (buffer->start + 1) % BUFFER_SIZE;
    }

    memcpy(buffer->data[buffer->end], data, size * sizeof(uint16_t));
    buffer->end = (buffer->end + 1) % BUFFER_SIZE;
}

void print_buffer(CircularBuffer *buffer) {
    int i = buffer->start;
    while (i != buffer->end) {
        print_register_values(buffer->data[i], 10); // Assuming 10 registers
        i = (i + 1) % BUFFER_SIZE;
    }
}

int main() {
    // Your modbus connection setup code here
    modbus_t *ctx;
    //...

    int reg_addr = 0; // Replace with your register address
    int number_of_registers = 10; // Replace with your desired number of registers to read

    CircularBuffer buffer;
    buffer_init(&buffer);

    uint16_t temp_data[number_of_registers];

    while (1) {
        int result = read_values_to_memory(ctx, reg_addr, number_of_registers, temp_data);
        if (result == EXIT_SUCCESS) {
            buffer_push(&buffer, temp_data, number_of_registers);
            print_buffer(&buffer);
        }
        sleep(1); // Add a sleep time if needed, e.g., 1 second
    }

    // Your modbus connection cleanup code here
    //...
    
    return 0;
}