#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "os/dev/leds.h"
//#include "arch/cpu/cc26x0-cc13x0/dev/cc26xx-uart.h"
//#include "os/dev/button-hal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "sys/log.h"
#include "sys/clock.h"
#include "sys/ctimer.h"
#include "sys/etimer.h"
//#include "batmon-sensor.h" // non c'è il batmon sensor su cooja
#include "random.h"
#include "parameters.h"

#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO


static linkaddr_t basestation_addr;
static struct ctimer alarm;
static int userTemperature = DEFAULT_TEMPERATURE;
static int userHumidity = DEFAULT_HUMIDITY;
bool alreadySynchronized = false;


PROCESS(window_proc, "window_proc");
AUTOSTART_PROCESSES(&window_proc);

void sendMsg(uint8_t op,linkaddr_t *dest, char* content){

	uint8_t size;
	if(content == NULL)
		size = 0;
	else
		size = strlen(content) + 1; 
	char msg[size + 1];
	msg[0] = (char)op;
	nullnet_len = size + 1;

	if(size != 0)
		memcpy(&msg[1],content,nullnet_len-1);

	nullnet_buf = (uint8_t *)msg;
	NETSTACK_NETWORK.output(dest);
	return;
}

void node_sync(){

	uint8_t op = DISCOVER_RESP;
	char * content = "window";
	char msg[strlen(content)+2];
	msg[0] = (char)op;
	nullnet_len = strlen(content)+2;

	memcpy(&msg[1],content,nullnet_len-1);
	nullnet_buf = (uint8_t *)msg;
	NETSTACK_NETWORK.output(&basestation_addr);

	return;
}

void alarmCallback(){	//mandare una specie di operation Completed
	LOG_INFO("Open roller shutter \n");
	return;
}

void handleOpenWindow(){	//mandare un ACK alla base station
	LOG_INFO("Open Window \n");
	return;
}


void handleCloseWindow(){	//mandare un ACK alla base station
	LOG_INFO("Close Window \n");
	return;
}

void handleSetTemperature(char* content){
	userTemperature = atoi(content);
	LOG_INFO("User userTemperature set to : %d degrees\n", userTemperature);
	sendMsg(OPERATION_OK, &basestation_addr, NULL);
}

void handleSetHumidity(char* content){
	userHumidity = atoi(content);
	LOG_INFO("User userHumidity set to : %d %\n", userHumidity);
	sendMsg(OPERATION_OK, &basestation_addr, NULL);
}

void handleSetTimer(char* content){
	LOG_INFO("handleSetTimer \n");
	char delim[] = ":";
	char* ptr = strtok(content,delim);
	uint8_t hours = atoi(ptr);
	bool error = false;
		
	if(hours < 0 || hours > 23){
		LOG_INFO("hours not correct \n");
		error = true;
	}

	ptr = strtok(NULL,delim);
	if(ptr == NULL){
		LOG_INFO("Format error: not enough parameters \n");
		error = true;
	}

	uint8_t minutes = atoi(ptr);
	
	if(minutes < 0 || minutes > 59){
		LOG_INFO("Alarm time not correct\n");
		error = true;
	}

	ptr = strtok(NULL,delim);
	if(ptr != NULL){
		LOG_INFO("Format error: too many parameters \n");
		error = true;
	}

	if(error){
		sendMsg(OPERATION_ERROR,&basestation_addr,NULL);
		return;
	}

	if(!hours && !minutes){	
		handleOpenWindow();
		return;
	}

	LOG_INFO("hours: %d\n",hours);
	LOG_INFO("minutes: %d\n",minutes);
	sendMsg(OPERATION_OK,&basestation_addr,NULL);	

	//int seconds = minutes*60 + hours*3600;

	ctimer_set(&alarm, minutes * CLOCK_SECOND, alarmCallback, NULL);	//modificare minutes con seconds
}


static void input_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest){
	
	if(linkaddr_cmp(dest, &linkaddr_null) && !alreadySynchronized){
		LOG_INFO("Broadcast received\n");
		leds_off(LEDS_RED);
		basestation_addr = *src;
		node_sync();
		alreadySynchronized = true;
	}

	
	if(linkaddr_cmp(dest,&linkaddr_node_addr) && linkaddr_cmp(src,&basestation_addr)){
		uint8_t op = *(uint8_t *)data;
		char* content = ((char*)data)+1;
		char received_data[strlen((char *)content) + 1];	//controllare se la usiamo o no e cambiare anche negli altri file
		if(len == strlen((char *)data) + 1) 
			memcpy(&received_data, content, strlen((char *)content) + 1);
		

		switch(op){
			case OPEN_WINDOW:
				handleOpenWindow();
				break;
			
			case CLOSE_WINDOW:
				handleCloseWindow();
				break;

			case SET_TIMER_WINDOW:
				handleSetTimer(content);
				break;

			case SET_TEMPERATURE:
				handleSetTemperature(content);
				break;

			case SET_HUMIDITY:
				handleSetHumidity(content);
				break;
			
			default:
				LOG_INFO("Unrecognized code\n");
				break;
		}
	
	}

}


PROCESS_THREAD(window_proc, ev, data){

	PROCESS_BEGIN();

	LOG_INFO("prima di nullnet\n");
	nullnet_set_input_callback(input_callback);
	leds_on(LEDS_RED);


	PROCESS_END();
}


