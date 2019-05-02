#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "sys/log.h"
#include "sys/clock.h"
#include "parameters.h"
#include "sys/ctimer.h"
#include "os/dev/serial-line.h"
#include "os/dev/leds.h"
//#include "arch/cpu/cc26x0-cc13x0/dev/cc26xx-uart.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO


static linkaddr_t oven_addr;
static linkaddr_t window_addr;
static struct ctimer broad_timer;
static struct ctimer reactive_broad_timer;
static struct ctimer green_led;
static bool oven_sync = false;
static bool window_sync = false;
static bool ovenBusy = false, windowBusy = false, basestationBusy = false;
static bool firstTime = true;
static int num_sync_nodes = 0;	
static int num_sync_attempts = 0;	
static int number_nodes = 0;

void broadtimeCallback();

PROCESS(basestation_proc, "basestation");

AUTOSTART_PROCESSES(&basestation_proc);


static void synchNode(char* received_data,const linkaddr_t *src){

		if(strcmp(received_data, "oven") == 0 && !oven_sync){
			oven_addr = *src;
			LOG_INFO("Oven address : ");
			LOG_INFO_LLADDR(src);
			LOG_INFO_("\n");
			oven_sync = true;
			num_sync_nodes++;
		}
		else if(strcmp(received_data, "window") == 0 && !window_sync){
			window_addr = *src;
			LOG_INFO("Window address : ");
			LOG_INFO_LLADDR(src);
			LOG_INFO_("\n");
			window_sync = true;
			num_sync_nodes++;
		}

	LOG_INFO("Number of synchronized nodes: %d \n", num_sync_nodes);

	if(num_sync_nodes == number_nodes){
		LOG_INFO("All the nodes are correctly synchronized \n");
		ctimer_stop(&broad_timer);
		leds_on(LEDS_GREEN);

		if(!firstTime){
			ctimer_stop(&green_led);
			ctimer_stop(&reactive_broad_timer);		
		}
	}
}

void handleOperationOK(const linkaddr_t* src){
	if(linkaddr_cmp(src,&oven_addr)){
		LOG_INFO("The oven has correctly received all the parameters. \n");
	}

	if(linkaddr_cmp(src,&window_addr)){
		LOG_INFO("The window has correctly received all the parameters. \n");
	}

	//LOG_INFO("Operazione andata a buon fine \n");
	basestationBusy = false;
	return;
}

void handleOperationError(const linkaddr_t* src){
	if(linkaddr_cmp(src,&oven_addr)){
		ovenBusy = false;
		LOG_INFO("The oven did not correctly received all the parameters. \n");
	}

	if(linkaddr_cmp(src,&window_addr)){
		windowBusy = false;
		LOG_INFO("The window did not correctly received all the parameters. \n");
	}

	//LOG_INFO("Operazione non andata a buon fine\n");
	basestationBusy = false;
	return;
}



void handleOperationCompleted(const linkaddr_t* src){

	if(linkaddr_cmp(src,&oven_addr)){	
		ovenBusy = false;
		printf("The oven is ready to be used\n");
	}
	if(linkaddr_cmp(src,&window_addr)){	
		windowBusy = false;
		printf("The window is ready to be used\n");
	}

	return;
}

void handleCancelOK(const linkaddr_t* src){

	if(linkaddr_cmp(src,&oven_addr)){	
		ovenBusy = false;
		printf("The oven is no longer used\n");
	}
	if(linkaddr_cmp(src,&window_addr)){	
		windowBusy = false;
		printf("The window is no longer used\n");
	}

	return;
}

void handleCancelErr(){
	printf("The preparation is already finished, cannot cancel the operation\n");
	return;
}



char* getMsg(const void *data, uint16_t len){
	if(len != strlen((char *)data) + 1){
		LOG_INFO("Message lenght error \n");
	}	
	char* content = ((char*)data)+1;
	return content;
}


static void input_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest){

	uint8_t op = *(uint8_t *)data;

	switch(op){

		case DISCOVER_RESP:
			synchNode(getMsg(data,len),src);
			break;

		case OPERATION_OK:
			handleOperationOK(src);
			break;

		case OPERATION_ERROR:
			handleOperationError(src);
			break;

		case OPERATION_COMPLETED:
			handleOperationCompleted(src);	
			break;
		case CANCEL_OK:
			handleCancelOK(src);
			break;
		case CANCEL_ERR:
			handleCancelErr();
			break;
		default:
			LOG_INFO("Error: Code not found \n");
			return;
	}
	
}


void sendMsg(uint8_t op,linkaddr_t *dest, char* content){

	int size;
	if(content == NULL)
		size = 0;
	else
		size = strlen(content) + 1; 
	char msg[size + 1];
	msg[0] = (char)op;
	nullnet_len = size + 1;

	if(size != 0)
		memcpy(&msg[1],content,size);

	nullnet_buf = (uint8_t *)msg;
	NETSTACK_NETWORK.output(dest);

	return;
}




void handle_serial_line(char* data){

	if(!basestationBusy){
		//LOG_INFO("Basestation available \n");
		if(!strcmp(data,"oven") && oven_sync){
			LOG_INFO("Selected device: OVEN\n");
			printf("Insert temperature (Celsius degrees) and cooking time (minutes) separated by a comma\n");
			printf("Example: 180,30\n");			
			ovenBusy = true;
			basestationBusy = true;
			return;
		}
		if(!strcmp(data,"window") && window_sync){
			LOG_INFO("Selected device: WINDOW\n");
			windowBusy = true;
			basestationBusy = true;
			return;
		}
		if(!strcmp(data,"cancel") && (oven_sync || window_sync)){
			LOG_INFO("Cancel of last operation \n");
			if(ovenBusy){
				sendMsg(CANCEL_OPERATION,&oven_addr,NULL);
				return;
			}
			if(windowBusy){
				sendMsg(CANCEL_OPERATION,&window_addr,NULL);
				return;
			}
			return;
		}

		if(!strcmp(data,"status")){
			if(oven_sync){
				if(ovenBusy){
					printf("Oven is busy\n");
				}else{
					printf("Oven is ready to be used\n");
				}
			}else{
				printf("Oven is not connected\n");
			}

			if(window_sync){
				if(windowBusy){
					printf("Window is busy\n");
				}else{
					printf("Window is ready to be used\n");
				}
			}else{
				printf("Window is not connected\n");
			}
			return;
		}
		if(number_nodes > 0)
			printf("Error: Command not found \n");
		else{
			number_nodes = atoi(data);
			if(number_nodes == 0 || number_nodes < 0){
				number_nodes = 0;
				printf("Error: Number of nodes not correct\n");
			}
			else
				LOG_INFO("Number of sensor nodes = %d\n", number_nodes);
		}

	}else{

		if(ovenBusy){	//receiving commands for the oven
			//LOG_INFO("dentro ovenBusy \n");
			if(!strcmp(data,"cancel")){
				LOG_INFO("Cancel of communication \n");
				ovenBusy = false;
				basestationBusy = false;
			}else{
				if(atoi(data)){//to avoid crash of the program, if the user types a string not convertible to number
					sendMsg(START_OPERATION,&oven_addr,data);
				}else{
					printf("Check the format of inserted data\n");
				}
			}

		}else{//receiving command for the window	

			if(!strcmp(data,"cancel")){
				LOG_INFO("Cancel of communication \n");
				windowBusy = false;
				basestationBusy = false;
			}else{
				if(atoi(data)){//to avoid crash of the program, if the user types a string not convertible to number
					sendMsg(START_OPERATION,&window_addr,data);
				}else{
					printf("Check the format of inserted data\n");
				}
			}
		}

	}

}

void blinkGreenLedCallback(){
	//LOG_INFO("dentro blinkGreenLedCallback \n");
	leds_toggle(LEDS_GREEN);
	ctimer_restart(&green_led);
}


void discoverNodes(){
	sendMsg(DISCOVER_REQ,NULL,NULL);
	num_sync_attempts++;
	LOG_INFO("Number of discoverNode's attempts: %d\n", num_sync_attempts);
	leds_toggle(LEDS_RED);

	if(num_sync_attempts == MAX_NUM_ATTEMPTS){
		num_sync_attempts = 0;
		LOG_INFO("Reached MAX_NUM_ATTEMPTS\n");
		ctimer_stop(&broad_timer);
		leds_off(LEDS_RED);

		if(num_sync_nodes > 0){		
			leds_on(LEDS_GREEN);
			if(firstTime)
				ctimer_set(&green_led, CLOCK_SECOND, blinkGreenLedCallback, NULL);
		}

		if(firstTime){
			ctimer_set(&reactive_broad_timer, 5 * CLOCK_SECOND, broadtimeCallback, NULL);
			//the wait is simulated in seconds and not in minutes
			firstTime = false;
		}		
		else
			ctimer_restart(&reactive_broad_timer);	
	}
	else
		ctimer_restart(&broad_timer);
}

void broadtimeCallback(){
	ctimer_restart(&broad_timer);
}


PROCESS_THREAD(basestation_proc, ev, data){

	PROCESS_BEGIN();

	//cc26xx_uart_set_input(serial_line_input_byte);
	//serial_line_init();
	leds_on(LEDS_RED);
	nullnet_set_input_callback(input_callback);

	printf("Insert the number of sensor nodes\n");

	while(1){
		PROCESS_WAIT_EVENT_UNTIL(ev == serial_line_event_message);
		//printf("received: %s\n",(char*)data);
		handle_serial_line((char*)data);

		if(number_nodes > 0 && num_sync_nodes != number_nodes){
			ctimer_set(&broad_timer,CLOCK_SECOND,discoverNodes,NULL);
		}
		else if(num_sync_nodes != number_nodes){
			printf("Insert the number of sensor nodes\n");
		}
	}

	PROCESS_END();
}


