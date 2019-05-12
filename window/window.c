#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "os/dev/leds.h"
//#include "arch/cpu/cc26x0-cc13x0/dev/cc26xx-uart.h" //LAUNCHPAD
//#include "os/dev/button-hal.h" //LAUNCHPAD
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "sys/log.h"
#include "sys/clock.h"
#include "sys/ctimer.h"
#include "sys/etimer.h"
//#include "batmon-sensor.h" //LAUNCHPAD
#include "random.h"
#include "parameters.h"

#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO


static linkaddr_t basestation_addr;

static struct ctimer alarm;
static struct ctimer checkInfoEnvironment;
static struct ctimer noCheckInfoEnvironment;

static int userTemperature = DEFAULT_TEMPERATURE;
static int userHumidity = DEFAULT_HUMIDITY;
static int currentTemp = 0;

static bool alreadySynchronized = false;
static bool windowOpened = false;


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

void noEnvironmentCallback(){
	LOG_INFO("riattivo il timer per ambiente \n");
	ctimer_restart(&checkInfoEnvironment);
}

void openWindow(){
	LOG_INFO("Open Window \n");
	windowOpened = true;
	leds_on(LEDS_GREEN);
	leds_off(LEDS_RED);
	ctimer_stop(&checkInfoEnvironment);	
	ctimer_restart(&noCheckInfoEnvironment);				

}

void closeWindow(){
	LOG_INFO("Close Window \n");
	windowOpened = false;
	leds_off(LEDS_GREEN);
	leds_on(LEDS_RED);
	ctimer_stop(&checkInfoEnvironment);
	ctimer_restart(&noCheckInfoEnvironment);				
}


void environmentCallback(){
	//LAUNCHPAD
	/*SENSORS_ACTIVATE(batmon_sensor);
	currentTemp = batmon_sensor.value(BATMON_SENSOR_TYPE_TEMP);	//set the current temperature of the oven
	SENSORS_DEACTIVATE(batmon_sensor);*/

	//COOJA
	currentTemp = DEFAULT_TEMPERATURE;
	
	bool changeStatus = false;

	if(windowOpened){
		currentTemp -= random_rand()%10;
		LOG_INFO("Window opened, current temperature: %d\n",currentTemp);
		if(currentTemp < userTemperature){
			changeStatus = true;
			closeWindow();
		}
	}
	else{
		currentTemp += random_rand()%10;
		LOG_INFO("Window closed, current temperature: %d\n",currentTemp);
		if(currentTemp > userTemperature){
			changeStatus = true;
			openWindow();
		}
	}

	if(!changeStatus){
		ctimer_restart(&checkInfoEnvironment);
	}
}



void alarmCallback(){	//mandare una specie di operation Completed
	LOG_INFO("Lift roller shutter \n");
	sendMsg(OPERATION_COMPLETED,&basestation_addr,NULL);
	return;
}

void handleOpenWindow(){	
	openWindow();
	sendMsg(OPERATION_OK,&basestation_addr,NULL);
	return;
}


void handleCloseWindow(){	
	closeWindow();
	sendMsg(OPERATION_OK,&basestation_addr,NULL);
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
	printf("stampo content in handleSetTimer %s\n", content);
	LOG_INFO("handleSetTimer \n");
	char delim[] = ":";
	char* ptr = strtok(content,delim);
	uint8_t hours = atoi(ptr);
	//bool error = false;
		
	ptr = strtok(NULL,delim);
	uint8_t minutes = atoi(ptr);
	
	LOG_INFO("hours: %d\n",hours);
	LOG_INFO("minutes: %d\n",minutes);

	//int seconds = minutes*60 + hours*3600;

	sendMsg(OPERATION_OK,&basestation_addr,NULL);		
	ctimer_set(&alarm, minutes * CLOCK_SECOND, alarmCallback, NULL);
		//modificare minutes con seconds
}

void handleCancelOperation(){
	LOG_INFO("Alarm reset\n");
	//userTemperature = DEFAULT_TEMPERATURE;
	//userHumidity = DEFAULT_HUMIDITY;
	ctimer_stop(&alarm);
	sendMsg(CANCEL_OK,&basestation_addr,NULL);
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

			case CANCEL_OPERATION:
				handleCancelOperation();
				break;
			
			default:
				LOG_INFO("Unrecognized code\n");
				break;
		}
	
	}

}


PROCESS_THREAD(window_proc, ev, data){

	PROCESS_BEGIN();

	nullnet_set_input_callback(input_callback);
	leds_on(LEDS_RED);

	//tempo momentaneo
	ctimer_set(&checkInfoEnvironment,5*CLOCK_SECOND,environmentCallback,NULL);
	ctimer_set(&noCheckInfoEnvironment,20*CLOCK_SECOND,noEnvironmentCallback,NULL);
	ctimer_stop(&noCheckInfoEnvironment);

	//LAUNCHPAD
	/*while(1){
		PROCESS_YIELD();
		button_hal_button_t *btn = (button_hal_button_t *)data;
		if(ev == button_hal_press_event){
			if(btn->unique_id == BOARD_BUTTON_HAL_INDEX_KEY_LEFT){	//la preparazione è stata inserita nel forno
				openWindow();
			} 
			else if(btn->unique_id == BOARD_BUTTON_HAL_INDEX_KEY_RIGHT) { //la preparazione è stata rimossa dal forno
				closeWindow();
			}
		}
	}*/



	PROCESS_END();
}


