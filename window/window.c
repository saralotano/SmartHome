#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "os/dev/leds.h"
//#include "arch/cpu/cc26x0-cc13x0/dev/cc26xx-uart.h"	//LAUNCHPAD
//#include "os/dev/button-hal.h" 						//LAUNCHPAD
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "sys/log.h"
#include "sys/clock.h"
#include "sys/ctimer.h"
#include "sys/etimer.h"
//#include "batmon-sensor.h"							//LAUNCHPAD
#include "random.h"
#include "parameters.h"

#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO


static linkaddr_t basestation_addr;

static struct ctimer lift_alarm;
static struct ctimer lower_alarm;
static struct ctimer automaticMode;
static struct ctimer manualMode;
static struct ctimer humidityTimer;

static int userTemperature = DEFAULT_TEMPERATURE;
static int userHumidity = DEFAULT_HUMIDITY;
static int currentTemp = 0;
static int currentHumidity = 0;

static bool alreadySynchronized = false;
static bool windowOpened = false;


PROCESS(window_proc, "window_proc");
AUTOSTART_PROCESSES(&window_proc);

char* getMsg(const void *data, uint16_t len){
	if(len != strlen((char *)data) + 1){
		LOG_INFO("ERROR: Message lenght not correct \n");
	}	
	char* content = ((char*)data)+1;
	return content;
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
	printf("Automatic mode timer is active\n");
	ctimer_restart(&automaticMode);
}

void openWindow(){
	printf("Open Window \n");
	windowOpened = true;
	leds_on(LEDS_GREEN);
	leds_off(LEDS_RED);
	ctimer_stop(&automaticMode);	
	ctimer_restart(&manualMode);				

}

void closeWindow(){
	printf("Close Window \n");
	windowOpened = false;
	leds_off(LEDS_GREEN);
	leds_on(LEDS_RED);
	ctimer_stop(&automaticMode);
	ctimer_restart(&manualMode);				
}


void environmentCallback(){
	
	//SENSORS_ACTIVATE(batmon_sensor);								//LAUNCHPAD
	//currentTemp = batmon_sensor.value(BATMON_SENSOR_TYPE_TEMP);	//LAUNCHPAD
	//SENSORS_DEACTIVATE(batmon_sensor);							//LAUNCHPAD

	currentTemp = DEFAULT_TEMPERATURE;	//COOJA	
	bool changeStatus = false;

	if(windowOpened){
		currentTemp -= random_rand()%10;
		LOG_INFO("Window opened, current temperature: %d °C\n",currentTemp);
		if(currentTemp < userTemperature){
			changeStatus = true;
			closeWindow();
		}
	}
	else{
		currentTemp += random_rand()%10;
		LOG_INFO("Window closed, current temperature: %d °C\n",currentTemp);
		if(currentTemp > userTemperature){
			changeStatus = true;
			openWindow();
		}
	}
	
	if(!changeStatus){
		ctimer_restart(&automaticMode);
	}
}


void manualLiftShutter(){
	printf("Lift roller shutter \n");
	sendMsg(OPERATION_COMPLETED,&basestation_addr,"manualLiftShutter");
	return;
}


void manualLowerShutter(){
	printf("Lower roller shutter \n");
	sendMsg(OPERATION_COMPLETED,&basestation_addr,"manualLowerShutter");
	return;
}


void liftAlarmCallback(){	
	printf("Lift roller shutter \n");
	sendMsg(OPERATION_COMPLETED,&basestation_addr,"liftShutter");
	return;
}


void lowerAlarmCallback(){
	printf("Lower roller shutter \n");
	sendMsg(OPERATION_COMPLETED,&basestation_addr,"lowerShutter");
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
	LOG_INFO("User temperature set to : %d °C\n", userTemperature);
	sendMsg(OPERATION_OK, &basestation_addr, NULL);
}

void handleSetHumidity(char* content){
	userHumidity = atoi(content);
	LOG_INFO("User humidity set to : %d %% \n", userHumidity);
	sendMsg(OPERATION_OK, &basestation_addr, NULL);
}


void handleSetTimerLift(char* content){
	LOG_INFO("handleSetTimerLift \n");
	char delim[] = ":";
	char* ptr = strtok(content,delim);
	uint8_t hours = atoi(ptr);
		
	ptr = strtok(NULL,delim);
	uint8_t minutes = atoi(ptr);
	
	LOG_INFO("hours: %d\n",hours);
	LOG_INFO("minutes: %d\n",minutes);

	//int seconds = minutes*60 + hours*3600; 
	sendMsg(OPERATION_OK,&basestation_addr,"setOpenShutter");		
	ctimer_set(&lift_alarm, minutes * CLOCK_SECOND, liftAlarmCallback, NULL);	//we must use seconds
}


void handleSetTimerLower(char* content){
	LOG_INFO("handleSetTimerLower \n");
	char delim[] = ":";
	char* ptr = strtok(content,delim);
	uint8_t hours = atoi(ptr);
		
	ptr = strtok(NULL,delim);
	uint8_t minutes = atoi(ptr);
	
	LOG_INFO("hours: %d\n",hours);
	LOG_INFO("minutes: %d\n",minutes);

	//int seconds = minutes*60 + hours*3600;
	sendMsg(OPERATION_OK,&basestation_addr,"setCloseShutter");		
	ctimer_set(&lower_alarm, minutes * CLOCK_SECOND, lowerAlarmCallback, NULL);	//we must use seconds
}


void handleCancelOperation(char* content){
	if(!strcmp(content,"liftTimer")){
		printf("Lift Alarm reset\n");
		ctimer_stop(&lift_alarm);
		sendMsg(CANCEL_OK,&basestation_addr,"liftAlarm");
	}
	else if(!strcmp(content,"lowerTimer")){
		printf("Lower Alarm reset\n");
		ctimer_stop(&lower_alarm);
		sendMsg(CANCEL_OK,&basestation_addr,"lowerAlarm");
	}
}


static void input_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest){
	
	if(linkaddr_cmp(dest, &linkaddr_null) && !alreadySynchronized){
		LOG_INFO("Broadcast received\n");
		leds_off(LEDS_RED);
		basestation_addr = *src;
		node_sync();
		alreadySynchronized = true;
		ctimer_restart(&automaticMode);
		ctimer_restart(&humidityTimer);
	}
	
	if(linkaddr_cmp(dest,&linkaddr_node_addr) && linkaddr_cmp(src,&basestation_addr)){
		uint8_t op = *(uint8_t *)data;
		char* content = ((char*)data)+1;
		char received_data[strlen((char *)content) + 1];
		if(len == strlen((char *)data) + 1) 
			memcpy(&received_data, content, strlen((char *)content) + 1);
		
		switch(op){
			case OPEN_WINDOW:
				handleOpenWindow();
				break;
			
			case CLOSE_WINDOW:
				handleCloseWindow();
				break;

			case LIFT_SHUTTER:
				manualLiftShutter();
				break;

			case LOWER_SHUTTER:
				manualLowerShutter();
				break;

			case SET_TIMER_LIFT:
				handleSetTimerLift(content);
				break;

			case SET_TIMER_LOWER:
				handleSetTimerLower(content);
				break;

			case SET_TEMPERATURE:
				handleSetTemperature(content);
				break;

			case SET_HUMIDITY:
				handleSetHumidity(content);
				break;

			case CANCEL_OPERATION:
				handleCancelOperation(getMsg(data,len));
				break;
			
			default:
				LOG_INFO("ERROR: Code not found\n");
				break;
		}	
	}
}

void humidityCallback(){
	if(alreadySynchronized){
		if(currentHumidity > userHumidity){
			currentHumidity=0;
		}
		else{
			currentHumidity+=random_rand()%10;
		}

		LOG_INFO("Current humidity %d\n",currentHumidity);
		if(currentHumidity > userHumidity){
			printf("Humidity level is too high. Sending message to the basestation\n");
			sendMsg(HUMIDITY_LEVEL,&basestation_addr,"humidity");
		}
	}
	ctimer_restart(&humidityTimer);
}


PROCESS_THREAD(window_proc, ev, data){

	PROCESS_BEGIN();

	nullnet_set_input_callback(input_callback);
	leds_on(LEDS_RED);

	ctimer_set(&automaticMode, 5*CLOCK_SECOND, environmentCallback, NULL);	
	ctimer_stop(&automaticMode);
	ctimer_set(&manualMode, 20*CLOCK_SECOND, noEnvironmentCallback, NULL);	
	ctimer_stop(&manualMode);
	ctimer_set(&humidityTimer, 15*CLOCK_SECOND, humidityCallback, NULL); //timer used for checking humidity periodically
	ctimer_stop(&humidityTimer);

	//LAUNCHPAD
	/*while(1){
		PROCESS_YIELD();
		button_hal_button_t *btn = (button_hal_button_t *)data;
		if(ev == button_hal_press_event){
			if(btn->unique_id == BOARD_BUTTON_HAL_INDEX_KEY_LEFT){	//the user has opened the window
				openWindow();
			} 
			else if(btn->unique_id == BOARD_BUTTON_HAL_INDEX_KEY_RIGHT) { //the user has closed the window
				closeWindow();
			}
		}
	}*/

	PROCESS_END();
}


