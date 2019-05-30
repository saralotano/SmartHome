#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "os/dev/leds.h"
#include "arch/cpu/cc26x0-cc13x0/dev/cc26xx-uart.h"	//LAUNCHPAD
#include "os/dev/button-hal.h"					    //LAUNCHPAD
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "sys/log.h"
#include "sys/clock.h"
#include "sys/ctimer.h"
#include "sys/etimer.h"
#include "batmon-sensor.h"							//LAUNCHPAD
#include "random.h"
#include "parameters.h"

#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO


static linkaddr_t basestation_addr;
static struct ctimer green_led;
static struct ctimer cooking_timer;					//LAUNCHPAD
static struct ctimer red_led;
static struct ctimer b_leds;
static bool firstTimeBlinkRed = true;
static bool buttonAvailable = false;
static bool alreadySynchronized = false;
static int current_temp = 0;
static int oven_degree = 0;
static int oven_time = 0;
static int phase = INITIAL_PHASE;


PROCESS(oven_proc, "oven_proc");

AUTOSTART_PROCESSES(&oven_proc);

//This function is used to send message from the oven to the basestaion
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


//This function is used in the oven pre-heating phase and it randomly adds a maximum of 50°C to the initial oven temperature
static void getCurrentTemperature(void){
	current_temp += random_rand()%50;
	printf("Pre-heating phase : current temperature = %d C\n", current_temp);
	return;
}

//This function is called when the cooking is finished. The red led starts blinking 
void blinkRedLedCallback(){
	leds_toggle(LEDS_RED);
	printf("Remove the preparation, please\n");	

	if(firstTimeBlinkRed){
		ctimer_set(&red_led, CLOCK_SECOND, blinkRedLedCallback, NULL);
		firstTimeBlinkRed = false;
	}
	else
		ctimer_restart(&red_led);

	return;
}

//This callback function is called when the cooking time expires
void endPreparationCallback(){
	printf("Cooking ended, remove the preparation and push the right botton\n");
	phase = PREPARATION_COMPLETED;
	buttonAvailable = true;
	leds_off(LEDS_GREEN);
	leds_on(LEDS_RED);
	ctimer_set(&red_led, 3*CLOCK_SECOND, blinkRedLedCallback, NULL);
	return;
}

//This function checks if the desiderd temperature is reached
void heatingPhase(){
	leds_toggle(LEDS_GREEN);
	getCurrentTemperature();

	if(current_temp >= oven_degree){		
		leds_on(LEDS_GREEN);
		printf("Insert the preparation and push the left button \n");
		buttonAvailable = true;
		phase = COOKING_PHASE;
		return;
	}

	ctimer_restart(&green_led);
	return;
}

//This function takes the current temperature of the batmon sensors and starts the pre-heating phase
void start_preparation (){
	LOG_INFO("\nCooking parameters: %d degrees for %d minutes\n", oven_degree, oven_time);
	
	SENSORS_ACTIVATE(batmon_sensor);								//LAUNCHPAD
	current_temp = batmon_sensor.value(BATMON_SENSOR_TYPE_TEMP);	//LAUNCHPAD
	SENSORS_DEACTIVATE(batmon_sensor);								//LAUNCHPAD
	
	LOG_INFO("Current sensor temperature : %d degrees\n", current_temp);
	phase = PREHEATING_PHASE;
	ctimer_set(&green_led, CLOCK_SECOND, heatingPhase, NULL);
	return;
}

//This message is used to communicate the MAC address to the basestation
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

//This function is called to simulate the start of the preparation
void handleStartOperation(char* content){
	char delim[] = ",";
	char* ptr = strtok(content,delim);
	oven_degree = atoi(ptr);
	ptr = strtok(NULL,delim);
	oven_time = atoi(ptr);
	LOG_INFO("Cooking temperature: %d °C \n",oven_degree);
	LOG_INFO("Cooking time: %d minutes \n",oven_time);
	sendMsg(OPERATION_OK,&basestation_addr,NULL);	
	start_preparation();
}

//This function handles the interruption of preparation phases
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
			LOG_INFO("Preparation completed, operation cannot be stopped\n");
			sendMsg(CANCEL_ERR,&basestation_addr,NULL);
			break;
		default:
			LOG_INFO("ERROR: phase status not consistent\n");
			break;
	}
}


//This function is called every time that the oven receives a message
//each message represents a specific code which deals with the corresponding handle function
static void input_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest){

	if(linkaddr_cmp(dest, &linkaddr_null) && !alreadySynchronized){	
		LOG_INFO("Broadcast message received\n");
		leds_off(LEDS_RED);
		basestation_addr = *src;
		node_sync();
		alreadySynchronized = true;
		return;
	}

	if(linkaddr_cmp(dest,&linkaddr_node_addr) && linkaddr_cmp(src,&basestation_addr)){
		uint8_t op = *(uint8_t *)data;
		char* content = ((char*)data)+1;

		switch(op){

			case START_OPERATION:
				handleStartOperation(content);
				break;

			case CANCEL_OPERATION:
				handleCancelOperation();
				break;

			default:
				LOG_INFO("ERROR: Code not found\n");
				break;
		}
	}
}


PROCESS_THREAD(oven_proc, ev, data){

	PROCESS_BEGIN();
	nullnet_set_input_callback(input_callback);
	leds_on(LEDS_RED);

	while(1){
		PROCESS_YIELD();
		button_hal_button_t *btn = (button_hal_button_t *)data;
		if(ev == button_hal_press_event){
			if(btn->unique_id == BOARD_BUTTON_HAL_INDEX_KEY_LEFT && buttonAvailable){ //the preparation is inserted in the oven
				buttonAvailable = false;
				printf("Cooking for %d minutes . . . \n", oven_time);
				ctimer_set(&cooking_timer, oven_time * CLOCK_SECOND, endPreparationCallback, NULL);
			} 
			else if(btn->unique_id == BOARD_BUTTON_HAL_INDEX_KEY_RIGHT && buttonAvailable) { //the preparationis removed from the oven
				buttonAvailable = false;
				ctimer_stop(&red_led);
				leds_off(LEDS_RED);
				printf("Preparation ended \n");
				sendMsg(OPERATION_COMPLETED,&basestation_addr,"preparation");
			}
		}
	}

	PROCESS_END();
}


