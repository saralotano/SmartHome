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
static struct ctimer b_leds;
//static struct etimer preparation;
static int current_temp = 0;
static int oven_degree = 0;
static int oven_time = 0;
int counter = 0; // andrà tolto quando non simuleremo più su cooja


PROCESS(oven_proc, "oven_proc");

AUTOSTART_PROCESSES(&oven_proc);

void sendMsg(uint8_t op,linkaddr_t *dest, char* content){

	//char * content = "oven";
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
	ctimer_set(&b_leds, CLOCK_SECOND, blinkRedLedCallback, NULL);

	if(counter == 2){	//tutto questo if sostituisce l'utilizzo del bottone, dopo andrà tolto
		ctimer_stop(&b_leds);
		leds_off(LEDS_RED);
		printf("Preparation ended \n");
		counter = 0;
		sendMsg(OPERATION_COMPLETED,&basestation_addr,NULL);
		//LOG_INFO("send message to base station: %d \n", OPERATION_COMPLETED);
	}

	return;
}

//è la funzione di callback chiamata allo scadere del tempo di cottura
void endPreparationCallback(){
	printf("Cooking ended\n");
	leds_off(LEDS_GREEN);
	leds_on(LEDS_RED);
	//etimer_set(&preparation, CLOCK_SECOND); //l'utente deve confermare di aver rimosso la preparazione del forno
											  //adesso è commentata perchè non funziona il bottone
	ctimer_set(&b_leds, 3*CLOCK_SECOND, blinkRedLedCallback, NULL);	//aspetta 3 secondi prima di iniziare a far lampeggiare il led rosso
	return;
}


//si occupa della fase di riscaldamento e di inizio cottura
void heatingPhase(){
	leds_toggle(LEDS_GREEN);
	getCurrentTemperature();

	if(current_temp >= oven_degree){		
		leds_on(LEDS_GREEN);
		printf("Insert the preparation, please \n");
		//etimer_set(&preparation, CLOCK_SECOND);	//aspettiamo che l'utente confermi l'inizio della cottura
													//adesso è commentata perchè non funziona il bottone

		printf("Cooking for %d minutes . . . \n", oven_time);	//andrà tolta quando non useremmo più cooja
		ctimer_set(&b_leds, oven_time * CLOCK_SECOND, endPreparationCallback, NULL);	//andrà tolta quando non useremmo più cooja
		//simula il tempo della ricetta in secondi e non in minuti
		return;
	}

	ctimer_set(&b_leds, 1*CLOCK_SECOND, heatingPhase, NULL);
	return;
}



void start_preparation (){
	LOG_INFO("\nCooking parameters: %d degrees for %d minutes\n", oven_degree, oven_time);

	/* non c'è il batmon sensor su cooja
	SENSORS_ACTIVATE(batmon_sensor);
	current_temp = batmon_sensor.value(BATMON_SENSOR_TYPE_TEMP);	//setta il valore iniziale della temperatura del forno
	SENSORS_DEACTIVATE(batmon_sensor);
	*/
	current_temp = 30; // va modificato e preso dal sensore
	LOG_INFO("Current sensor temperature : %d degrees\n", current_temp);

	ctimer_set(&b_leds, 1*CLOCK_SECOND, heatingPhase, NULL);	//inizio fase di preriscaldamento del forno
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



static void input_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest){
	LOG_INFO("input_callback\n");
	//LORENZO
	if(linkaddr_cmp(dest, &linkaddr_null)){
		//BROADCAST MSG received
		LOG_INFO("Broadcast received\n");
		basestation_addr = *src;
		node_sync();
		return;
	}
	if(linkaddr_cmp(dest,&linkaddr_node_addr) && linkaddr_cmp(src,&basestation_addr)){
		//LOG_INFO("basestation mi ha inviato un msg \n");
		uint8_t op = *(uint8_t *)data;
		char* content = ((char*)data)+1;
		char received_data[strlen((char *)content) + 1];
		if(len == strlen((char *)data) + 1) 
			memcpy(&received_data, content, strlen((char *)content) + 1);
		LOG_INFO("op is %d and content in %s \n",(int)op,content);
		char delim[] = ",";
		char* ptr = strtok(content,delim);
		oven_degree = atoi(ptr);
		bool error = false;
		
		if(oven_degree < 50 || oven_degree > 250){
			LOG_INFO("temperatura non corretta \n");
			error = true;
		}

		ptr = strtok(NULL,delim);
		if(ptr == NULL){
			LOG_INFO("formato errato \n");
			error = true;
		}

		oven_time = atoi(ptr);
		if(oven_time <= 0 || oven_time > 300){
			LOG_INFO("tempo di cottura non corretto\n");
			error = true;
		}

		ptr = strtok(NULL,delim);
		if(ptr != NULL){
			LOG_INFO("formato errato \n");
			error = true;
		}

		if(error){
			sendMsg(OPERATION_ERROR,&basestation_addr,NULL);
			return;
		}
		LOG_INFO("oven_degree: %d\n",oven_degree);
		LOG_INFO("oven_time: %d\n",oven_time);
		sendMsg(OPERATION_OK,&basestation_addr,NULL);	// invece di operation ok, va scritto che la basestation può essere utilizzata da un altro sensore
														// e che da questo momento il forno è occupato 

		//a questo punto posso iniziare a simulare il funzionamento del forno perchè i parametri sono settati correttamente
		start_preparation();

	}

}

PROCESS_THREAD(oven_proc, ev, data){

	PROCESS_BEGIN();
	nullnet_set_input_callback(input_callback);

	/* //questa parte si occupa della gestione degli eventi legati ai buttons

	while(1){
		PROCESS_YIELD();
		button_hal_button_t *btn = (button_hal_button_t *)data;
		if(ev == button_hal_press_event){
			if(btn->unique_id == BOARD_BUTTON_HAL_INDEX_KEY_LEFT){	//la preparazione è stata inserita nel forno
				printf("Cooking for %d minutes . . . \n", oven_time);
				ctimer_set(&b_leds, oven_time*CLOCK_SECOND, endPreparationCallback, NULL);
			} 
			else if(btn->unique_id == BOARD_BUTTON_HAL_INDEX_KEY_RIGHT) { //la preparazione è stata rimossa dal forno
				ctimer_stop(&b_leds);
				leds_off(LEDS_RED);
				printf("Preparation ended \n");
				sendMsg(OPERATION_COMPLETED,&basestation_addr,NULL);
			}
		}
	}

	*/

	PROCESS_END();
}


