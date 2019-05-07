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
static struct ctimer green_led;
static struct ctimer cooking_timer;
static struct ctimer red_led;
static struct ctimer b_leds;
static bool firstTimeBlinkRed = true;
static bool buttonAvailable = false;
static bool alreadySynchronized = false;
static int current_temp = 0;
static int oven_degree = 0;
static int oven_time = 0;
static int phase = INITIAL_PHASE;
int counter = 0; // andrà tolto quando non simuleremo più su cooja


PROCESS(oven_proc, "oven_proc");

AUTOSTART_PROCESSES(&oven_proc);

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


//serve a simulare la fase di riscaldamento del forno aggiunge alla tempratura del sensore un valore random di massimo 50 gradi
static void getCurrentTemperature(void){
	current_temp += random_rand()%50;
	printf("Pre-heating phase : current temperature = %d C\n", current_temp);
	return;
}


//si occupa di far lampeggiare il led rosso quando la cottura è terminata
void blinkRedLedCallback(){
	counter++;	//andrà tolto quando non useremo più cooja
	leds_toggle(LEDS_RED);
	printf("Remove the preparation, please\n");	

	if(firstTimeBlinkRed){
		ctimer_set(&red_led, CLOCK_SECOND, blinkRedLedCallback, NULL);
		firstTimeBlinkRed = false;
	}
	else
		ctimer_restart(&red_led);

	if(counter == 2){	//tutto questo if sostituisce l'utilizzo del bottone, dopo andrà tolto
		ctimer_stop(&red_led);
		phase = INITIAL_PHASE;
		firstTimeBlinkRed = true;
		leds_off(LEDS_RED);
		printf("Preparation ended \n");
		counter = 0;
		sendMsg(OPERATION_COMPLETED,&basestation_addr,NULL);
	}

	return;
}

//this callback function is called when the cooking time expires
void endPreparationCallback(){
	printf("Cooking ended, remove the preparation and push the right botton\n");
	phase = PREPARATION_COMPLETED;
	buttonAvailable = true;
	leds_off(LEDS_GREEN);
	leds_on(LEDS_RED);
	ctimer_stop(&cooking_timer); //Non necessario, perchè non viene fatta la restart su questo timer e si stoppa in automatico
	ctimer_set(&red_led, 3*CLOCK_SECOND, blinkRedLedCallback, NULL);
	return;
}


//prehating phase function
void heatingPhase(){
	leds_toggle(LEDS_GREEN);
	getCurrentTemperature();

	if(current_temp >= oven_degree){		
		leds_on(LEDS_GREEN);
		printf("Insert the preparation and push the left button \n");
		buttonAvailable = true;
		phase = COOKING_PHASE;
		printf("Cooking for %d minutes . . . \n", oven_time);	//andrà tolta quando non useremo più cooja
		ctimer_set(&b_leds, oven_time * CLOCK_SECOND, endPreparationCallback, NULL);	//andrà tolta quando non useremo più cooja
		//cooking time is simulated in seconds and not in minutes
		return;
	}

	ctimer_restart(&green_led);
	return;
}



void start_preparation (){
	LOG_INFO("\nCooking parameters: %d degrees for %d minutes\n", oven_degree, oven_time);

	/* non c'è il batmon sensor su cooja
	SENSORS_ACTIVATE(batmon_sensor);
	current_temp = batmon_sensor.value(BATMON_SENSOR_TYPE_TEMP);	//set the current temperature of the oven
	SENSORS_DEACTIVATE(batmon_sensor);
	*/
	current_temp = 30; // va modificato e preso dal sensore
	LOG_INFO("Current sensor temperature : %d degrees\n", current_temp);
	phase = PREHEATING_PHASE;
	ctimer_set(&green_led, CLOCK_SECOND, heatingPhase, NULL);
	return;
}



void node_sync(){

	uint8_t op = DISCOVER_RESP;
	char * content = "oven";
	char msg[strlen(content)+2];
	msg[0] = (char)op;
	nullnet_len = strlen(content)+2;

	memcpy(&msg[1],content,nullnet_len-1);
	nullnet_buf = (uint8_t *)msg;
	NETSTACK_NETWORK.output(&basestation_addr);

	return;
}

void handleStartOperation(char* content){
	char delim[] = ",";
	char* ptr = strtok(content,delim);
	oven_degree = atoi(ptr);
	bool error = false;
		
	if(oven_degree < 50 || oven_degree > 250){
		LOG_INFO("Temperature not correct \n");
		error = true;
	}

	ptr = strtok(NULL,delim);
	if(ptr == NULL){
		LOG_INFO("Format error: not enough parameters \n");
		error = true;
	}

	oven_time = atoi(ptr);
	
	if(oven_time <= 0 || oven_time > 300){
		LOG_INFO("Cooking time not correct\n");
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
	LOG_INFO("oven_degree: %d\n",oven_degree);
	LOG_INFO("oven_time: %d\n",oven_time);
	sendMsg(OPERATION_OK,&basestation_addr,NULL);	
	start_preparation();
}

void handleCancelOperation(){
	switch(phase){
		case PREHEATING_PHASE:
			LOG_INFO("Preheating phase stopped\n");
			ctimer_stop(&green_led);
			phase = INITIAL_PHASE;
			sendMsg(CANCEL_OK,&basestation_addr,NULL);
			break;
		case COOKING_PHASE:
			LOG_INFO("Cooking phase stopped\n");
			ctimer_stop(&b_leds);
			phase = INITIAL_PHASE;
			sendMsg(CANCEL_OK,&basestation_addr,NULL);
			break;
		case PREPARATION_COMPLETED:
			LOG_INFO("Prepartion completed, operation cannot be stopped\n");
			sendMsg(CANCEL_ERR,&basestation_addr,NULL);
			break;
		default:
			LOG_INFO("Phase error\n");
			break;
	}
}



static void input_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest){

	if(linkaddr_cmp(dest, &linkaddr_null) && !alreadySynchronized){	
		LOG_INFO("Broadcast received\n");
		leds_off(LEDS_RED);
		basestation_addr = *src;
		node_sync();
		alreadySynchronized = true;
		return;
	}

	if(linkaddr_cmp(dest,&linkaddr_node_addr) && linkaddr_cmp(src,&basestation_addr)){
		uint8_t op = *(uint8_t *)data;
		char* content = ((char*)data)+1;
		char received_data[strlen((char *)content) + 1];
		if(len == strlen((char *)data) + 1) 
			memcpy(&received_data, content, strlen((char *)content) + 1);
		
		//splitting of the received string temperature,cooking_time
		switch(op){
			case START_OPERATION:
				handleStartOperation(content);
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

PROCESS_THREAD(oven_proc, ev, data){

	PROCESS_BEGIN();
	nullnet_set_input_callback(input_callback);
	leds_on(LEDS_RED);

	/* //questa parte si occupa della gestione degli eventi legati ai buttons

	while(1){
		PROCESS_YIELD();
		button_hal_button_t *btn = (button_hal_button_t *)data;
		if(ev == button_hal_press_event){
			if(btn->unique_id == BOARD_BUTTON_HAL_INDEX_KEY_LEFT && buttonAvailable){	//la preparazione è stata inserita nel forno
				buttonAvailable = false;
				printf("Cooking for %d minutes . . . \n", oven_time);
				ctimer_set(&cooking_timer, oven_time * CLOCK_SECOND, endPreparationCallback, NULL);
			} 
			else if(btn->unique_id == BOARD_BUTTON_HAL_INDEX_KEY_RIGHT && buttonAvailable) { //la preparazione è stata rimossa dal forno
				buttonAvailable = false;
				ctimer_stop(&red_led);
				leds_off(LEDS_RED);
				printf("Preparation ended \n");
				sendMsg(OPERATION_COMPLETED,&basestation_addr,NULL);
			}
		}
	}

	*/

	PROCESS_END();
}


