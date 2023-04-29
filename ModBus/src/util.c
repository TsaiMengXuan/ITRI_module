/*
 * util.c
 *
 *  Created on: Nov 6, 2018
 *      Author: arthur
 */

//below is for POSIX to resolve
// Linux: gcc with -std=c99 complains about not knowing struct timespec
// without -std=c99 and linker option -lm
// both pow() and round() in <math.h> will not accept by the compiler
#if __STDC_VERSION__ >= 199901L
#define _XOPEN_SOURCE 600
#else
#define _XOPEN_SOURCE 500
#endif /* __STDC_VERSION__ */


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <libconfig.h>

#include "util.h"

int util_DEBUG =0;

// check if a file exists
/**
    there are few ways to check file existence
    1. open the file for read, if open success, then it exists, remember to close the file after done.
    2. use access()
    3. use stat()
    reference:
    https://stackoverflow.com/questions/230062/whats-the-best-way-to-check-if-a-file-exists-in-c-cross-platform
*/
int is_file_exists(const char * filename)
{
    FILE * file;
    if ((file = fopen(filename, "r")))
    {
        fclose(file);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

/** function for calculate program performance */
struct timespec diff(struct timespec start, struct timespec end)
{
  struct timespec temp;
  printf("start:%"PRIdMAX".%03ld\n",start.tv_sec,start.tv_nsec);
  printf("start:%"PRIdMAX".%03ld\n",end.tv_sec,end.tv_nsec);
  if ((end.tv_nsec-start.tv_nsec)<0) {
    temp.tv_sec = end.tv_sec-start.tv_sec-1;
    temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
  } else {
    temp.tv_sec = end.tv_sec-start.tv_sec;
    temp.tv_nsec = end.tv_nsec-start.tv_nsec;
  }
  return temp;
}

void print_current_time_with_ms (void)
{
    long            ms; // Milliseconds
    time_t          s;  // Seconds
    struct timespec spec;

    clock_gettime(CLOCK_REALTIME, &spec);

    s  = spec.tv_sec;
    ms = round(spec.tv_nsec / 1.0e6); // Convert nanoseconds to milliseconds
    if (ms > 999) {
        s++;
        ms = 0;
    }

    printf("Current time: %"PRIdMAX".%03ld seconds since the Epoch\n",
           (intmax_t)s, ms);
}

void generate_current_timestamp (time_t *sec, long *msec)
{
    long            ms; // Milliseconds
    time_t          s;  // Seconds
    struct timespec spec;

    clock_gettime(CLOCK_REALTIME, &spec);

    s  = spec.tv_sec;
    ms = round(spec.tv_nsec / 1.0e6); // Convert nanoseconds to milliseconds
    if (ms > 999) {
        s++;
        ms = 0;
    }

    *sec = s;
    *msec = ms;
}

double generate_timetick (struct timespec time_base)
{
    long            ms; // Milliseconds
    time_t          s;  // Seconds
    struct timespec tnow;

    clock_gettime(CLOCK_REALTIME, &tnow);
if (util_DEBUG) {
    printf("BASE:%ld.%ld_\n",time_base.tv_sec,time_base.tv_nsec);
    printf("NOW :%ld.%ld_\n",tnow.tv_sec,tnow.tv_nsec);
}

    if ((tnow.tv_nsec-time_base.tv_nsec)<0) {
      s = tnow.tv_sec-time_base.tv_sec-1;
      ms = round((1000000000+tnow.tv_nsec-time_base.tv_nsec) / 1.0e6);
    } else {
        s = tnow.tv_sec-time_base.tv_sec;
        ms = round((tnow.tv_nsec-time_base.tv_nsec) / 1.0e6);
    }

//    s  = tnow.tv_sec - time_base.tv_sec;
//    printf("nsec:%ld_\n",tnow.tv_nsec - time_base.tv_nsec);
//    ms = round((tnow.tv_nsec - time_base.tv_nsec) / 1.0e6); // Convert nanoseconds to milliseconds
if (util_DEBUG) {
    printf("S:%ld, MS:%ld_\n",s,ms);
}
    if (ms > 999) {
        s++;
        ms = 0;
    }

    return (s + ms/1000.0);
}

// load all necessary config into structures
int load_config(char * filename, main_config_t * main_config, data_command_config_t * cmd_config)
{
    config_t cfg;

    // initialize config object
    config_init(&cfg);

    /* Read the file. If there is an error, report it and exit. */
    if(! config_read_file(&cfg, filename))
    {
        fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg),
                config_error_line(&cfg), config_error_text(&cfg));
        fprintf(stderr, "The config file [%s] not found\n",filename);
        config_destroy(&cfg);
        return(EXIT_FAILURE);
    }
    //=== load main config
    if (main_config != NULL) {
    	load_main_config(&cfg, main_config);
    }

    //=== load register config
    if (cmd_config != NULL) {
    	load_command_config(&cfg, cmd_config);
    }

    config_destroy(&cfg);
    return(EXIT_SUCCESS);
}

/*
Function: int config_lookup_int (const config_t * config, const char * path, int * value)
Function: int config_lookup_int64 (const config_t * config, const char * path, long long * value)
Function: int config_lookup_float (const config_t * config, const char * path, double * value)
Function: int config_lookup_bool (const config_t * config, const char * path, int * value)
Function: int config_lookup_string (const config_t * config, const char * path, const char ** value)
*/
int get_config_wdef_int(const config_t * config, const char * path, int defaultvalue) {
	int intval = 0;
    if ( config_lookup_int(config, path,  &intval) == CONFIG_TRUE) {
    	return intval;
    }
    else {
    	return defaultvalue;
    }

}

char * get_config_wdef_string(const config_t * config, const char * path, const char * defaultvalue) {
    const char *str;
	if ( config_lookup_string(config, path, &str) == CONFIG_TRUE) {
		return (char *)str;
	}
	else {
		return (char *)defaultvalue;
	}

}
/*
Function: int config_setting_lookup_int (const config_setting_t * setting, const char * name, int * value)
Function: int config_setting_lookup_int64 (const config_setting_t * setting, const char * name, long long * value)
Function: int config_setting_lookup_float (const config_setting_t * setting, const char * name, double * value)
Function: int config_setting_lookup_bool (const config_setting_t * setting, const char * name, int * value)
Function: int config_setting_lookup_string (const config_setting_t * setting, const char * name, const char ** value)
*/
int get_config_setting_wdef_int(const config_setting_t * setting, const char * name, int defaultvalue) {
	int intval = 0;
    if ( config_setting_lookup_int(setting, name,  &intval) == CONFIG_TRUE) {
    	return intval;
    }
    else {
    	return defaultvalue;
    }

}

char * get_config_setting_wdef_string(const config_setting_t * setting, const char * name, const char * defaultvalue) {
    const char *str;
	if ( config_setting_lookup_string(setting, name, &str) == CONFIG_TRUE) {
		return (char *)str;
	}
	else {
		return (char *)defaultvalue;
	}

}

int load_main_config(config_t * cfg, main_config_t * config)
{
//	config_setting_t *setting;
    const char *str;
    int intval;

	/////
    config->debug = get_config_wdef_int(cfg, "main.debug",0);
	util_DEBUG = config->debug;

	// MySQL DB info
    str = get_config_wdef_string(cfg, "main.mysql_db_prefix", "INJPRO");
	strncpy(config->mysql_db_prefix,str,strlen(str));

    str = get_config_wdef_string(cfg, "main.mysql_server_ip", "127.0.0.1");
	strncpy(config->mysql_server_ip,str,strlen(str));

    str = get_config_wdef_string(cfg, "main.mysql_user_name", "arthur");
	strncpy(config->mysql_user_name,str,strlen(str));

    str = get_config_wdef_string(cfg, "main.mysql_password", "arthurchen");
	strncpy(config->mysql_password,str,strlen(str));

    intval = get_config_wdef_int(cfg, "main.daq_ai_resolution",16);
    config->daq_ai_resolution   = pow(2.0,intval) - 1.0 ;
    config->modbus_device_id    = get_config_wdef_int(cfg, "main.modbus_device_id",1);
    config->modbus_timeout_sec  = get_config_wdef_int(cfg, "main.modbus_timeout_sec",3);
    config->modbus_timeout_usec = get_config_wdef_int(cfg, "main.modbus_timeout_usec",0);
    config->modbus_debug        = get_config_wdef_int(cfg, "main.modbus_debug",0);

    str = get_config_wdef_string(cfg, "main.command_mold_status", "MoldStatus");
	strncpy(config->command_mold_status,str,strlen(str));

    str = get_config_wdef_string(cfg, "main.command_batch_data", "AIVALUE");
	strncpy(config->command_batch_data,str,strlen(str));

    str = get_config_wdef_string(cfg, "main.command_ai_typecode", "AITYPECODE");
	strncpy(config->command_ai_typecode,str,strlen(str));

    config->mold_do_on_daq_channel = get_config_wdef_int(cfg, "main.Mold_do_on_daq_channel",1);
    config->time_between_query     = get_config_wdef_int(cfg, "main.time_between_query",100000);
    config->standby_delay          = get_config_wdef_int(cfg, "main.standby_delay",1000000);
    config->duplicate_output_time  = get_config_wdef_int(cfg, "main.duplicate_output_time",5);

    return EXIT_SUCCESS;
};


int load_command_config(config_t * cfg, data_command_config_t * config)
{
    config_setting_t *setting;
//    int intval;
    const char *str;

    config->delay_interval_sec  = get_config_wdef_int(cfg, "command_delay.interval_sec",1);
    config->delay_interval_usec = get_config_wdef_int(cfg, "command_delay.interval_usec",0);

    setting = config_lookup(cfg, "command");
    if(setting != NULL)
    {
        int count = config_setting_length(setting);
        config->command_count = count;
        config->command = (data_addr_t *)malloc(count * sizeof(data_addr_t)+1);

        for(int i = 0; i < count; ++i)
        {
            config_setting_t *reggrp = config_setting_get_elem(setting, i);

            memset(&config->command[i],'\0',sizeof(config->command[i]));
            strncpy(config->command[i].tag,reggrp->name,strlen(reggrp->name));

            config->command[i].fcode  = get_config_setting_wdef_int(reggrp, "fc",0);
            config->command[i].addr   = get_config_setting_wdef_int(reggrp, "addr",0);
            config->command[i].amount = get_config_setting_wdef_int(reggrp, "amount",0);

            str = get_config_setting_wdef_string(reggrp, "desc", "----");
        	strncpy(config->command[i].desc,str,strlen(str));
        }
    }

    return(EXIT_SUCCESS);
}


// DAQ channel config require some information from DAQ
// the full DAQ channel config takes two task
// first task can be done after other configs loaded
// the second task need AI channel typecode
// therefore it will and should run after modbus connection established.
//
// NOTE:
//      this function should not be included into load_config()
//      because it has it own config_init() and config_destory()
//      if merge to load_config(), DO deal with them.
int load_DAQ_channel_config(char * filename, daq_channel_config_t * config)
{
    config_t cfg;
    config_setting_t *setting;
    int intval;
    const char *str;

    // initialize config object
    config_init(&cfg);

    /* Read the file. If there is an error, report it and exit. */
    if(! config_read_file(&cfg, filename))
    {
        fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg),
                config_error_line(&cfg), config_error_text(&cfg));
        fprintf(stderr, "The config file [%s] not found\n",filename);
        config_destroy(&cfg);
        return(EXIT_FAILURE);
    }
////
    /*
     *
adam6017_channel_type ={
	  ai1 = {ch=0; type="MDO"; of=1;} // Mold close DO connect to AI CH#0, map to do0
	  ai2 = {ch=1; type="MDO"; of=1;} // Mold open  DO connect to AI CH#1, map to do1
	  ai3 = {ch=2; type="STP"; of=1;} // for sensor connection
	  ai4 = {ch=3; type="SPR"; of=1;} // for sensor connection
	  ai5 = {ch=4; type="SPR"; of=1;} // for sensor connection
	  ai6 = {ch=5; type="SPR"; of=1;} // for sensor connection
	  ai7 = {ch=6; type="STP"; of=1;} // for sensor connection
	  ai8 = {ch=7; type="STP"; of=1;} // for sensor connection
}
     */
    setting = config_lookup(&cfg, "adam6017_channel_type");

    if(setting != NULL)
    {
        int count = config_setting_length(setting);

        for(int i = 0; i < count; ++i)
        {
            config_setting_t *elem = config_setting_get_elem(setting, i);

            config_setting_lookup_int(elem, "ch", &intval);
            config[i].channel_seqno = intval;

            config_setting_lookup_string(elem, "type", &str);
            memset(config[i].channel_usage,'\0',sizeof(config[i].channel_usage));
            strncpy(config[i].channel_usage,str,strlen(str));

            config_setting_lookup_int(elem, "of", &intval);
            config[i].output_factor = intval;

//            config_setting_lookup_int(elem, "mosensorsn", &intval);
            config[i].MOSensorSN = -1;

        }
    }

////
    config_destroy(&cfg);
    return(EXIT_SUCCESS);
}


void Show_config(main_config_t * main_config, data_command_config_t * cmd_config)
{
	if (main_config != NULL) {

		printf("=================================\n");
		printf("===config.cfg main config==\n");
		printf("debug:%d\n",        main_config->debug);
		printf("mysql_db_prefix:%s\n", main_config->mysql_db_prefix);
		printf("mysql_server_ip:%s\n", main_config->mysql_server_ip);
		printf("mysql_user_name:%s\n", main_config->mysql_user_name);
		printf("mysql_password:%s\n", main_config->mysql_password);
		printf("daq_ai_resolution:%.1lf\n", main_config->daq_ai_resolution);
		printf("device_id:%d\n",        main_config->modbus_device_id);
		printf("timeout_sec:%d\n",      main_config->modbus_timeout_sec);
		printf("timeout_usec:%d\n",     main_config->modbus_timeout_usec);
		printf("debug:%d\n",            main_config->modbus_debug);
		printf("command_mold_status:%s\n", main_config->command_mold_status);
		printf("command_batch_data:%s\n", main_config->command_batch_data);
		printf("command_ai_typecode:%s\n", main_config->command_ai_typecode);
		printf("Mold_do_on_daq_channel:%d\n", main_config->mold_do_on_daq_channel);
		printf("time_between_query:%d\n", main_config->time_between_query);
		printf("standby_delay:%d\n",      main_config->standby_delay);
		printf("duplicate_output_time:%d\n", main_config->duplicate_output_time);

		printf("=================================\n");
	}

	if (cmd_config != NULL) {
		printf("===config.cfg command config==\n");
		printf("Command Delay Interval_sec=%d\n", cmd_config->delay_interval_sec);
		printf("Command Delay Interval_usec=%d\n", cmd_config->delay_interval_usec);

		printf("---command-[%d]--------------------------------\n",cmd_config->command_count);
		for (int i=0;i<cmd_config->command_count;i++){
			printf("%s:fc=%d, addr=%d, amount=%d, desc=%s\n",
					cmd_config->command[i].tag,
					cmd_config->command[i].fcode,
					cmd_config->command[i].addr,
					cmd_config->command[i].amount,
					cmd_config->command[i].desc);
		}
		printf("=======================================\n");
	}
}
/*
typedef struct _adam6017_aichannel_typecode
{
	int  channel_seqno;
	uint16_t type;
	int upper;
	int lower;
	char channel_usage[8];	// channel usage, include Input and output
	int  output_factor;	// engineering unit to final value factor
						// (engineering unit * output_factor = final result)
} daq_channel_config_t;
 */
void Show_DAQ_channel_config(daq_channel_config_t *config)
{
	if (config != NULL) {
//		printf("==={%ld-%ld}-[%ld-%ld]===\n",
//				sizeof(config),sizeof((daq_channel_config_t *)config),
//				sizeof(config[0]),sizeof(struct _adam6017_aichannel_typecode));
		printf("===config.cfg DAQ channel config===\n");
		for (int i=0;i<10;i++){
			printf("ch#_:%02d\t"		,config[i].channel_seqno);
			printf("TC:0x%04x\t"	,config[i].type);
			printf("IR_t:%02d\t"		,config[i].upper);
			printf("IR_b:%02d\t"		,config[i].lower);
			printf("ch_usage:%s\t"		,config[i].channel_usage);	// channel usage, include Input and output
			printf("of:%d\t"	,config[i].output_factor);
			printf("MOSensorSN:%d_\n"	,config[i].MOSensorSN);

		}
		printf("=======================================\n");
	}
}
