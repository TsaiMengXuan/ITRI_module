/*
 ============================================================================
 Name        : INJPRO_MbSDC.c
 Author      : Arthur Chen
 Version     :
 Copyright   : All Rights Reserved
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

//below is for POSIX to resolve
// Linux: gcc with -std=c99 complains about not knowing struct timespec
// without -std=c99 and linker option -lm
// both pow() and round() in <math.h> will not accept by the compiler

/* https://www.gnu.org/software/libc/manual/html_node/Feature-Test-Macros.html
   If the macro _XOPEN_SOURCE has the value 500 this includes all functionality
   described so far plus some new definitions from the Single Unix Specification, version 2.
   The value 600 (corresponding to the sixth revision) includes definitions from SUSv3,
   and using 700 (the seventh revision) includes definitions from SUSv4.
*/
/*
 * https://stackoverflow.com/questions/9294207/what-is-the-stdc-version-value-for-c11
 * C90 had no value, C90 amendment 1 had 199401L and C99 had 199901L.
 * With -std=c11 in gcc, 201112L is used for __STDC_VERSION__
 */

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
#include <signal.h>
#include <sys/types.h>
//#include <sys/ipc.h>
//#include <sys/shm.h>
//#include <sys/wait.h>
#include <modbus.h>
#include <mysql/mysql.h>

#include "INJPRO_MbSDC.h"
#include "itri_injpro.h"
#include "util.h"

// if TABLESN_PRESET removed, program will look for tableSN in DB
#define TABLESN_PRESET 1

static char DB_PREFIX[32] = "INJPRO";
int myDebug=0;
int intcommanddebug=0;

/** infinite loop control variable, 1=keep looping, 0 = end loop, control by SIGINT*/
static int Running = 1;
//TODO /** use SIGHUP to reload configure, when ReloadConfig == 1, do ReloadConfig() */
//static int ReloadConfig = 0;

MYSQL mysqlCon;
MYSQL_RES *result;
MYSQL_ROW row;

// latest read data kept here
rdlast_t RD_last[16];
struct timespec timestamp_base;
//debug
int qcnt=0;

/**
 * MAIN Start here
 */
int main(int argc, char *argv[])
{
    modbus_t        *ctxSD;
    struct timeval  timeout;
    int rc=0;
	pid_t  pid;
    // for mold open/close status check
    int mold_status_now=0; // default to unknown
    // by default the config file should be in the same directory of the program binary file
    char *config_filename = "injpro_mbsdc.cfg";
    // storage for modbus ai channel typecode query response
    uint16_t rsp_ai_typecode[125];
    //IMMSN  will passed as startup arguments
    // INJPRO_MbSDC -IMMSN nn -CONFIG <config file path>
    // 0		     1	   2   3      4
	int intShotSN=0, intTableSN=0;
	int intMOSensorSN[10] = {0};
    int isMolding=0;
    int mold_status=0;	// Mold status: 1-Opened, 2-Closing, 3-Closed, 4-Opening, 0-unknown

    // config setting holders
    main_config_t           my_main_config;
    data_command_config_t 	my_command_config;
    // structure array for storing AI channel Typecode and its upper/lower range and others
    daq_channel_config_t 	daq_channel[10];

    // command pointer, pointing into my_command_config
    data_addr_t *command_batch_data;
    data_addr_t *command_mold_status;
    data_addr_t *command_ai_typecode;

    memset(&my_main_config,    '\0',sizeof(my_main_config));
    memset(&my_command_config, '\0',sizeof(my_command_config));
    memset(&RD_last,           '\0',sizeof(RD_last));

    //Get IMMSN, config file from Parameters
    for (int i=1;i<argc;i=i+2) {
		if ( strcmp(argv[i],"-IMMSN") ==0){
			my_main_config.intIMMSN=atoi(argv[i+1]);
		}
//		else if ( strcmp(argv[i],"-MOSN" )==0){
//			intMOSN=atoi(argv[i+1]);
//		}
		else if ( strcmp(argv[i],"-DEBUG" )==0){ // DO NOT USE THIS IN PRODUCTION. normally not use,
			intcommanddebug=atoi(argv[i+1]);
		}
		else if ( strcmp(argv[i],"-CONFIG" )==0){
	        if (argv[i+1] !=NULL){
	            if (is_file_exists(argv[i+1])){
	                // file exist, use it as the config file
	                config_filename = argv[i+1];
	            }
	            else {
	                fprintf(stderr, "The config file : <%s> does not exist, please check!\n", argv[i+1]);
	                fprintf(stderr, "Should you want to use a config file other than the default config file,\n");
	                fprintf(stderr, "Please start the program as : INJPRO_MbSDC -IMMSN nn -CONFIG <config file path>\n");
	                exit(EXIT_FAILURE);
	            }
	        }
		}
    }
    if (my_main_config.intIMMSN ==0) {
        fprintf(stderr, "Incorrect IMMSN[%d], please check!\n", my_main_config.intIMMSN);
        exit(EXIT_FAILURE);
    }

    printf("Startup parameters: IMMSN[%d] config file:[%s}\n",my_main_config.intIMMSN, config_filename);
    printf("Should you want to use a config file other than the default config file,\n");
    fprintf(stderr, "Please start the program as : INJPRO_MbSDC -IMMSN nn -CONFIG <config file path>\n");

    pid = getpid();

    // Signal handling
    setup_signal_handling();

    rc = load_config(config_filename, &my_main_config, &my_command_config);
    if (rc == EXIT_FAILURE)
    {
        fprintf(stderr, "Config file read failed\n");
        exit(1);
    }
    Show_config(&my_main_config, &my_command_config);

    myDebug = my_main_config.debug;

    // initialize memory before get DAQ channel config
    memset(&daq_channel,         '\0',sizeof(daq_channel));
    load_DAQ_channel_config(config_filename, daq_channel);
    Show_config(&my_main_config, &my_command_config);

    // setup DB prefix
	printf("DB_PREFIX Default=[%s]\n",DB_PREFIX);
    if (strlen(my_main_config.mysql_db_prefix) > 0) {
    	strncpy(DB_PREFIX, my_main_config.mysql_db_prefix,strlen(my_main_config.mysql_db_prefix));
    	printf("override by config=[%s]\n",DB_PREFIX);
    }

    // get necessary command from command config
	for (int i=0;i<my_command_config.command_count;i++){
	    // get mold status checking command
		if (strncmp(my_main_config.command_mold_status,my_command_config.command[i].tag,strlen(my_command_config.command[i].tag))==0){
			command_mold_status = &my_command_config.command[i];
		}
	    // get AI Query Command
		else if (strncmp(my_main_config.command_batch_data,my_command_config.command[i].tag,strlen(my_command_config.command[i].tag))==0){
			command_batch_data = &my_command_config.command[i];
		}
	    // get AI channel typecode Command
		else if (strncmp(my_main_config.command_ai_typecode,my_command_config.command[i].tag,strlen(my_command_config.command[i].tag))==0){
			command_ai_typecode = &my_command_config.command[i];
		}
	}

    /* Connect to MySQL Database */
	fprintf(stderr,"%-45s%s","Connecting to MySQL Server...",my_main_config.mysql_server_ip);
	mysql_init(&mysqlCon);

	if(!mysql_real_connect(&mysqlCon,
							my_main_config.mysql_server_ip,
							my_main_config.mysql_user_name,
							my_main_config.mysql_password,
							NULL,0,NULL,CLIENT_FOUND_ROWS))
	{
		fprintf(stderr,"[\033[31mFail\033[m]\n");
		if (mysql_errno(&mysqlCon)){
			fprintf(stderr, "Fail to connect to MySql server %d: %s\n",mysql_errno(&mysqlCon),mysql_error(&mysqlCon));
		}
		return EXIT_FAILURE;
	}else{
		fprintf(stderr,"[\033[32mOK\033[m]\n");
	}

	// IMMSN are passed in as argument
	if (my_main_config.intIMMSN > 0) {
	    if ( db_update_pid(my_main_config.intIMMSN, pid) != EXIT_SUCCESS) {
	        fprintf(stderr, "update PID to DB failed\n");
	    }

	    // get ModBusIP, ModBusPort, OPCUAVersionSN, MoldSignalType from IMMList
		if ( get_Modbus_info_by_IMMSN(my_main_config.intIMMSN, &my_main_config) == EXIT_FAILURE ) {
			fprintf(stderr, "get ModBus Info Failed.\n");
		    free(my_command_config.command);
			mysql_close(&mysqlCon);
			exit(EXIT_FAILURE);
		}
		// call API to get MOSN by IMMSN
		if ( get_MOSN_by_IMMSN(my_main_config.intIMMSN, &my_main_config.intMOSN) != EXIT_SUCCESS ) {
			fprintf(stderr, "No MO assigned to IMM[%d], ABORT.\n",my_main_config.intIMMSN);
		    free(my_command_config.command);
			mysql_close(&mysqlCon);
			exit(EXIT_FAILURE);
		}

		if ( get_ShotSN_by_IMMSN_MOSN(my_main_config.intIMMSN, my_main_config.intMOSN, &intShotSN) != EXIT_SUCCESS) {
			fprintf(stderr, "No ShotSN available from IMM[%d], ABORT.\n",my_main_config.intIMMSN);
		    free(my_command_config.command);
			mysql_close(&mysqlCon);
			exit(EXIT_FAILURE);
		}
		// get MOSensorSN and DAQ AI channel mapping from Database
		if (get_MOSensorSN_by_MOSN(my_main_config.intMOSN, intMOSensorSN, daq_channel) != EXIT_SUCCESS ){
			fprintf(stderr, "No MOSensorSN available by MOSN[%d], ABORT.\n",my_main_config.intMOSN);
		    free(my_command_config.command);
			mysql_close(&mysqlCon);
			exit(EXIT_FAILURE);
		}

		Show_DAQ_channel_config(daq_channel);

		// intTableSN indicate which table to write sensor Data
#ifdef TABLESN_PRESET
		intTableSN = 1;
#else
		if (intMOSN > 0 && intShotSN > 0) {
			get_MOSensorTableSN_by_MOSN_ShotSN(my_main_config.intMOSN, intShotSN, &intTableSN);
		}
#endif
	}
	if (myDebug) {
		printf("Modbus:ip[%s],port[%d],opcua_version_sn[%d],mold_signal_type[%d]\n",
				my_main_config.modbus_ip, my_main_config.modbus_port,
				my_main_config.opcua_version_sn, my_main_config.mold_signal_type);
		printf("Parameters :intIMMSN=%d, intMOSN=%d, intShotSN=%d, intTableSN=%d_>\n",
				my_main_config.intIMMSN, my_main_config.intMOSN, intShotSN, intTableSN);
		printf("MOSensorSN:");
		for (int i=0;i<8;i++){
			printf("\t%d", intMOSensorSN[i]);
		}
		printf("\n");
	}


    // Create and init Modbus TCP Object
    ctxSD = modbus_new_tcp(my_main_config.modbus_ip, my_main_config.modbus_port);

    // Init modbus connection to Sensor Data DAQ
    if (ctxSD == NULL) {
        fprintf(stderr, "Unable to create the libmodbus context for Sensor Data\n");
        free(my_command_config.command);
        modbus_free(ctxSD);
    	mysql_close(&mysqlCon);
        exit(-1);
    }

    /* set device ID */
    modbus_set_slave(ctxSD, my_main_config.modbus_device_id);

    /* Debug mode */
    modbus_set_debug(ctxSD, my_main_config.modbus_debug);

    /* set timeout */
    timeout.tv_sec = my_main_config.modbus_timeout_sec;
    timeout.tv_usec = my_main_config.modbus_timeout_usec;
    modbus_get_byte_timeout(ctxSD, &timeout);

    timeout.tv_sec = my_main_config.modbus_timeout_sec;
    timeout.tv_usec = my_main_config.modbus_timeout_usec;
    modbus_set_response_timeout(ctxSD, &timeout);

    // Connect to DAQ SD
    if (modbus_connect(ctxSD) == -1) {
        fprintf(stderr, "Modbus Connection failed: %s\n", modbus_strerror(errno));
        free(my_command_config.command);
        modbus_free(ctxSD);
    	mysql_close(&mysqlCon);
        exit(-1);
    }

    // get DAQ AI channel typecode, and its upper/lower value
    rc = do_modbus_command(ctxSD, command_ai_typecode, NULL, NULL, NULL, rsp_ai_typecode);
    for (int i=0;i<rc;i++){
    	daq_channel[i].type = rsp_ai_typecode[i];
        // use typecode to get the actual input range
        typecode_to_input_range(daq_channel[i].type, &daq_channel[i].upper, &daq_channel[i].lower);
//        printf("[%d]type:%d,UP:%d,LOW:%d>\n", i, daq_channel[i].type, daq_channel[i].upper, daq_channel[i].lower);
    }

    Show_DAQ_channel_config(daq_channel);
    //////////

    int arc=0;
    // main loop
    while (Running == 1){
    	// check the mold status, open or close
    	// Mold status: 1-Opened, 2-Closing, 3-Closed, 4-Opening
    	// int check_mold_status2(modbus_t *ctx, data_addr_t * command, int status_now, int mmtype);
        rc = check_mold_status2(ctxSD, command_mold_status ,mold_status_now, my_main_config.mold_signal_type);
        if (myDebug)
            printf("---MoldStatus[Before check=%d]-[rc=%d]---\n", mold_status_now, rc);

        if (rc == 0) { // mold status unknown
            if (myDebug)
                printf("---MoldStatus Unknown [%d],---\n", rc);
            // Mold status unknown, go for next check, with delay
            usleep(my_main_config.standby_delay);
            continue;
        }

        mold_status_now = rc;

        if ( mold_status_now == 1 ) {
            // door is opened

        	if (isMolding == 1) {
        		printf("Mold opened\n");
				mold_status = mold_status_now;
				// 20180911, update OPCUA Node
				if (my_main_config.opcua_version_sn == 1) {
					printf("Mold opened, update OPCUANode,0:1\n");
					arc = IMM_WriteOPCUANodeValue(mysqlCon, my_main_config.intIMMSN, IMMPARA_DI_MOLD_CLAMPED,"0");
					if (arc != EXIT_SUCCESS) {
						unsigned int logsn = SYS_InsertSysErrMsg(mysqlCon,ERRCLASS_MODBUS,
								my_main_config.intMOSN, my_main_config.intIMMSN,
						arc,"Updating Mold CLAMPED to opened fail");
						printf("IMM_WriteOPCUANodeValue return error:%d[%d]\n", arc, logsn);
					}
					arc = IMM_WriteOPCUANodeValue(mysqlCon, my_main_config.intIMMSN, IMMPARA_DI_MOLD_RELEASED,"1");
					if (arc != EXIT_SUCCESS) {
						unsigned int logsn = SYS_InsertSysErrMsg(mysqlCon,ERRCLASS_MODBUS,
								my_main_config.intMOSN, my_main_config.intIMMSN,
						arc,"Updating Mold RELEASE to opened fail");
						printf("IMM_WriteOPCUANodeValue return error:%d[%d]\n", arc, logsn);
					}
				}
				isMolding = 0;

//	            int    status;
//	            wait(&status);

	            // increasing local copy of ShotSN by 1 after Mold_opened
	            intShotSN ++;
	            if (myDebug)   printf("Next ShotSN will be[%d]\n",intShotSN);
	            // clear RD_last
	            memset(RD_last,'\0',sizeof(RD_last));
        	}
        }
        else if ( mold_status_now == 2 ) {
            // door is closing
            printf("Mold closing\n");

            //TODO:leave TABLESN check for future use.
			// MOSN got by IMMSN, ShotSN calculated locally
			// check ShotSN with local ShotSN
			// if ShotSN != local ShotSN, show warning
			// and use ShotSN from DB
			int dbShotSN=0;
			get_ShotSN_by_IMMSN_MOSN(my_main_config.intIMMSN, my_main_config.intMOSN, &dbShotSN);
			if (dbShotSN != intShotSN) {
				// show Warning message, write Errlog
				char errmsg[128];
				sprintf(errmsg,"Warning: Local ShotSN[%d] mismatch with DB ShotSN[%d].", intShotSN,dbShotSN);
				unsigned int logsn = SYS_InsertSysErrMsg(mysqlCon,ERRCLASS_MODBUS,
						my_main_config.intMOSN, my_main_config.intIMMSN, 1234,errmsg);
				printf("%s:%d\n",errmsg, logsn);

				intShotSN = dbShotSN;

			}

#ifdef TABLESN_PRESET
       		intTableSN = 1;
#else
			if (intMOSN > 0 && intShotSN > 0) { // IMM is running MO
				get_MOSensorTableSN_by_MOSN_ShotSN(my_main_config.intMOSN, intShotSN, &intTableSN);
				// intTableSN indicate with table to write sensor Data
				if (intTableSN == 0)
					intTableSN = 1;
			}
			else {
				printf("Cannot get TableSN using MOSN[%d] and SHOTSN[%d], ABORT.\n",my_main_config.intMOSN, intShotSN);
				printf("Go with TableSN default set to '1'.\n");
				intTableSN = 1;
			}
#endif

            // get base timestamp when mold_status changing from 1-opened to 2-closing
            if ((mold_status == 1) || (mold_status == 0))
            	clock_gettime(CLOCK_REALTIME, &timestamp_base);

            mold_status = mold_status_now;
            isMolding = 1;

            // 20180911, update OPCUA Node
			if (my_main_config.opcua_version_sn == 1) {
				printf("Mold closing, update OPCUANode,0:0\n");
				arc = IMM_WriteOPCUANodeValue(mysqlCon, my_main_config.intIMMSN, IMMPARA_DI_MOLD_CLAMPED,"0");
				if (arc != EXIT_SUCCESS) {
					unsigned int logsn = SYS_InsertSysErrMsg(mysqlCon,ERRCLASS_MODBUS,
							my_main_config.intMOSN, my_main_config.intIMMSN,
					arc,"Updating Mold CLAMPED to closing fail");
					printf("IMM_WriteOPCUANodeValue return error:%d[%d]\n", arc, logsn);
				}
				arc = IMM_WriteOPCUANodeValue(mysqlCon, my_main_config.intIMMSN, IMMPARA_DI_MOLD_RELEASED,"0");
				if (arc != EXIT_SUCCESS) {
					unsigned int logsn = SYS_InsertSysErrMsg(mysqlCon,ERRCLASS_MODBUS,
							my_main_config.intMOSN, my_main_config.intIMMSN,
					arc,"Updating Mold RELEASE to closing fail");
					printf("IMM_WriteOPCUANodeValue return error:%d[%d]\n", arc, logsn);
				}
			}

            // Fork child process to query DAQ and write to DB, till shared memory indicate mold opened
//			printf("Server is about to fork a child process...\n");
//			pid = fork();
//			if (pid < 0) {
//				printf("*** fork error (server) ***\n");
//				//exit(1);
//			}
//			else if (pid == 0) {
//				SensorDataProcess(ctxSD, command_batch_data, &my_main_config, daq_channel, my_main_config.mbSD.daq_ai_resolution, ShmPTR,
//									intMOSN, intMOSensorSN, intTableSN, intShotSN);
//				// forked child will exit after shot completed
//				exit(EXIT_SUCCESS);
//			}
        }
        else if ( mold_status_now == 4 ) {
            // door is opening
            printf("Mold opening\n");
        	if (isMolding == 1) {
				mold_status = mold_status_now;
				//isMolding = 1;
				// 20180911, update OPCUA Node
				if (my_main_config.opcua_version_sn == 1) {
					printf("Mold opening, update OPCUANode,0:0\n");
					arc = IMM_WriteOPCUANodeValue(mysqlCon, my_main_config.intIMMSN, IMMPARA_DI_MOLD_CLAMPED,"0");
					if (arc != EXIT_SUCCESS) {
						unsigned int logsn = SYS_InsertSysErrMsg(mysqlCon,ERRCLASS_MODBUS,
								my_main_config.intMOSN, my_main_config.intIMMSN,
						arc,"Updating Mold CLAMPED to opening fail");
						printf("IMM_WriteOPCUANodeValue return error:%d[%d]\n", arc, logsn);
					}
					arc = IMM_WriteOPCUANodeValue(mysqlCon, my_main_config.intIMMSN, IMMPARA_DI_MOLD_RELEASED,"0");
					if (arc != EXIT_SUCCESS) {
						unsigned int logsn = SYS_InsertSysErrMsg(mysqlCon,ERRCLASS_MODBUS,
								my_main_config.intMOSN, my_main_config.intIMMSN,
						arc,"Updating Mold RELEASE to opening fail");
						printf("IMM_WriteOPCUANodeValue return error:%d[%d]\n", arc, logsn);
					}
				}
        	}
        }
        else if ( mold_status_now == 3 ) {
            // door closed
            printf("mold closed\n");
            //isMolding = 1;
// 20180911, update OPCUA Node
            if (mold_status == 2) {
				if (my_main_config.opcua_version_sn == 1) {
					printf("Mold closed, update OPCUANode,1:0\n");
					arc = IMM_WriteOPCUANodeValue(mysqlCon, my_main_config.intIMMSN, IMMPARA_DI_MOLD_CLAMPED,"1");
					if (arc != EXIT_SUCCESS) {
						unsigned int logsn = SYS_InsertSysErrMsg(mysqlCon,ERRCLASS_MODBUS,
								my_main_config.intMOSN, my_main_config.intIMMSN,
						arc,"Updating Mold CLAMPED to closing fail");
						printf("IMM_WriteOPCUANodeValue return error:%d[%d]\n", arc, logsn);
					}
					arc = IMM_WriteOPCUANodeValue(mysqlCon, my_main_config.intIMMSN, IMMPARA_DI_MOLD_RELEASED,"0");
					if (arc != EXIT_SUCCESS) {
						unsigned int logsn = SYS_InsertSysErrMsg(mysqlCon,ERRCLASS_MODBUS,
								my_main_config.intMOSN, my_main_config.intIMMSN,
						arc,"Updating Mold RELEASE to closed fail");
						printf("IMM_WriteOPCUANodeValue return error:%d[%d]\n", arc, logsn);
					}
				}
            }
            mold_status = mold_status_now;
        }
// 20181004 End
    	printf("mold_status=%d; isMolding=%d;\n",mold_status,isMolding);
//DEBUG
    	// Mold status: 1-Opened, 2-Closing, 3-Closed, 4-Opening
    	//start collect data after mold closed, stop when opening
    	//therefore only mold_status == 3 need to write DB
        if (mold_status == 3) {
//        	SensorDataProcess(ctxSD, command_batch_data, &my_main_config, daq_channel,
//        			my_main_config.daq_ai_resolution, NULL,
//					my_main_config.intMOSN, intMOSensorSN, intTableSN, intShotSN);
        	SensorDataProcess(ctxSD, command_batch_data, &my_main_config, daq_channel,
        			my_main_config.daq_ai_resolution,
					my_main_config.intMOSN, intTableSN, intShotSN);
        }

        // delay before next mold door check
        // mold_status Mold status: 1-Opened, 2-Closing, 3-Closed, 4-Opening
        if ( ( mold_status == 1 ) || ( mold_status == 0 ) )
        	usleep(my_main_config.standby_delay);
//        else
//        	usleep(100000);
    } // End of  while (Running == 1)

    if ( db_update_pid(my_main_config.intIMMSN, 0) != EXIT_SUCCESS) {
        fprintf(stderr, "update PID to DB failed\n");
    }

    // free allocated memory
    free(my_command_config.command);

    /* Close the DAQ Modbus connection */
    modbus_close(ctxSD);
    modbus_free(ctxSD);

	//Close connection from MySQL Server
	mysql_close(&mysqlCon);

    printf("Program terminated.\n");
    exit(EXIT_SUCCESS);
}


/**
 * Converting DAQ AI Raw data to engineer unit
 *
Voltage = <decimal data> / 65535 * <input range> + <input range Lower bound>
where
    <decimal data> is the value read from DAQ AI,since libmodbus already do the translation from Hex to Dec
                    we can just use the value we got in the modbus response.
                    and the reason this value MUST divide by 65535 is because DAQ's AI resolution is 16bits
    <input range>   if the AI channel input range is -10mv ~ +10mv, then this value would be : (+10) - (-10) = 20
    <input range Lower bound> as above the lower bound of the input range is : -10
    where 65535 is the DAQ AI channel resolution(16 bits)
*/
double convert_voltage(int rawdec, int typecode, double resolution)
{
    int uv=0,lv=0;
    double result;

    // use typecode to get the actual input range
    typecode_to_input_range(typecode, &uv, &lv);
    //printf("raw=%d, resolution=%d,TC=%x,uv=%d, lv=%d\n",rawdec,resolution,typecode,uv, lv);
    result = ((rawdec *1.0) / resolution) * (uv - lv) + lv;

    return result;
}

/*
 * same as convert_voltage(), except the input range (upper and lower)
 * are direct passed into this function
 */
/*
[0]type:323,UP:10,LOW:-10>
[1]type:323,UP:10,LOW:-10>
[2]type:323,UP:10,LOW:-10>
[3]type:4224,UP:20,LOW:4>
[4]type:322,UP:5,LOW:-5>
[5]type:322,UP:5,LOW:-5>
[6]type:322,UP:5,LOW:-5>
[7]type:4226,UP:20,LOW:0>
[8]type:259,UP:150,LOW:-150>

arthur@arthur-Linux:~/itri/log$ head DAQS_20180721-134827.raw
Timestamp,          AI01,     AI02,     AI03,    AI04,     AI05,     AI06,     AI07,    AI08, Average
1532152107.665,     4369,     8738,    13107,   17476,    21845,    26214,    30583,   32145, 0
1532152107.665,-8.666667,-7.333333,-6.000000,8.266667,-1.666667,-1.000000,-0.333333,9.810025,-150.000000
*/
double convert_voltage2(int rawdec, int ir_upper, int ir_lower, double resolution)
{
    double result;
//    printf("raw=%d, resolution=%f,uv=%d, lv=%d\n",rawdec,resolution,ir_upper, ir_lower);
    result = ((rawdec *1.0) / resolution) * (ir_upper - ir_lower) + ir_lower;

    return result;
}


/**
  input: typecode: value from DAQ AI Type code register
  output: ir_upper: upper value of input range, ir_lower: lower value of input range
  if the type code is unknow, the return result will both be 0

*/
int typecode_to_input_range(int typecode, int *ir_upper, int *ir_lower)
{
    switch(typecode)
    {
        case 0x0103:    //mV
            *ir_upper = TYPECODE_0103_UP;
            *ir_lower = TYPECODE_0103_LOW;
            break;
        case 0x0104:    //mV
            *ir_upper = TYPECODE_0104_UP;
            *ir_lower = -TYPECODE_0104_LOW;
            break;
        case 0x0105:    //mV
            *ir_upper = TYPECODE_0105_UP;
            *ir_lower = TYPECODE_0105_LOW;
            break;
        case 0x0106:    //mV
            *ir_upper = TYPECODE_0106_UP;
            *ir_lower = TYPECODE_0106_LOW;
            break;
        case 0x0140:    //V
            *ir_upper = TYPECODE_0140_UP;
            *ir_lower = TYPECODE_0140_LOW;
            break;
        case 0x0142:    //V
            *ir_upper = TYPECODE_0142_UP;
            *ir_lower = TYPECODE_0142_LOW;
            break;
        case 0x0143:    //V
            *ir_upper = TYPECODE_0143_UP;
            *ir_lower = TYPECODE_0143_LOW;
            break;
        case 0x0145:    //V
            *ir_upper = TYPECODE_0145_UP;
            *ir_lower = TYPECODE_0145_LOW;
            break;
        case 0x0147:    //V
            *ir_upper = TYPECODE_0147_UP;
            *ir_lower = TYPECODE_0147_LOW;
            break;
        case 0x0148:    //V
            *ir_upper = TYPECODE_0148_UP;
            *ir_lower = TYPECODE_0148_LOW;
            break;
        case 0x0181:    //mA
            *ir_upper = TYPECODE_0181_UP;
            *ir_lower = TYPECODE_0181_LOW;
            break;
        case 0x1080:    //mA
            *ir_upper = TYPECODE_1080_UP;
            *ir_lower = TYPECODE_1080_LOW;
            break;
        case 0x1082:    //mA
            *ir_upper = TYPECODE_1082_UP;
            *ir_lower = TYPECODE_1082_LOW;
            break;
        default:        // unknow type code
            *ir_upper = TYPECODE_UNKNOW_UP;
            *ir_lower = TYPECODE_UNKNOW_LOW;
            break;
    }
    if (ir_upper == 0 && ir_lower == 0)
        return -1;
    return 1;
}

// signal handling function
void handle_signal(int signal)
{
    const char *signal_name;

    // Find out which signal we're handling
    switch (signal) {
        case SIGHUP:
            signal_name = "SIGHUP";
            //ReloadConfig = 1;
            break;
        case SIGUSR1:
            signal_name = "SIGUSR1";
            break;
        case SIGINT:
        /** when got SIGINT, Running will be set to 0
            cause the while loop in main to exit
            hence terminate the program
        */
            printf("Caught SIGINT, exiting now\n");
            signal_name = "SIGINT";
            Running = 0;
            //exit(0);
            break;
        default:
            fprintf(stderr, "Caught =wrong= signal: %d\n", signal);
            return;
    }

    printf("Done handling %s\n\n", signal_name);
}

// latest suggestion is using sigaction() instead of signal()
void setup_signal_handling()
{
    struct sigaction sa;

    // Print pid, so that we can send signals from other shells
    // TODO: Sholud we keep the PID in file for later use?
    printf("My pid is: %d\n", getpid());

    // Setup the sighub handler
    sa.sa_handler = &handle_signal;

    // Restart the system call, if at all possible
    sa.sa_flags = SA_RESTART;

    // Block every signal during the handler
    sigfillset(&sa.sa_mask);

    // Intercept SIGHUP and SIGINT
    if (sigaction(SIGHUP, &sa, NULL) == -1) {
        perror("Error: cannot handle SIGHUP"); // Should not happen
    }

    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("Error: cannot handle SIGUSR1"); // Should not happen
    }

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("Error: cannot handle SIGINT"); // Should not happen
    }
}

/**
    two variables here, pre and now, is used to keep the last status and current status
    when check_mold_status() been called, it will update pre with current now
    then use the DAQ DO1 status to update now.

    20180911
    I split DAQ into two object, one contain AI channels for connect and reading sensor(ALL AI config)
    one for IMM mold open/close monitoring(DI(AI) -> DO config)
    for there will be max 8 sensors per mold(for now, 20180911)
    if only one adam6017 used, the max sensor number will reduce to 7
    therefore, set a new architecture for future use

    the modbus_t pass to check_mold_status should be the one for IMM mold open/close monitoring
    the algorithm here assume only one DO is used to indicate the mold open/close status

    20181003
    For requested requirement
    There are two types of IMM signaling the mold open/close, single-loop and multi-loop

    For single-loop, it will only indicate the mold is opening or closing, by pulling
    corresponding output pin HIGH, after mold opened/closed, they will become LOW.

	single loop		opened	closing	closed	opening
    --------------- ======	=======	======	=======
    mold close pin    0		  1		  0		  0
    mold open pin     0		  0		  0		  1

    For multi-loop, when mold is opened/closed, there will be always a corresponding pin at HIGH
    but both will be low during the opening/closing

	multi loop		opened	closing	closed	opening
    --------------- ======	=======	======	=======
    mold close pin    0		  0		  1		  0
    mold open pin     1		  0		  0		  0

    The requirement is to conform with both single/multi loop
    and return current mold status to caller
    some of the setting/config needed will be kept in database

*/
// modbus_t *ctx : modbus connectin context
// data_addr_t * command : DAQ Mold status monitoring command
// int status_now : current mold status from caller
// int mmtype : mold monitoring type 1=single, 2=multi
// out : Mold status: 1-Opened, 2-Closing, 3-Closed, 4-Opening, 0-Unknown
// TODO: to be update/merge
int check_mold_status2(modbus_t *ctx, data_addr_t * command, int status_now, int mmtype)
{
    uint8_t     bits[MODBUS_MAX_READ_BITS] = {0};
    int opening=0,closing=0; // value from DO 0&1 on DAQ
    int ret_status=0;	// Mold status: 1-Opened, 2-Closing, 3-Closed, 4-Opening, 0-unknown

    memset(bits,'\0',sizeof(bits));
    int rc = do_modbus_command(ctx, command, NULL, NULL, bits,NULL);
    if (rc == 0)
    	return 0;
    // bits[0] is DAQ DO0(for colsing)
    // bits[1] is DAQ DO1(for opening)
    if (bits[0] == 1) closing=1;
	if (bits[1] == 1) opening=1;

    if (mmtype == 1) { // single loop
//    	single loop		  1 opened	2 closing	3 closed	4 opening
//        --------------- ======	=======		======		=======
//        mold close pin    0		  1			  0			  0
//        mold open pin     0		  0			  0			  1
		if ( (closing == 1) && (opening == 0) ) { // closing
			ret_status=2;
		}
		else if ( (closing == 0) && (opening == 1) ) { // opening
			ret_status=4;
		}
		else if ( (closing == 0) && (opening == 0) ) {
	    	if (status_now == 1 ) { // opened
	    		// remain opened
	    		ret_status=1;
	    	}
	    	else if (status_now == 2 ) { // closing
	    		// closing -> closed
	    		ret_status=3;
	    	}
	    	else if (status_now == 3 ) { // closed
	    		// remain closed
	    		ret_status=3;
	    	}
	    	else if (status_now == 4 ) { // opening
	    		// opening -> opened
	    		ret_status=1;
	    	}
	    	else if (status_now == 0 ) { // unknow or first time check
	    		// opened
	    		// when first time check mold status, caller will pass status_now==0
	    		// if closing pin and opening pin are both LOW(0)
	    		// assume mold is opened
	    		ret_status=1;
	    	}
	    	else { // status unknown
	    		ret_status=0;
	    	}
		}
    }// End of mmtype = 1
    else if (mmtype == 2) { // multi loop
//    	multi loop		1 opened	2 closing	3 closed	4 opening
//      --------------- ======		=======		======		=======
//      mold close pin    0			  0			  1			  0
//      mold open pin     1			  0			  0			  0

		// when first time check mold status, caller will pass status_now==0
		// if closing pin and opening pin are both LOW(0)
		// there is no way to tell if mold is closing or opening
    	// so leave ret_status = 0, and let mold status be determined in the next(or next few) check

		if ( (closing == 1) && (opening == 0) ) {
			// closed
			ret_status=3;
		}
		else if ( (closing == 0) && (opening == 1) ) {
			// opened
			ret_status=1;
		}

		if ( (closing == 0) && (opening == 0) ) {
			if (status_now == 1 ) { // opened
				// opened -> closing
				ret_status=2;
	    	}
	    	else if (status_now == 2 ) { // closing
	    		// remain closing
	    		ret_status=2;
	    	}
	    	else if (status_now == 3 ) { // closed
	    		// closed -> opening
	    		ret_status=4;
	    	}
	    	else if (status_now == 4 ) { // opening
	    		// remain opening
	    		ret_status=4;
	    	}
	    	else { // status unknown
	    		ret_status=0;
	    	}
		}
    }// End of mmtype = 2
	else {
		// mmtype unknown, return status unknown
		ret_status=0;
	}
   	return ret_status;
}


/**
 * execute modbus command based on the passed command
 * the response result for fc=1,2,3,4 in rsp_b(bits), rsp_r(registers)
 * the bits/register value to be written to DAQ in write command are passed
 * in req_b, req_r
 * if the command fcode known, you can pass only the necessary parameter
 * (one of req_b, req_r, rsp_b, rsp_r), others can be NULL
 *
 * if the modbus connection fail, and the error number is
 * EBADF || ECONNRESET || EPIPE , will try to auto-re-connect to modbus device
 * max retry count is 20, HARD-CODED
 */
int do_modbus_command(modbus_t *ctx, data_addr_t *command, uint8_t *req_b, uint16_t *req_r,uint8_t *rsp_b,uint16_t *rsp_r)
{

	int ret,reconn=0;

	/*
	 * Note: If the connection to modbus device broken or dropped
	 * this loop will not stop until it successfully
	 * re-connect to the modbus device and message sent to modbus device
	 */
	do {
		if (command->fcode == 0x01) {
			/* read coils (0x01 function code) */
			ret = modbus_read_bits(ctx, command->addr, command->amount, rsp_b);
		}
		else if (command->fcode == 0x02) {
			/* read discrete input (0x02 function code) */
			ret = modbus_read_input_bits(ctx, command->addr, command->amount, rsp_b);
		}
		else if (command->fcode == 0x05) {
			/* write single coil (0x05 function code) */
			ret = modbus_write_bit(ctx, command->addr, *req_b);
		}
		else if (command->fcode == 0x0F) {
			/* write multi coil (0x0F function code) */
			ret = modbus_write_bits(ctx, command->addr, command->amount, req_b);
		}
		else if (command->fcode == 0x03) {
			/* read holding registers (0x03 function code) */
			ret = modbus_read_registers(ctx,command->addr, command->amount, rsp_r);
		}
		else if (command->fcode == 0x04) {
			/* read input registers (0x04 function code) */
			ret = modbus_read_input_registers(ctx, command->addr, command->amount, rsp_r);
		}
		else if (command->fcode == 0x06) {
			/* write single register (0x06 function code) */
			ret = modbus_write_register(ctx, command->addr, *req_r);
		}
		else if (command->fcode == 0x10) {
			/* write multi registers (0x10 function code) */
			ret = modbus_write_registers(ctx, command->addr, command->amount, req_r);
		}
//////////
		if (ret < 0) {
			fprintf(stderr, "XXXX:%s\n", modbus_strerror(errno));
			// try to re-establish connection to modbus device
			if ((errno == EBADF || errno == ECONNRESET || errno == EPIPE)) {
				// close modbus connection and then connect
				sleep(1); // one second delay before re-connect
				printf("Try re-connect to modbus device\n");
				modbus_close(ctx);
				modbus_connect(ctx);
				reconn++;
			}
			else{
				break;
			}
		}
		else {

			if (myDebug) {
				int ii;
				if (command->fcode == 0x01) {
					printf("BITS COILS:");
					for (ii=0; ii < ret; ii++) {
						printf("[%d]=%d\t", ii, rsp_b[ii]);
					}
					printf(">\n");
				}
				else if (command->fcode == 0x02) {
					printf("BITS DISCRETE:");
					for (ii=0; ii < ret; ii++) {
						printf("[%d]=%d\t", ii, rsp_b[ii]);
					}
					printf(">\n");
				}
				else if (command->fcode == 0x03) {
					printf("HOLDING REGISTERS:");
					for (ii=0; ii < ret; ii++) {
						printf("[%d]%d\t", ii, rsp_r[ii]);
					}
					printf(">\n");
				}
				else if (command->fcode == 0x04) {
					printf("INPUT REGISTERS:");
					for (ii=0; ii < ret; ii++) {
						printf("[%d]=%d\t", ii, rsp_r[ii]);
					}
					printf(">\n");
				}
			}
		}
	} while (ret < 0 && reconn < 20);

	return ret;
}


/*
 * this function query sensor data from DAQ and write them into database
 *
 check if raw data duplicated
 20181004
 Requirement: When sensor readings are duplicated for a certain period,
 write the duplicated data to DB in a configurable time period, say 1 second.

 Solution:
 add a member variable double dup_start_time_tick; in structure rdlast_t
 the variable is used to keep the time_tick when duplication occurred.
 then when duplication continues, we previously only update the time_tick
 now do addition check on dup_start_time_tick and time_tick
 if the difference between dup_start_time_tick and time_tick are greater than
 duplicate_output_time set in config, add the RD_last data into realdata for writing to DB


 if data duplicate stops(dup flag == 1, and new data != last data)
 write RD_last to DB too

 * for DAQ AI channel number(ADAM 6017 as example)
 * if sensors and mold status signal connect to same DAQ
 * the mold status signal MUST connect to DAQ AI channel#0 and #1
 *
 * also, the DAQ AI channel configuration (daq_channel_config_t)
 * contains all DAQ AI channel info, the index start from 0
 * which include the AI channel Input Range for each channel,
 * that will be used to convert rawdata to real value
 *
 * in order to get the correct value to convert the rawdata,
 * we need to know the real DAQ channel number, instead of the query result array index
 *
 * Our method as follow:
 * in config file, list all 8 AI channel, then when loading config, set daq_channel_config_t.MOSensorSN to -1
 * also, the AIVALUE Modbus command set to read ALL 8 channel's value register,
 * so each DAQ query will have 8 values for ch#0 - ch#7
 *
 * In Database, each sensor in table MOSensorSNList has an unique SN number
 * and the SN number MUST map to the correct DAQ AI channel which the sensor connected to
 * (MOSensorChannelSN, 0-7)
 * otherwise the sensor data insert into DB will be placed in wrong table
 * if the sensor listed in MOSensorSNList not in use, its MOSensorChannelSN MUST set to NULL
 *
 * when retrieve MOSensorSN and MOSensorChannelSN from database,
 * select only records that MOSensorChannelSN is NOT NULL
 * then update MOSensorSN into daq_channel_config_t using MOSensorChannelSN as array index
 * MOSensorSN will be in the correct position
 *
 * After that, when reading value from DAQ,
 * skip the values where the corresponding daq_channel_config_t.MOSensorSN == -1
 * and we will got the sensor values we WANT, and skip those sensors that did not connect to DAQ
 */
void  SensorDataProcess(modbus_t *ctx, data_addr_t * command,
		main_config_t *main_config, daq_channel_config_t *daq_ai_config,
		double daq_ai_res, int mosn, int tablesn, int shotsn)
{
	// store data queried from DAQ
	dataset_t *rawdata = (dataset_t *)malloc(sizeof(dataset_t)+1);
	// the array for data that will write to DB
	// since we know how many registers we will read in
	// an array size for twice the register number will be enough
	MO_sensor_data_t realdata[command->amount * 2];
	memset(&realdata,'\0',sizeof(realdata));
	int realdata_count=0;
	int iRetlen;

	// get sensor data by modbus and get timestamp
		iRetlen = chd_do_sensor_data_query(ctx, command, rawdata, timestamp_base);
		if (iRetlen != command->amount)
			printf("ERR:GOT %d value, Expected %d value.\n",iRetlen,command->amount);
		if (myDebug) {
			printf("AFTERQUERY=[rawdata]%s[%lf]", rawdata->timestamp, rawdata->time_tick);
			for (int j=0; j < rawdata->len; j++) {
				printf("\t[%d]%u", j, rawdata->rdata[j]);
			}
			printf("\n");
		}

		if (intcommanddebug) {
			//DEBUG
			++qcnt;
			if ((qcnt%5) == 0){
				for (int j=0; j < rawdata->len; j++) {
					rawdata->rdata[j] += qcnt;
				}
				printf("AFTERDEBUG=[rawdata]%s[%lf]", rawdata->timestamp, rawdata->time_tick);
				for (int j=0; j < rawdata->len; j++) {
					printf("\t[%d]%u", j, rawdata->rdata[j]);
				}
				printf("\n");

			}
		}

//		if (myDebug) {
//			printf("BEFORE RD_LAST UPDATE\n");
//			for (int i=0;i < command->amount;i++){
//				printf("<_==RD_LAST=%d=",i);
//				printf("dup?:%d\t",RD_last[i].isdup);
//				printf("time:%lf\t",RD_last[i].time_tick);
//				printf("data:%u\t",RD_last[i].rdata);
//				printf("_>\n");
//			}
//		}
		// go through all 8 values
		for (int i=0; i < command->amount; i++){
			//skip values that its corresponding MOSensorChannelSN is less then 0 (-1)
			if (daq_ai_config[i].MOSensorSN >= 0) {
				// check if raw data duplicated
				if ( RD_last[i].rdata != rawdata->rdata[i] ) {
					// if new data != last data, and duplicate flag is set
					// write the last data to DB too
					if ( RD_last[i].isdup ==1 ) {
						realdata[realdata_count].ch_id 		= daq_ai_config[i].channel_seqno;
						realdata[realdata_count].sensor_sn 	= daq_ai_config[i].MOSensorSN;
						realdata[realdata_count].shot_sn 	= shotsn;
						realdata[realdata_count].time_tick 	= RD_last[i].time_tick;
						realdata[realdata_count].value 		= RD_last[i].rdata;
						realdata_count++;
					} // END of if ( RD_last[i].isdup ==1 ) {

					// update RD_last to latest data, reset dup flag
					RD_last[i].isdup = 0;
					RD_last[i].rdata = rawdata->rdata[i];
					RD_last[i].time_tick = rawdata->time_tick;
					// 20181004 reset dup_start_time_tick
					RD_last[i].dup_start_time_tick = 0;

					// put current rawdata into writing list
					realdata[realdata_count].ch_id 		= daq_ai_config[i].channel_seqno;
					realdata[realdata_count].sensor_sn 	= daq_ai_config[i].MOSensorSN;
					realdata[realdata_count].shot_sn 	= shotsn;
					realdata[realdata_count].time_tick 	= rawdata->time_tick;
					realdata[realdata_count].value 		= rawdata->rdata[i];
					realdata_count++;
				}
				else {
					// duplicated, set dup flag to 1, and update time_tick
					RD_last[i].isdup = 1;
					RD_last[i].time_tick = rawdata->time_tick;
					// 20181004 keep first duplicated time tick
					if (RD_last[i].dup_start_time_tick == 0)
						RD_last[i].dup_start_time_tick = rawdata->time_tick;
					// Duplicated data should write to DB every xxx seconds(in config file)
					// 20181004 check if duplicate_output_time exceeded
					//printf("====[%lf] - [%lf] = [%lf]====\n",RD_last[i].time_tick, RD_last[i].dup_start_time_tick, (RD_last[i].time_tick - RD_last[i].dup_start_time_tick));
					if ((RD_last[i].time_tick - RD_last[i].dup_start_time_tick) >= main_config->duplicate_output_time) {
						// 20181004 update dup_start_time_tick
						RD_last[i].dup_start_time_tick = rawdata->time_tick;

						realdata[realdata_count].ch_id = daq_ai_config[i].channel_seqno;
						realdata[realdata_count].sensor_sn = daq_ai_config[i].MOSensorSN;
						realdata[realdata_count].shot_sn = shotsn;
						realdata[realdata_count].time_tick = RD_last[i].time_tick;
						realdata[realdata_count].value = RD_last[i].rdata;
						realdata_count++;
					}
				}
			}
		}
//		if (myDebug) {
//			printf("\n");
//			printf("AFTER  RD_LAST UPDATE\n");
//			for (int i=0;i < command->amount;i++){
//				printf("<_==RD_LAST=%d=",i);
//				printf("dup?:%d\t",RD_last[i].isdup);
//				printf("time:%lf\t",RD_last[i].time_tick);
//				printf("data:%u\t",RD_last[i].rdata);
//				printf("_>\n");
//			}
//		}

		// if there are data to be write to DB, convert the rawdata to real value before insert
		if (realdata_count > 0) {
			for (int i=0;i < realdata_count;i++){
				realdata[i].value = chd_raw_real_conversion(&realdata[i], daq_ai_config, daq_ai_res);
			}
		}

		if (myDebug) {
			printf("<_==realdata LIST\n");
			for (int i=0;i < realdata_count;i++){
				printf("chID:%d\t",realdata[i].ch_id);
				printf("val :%lf\t",realdata[i].value);
				printf("tick:%lf\t",realdata[i].time_tick);
				printf("sesn:%d\t",realdata[i].sensor_sn);
				printf("stsn:%d\n",realdata[i].shot_sn);
			}
		}

		// Insert realdata into database by calling ITRI API
		//void chd_DB_write_Sensor_Data(MO_sensor_data_t *data, int length);
		if (realdata_count > 0)
			chd_DB_write_Sensor_Data(realdata, realdata_count, mosn, tablesn);

		// clear realdata for next round, reset realdata_count
		memset(&realdata,'\0',sizeof(realdata));
		realdata_count=0;
		usleep(main_config->time_between_query);

		free(rawdata);
//	} // End of while (SharedMem[1]== 1)
	//exit(EXIT_SUCCESS);
}

// TODO: 20181004 sensor data linear/non-linear convertion
// previously we use a config setting called output_factor to multiply the engineering unit converted from DAQ raw data
// to get the real sensor reading value, such as temperature or pressure
// BUT there are some sensors that is non-linear, and a transfer algorithm is needed
// I created sensorutil.c, hope to collect all the related sensor transfer function in it.
//
// Terms :
// EU : Engineering unit, the value converted from DAQ Raw Data, normally in Voltage(V)
// Linear : The sensor EU can be multiply by a number to get its real reading
// non-Linear : The sensor EU need an algorithm to compensate and get its real reading
//
// For All of these to work, we need to know is the sensor linear or non-linear,
// if it is linear, what is its multiply factor
//		this might be done by keep the necessary parameters in DB, say sensor manufacturer, model, and its factor
// if it is non-linear, what is its transfer function
//		this will require a coding work done on that specified sensor

double chd_raw_real_conversion(MO_sensor_data_t *data, daq_channel_config_t *ai_type, double ai_res)
{
    double result;
//    printf("raw=%d, resolution=%f,uv=%d, lv=%d\n",rawdec,resolution,ir_upper, ir_lower);
    result = ( ((data->value) / ai_res) * (ai_type[data->ch_id].upper - ai_type[data->ch_id].lower) + ai_type[data->ch_id].lower ) * ai_type[data->ch_id].output_factor;

    return result;
}


int chd_do_sensor_data_query(modbus_t *ctx, data_addr_t * command, dataset_t *data, struct timespec tbase)
{
   	data->len = do_modbus_command(ctx, command, NULL, NULL, NULL, data->rdata);

    //generate_current_timestamp (&sec, &msec);
    //sprintf(data->timestamp,"%ld.%03ld", sec, msec);
   	double tmptt = generate_timetick(tbase);
   	sprintf(data->timestamp,"%03lf", tmptt );
   	data->time_tick = tmptt;

    return data->len;
}

void chd_DB_write_Sensor_Data(MO_sensor_data_t *data, int length, int mosn, int tablesn)
{
	if (length == 0){
		printf("Err:No data avaliable for insert DB\n");
		return;
	}
	if (myDebug) {
		printf("<_==SHOW realdata LIST before insert\n");
		for (int i=0;i < length;i++){
			printf("chID:%d\t",data[i].ch_id);
			printf("srsn:%d\t",data[i].sensor_sn);
			printf("stsn:%d\t",data[i].shot_sn);
			printf("val :%lf\t",data[i].value);
			printf("tick:%lf\n",data[i].time_tick);

		}
	}
/*int DB_InsertMOSensorData(
	MYSQL mysqlCon,	unsigned int intMOSN,	unsigned int intMOSensorSN,	unsigned int intTableSN,
 	unsigned int intShotSN,	double doubleElapsedTime,	double doubleValue);
*/
	for (int i=0;i < length;i++){
		if (myDebug){
			printf("DEBUG: DB_InsertMOSensorData():\t"
					"MOSN[%d],SensorSN[%d],ShotSN[%d],Timetick[%lf],Value[%lf].\t",
					mosn, data[i].sensor_sn, data[i].shot_sn,
					data[i].time_tick, data[i].value);
		}
		int rc = DB_InsertMOSensorData( mysqlCon,
				mosn, data[i].sensor_sn, data[i].shot_sn,
				data[i].time_tick, data[i].value);
		if (myDebug)
			printf("DONE\n");
		if (rc != EXIT_SUCCESS) {
			printf("ERR[%d]: insert sensor data into db failed : MOSN[%d],SensorSN[%d],ShotSN[%d],Timetick[%lf],Value[%lf].\n",
					rc, mosn, data[i].sensor_sn, data[i].shot_sn,
					data[i].time_tick, data[i].value);
		}
		usleep(10000);
	}

}

int db_update_pid(int immsn, pid_t pid) {
	char sql_cmdstr[1024]={0};
	int num_rows=0;

	if (pid >0) {
		sprintf(sql_cmdstr,"UPDATE %s_Data.IMMList SET ModbusPID=%d WHERE IMMSN=%d", DB_PREFIX, pid, immsn);
	}
	else {
		sprintf(sql_cmdstr,"UPDATE %s_Data.IMMList SET ModbusPID=NULL WHERE IMMSN=%d", DB_PREFIX, immsn);
	}
	if (myDebug)	printf("db_update_pid:%s\n", sql_cmdstr);

	if (mysql_query(&mysqlCon, sql_cmdstr))
	{
		fprintf(stderr, "%s\n", mysql_error(&mysqlCon));
		return EXIT_FAILURE;
	}

	if (mysql_errno(&mysqlCon))
	{
		fprintf(stderr, "%s\n", mysql_error(&mysqlCon));
		return EXIT_FAILURE;
	}
	else if (mysql_field_count(&mysqlCon) == 0)
	{
		// query does not return data
		// (it was not a SELECT)
		num_rows = mysql_affected_rows(&mysqlCon); // For SELECT statements, mysql_affected_rows() works like mysql_num_rows().
	}
	printf("pid update complete,%d row affected\n", num_rows);


	return EXIT_SUCCESS;

}
//void shm_wait_and_remove() {
//	int status;
//
//	wait(&status);
////	if (myDebug)	printf("Forked children completed.\n");
//	shmdt((void *) ShmPTR);
////	if (myDebug)	printf("Detached shared memory..\n");
//	shmctl(ShmID, IPC_RMID, NULL);
////	if (myDebug)	printf("Shared memory removed...\n");
//	if (myDebug)	printf("Shared memory cleared.\n");
//}

// Since IMMSN and MOSN are now passed as command line arguments,
// int get_IMMSN_by_OPCUAIP(char * opcuaip, int *immsn) is no longer used
int get_IMMSN_by_OPCUAIP(char * opcuaip, int *immsn)
{
	char sql_cmdstr[1024]={0};
	int num_fields=0,num_rows=0;

	sprintf(sql_cmdstr,"SELECT IMMSN FROM %s_Data.IMMList WHERE OPCUAIP='%s'", DB_PREFIX, opcuaip);
	if (myDebug)	printf("1:%s\n", sql_cmdstr);
	if (mysql_query(&mysqlCon, sql_cmdstr))
	{
		fprintf(stderr, "%s\n", mysql_error(&mysqlCon));
		return EXIT_FAILURE;
	}

	result = mysql_store_result(&mysqlCon);
	if (result)  // there are rows
	{
		num_fields = mysql_num_fields(result);
		// retrieve rows, then call mysql_free_result(result)
		num_rows = mysql_num_rows(result);
		if (myDebug)		printf("1:%d,%d,%d\n", 2,num_fields, num_rows);
	}
	else  // mysql_store_result() returned nothing; should it have?
	{
		if (mysql_errno(&mysqlCon))
		{
			fprintf(stderr, "%s\n", mysql_error(&mysqlCon));
			return EXIT_FAILURE;
		}
		else if (mysql_field_count(&mysqlCon) == 0)
		{
			// query does not return data
			// (it was not a SELECT)
			num_rows = mysql_affected_rows(&mysqlCon); // For SELECT statements, mysql_affected_rows() works like mysql_num_rows().
		}
	}

	if (num_rows ) {
		row  = mysql_fetch_row(result);
		if (row) {
			*immsn = atoi(row[0]);
		}

		if(result)
		{
			mysql_free_result(result);
			result = NULL;
		}
	}

	return EXIT_SUCCESS;
}

// get ModBusIP, ModBusPort, OPCUAVersionSN, MoldSignalType from IMMList
int get_Modbus_info_by_IMMSN(int immsn, main_config_t * config){
	char sql_cmdstr[1024]={0};
	int num_fields=0,num_rows=0;

	sprintf(sql_cmdstr,"SELECT ModbusIP, ModbusPort, OPCUAVersionSN, MoldSignalType FROM %s_Data.IMMList WHERE IMMSN=%d",DB_PREFIX, config->intIMMSN);

	if (mysql_query(&mysqlCon, sql_cmdstr))
	{
		fprintf(stderr, "%s\n", mysql_error(&mysqlCon));
		return EXIT_FAILURE;
	}

	result = mysql_store_result(&mysqlCon);
	if (result)  // there are rows
	{
		num_fields = mysql_num_fields(result);
		// retrieve rows, then call mysql_free_result(result)
		num_rows = mysql_num_rows(result);
	}
	else  // mysql_store_result() returned nothing; should it have?
	{
		if (mysql_errno(&mysqlCon))
		{
			fprintf(stderr, "%s\n", mysql_error(&mysqlCon));
			return EXIT_FAILURE;
		}
		else if (mysql_field_count(&mysqlCon) == 0)
		{
			// query does not return data
			// (it was not a SELECT)
			num_rows = mysql_affected_rows(&mysqlCon); // For SELECT statements, mysql_affected_rows() works like mysql_num_rows().
		}
	}

	if (num_rows ) {
		row  = mysql_fetch_row(result);
		if (row) {
			if (myDebug) {
				for (int i =0;i<num_fields;i++)
				{
					printf("1:%s ,", row[i] ? row[i] : "NULL");
				}
			}
			// get ModBusIP, ModBusPort, OPCUAVersionSN, MoldSignalType from IMMList
			strncpy(config->modbus_ip,row[0], strlen(row[0]) );
			config->modbus_port = row[1] ? atoi(row[1]) : 502;
			config->opcua_version_sn = row[2] ? atoi(row[2]) : 0;
			config->mold_signal_type = row[3] ? atoi(row[3]) : 0;
		}
	}

	if(result)
	{
		mysql_free_result(result);
		result = NULL;
	}
	if ( (config->modbus_ip == NULL)     ||
		 (config->modbus_port == 0)      ||
		 (config->opcua_version_sn == 0) ||
		 ( (config->mold_signal_type != 1) &&
		   (config->mold_signal_type != 2) ) )
	{
		fprintf(stderr, "get ModBus Info Failed.");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

int get_MOSN_by_IMMSN(int immsn, int *mosn) {

	unsigned int intMOSN=0;
	int rc = DB_SelectMOSNbyIMMSN(mysqlCon, (unsigned int)immsn, &intMOSN);
	if (rc == EXIT_SUCCESS)
		*mosn = intMOSN;

	return rc;
}

int get_ShotSN_by_IMMSN_MOSN(int immsn, int mosn, int *shotsn)
{
	char sql_cmdstr[1024]={0};
	int num_fields=0,num_rows=0;

//	sprintf(sql_cmdstr,"SELECT ShotSN FROM POSDSSDEMO_Data.IMMList WHERE IMMSN=%d AND MOSN=%d",immsn, mosn);
	sprintf(sql_cmdstr,"SELECT ShotSN FROM %s_Data.IMMList WHERE IMMSN=%d AND MOSN=%d",DB_PREFIX, immsn, mosn);
	if (mysql_query(&mysqlCon, sql_cmdstr))
	{
		fprintf(stderr, "%s\n", mysql_error(&mysqlCon));
		return EXIT_FAILURE;
	}
	result = mysql_store_result(&mysqlCon);
	if (result)  // there are rows
	{
		num_fields = mysql_num_fields(result);
		// retrieve rows, then call mysql_free_result(result)
		num_rows = mysql_num_rows(result);
	}
	else  // mysql_store_result() returned nothing; should it have?
	{
		if (mysql_errno(&mysqlCon))
		{
			fprintf(stderr, "%s\n", mysql_error(&mysqlCon));
			return EXIT_FAILURE;
		}
		else if (mysql_field_count(&mysqlCon) == 0)
		{
			// query does not return data
			// (it was not a SELECT)
			num_rows = mysql_affected_rows(&mysqlCon); // For SELECT statements, mysql_affected_rows() works like mysql_num_rows().
		}
	}

	if (num_rows ) {
		row  = mysql_fetch_row(result);
		if (row) {
			if (myDebug) {
				for (int i =0;i<num_fields;i++)
				{
					printf("%s ,", row[i] ? row[i] : "NULL");
				}
			}
			*shotsn = row[0] ? atoi(row[0]) : 0;
		}
	}

	if(result)
	{
		mysql_free_result(result);
		result = NULL;
	}

	if (shotsn == 0)
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}

int get_MOSensorSN_by_MOSN(int mosn, int *intMOSensorSN, daq_channel_config_t *daq_mapping)
{
	char sql_cmdstr[1024]={0};
	int num_fields=0,num_rows=0;

	//////
	if (mosn == 0) {
		fprintf(stderr, "ERR:%s\n","MOSN is ZERO!" );
		return EXIT_FAILURE;
	}
//	sprintf(sql_cmdstr,"SELECT MOSensorSN, MOSensorChannelSN FROM POSDSSDEMO_Data_MO_%d_Info_Meta.MOSensorSNList",mosn);
	// select data in order of MOSensorChannelSN
	sprintf(sql_cmdstr,"SELECT MOSensorSN, MOSensorChannelSN FROM %s_Data_MO_%d_Info_Meta.MOSensorSNList WHERE MOSensorChannelSN IS NOT NULL ORDER BY MOSensorChannelSN",DB_PREFIX, mosn);
	if (mysql_query(&mysqlCon, sql_cmdstr))
	{
		fprintf(stderr, "%s\n", mysql_error(&mysqlCon));
		return EXIT_FAILURE;
	}

	result = mysql_store_result(&mysqlCon);
	if (result)  // there are rows
	{
		num_fields = mysql_num_fields(result);
		// retrieve rows, then call mysql_free_result(result)
		num_rows = mysql_num_rows(result);
		printf("num_fields:%d,num_rows:%d\n",num_fields,num_rows);
	}
	else  // mysql_store_result() returned nothing; should it have?
	{
		if (mysql_errno(&mysqlCon))
		{
			fprintf(stderr, "%s\n", mysql_error(&mysqlCon));
			return EXIT_FAILURE;
		}
		else if (mysql_field_count(&mysqlCon) == 0)
		{
			// query does not return data
			// (it was not a SELECT)
			num_rows = mysql_affected_rows(&mysqlCon); // For SELECT statements, mysql_affected_rows() works like mysql_num_rows().
		}
	}

	//row  = mysql_fetch_row(result);
	int ifield=0;
	int intMOSensorChannelSN=0;
	while ((row = mysql_fetch_row(result)))
	{
		intMOSensorSN[ifield] = atoi(row[0]);
		intMOSensorChannelSN = row[1] ? atoi(row[1]) : 0;

		// DAQ AI channel mapping to MOSensorSN
		// daq_mapping[DAQ channel ID]->MOSensorSN = intMOSensorSN[ifield];
		if (myDebug)
			printf("MOSensorSN[%d]=>MOSensorChannelSN[%d]\n",intMOSensorSN[ifield],intMOSensorChannelSN);

		daq_mapping[intMOSensorChannelSN].MOSensorSN = intMOSensorSN[ifield];

		ifield++;
	}

	if(result)
	{
		mysql_free_result(result);
		result = NULL;
	}

	if ( ifield == 0)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}

int get_MOSensorTableSN_by_MOSN_ShotSN(int mosn, int shotsn, int *tablesn)
{
	char sql_cmdstr[1024]={0};
	int num_fields=0,num_rows=0;

	if (shotsn == 0) {
		fprintf(stderr, "ERR:%s\n","ShotSN is ZERO!" );
		return EXIT_FAILURE;
	}

//	sprintf(sql_cmdstr,"SELECT MOSensorTableSN FROM POSDSSDEMO_Data_MO_%d_RawData.ShotSNList WHERE ShotSN = %d",mosn, shotsn);
	sprintf(sql_cmdstr,"SELECT MOSensorTableSN FROM %s_Data_MO_%d_RawData.ShotSNList WHERE ShotSN = %d",DB_PREFIX, mosn, shotsn);

	if (mysql_query(&mysqlCon, sql_cmdstr))
	{
		fprintf(stderr, "%s\n", mysql_error(&mysqlCon));
		return EXIT_FAILURE;
	}

	result = mysql_store_result(&mysqlCon);
	if (result)  // there are rows
	{
		num_fields = mysql_num_fields(result);
		// retrieve rows, then call mysql_free_result(result)
		num_rows = mysql_num_rows(result);
		printf("num_fields:%d,num_rows:%d\n",num_fields,num_rows);
	}
	else  // mysql_store_result() returned nothing; should it have?
	{
		if (mysql_errno(&mysqlCon))
		{
			fprintf(stderr, "%s\n", mysql_error(&mysqlCon));
			return EXIT_FAILURE;
		}
		else if (mysql_field_count(&mysqlCon) == 0)
		{
			// query does not return data
			// (it was not a SELECT)
			num_rows = mysql_affected_rows(&mysqlCon); // For SELECT statements, mysql_affected_rows() works like mysql_num_rows().
		}
	}

	if (num_rows){
		row  = mysql_fetch_row(result);
		if (row){
			*tablesn = atoi(row[0]);
		}
	}
	if(result)
	{
		mysql_free_result(result);
		result = NULL;
	}			//////////

	if (tablesn ==0)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}


