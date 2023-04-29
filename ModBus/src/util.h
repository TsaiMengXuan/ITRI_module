/*
 * util.h
 *
 *  Created on: Nov 6, 2018
 *      Author: arthur
 */

#ifndef UTIL_H_
#define UTIL_H_

#include <libconfig.h>

/**
 * AI channel typecode mapping to its Input Range
 * upper limit and lower limit
 */
#define TYPECODE_0103_UP 150 //mV
#define TYPECODE_0103_LOW -150
#define TYPECODE_0104_UP 500 //mV
#define TYPECODE_0104_LOW -500
#define TYPECODE_0105_UP 150 //mV
#define TYPECODE_0105_LOW 0
#define TYPECODE_0106_UP 500 //mV
#define TYPECODE_0106_LOW 0
#define TYPECODE_0140_UP 1 //V
#define TYPECODE_0140_LOW -1
#define TYPECODE_0142_UP 5 //V
#define TYPECODE_0142_LOW -5
#define TYPECODE_0143_UP 10 //V
#define TYPECODE_0143_LOW -10
#define TYPECODE_0145_UP 1 //V
#define TYPECODE_0145_LOW 0
#define TYPECODE_0147_UP 5 //V
#define TYPECODE_0147_LOW 0
#define TYPECODE_0148_UP 10  //V
#define TYPECODE_0148_LOW 0
#define TYPECODE_0181_UP 20 //mA
#define TYPECODE_0181_LOW -20
#define TYPECODE_1080_UP 20 //mA
#define TYPECODE_1080_LOW 4
#define TYPECODE_1082_UP 20 //mA
#define TYPECODE_1082_LOW 0
#define TYPECODE_UNKNOW_UP 0
#define TYPECODE_UNKNOW_LOW 0

//// storage for modbus config
//typedef struct _modbus_config
//{
//	// For Modbus-RTU
//    char    serial_dev[32];     /* "/dev/ttyS0" */
//    int     serial_baudrate;    /* 115200 : 9600, 38400, 115200, ... */
//    char    serial_parity;      /* 'N'    : 'N', 'E', or 'O' */
//    int     serial_databits;    /* 8      : 5, 6, 7, or 8 */
//    int     serial_stopbits;    /* 1      : 1 or 2 */
//
//	// For Modbus-TCP
//    char    server_ip[16];
//    int     server_port;
//
//    // other modbus parameters
//    int     device_id;
//    int     timeout_sec;
//    int     timeout_usec;
//    int 	debug;
//
//} modbus_config_t;

// storage for config main section
typedef struct _main_config
{
    int debug;
    int	intIMMSN;
    int	intMOSN;

	char mysql_db_prefix[128];
	char mysql_server_ip[16];
	char mysql_user_name[32];
	char mysql_password[128];

//	modbus_config_t mbSD;	// structure for sensor connected modbus device(DAQ)

    double  daq_ai_resolution;	// in bit
    // other modbus parameters
    int modbus_device_id;
    int modbus_timeout_sec;
    int modbus_timeout_usec;
    int modbus_debug;

	char modbus_ip[16];
	int  modbus_port;
	int  opcua_version_sn;
	int  mold_signal_type;

	char command_mold_status[128];
	char command_batch_data[128];
	char command_ai_typecode[128];

	int mold_do_on_daq_channel;
    // time_between_query is the query delay between each query
	int time_between_query; // usec 0 - 1000000, max 1 second(1000000)
    int	standby_delay; // while loop sleep time when waiting for connection, in usec
	int duplicate_output_time;

} main_config_t;

// storage for modbus command with a given name and modbus command fields
typedef struct _data_table_addr
{
	char tag[256];	// command alias
	int fcode;		// modbus function code
	int addr;   	// register starting address
    int amount;    	// number of registers
    char desc[256];	// register description in string
} data_addr_t;

// storage for all commands set in config
typedef  struct _data_command_config
{
  int delay_interval_sec;
  int delay_interval_usec;
  int command_count;		// total command count in config
  data_addr_t   *command;	// command array
} data_command_config_t;


//storage for Advantech ADAM 6017 DAQ channels
typedef struct _adam6017_aichannel_typecode
{
	int  	 channel_seqno; 	// this should be the real channel number for DAQ
	uint16_t type;				// channel typecode
	int 	 upper;				// input upper range, in V,mV,mA
	int 	 lower;				// input lower range, in V,mV,mA
	char 	 channel_usage[8];	// channel usage, include Input and output, see config file comment
	int  	 output_factor;		// engineering unit to final value factor
								// (engineering unit * output_factor = final result)
	int 	 MOSensorSN; 		// the Sensor's MOSensorSN from DB, for mapping
} daq_channel_config_t;

/*
 * // ADAM 6017 channel usage
// type: STP = temperature sensor,
//		 SPR = Pressure sensor,
//		 NA = not available, or no sensor attached to this channel
//		 AI = Analog IN
//		 DO = Digital OUT(mold open/close trigger)
adam6017_channel_type ={
	ai1 = {type="STP"; of=100;}
	ai2 = {type="STP"; of=100;}
	ai3 = {type="STP"; of=100;}
	ai4 = {type="SPR"; of=250;}
	ai5 = {type="SPR"; of=250;}
	ai6 = {type="SPR"; of=250;}
	ai7 = {type="NA"; of=0;}
	ai8 = {type="AI"; of=1;}
	do1 = {type="DO"; of=1;}
	do2 = {type="NA"; of=0;}
}
 */



// check if the passed filename exists and readable
int is_file_exists(const char * filename);

struct timespec diff(struct timespec start, struct timespec end);
void print_current_time_with_ms (void);
// generate timestamp for the data record
void generate_current_timestamp (time_t *sec, long *msec);

double generate_timetick (struct timespec time_base);

// load config in one time, section: main, command, logfile
int load_config(char * filename, main_config_t * main_config, data_command_config_t * cmd_config);
// load only main section config (include modbus)
int load_main_config(config_t * cfg, main_config_t * config);
// load only command config
int load_command_config(config_t * cfg, data_command_config_t * config);
// load DAQ channel settings, with config file open and close
int load_DAQ_channel_config(char * filename, daq_channel_config_t * config);
// print loaded config parameters
void Show_config(main_config_t * main_config, data_command_config_t * cmd_config);
// print DAQ channel config settings
void Show_DAQ_channel_config(daq_channel_config_t *config);



#endif /* UTIL_H_ */
