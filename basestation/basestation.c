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
static struct ctimer ACK_timer;
static struct ctimer green_led;
static bool oven_sync = false;
static bool statusWindow = false;
static bool settingOpenWindow = false, settingTemperature = false, settingHumidity = false;
static bool window_sync = false;
static bool settingParametersOven = false;
static bool communicationWithOven = false, communicationWithWindow = false;
static bool waitingForACK = false;
static bool windowSet = false; //to know if there are parameters set on the window
static bool ovenBusy = false, windowBusy = false, basestationBusy = false;
static bool firstTime = true;
static int num_sync_nodes = 0;	
static int num_sync_attempts = 0;	
static int number_nodes = 0;

void broadtimeCallback();
void selectDevice();

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
		}else{
			selectDevice();
		}
	}
}

void handleOperationOK(const linkaddr_t* src){
	if(linkaddr_cmp(src,&oven_addr)){
		printf("The oven has correctly received all the parameters. \n");
		ovenBusy = true;
	}

	if(linkaddr_cmp(src,&window_addr)){
		printf("The window has correctly received all the parameters. \n");
		if(statusWindow)
			windowSet = true;
		//N.B. cosi non viene gestito il caso in cui voglio annullare l'apertura/chiusura finestra
	}

	//LOG_INFO("Operazione andata a buon fine \n");
	ctimer_stop(&ACK_timer);
	waitingForACK = false;
	basestationBusy = false;
	selectDevice();
	return;
}

/*void handleOperationError(const linkaddr_t* src){
	if(linkaddr_cmp(src,&oven_addr)){
		ovenBusy = false;
		LOG_INFO("The oven did not correctly received all the parameters. \n");
	}

	if(linkaddr_cmp(src,&window_addr)){
		windowBusy = false;
		settingOpenWindow = false;
		LOG_INFO("The window did not correctly received all the parameters. \n");
	}

	//LOG_INFO("Operazione non andata a buon fine\n");
	basestationBusy = false;
	return;
}
*/


void handleOperationCompleted(const linkaddr_t* src){

	if(linkaddr_cmp(src,&oven_addr)){	
		ovenBusy = false;
		printf("The oven is ready to be used\n");
	}
	if(linkaddr_cmp(src,&window_addr)){	
		windowBusy = false;
		statusWindow = false;
		printf("The window is ready to be used\n");
	}
	//PER LA FINESTRA OPERATION OK EQUIVALE A OPERATION COMPLETED

	return;
}

void handleCancelOK(const linkaddr_t* src){

	if(linkaddr_cmp(src,&oven_addr)){	
		ovenBusy = false;
		printf("The oven is no longer used\n");
	}
	if(linkaddr_cmp(src,&window_addr)){	
		windowSet = false;
		statusWindow = false;
		printf("The window is no longer used\n");
	}

	waitingForACK = false;
	basestationBusy = false;
	ctimer_stop(&ACK_timer);
	selectDevice();

	return;
}

void handleCancelErr(){
	printf("The preparation is already finished, cannot cancel the operation\n");
	waitingForACK = false;
	basestationBusy = false;
	ctimer_stop(&ACK_timer);
	selectDevice();
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

		/*case OPERATION_ERROR:
			handleOperationError(src);
			break;*/

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

void checkMsgToWindow(char* data){

	LOG_INFO("Dentro checkMsgToWindow \n");
	//bool operationEnded = false;

	/*if(settingOpenWindow){
		if(atoi(data)){//to avoid crash of the program, if the user types a string not convertible to number
			sendMsg(SET_TIMER_WINDOW,&window_addr,data);
			waitingForACK = true;
			ctimer_restart(&ACK_timer);
		}else{
			printf("Check the format of inserted data\n");
		}	
	}*/

	//else{
		if(!strcmp(data,"open")){
			LOG_INFO("Open window \n");
			sendMsg(OPEN_WINDOW,&window_addr,NULL);
			waitingForACK = true;
			ctimer_restart(&ACK_timer);
			//operationEnded = true;
		}
		else if(!strcmp(data,"close")){
			LOG_INFO("Close window\n");
			//operationEnded = true;
			sendMsg(CLOSE_WINDOW,&window_addr,NULL);
			waitingForACK = true;
			ctimer_restart(&ACK_timer);
		}
	
		else if(!strcmp(data,"setTimer") || !strcmp(data,"settimer")){
			LOG_INFO("Set Timer \n");
			settingOpenWindow = true;
			printf("Insert the timer for the roller shutter in format HH:MM\n");
		}/*
		else
			LOG_INFO("Unrecognized command \n");
	
		if(operationEnded){
			windowBusy = false;
			basestationBusy = false;
		}*/
	//}
}

bool checkParametersWindow(char* content,uint8_t* retHours, uint8_t* retMinutes){
	//LOG_INFO("handleSetTimer \n");
	char delim[] = ":";
	char* ptr = strtok(content,delim);
	if(ptr == NULL){
		printf("Format error: unknown parameters \n");
		return false;
	}

	uint8_t hours = atoi(ptr);
	if(!hours){
		if(strcmp(ptr,"0") && strcmp(ptr,"00")){
			printf("Format error: hours not correct \n");
			return false;
		}
	}

	if(hours < 0 || hours > 23){
		printf("Format error: hours not correct \n");
		return false;
	}

	ptr = strtok(NULL,delim);
	if(ptr == NULL){
		printf("Format error: not enough parameters \n");
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
		printf("Format error: alarm time not correct\n");
		return false;
	}

	ptr = strtok(NULL,delim);
	if(ptr != NULL){
		printf("Format error: too many parameters \n");
		return false;
	}

	/*if(error){
		sendMsg(OPERATION_ERROR,&basestation_addr,NULL);
		return;
	}

	if(!hours && !minutes){	
		handleOpenWindow();
		return;
	}*/
	
	LOG_INFO("hours: %d\n",hours);
	LOG_INFO("minutes: %d\n",minutes);
	//sendMsg(OPERATION_OK,&basestation_addr,NULL);	

	//int seconds = minutes*60 + hours*3600;

	//ctimer_set(&alarm, minutes * CLOCK_SECOND, alarmCallback, NULL);	//modificare minutes con seconds
	*retHours = hours;
	*retMinutes = minutes;
	return true;
}

bool checkParametersOven(char* content){

	if(!atoi(content)){
			printf("Format error: unknown parameters \n");
			return false;
	}

	char delim[] = ",";
	char* ptr = strtok(content,delim);
	if(ptr == NULL){
		printf("Format error: unknown parameters \n");
		return false;
	}
	int oven_time, oven_degree;
	oven_degree = atoi(ptr);
	//bool error = false;
		
	if(oven_degree < 50 || oven_degree > 250){
		printf("Format error: temperature not correct \n");
		return false;
	}

	ptr = strtok(NULL,delim);
	if(ptr == NULL){
		printf("Format error: not enough parameters\n");
		return false;
	}

	oven_time = atoi(ptr);
	
	if(oven_time <= 0 || oven_time > 300){
		printf("Format error: cooking time not correct\n");
		return false;
	}

	ptr = strtok(NULL,delim);
	if(ptr != NULL){
		printf("Format error: too many parameters \n");
		return false;
	}

	LOG_INFO("checkParametersOven OK \n");

	return true;
}

void handleCommunicationWithOven(char* data){

	if(settingParametersOven){
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
		
		return;
	}

	else{
	
		if(!strcmp(data,"cancel")){
	
			if(ovenBusy){

				sendMsg(CANCEL_OPERATION,&oven_addr,NULL);
				waitingForACK = true;
				ctimer_restart(&ACK_timer);			

			}
			else{
				printf("Oven is not busy \n");
				return;
				//STAMPA DISPOSITIVI DISPONIBILI
			}
			return;
		}

		if(!strcmp(data,"back")){
			basestationBusy = false;
			communicationWithOven = false;
			selectDevice();	
			return;		
		}
	
		if(!strcmp(data,"setParameters") || !strcmp(data,"setparameters")){
			settingParametersOven = true;
			printf("Insert temperature (Celsius degrees) and cooking time (minutes) separated by a comma\n");
			printf("Example: 180,30\n");
			return;			
				
		}
		printf("ERROR: Command not found\n");
	}
}

void handleCommunicationWithWindow(char* data){
	if(settingTemperature){
		if(!strcmp(data,"back")){
			selectDevice();
			settingTemperature = false;
			basestationBusy = false;
			communicationWithWindow = false;
			return;
		}
		int temperature = atoi(data);
		if(temperature){
			sendMsg(SET_TEMPERATURE,&window_addr,data);
			waitingForACK = true;
			settingTemperature = false;
			ctimer_restart(&ACK_timer);	
		}else{
			printf("ERROR: Temperature must be a number\n");
		}
		return;
	}

	if(settingHumidity){
		if(!strcmp(data,"back")){
			selectDevice();
			settingTemperature = false;
			basestationBusy = false;
			communicationWithWindow = false;
			return;
		}
		int humidity = atoi(data);
		if(humidity){
			sendMsg(SET_HUMIDITY,&window_addr,data);
			waitingForACK = true;
			settingHumidity = false;
			ctimer_restart(&ACK_timer);	
		}else{
			printf("ERROR: Humidity must be a number\n");
		}
		return;
	}

	if(settingOpenWindow){
		if(!strcmp(data,"back")){
			selectDevice();
			settingOpenWindow = false;
			basestationBusy = false;
			communicationWithWindow = false;
			return;
		}
		char parametersWindow[strlen(data)+1];
		strcpy(parametersWindow,data);
		uint8_t hours,minutes;
		if(checkParametersWindow(data,&hours,&minutes)){//to avoid crash of the program, if the user types a string not convertible to number
			LOG_INFO("superato il checkParametersWindow \n");
			sendMsg(SET_TIMER_WINDOW,&window_addr,parametersWindow);
			waitingForACK = true;
			if(hours || minutes)
				statusWindow = true;
			settingOpenWindow = false;
			ctimer_restart(&ACK_timer);
		}else{
			printf("Check the format of inserted data\n");
		}	
		return;
	}

	if(!strcmp(data,"back")){
		basestationBusy = false;
		communicationWithOven = false;
		selectDevice();	
		return;		
	}

	if(!strcmp(data,"cancel")){
	
		if(windowSet){
			sendMsg(CANCEL_OPERATION,&window_addr,NULL);
			waitingForACK = true;
			ctimer_restart(&ACK_timer);			

		}
		else{
			printf("Window has no parameters configured\n");
			return;
			//STAMPA DISPOSITIVI DISPONIBILI
		}
		return;
	}

	if(!strcmp(data,"setTemperature") || !strcmp(data,"settemperature")){
		settingTemperature = true;
		printf("Insert desired temperature expressed in °C \n");
		return;
	}

	if(!strcmp(data,"setHumidity") || !strcmp(data,"sethumidity")){
		settingHumidity = true;
		printf("Insert desired percentage of humidity \n");
		return;
	}

	

	if(!strcmp(data,"close") || !strcmp(data,"open") || !strcmp(data,"setTimer") || !strcmp(data,"settimer"))//scrittura BRUTTISSIMA, SISTEMARE
		checkMsgToWindow(data);
	else
		printf("ERROR: Command not found\n");
}

void selectDevice(){	//cambiarla per renderla più generica
	printf("Select a device to comunicate with \n");
	if(oven_sync)
		printf(" - oven\n");
	if(window_sync)
		printf(" - window\n");
}

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
			printf("Available commands:\n -back \n -cancel(only if window has been set) \n -open \n");
			printf(" -close \n -setTimer \n -setTemperature \n -setHumidity \n");
			communicationWithWindow = true;
			basestationBusy = true;
			return;
		}

		if(number_nodes > 0){
			printf("Error: Command not found \n");
			selectDevice();
		}
		else{
			number_nodes = atoi(data);
			if(number_nodes == 0 || number_nodes < 0){
				number_nodes = 0;
				printf("Error: Number of nodes not correct\n");
			}
			else
				LOG_INFO("Number of sensor nodes = %d\n", number_nodes);
		}
	}

	//BASESTATION BUSY
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


/*void handle_serial_line(char* data){

	if(!basestationBusy){
		//LOG_INFO("Basestation available \n");
		if(!strcmp(data,"oven") && oven_sync){
			if(ovenBusy){
				LOG_INFO("Selected device is busy, please wait until the end of the operation or cancel it \n");
				return;
			}
			LOG_INFO("Selected device: OVEN\n");			
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

				checkMsgToWindow(data);
			}
		}

	}

}*/

void blinkGreenLedCallback(){
	//LOG_INFO("dentro blinkGreenLedCallback \n");
	leds_toggle(LEDS_GREEN);
	ctimer_restart(&green_led);
}



void ackTimerCallback(){
	if(communicationWithWindow){
		communicationWithWindow = false;
		statusWindow = false;
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

	ctimer_set(&ACK_timer,5 * CLOCK_SECOND, ackTimerCallback, NULL);
	ctimer_stop(&ACK_timer);

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


