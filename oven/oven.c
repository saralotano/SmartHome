#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "sys/log.h"
#include "sys/clock.h"
#include "parameters.h"

#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO
#define SEND_INTERVAL (8 * CLOCK_SECOND)
#define SEND_INTERVAL_BRO (13 * CLOCK_SECOND)


static linkaddr_t basestation_addr;


PROCESS(oven_proc, "oven_proc");

AUTOSTART_PROCESSES(&oven_proc);

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
		LOG_INFO("basestation mi ha inviato un msg \n");
		uint8_t op = *(uint8_t *)data;
		char* content = ((char*)data)+1;
		char received_data[strlen((char *)content) + 1];
		if(len == strlen((char *)data) + 1) 
			memcpy(&received_data, content, strlen((char *)content) + 1);
		LOG_INFO("op is %d and content in %s \n",(int)op,content);
		char delim[] = ",";
		char* ptr = strtok(content,delim);
		int oven_degree, oven_time;
		oven_degree = atoi(ptr);
		if(oven_degree < 50 || oven_degree > 250){
			LOG_INFO("temperatura non corretta \n");
			return;
		}

		ptr = strtok(NULL,delim);
		if(ptr == NULL){
			LOG_INFO("formato errato \n");
			return;
		}

		oven_time = atoi(ptr);
		if(oven_time <= 0 || oven_time > 300){
			LOG_INFO("tempo di cottura non corretto\n");
			return;
		}

		ptr = strtok(NULL,delim);
		if(ptr != NULL){
			LOG_INFO("formato errato \n");
			return;
		}

		LOG_INFO("oven_degree: %d\n",oven_degree);
		LOG_INFO("oven_time: %d\n",oven_time);

	}

}

PROCESS_THREAD(oven_proc, ev, data){

	PROCESS_BEGIN();
	nullnet_set_input_callback(input_callback);

	PROCESS_END();
}


