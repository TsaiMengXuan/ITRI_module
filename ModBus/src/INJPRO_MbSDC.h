/*
 * INJPRO_MbSDC.h
 *
 *  Created on: Nov 6, 2018
 *      Author: arthur
 */

#ifndef INJPRO_MBSDC_H_
#define INJPRO_MBSDC_H_

#include "util.h"
// define use modbus_tcp or not, 0 to use modbus_rtu
#define MODBUS_TCP 1

// storage for each data record in batch data query
typedef struct _batch_dataset
{
    uint16_t    rdata[MODBUS_MAX_READ_REGISTERS];   // data record for each query
    int         len;                                // number of register read
    char        timestamp[32];
	double      time_tick;
} dataset_t;

typedef struct _rawdata_latest
{
	double      dup_start_time_tick;
	double		time_tick;
	uint16_t    rdata;
	int         isdup; // value duplicate indicator
} rdlast_t;

typedef struct _MO_sensor_data
{
	int ch_id;			// DAQ channel ID
	int sensor_sn;		// POSDSS SensorSN
	uint16_t shot_sn;	// POSDSS ShotSN
	double time_tick;	// elapsed time
	double value;		// sensor reading
} MO_sensor_data_t;

// execute modbus command, by function code in command
// the req_b, req_r, rsp_b, rsp_r can be NULL if not needed
int do_modbus_command(modbus_t *ctx, data_addr_t *command, uint8_t *req_b, uint16_t *req_r,uint8_t *rsp_b,uint16_t *rsp_r);
//int my0x01(modbus_t *ctx, int addr, int nb, uint8_t *dest);
//int my0x02(modbus_t *ctx, int addr, int nb, uint8_t *dest);
//int my0x03(modbus_t *ctx, int addr, int nb, uint16_t *dest);
//int my0x04(modbus_t *ctx, int addr, int nb, uint16_t *dest);
//int my0x05(modbus_t *ctx, int addr, int status);
//int my0x06(modbus_t *ctx, int addr, int value);
//int my0x10(modbus_t *ctx, int addr, int nb, const uint16_t *src);
//int my0x0F(modbus_t *ctx, int addr, int nb, const uint8_t *src);

// check if the mold is now open or close
/*
 * the DO of mold injecter connect to one of the DAQ AI
 * the DAQ AI set HIGH ALARM (momentary mode) and map to DAQ DO1
 * this function check DAQ DO1 to decide the mold open/close status
 */
int check_mold_status(modbus_t *ctx, data_addr_t * command, _Bool *pre,_Bool *now);
int check_mold_status2(modbus_t *ctx, data_addr_t * command, int status_now, int mmtype);
// generate raw file name and logfile name, ex: DAQS_20180703-143950.csv, DAQS_20180703-143950.raw
//int generate_logfile_name(logfile_config_t *config, char * filename, char * rawfile);
/** flush and close data log file, if filehandle is not NULL */
//void do_logfile_close(FILE *filehandle);
/** open data log file for writing collected data */
//FILE *do_logfile_open(char * filename);
/** do_data_batch_collect will collect data record each time according passed maxcnt */
void do_data_batch_collect(modbus_t *ctx, main_config_t *config, data_addr_t * command, dataset_t *data, int maxcnt);
/** write previous collected data record to file */
//void write_data_to_file(FILE *fp, dataset_t *data, int maxcount);
// get input range using AI channel type code
int typecode_to_input_range(int typecode, int *ir_upper, int *ir_lower);
// convert raw data to engineer unit (voltage)
double convert_voltage(int rawdec, int typecode, double resolution);
// if input range is available, use this function instead of convert_voltage()
double convert_voltage2(int rawdec, int ir_upper, int ir_lower, double resolution);
// raw file as input, remove any duplicated record, convert raw data to final output value, write to logfile
void remove_duplicated_record_in_file(char *rawfilename, char *logfilename, daq_channel_config_t *ai_type, double ai_res);
// mainly the attached sensor type and its output factor
int load_DAQ_channel_config(char * filename, daq_channel_config_t * config);

// signal handle for SIGINT, SIGHUP, USR1, current use only SIGINT to terminate program
void handle_signal(int signal);
//void handle_sigalrm(int signal);
void setup_signal_handling();

void  SensorDataProcess(modbus_t *ctx, data_addr_t * command, main_config_t *main_config, daq_channel_config_t *daq_ai_config, double daq_ai_res, int mosn, int tablesn, int shotsn);
int chd_do_sensor_data_query(modbus_t *ctx, data_addr_t * command, dataset_t *data, struct timespec tbase);
double chd_raw_real_conversion(MO_sensor_data_t *data, daq_channel_config_t *ai_type, double ai_res);
void chd_DB_write_Sensor_Data(MO_sensor_data_t *data, int length, int mosn, int tablesn);

//int get_IMMSN_by_OPCUAIP(char * opcuaip, int *immsn);

// get ModBusIP, ModBusPort, OPCUAVersionSN, MoldSignalType from IMMList
int get_Modbus_info_by_IMMSN(int immsn, main_config_t * config);

int get_MOSN_by_IMMSN(int immsn, int *mosn);

int get_ShotSN_by_IMMSN_MOSN(int immsn, int mosn, int *shotsn);

int get_MOSensorSN_by_MOSN(int mosn, int *intMOSensorSN, daq_channel_config_t *daq_mapping);

int get_MOSensorTableSN_by_MOSN_ShotSN(int mosn, int shotsn, int *tablesn);

int db_update_pid(int immsn, pid_t pid);


#endif /* INJPRO_MBSDC_H_ */
