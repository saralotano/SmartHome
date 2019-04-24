#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "os/dev/leds.h"
#include "arch/cpu/cc26x0-cc13x0/dev/cc26xx-uart.h"
#include "os/dev/button-hal.h"
#include <stdio.h>
#include <string.h>
#include "sys/ctimer.h"
#include "sys/etimer.h"
#include "batmon-sensor.h"
#include "random.h"

PROCESS(oven_process, "oven process");
AUTOSTART_PROCESSES(&oven_process);

static struct ctimer b_leds;
static struct etimer preparation;
static int current_temp = 0;
static int oven_degree = 0;
static int oven_time = 0;

//serve a simulare la fase di riscaldamento del forno
//aggiunge alla tempratura del sensore un valore random di massimo 50 gradi
static void get_current_temp(void){
	current_temp += random_rand()%50;
	printf("Pre-heating phase : current temperature = %d C\n", current_temp);
	return;
}

//si occupa di far lampeggiare il led rosso quando la cottura è terminata
void blink_red_callback(){
	leds_toggle(LEDS_RED);
	printf("Remove preparation!! \n");	
	ctimer_set(&b_leds, CLOCK_SECOND, blink_red_callback, NULL);
	return;
}

//segnala la fine della cottura
void end_preparation_callback(){
	printf("Cooking ended\n");
	leds_off(LEDS_GREEN);
	leds_on(LEDS_RED);
	etimer_set(&preparation, CLOCK_SECOND); //l'utente deve confermare di aver rimosso la preparazione del forno
	ctimer_set(&b_leds, 5*CLOCK_SECOND, blink_red_callback, NULL);
	return;
}

//si occupa della fase di riscaldamento e di inizio cottura
void blink_green_callback(){
	leds_toggle(LEDS_GREEN);
	get_current_temp();

	if(current_temp >= oven_degree){		
		leds_on(LEDS_GREEN);
		printf("Insert the preparation, please \n");
		etimer_set(&preparation, CLOCK_SECOND);	//aspettiamo che l'utente confermi l'inizio della cottura
		return;
	}

	ctimer_set(&b_leds, 1*CLOCK_SECOND, blink_green_callback, NULL);
	return;
}

//setta il valore della temperatura iniziale del forno
//setta il tempo di cottura
void start_preparation (int degrees, int time){
	SENSORS_ACTIVATE(batmon_sensor);
	oven_degree = degrees;
	oven_time = time;
	current_temp = batmon_sensor.value(BATMON_SENSOR_TYPE_TEMP);
	SENSORS_DEACTIVATE(batmon_sensor);
	printf("\nCooking parameters: %d degrees for %d minutes\n", degrees, time);
	return;
}

//va cambiata
void blink_callback(){
	leds_off(LEDS_RED);
	leds_on(LEDS_GREEN);

	start_preparation (180, 10);	//questi parametri saranno inviati dalla base station

	if((leds_get()&LEDS_GREEN)==2){
		ctimer_set(&b_leds, 1*CLOCK_SECOND, blink_green_callback, NULL);
		return;
	}

}


PROCESS_THREAD(oven_process, ev, data){

	PROCESS_BEGIN();
	leds_on(LEDS_RED);
	ctimer_set(&b_leds, 5*CLOCK_SECOND, blink_callback, NULL); //questa verrà sostituita dalla fase di broadcast

	while(1){
		PROCESS_YIELD();
		button_hal_button_t *btn = (button_hal_button_t *)data;
		if(ev == button_hal_press_event){
			if(btn->unique_id == BOARD_BUTTON_HAL_INDEX_KEY_LEFT){	//la preparazione è stata inserita nel forno
				printf("Cooking for %d minutes . . . \n", oven_time);
				ctimer_set(&b_leds, oven_time*CLOCK_SECOND, end_preparation_callback, NULL);
			} 
			else if(btn->unique_id == BOARD_BUTTON_HAL_INDEX_KEY_RIGHT) { //la preparazione è stata rimossa dal forno
				ctimer_stop(&b_leds);
				leds_off(LEDS_RED);
				printf("Preparation ended \n");
			}
		}
	}

	PROCESS_END();
	
}

