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
	/*
	LOG_INFO("Received BROADCAST msg \"%d\" \n" , *(uint8_t *)data);
	nullnet_len = strlen("window")+1;
	char ack[strlen("window")+1];
	memcpy(ack,"window",nullnet_len);
	LOG_INFO("Write ACK \"%s\" \b",ack);
	nullnet_buf = (uint8_t *)ack;
	NETSTACK_NETWORK.output(&basestation);
	*/

	uint8_t op = DISCOVER_RESP;
	char * content = "window";
	char msg[strlen(content)+2];
	msg[0] = (char)op;
	nullnet_len = strlen(content)+2;

	memcpy(&msg[1],content,nullnet_len-1);
	nullnet_buf = (uint8_t *)msg;
	NETSTACK_NETWORK.output(&basestation_addr);

	//char received_data[strlen((char *)content) + 1];
	//LOG_INFO("op is %d and content is \"%s\"\n",(int)op,content);
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

	//setTimer(hours,minutes); cancellare
	//int seconds = minutes*60 + hours*3600;

	ctimer_set(&alarm, minutes * CLOCK_SECOND, alarmCallback, NULL);	//modificare minutes con seconds
}


static void input_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest){
	LOG_INFO("input_callback\n");
	
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
		char received_data[strlen((char *)content) + 1];
		if(len == strlen((char *)data) + 1) 
			memcpy(&received_data, content, strlen((char *)content) + 1);
		
		//splitting of the received string temperature,cooking_time
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
			
			default:
				LOG_INFO("Unrecognized code\n");
				break;
		}
	
	}

	/*char received_data[strlen((char*)data)+1];
	LOG_INFO("input_callback\n");

	if(len == strlen((char*)data)+1){
		memcpy(&received_data, data, strlen((char*)data)+1);
		LOG_INFO("Received BROADCAST msg \"%s\"", received_data);	*/

	/* *uni_msg = DISCOVER_RESP;
	nullnet_buf = bro_msg;
	nullnet_len = 1;
	NETSTACK_NETWORK.output(NULL);

	LOG_INFO("Sending BROADCAST \"%d\" " , DISCOVER_MSG);
	//LOG_INFO_LLADDR(&dest_addr);
	LOG_INFO_("\n");


	char received_data[strlen((char*)data)+1];
	if(len == strlen((char*)data)+1){
		memcpy(&received_data, data, strlen((char*)data)+1);
		LOG_INFO("TIMESTAMP: %lu, Received \"%s\", from ", clock_seconds(), received_data);
		LOG_INFO_LLADDR(src);
		LOG_INFO_("\n");
	}*/
	//}
}

PROCESS_THREAD(window_proc, ev, data){
	/*static struct etimer periodic_timer;
	static struct etimer broadcast_periodic_timer;
	static uint8_t * uni_msg;*/
	//sprintf(uni_msg, "Hello, I send you my msg at time: %d", uni_period);
	//sprintf(bro_msg, "Hello, I am an annoying spammer, time: %d", bro_period);

	PROCESS_BEGIN();

	LOG_INFO("prima di nullnet\n");
	nullnet_set_input_callback(input_callback);
	leds_on(LEDS_RED);

	//while(1);

	/*if(IEEE_ADDR_NODE_ID == 3){
		nullnet_buf = (uint8_t *)bro_msg;
		etimer_set(&broadcast_periodic_timer, SEND_INTERVAL_BRO);
	}
	else if(!linkaddr_cmp(&dest_addr, &linkaddr_node_addr) && IEEE_ADDR_NODE_ID != 3) {
		nullnet_buf = (uint8_t *)uni_msg;
		etimer_set(&periodic_timer, SEND_INTERVAL);
	}
	while(1) {
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer) || etimer_expired(&broadcast_periodic_timer));
		if(etimer_expired(&periodic_timer) && IEEE_ADDR_NODE_ID != 3 ){
			LOG_INFO("TIMESTAMP: %lu, Sending UNICAST \"%s\", to ", clock_seconds(), uni_msg);
			LOG_INFO_LLADDR(&dest_addr);
			LOG_INFO_("\n");
			nullnet_len = strlen(uni_msg)+1;
			NETSTACK_NETWORK.output(&dest_addr);
			etimer_reset(&periodic_timer);
			uni_period += 8;
		}
		if(etimer_expired(&broadcast_periodic_timer) && IEEE_ADDR_NODE_ID == 3){
			LOG_INFO("TIMESTAMP: %lu, Sending BROADCAST \"%s\", to ", clock_seconds(), bro_msg);
			LOG_INFO_LLADDR(NULL);
			LOG_INFO_("\n");
			nullnet_len = strlen(bro_msg)+1;
			NETSTACK_NETWORK.output(NULL);
			etimer_reset(&broadcast_periodic_timer);
			bro_period+=13;
		}
	}*/



	PROCESS_END();
}


