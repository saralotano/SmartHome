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
#include "arch/cpu/cc26x0-cc13x0/dev/cc26xx-uart.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

static struct ctimer broad_timer;
static struct ctimer reactive_broad_timer;
static struct ctimer ACK_timer;
static struct ctimer green_led;
static bool waitingForACK = false;
static bool firstTime = true;
static int num_sync_nodes = 0;	
static int num_sync_attempts = 0;	
static int number_nodes = 0;
static bool basestationBusy = false;

//oven variables
static linkaddr_t oven_addr;
static bool oven_sync = false;
static bool ovenBusy = false;
static bool settingParametersOven = false;

//window variables
static linkaddr_t window_addr;
static bool window_sync = false;
static bool windowBusy = false;
static bool openStatusWindow = false, closeStatusWindow = false;
static bool settingLiftRollerShutter = false, settingLowerRollerShutter = false;
static bool settingTemperature = false, settingHumidity = false;
static bool liftRollerSet = false, lowerRollerSet = false; //they are used to know if the window has some parameters set
static bool communicationWithOven = false, communicationWithWindow = false;

void broadtimeCallback();
void selectDevice();


PROCESS(basestation_proc, "basestation");
AUTOSTART_PROCESSES(&basestation_proc);

//This function is called when a sensor node sends its MAC address to the basestation
static void synchNode(char* received_data,const linkaddr_t *src){

	if(num_sync_nodes >= number_nodes)
		return;

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
		printf("All the nodes are correctly synchronized \n");
		ctimer_stop(&broad_timer);
		leds_on(LEDS_GREEN);
		leds_off(LEDS_RED);

		if(!firstTime){
			ctimer_stop(&green_led);
			ctimer_stop(&reactive_broad_timer);		
		}else{
			selectDevice();
		}
	}
}

//This function changes the status of the devices when they correctly receive all the parameters
void handleOperationOK(const linkaddr_t* src){
	if(linkaddr_cmp(src,&oven_addr)){
		printf("The oven has correctly received all the parameters. \n");
		ovenBusy = true;
	}
	if(linkaddr_cmp(src,&window_addr)){
		printf("The window has correctly received all the parameters. \n");
		if(openStatusWindow){
			liftRollerSet = true;
			openStatusWindow = false;
		}else if(closeStatusWindow){
			lowerRollerSet = true;
			closeStatusWindow = false;
		}
	}

	ctimer_stop(&ACK_timer);
	waitingForACK = false;
	basestationBusy = false;
	selectDevice();
	return;
}

//This function is called every time that a device sends a message to the basestation 
//to communicate the end of a previous operation
void handleOperationCompleted(const linkaddr_t* src, char* content){

	if(linkaddr_cmp(src,&oven_addr)){	
		ovenBusy = false;
		printf("The oven is ready to be used\n");
	}
	if(linkaddr_cmp(src,&window_addr)){	
		windowBusy = false;
		if(!strcmp(content,"liftShutter")){
			liftRollerSet = false;
			printf("The roller shutter is lifted up\n");
		}else if(!strcmp(content,"lowerShutter")){
			lowerRollerSet = false;
			printf("The roller shutter is lowered\n");
		}else if(!strcmp(content,"manualLiftShutter")){
			printf("The roller shutter is lifted up\n");
			ctimer_stop(&ACK_timer);
			waitingForACK = false;
			basestationBusy = false;
			selectDevice();
		}
		else if(!strcmp(content,"manualLowerShutter")){
			printf("The roller shutter is lowered\n");
			ctimer_stop(&ACK_timer);
			waitingForACK = false;
			basestationBusy = false;
			selectDevice();
		}
	}
}

//This function is called when the user wants to cancel a command previously inserted 
void handleCancelOK(const linkaddr_t* src, char* content){

	if(linkaddr_cmp(src,&oven_addr)){	
		ovenBusy = false;
		printf("The oven is no longer used\n");
	}
	if(linkaddr_cmp(src,&window_addr)){	
		if(!strcmp(content,"liftAlarm")){
			liftRollerSet = false;
			printf("The lifting up command for roller shutter has been canceled\n");
		}
		else if(!strcmp(content,"lowerAlarm")){
			lowerRollerSet = false;
			printf("The lowering command for roller shutter has been canceled\n");
		}
		if(!lowerRollerSet && !liftRollerSet)
		printf("The window has now no timers assigned\n");
	}

	waitingForACK = false;
	basestationBusy = false;
	ctimer_stop(&ACK_timer);
	selectDevice();
}

//This is an error function called when the user tries to cancel the operation but the preparation is already finished
void handleCancelErr(){
	printf("The preparation is already finished, cannot cancel the operation\n");
	waitingForACK = false;
	basestationBusy = false;
	ctimer_stop(&ACK_timer);
	selectDevice();
	return;
}

//This function is called every time that the window humidity sensor detect that the humidity in the room is to high
//the function could act on a possible dehumidifier in order to fix humidity level 
void handleHumidityLevel(){
	printf("Room humidity is too high. Sending command to actuators.\n");
}

//This function checks the lenght of each received message
char* getMsg(const void *data, uint16_t len){
	if(len != strlen((char *)data) + 1){
		LOG_INFO("Message length error \n");
	}	
	char* content = ((char*)data)+1;
	return content;
}

//This function is called every time that the basestation receives a message
//each message represents a specific code which deals with the corresponding handle function
static void input_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest){
	uint8_t op = *(uint8_t *)data;

	switch(op){

		case DISCOVER_RESP:
			synchNode(getMsg(data,len),src);
			break;

		case OPERATION_OK:
			handleOperationOK(src);
			break;

		case OPERATION_COMPLETED:
			handleOperationCompleted(src, getMsg(data,len));	
			break;

		case CANCEL_OK:
			handleCancelOK(src, getMsg(data,len));
			break;

		case CANCEL_ERR:
			handleCancelErr();
			break;

		case HUMIDITY_LEVEL:
			handleHumidityLevel();
			break;

		default:
			LOG_INFO("ERROR: Code not found \n");
			return;
	}
}

//This function is used to send message from the basestation to the other sensor nodes
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

//This function is used when the user sets a timer for the window and it checks if all the parameters are correct
bool checkParametersWindow(char* content,uint8_t* retHours, uint8_t* retMinutes){
	char delim[] = ":";
	char* ptr = strtok(content,delim);
	if(ptr == NULL){
		printf("FORMAT_ERROR: unknown parameters \n");
		return false;
	}

	uint8_t hours = atoi(ptr);
	if(!hours){
		if(strcmp(ptr,"0") && strcmp(ptr,"00")){
			printf("FORMAT_ERROR: hours not correct \n");
			return false;
		}
	}

	if(hours < 0 || hours > 23){
		printf("FORMAT_ERROR: hours not correct \n");
		return false;
	}

	ptr = strtok(NULL,delim);
	if(ptr == NULL){
		printf("FORMAT_ERROR: not enough parameters \n");
		return false;
	}

	uint8_t minutes = atoi(ptr);
	if(!minutes){
		if(strcmp(ptr,"0") && strcmp(ptr,"00")){
			printf("Format error: minutes not correct \n");
			return false;
		}
	}
	
	if(minutes < 0 || minutes > 59){
		printf("FORMAT_ERROR: alarm time not correct\n");
		return false;
	}

	ptr = strtok(NULL,delim);
	if(ptr != NULL){
		printf("FORMAT_ERROR: too many parameters \n");
		return false;
	}
	
	*retHours = hours;
	*retMinutes = minutes;
	return true;
}

//This function is used when the user sets a timer for the oven and it checks if both parameters are correct
bool checkParametersOven(char* content){
	if(!atoi(content)){
			printf("FORMAT_ERROR: unknown parameters \n");
			return false;
	}
	char delim[] = ",";
	char* ptr = strtok(content,delim);
	if(ptr == NULL){
		printf("FORMAT_ERROR: unknown parameters \n");
		return false;
	}
	int oven_time, oven_degree;
	oven_degree = atoi(ptr);
		
	if(oven_degree < 50 || oven_degree > 250){
		printf("FORMAT_ERROR: temperature not correct \n");
		return false;
	}

	ptr = strtok(NULL,delim);
	if(ptr == NULL){
		printf("FORMAT_ERROR: not enough parameters\n");
		return false;
	}

	oven_time = atoi(ptr);	
	if(oven_time <= 0 || oven_time > 300){
		printf("FORMAT_ERROR: cooking time not correct\n");
		return false;
	}
	ptr = strtok(NULL,delim);
	if(ptr != NULL){
		printf("FORMAT_ERROR: too many parameters \n");
		return false;
	}

	return true;
}

//This function calls the function used to check the correctness of the parameters and sends them to the oven
void checkAndSendParametersOven(char* data){
	if(!strcmp(data,"back")){
		selectDevice();
		settingParametersOven = false;
		basestationBusy = false;
		communicationWithOven = false;
		return;
	}
	char parametersOven[strlen(data)+1];
	strcpy(parametersOven,data);
	if(checkParametersOven(data)){
		sendMsg(START_OPERATION,&oven_addr,parametersOven);
		waitingForACK = true;
		ctimer_restart(&ACK_timer);			
		settingParametersOven = false; 
	}
}

//This function handles all the existing commands for the oven
void handleCommunicationWithOven(char* data){
	if(settingParametersOven)
		checkAndSendParametersOven(data);	
	else{

		if(!strcmp(data,"cancel")){	
			if(ovenBusy){
				sendMsg(CANCEL_OPERATION,&oven_addr,NULL);
				waitingForACK = true;
				ctimer_restart(&ACK_timer);	
				communicationWithOven = false;
				selectDevice();		
			}
			else{
				printf("Oven is not busy \n");
			}
		}
		else if(!strcmp(data,"back")){
			basestationBusy = false;
			communicationWithOven = false;
			selectDevice();		
		}	
		else if(!strcmp(data,"setParameters") || !strcmp(data,"setparameters")){
			settingParametersOven = true;
			printf("Insert temperature (in °C) and cooking time (minutes) separated by a comma\n");
			printf("Example: 180,30\n");			
		}
		else
			printf("ERROR: Command not found\n");
	}
}

//This function calls the function used to check the correctness of the temperature and sends the value to the window
void checkAndSendTemperature(char* data){
	if(!strcmp(data,"back")){
		selectDevice();
		settingTemperature = false;
		basestationBusy = false;
		communicationWithWindow = false;
		return;
	}
	int temperature = atoi(data);
	if(temperature >=15 && temperature <=30){
		sendMsg(SET_TEMPERATURE,&window_addr,data);
		waitingForACK = true;
		settingTemperature = false;
		ctimer_restart(&ACK_timer);	
	}else{
		printf("ERROR: Temperature must be a number between 15°C and 30°C\n");
	}
}

//This function calls the function used to check the correctness of the humidity and sends the value to the window
void checkAndSendHumidity(char* data){
	if(!strcmp(data,"back")){
		selectDevice();
		settingTemperature = false;
		basestationBusy = false;
		communicationWithWindow = false;
		return;
	}
	int humidity = atoi(data);
	if(humidity <= 60 && humidity >= 15){
		sendMsg(SET_HUMIDITY,&window_addr,data);
		waitingForACK = true;
		settingHumidity = false;
		ctimer_restart(&ACK_timer);	
	}else{
		printf("ERROR: Humidity must be a number between 15%% and 60%% \n");
	}
}

//This function is used when the user sets a timer for roller shutter lifting and it checks if the parameters are correct
void checkAndSendLiftRollerShutter(char* data){
	if(!strcmp(data,"back")){
		selectDevice();
		settingLiftRollerShutter = false;
		basestationBusy = false;
		communicationWithWindow = false;
		return;
	}
	char parametersWindow[strlen(data)+1];
	strcpy(parametersWindow,data);
	uint8_t hours,minutes;
	if(checkParametersWindow(data,&hours,&minutes)){
		sendMsg(SET_TIMER_LIFT,&window_addr,parametersWindow);
		waitingForACK = true;
		if(hours || minutes)
			openStatusWindow = true;
		settingLiftRollerShutter = false;
		ctimer_restart(&ACK_timer);
	}else{
		printf("ERROR: Check the format of inserted data\n");
	}	
}

//This function is used when the user sets a timer for roller shutter lowering and it checks if the parameters are correct
void checkAndSendLowerRollerShutter(char* data){
	if(!strcmp(data,"back")){
		selectDevice();
		settingLowerRollerShutter = false;
		basestationBusy = false;
		communicationWithWindow = false;
		return;
	}
	char parametersWindow[strlen(data)+1];
	strcpy(parametersWindow,data);
	uint8_t hours,minutes;
	if(checkParametersWindow(data,&hours,&minutes)){
		sendMsg(SET_TIMER_LOWER,&window_addr,parametersWindow);
		waitingForACK = true;
		if(hours || minutes)
			closeStatusWindow = true;
		settingLowerRollerShutter = false;
		ctimer_restart(&ACK_timer);
	}else{
		printf("ERROR: Check the format of inserted data\n");
	}	
}

//This function handles all the existing commands for the window
void handleCommunicationWithWindow(char* data){
	if(settingTemperature){
		checkAndSendTemperature(data);
	}

	else if(settingHumidity){
		checkAndSendHumidity(data);
	}

	else if(settingLiftRollerShutter){
		checkAndSendLiftRollerShutter(data);
	}

	else if(settingLowerRollerShutter){
		checkAndSendLowerRollerShutter(data);
	}

	else if(!strcmp(data,"back")){
		basestationBusy = false;
		communicationWithOven = false;
		selectDevice();		
	}

	else if(!strcmp(data,"cancelLiftTimer") || !strcmp(data,"cancellifttimer")){
		if(liftRollerSet){
			sendMsg(CANCEL_OPERATION,&window_addr,"liftTimer");
			waitingForACK = true;
			ctimer_restart(&ACK_timer);
		}
		else{
			printf("Window has no timer for roller shutter opening configured\n");
			selectDevice();
			communicationWithWindow = false;
			basestationBusy = false;
		}
	}

	else if(!strcmp(data,"cancelLowerTimer") || !strcmp(data,"cancellowertimer")){
		if(lowerRollerSet){
			sendMsg(CANCEL_OPERATION,&window_addr,"lowerTimer");
			waitingForACK = true;
			ctimer_restart(&ACK_timer);
		}
		else{
			printf("Window has no timer for roller shutter closing configured\n");
			selectDevice();
			communicationWithWindow = false;
			basestationBusy = false;
		}
	}

	else if(!strcmp(data,"setTemperature") || !strcmp(data,"settemperature")){
		settingTemperature = true;
		printf("Insert desired temperature expressed in °C \n");
	}

	else if(!strcmp(data,"setHumidity") || !strcmp(data,"sethumidity")){
		settingHumidity = true;
		printf("Insert desired percentage of humidity \n");
	}

	else if(!strcmp(data,"closeWindow") || !strcmp(data,"closewindow")){
		LOG_INFO("Close window\n");
		sendMsg(CLOSE_WINDOW,&window_addr,NULL);
		waitingForACK = true;
		ctimer_restart(&ACK_timer);
	}

	else if(!strcmp(data,"openWindow") || !strcmp(data,"openwindow")){
		LOG_INFO("Open window \n");
		sendMsg(OPEN_WINDOW,&window_addr,NULL);
		waitingForACK = true;
		ctimer_restart(&ACK_timer);
	}

	else if(!strcmp(data,"liftRollerShutter") || !strcmp(data,"liftrollershutter")){
		LOG_INFO("Lift up roller shutter\n");
		sendMsg(LIFT_SHUTTER,&window_addr,NULL);
		waitingForACK = true;
		ctimer_restart(&ACK_timer);
	}

	else if(!strcmp(data,"lowerRollerShutter") || !strcmp(data,"lowerrollershutter")){
		LOG_INFO("Lower roller shutter\n");
		sendMsg(LOWER_SHUTTER,&window_addr,NULL);
		waitingForACK = true;
		ctimer_restart(&ACK_timer);
	}

	else if(!strcmp(data,"setLiftRollerShutter") || !strcmp(data,"setliftrollershutter")){
		LOG_INFO("Set lift roller shutter \n");
		settingLiftRollerShutter = true;
		printf("Insert timer for the lifting up of roller shutter in format HH:MM\n");
	}

	else if(!strcmp(data,"setLowerRollerShutter") || !strcmp(data,"setlowerrollershutter")){
		LOG_INFO("Set lower roller shutter\n");
		settingLowerRollerShutter = true;
		printf("Insert timer for the lowering of roller shutter in format HH:MM\n");
	}

	else
		printf("ERROR: Command not found\n");
}

//This function shows a list of devices correctly synched
void selectDevice(){
	printf("Select a device to comunicate with \n");
	if(oven_sync)
		printf(" - oven\n");
	if(window_sync)
		printf(" - window\n");
}

//This function reads commands from the serial line
void handle_serial_line(char* data){

	if(waitingForACK){
		printf("Wait until the end of the operation \n");
		return;
	}

	if(!basestationBusy){
		if(!strcmp(data,"oven") && oven_sync){
			printf("Available commands:\n -back \n -cancel(only if oven is working) \n -setParameters \n");
			communicationWithOven = true;
			basestationBusy = true;
			return;
		}
		if(!strcmp(data,"window") && window_sync){
			printf("Available commands:\n -back \n -openWindow \n -closeWindow \n");
			printf(" -liftRollerShutter \n -lowerRollerShutter \n -setLiftRollerShutter \n -setLowerRollerShutter \n");
			printf(" -cancelLiftTimer \n -cancelLowerTimer \n -setTemperature \n -setHumidity \n");
			communicationWithWindow = true;
			basestationBusy = true;
			return;
		}

		if(number_nodes > 0){
			printf("ERROR: Command not found \n");
			selectDevice();
		}
		else{
			number_nodes = atoi(data);
			if(number_nodes <= 0){
				number_nodes = 0;
				printf("ERROR: Number of nodes not correct\nInsert the number of sensor nodes\n");
			}
			else{
				LOG_INFO("Number of sensor nodes = %d\n", number_nodes);
				ctimer_restart(&broad_timer);
			}
		}
	}
	//BASESTATION BUSY, the user has inserted the command "deviceName"
	else{
		if(communicationWithOven){
			handleCommunicationWithOven(data);
			return;
		}
		if(communicationWithWindow){
			handleCommunicationWithWindow(data);
			return;
		}

	}

}

//This function handles green leds blinking when at least one device is correctly synchronized
void blinkGreenLedCallback(){
	leds_toggle(LEDS_GREEN);
	ctimer_restart(&green_led);
}

//This function waits for a sensor ACK. 
//If the basestation does not receive an ACK within 5 seconds, that sensor is considered disconnected 
void ackTimerCallback(){
	if(communicationWithWindow){
		communicationWithWindow = false;
		openStatusWindow = false;
		closeStatusWindow = false;
		window_sync = false;
		printf("ERROR: Communication with window failed \n");
	}

	if(communicationWithOven){
		communicationWithOven = false;
		oven_sync = false;
		printf("ERROR: Communication with oven failed \n");
	}

	selectDevice();
	num_sync_nodes--;
	num_sync_attempts = 0;
	ctimer_restart(&broad_timer);
	basestationBusy = false;
	waitingForACK = false;
}

//This function is called when the basestation needs to discover sensor nodes' MAC addresses
//after 5 attempts, it waits for 5 seconds and then it tries for other 5 times 
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

//This function is called to restart the broadcast timer
void broadtimeCallback(){
	ctimer_restart(&broad_timer);
}


PROCESS_THREAD(basestation_proc, ev, data){

	PROCESS_BEGIN();
	
	cc26xx_uart_set_input(serial_line_input_byte);	//LAUNCHPAD
	serial_line_init();								//LAUNCHPAD
	leds_on(LEDS_RED);
	nullnet_set_input_callback(input_callback);

	printf("Insert the number of sensor nodes\n");

	ctimer_set(&ACK_timer,5 * CLOCK_SECOND, ackTimerCallback, NULL);
	ctimer_stop(&ACK_timer);
	ctimer_set(&broad_timer,CLOCK_SECOND,discoverNodes,NULL);
	ctimer_stop(&broad_timer);

	while(1){

		PROCESS_WAIT_EVENT_UNTIL(ev == serial_line_event_message);
		handle_serial_line((char*)data);	
	}

	PROCESS_END();
}


