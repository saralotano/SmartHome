#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include <string.h>
#include <stdio.h>
#include "sys/log.h"
#include "sys/clock.h"
#include "parameters.h"

#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO
//#define SEND_INTERVAL (8 * CLOCK_SECOND)
//#define SEND_INTERVAL_BRO (13 * CLOCK_SECOND)




/*
static linkaddr_t dest_addr = {{ 0x0000, 0x0012, 0x004b, 0x0000, 0x000f, 0x0082, 0x0000, 0x0002 }};
static int bro_period = 0;
static int uni_period = 0;
*/

static linkaddr_t oven_addr;

PROCESS(basestation_proc, "basestation");

AUTOSTART_PROCESSES(&basestation_proc);

static void input_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest){

	char received_data[strlen((char *)data) + 1];
	if(len == strlen((char *)data) + 1) {
		memcpy(& received_data, data, strlen((char *)data) + 1);
		if(strcmp(received_data, "oven")==0){
			LOG_INFO("strcmp\n");
			oven_addr = *src;
			LOG_INFO("Oven address : ");
			LOG_INFO_LLADDR(src);
			LOG_INFO_("\n");
		}
		else if(strcmp(received_data, "windown")==0){
			LOG_INFO("strcmp\n");
			oven_addr = *src;
			LOG_INFO("Windown address : ");
			LOG_INFO_LLADDR(src);
			LOG_INFO_("\n");
		}
	}


	
}

PROCESS_THREAD(basestation_proc, ev, data){
	//static struct etimer periodic_timer;
	//static struct etimer broadcast_periodic_timer;
	//static char uni_msg[70];
	//static char bro_msg[70];
	//sprintf(uni_msg, "Hello, I send you my msg at time: %d", uni_period);
	//sprintf(bro_msg, "Discover message");

	PROCESS_BEGIN();

	nullnet_set_input_callback(input_callback);

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
		static uint8_t* bro_msg;

		*bro_msg = (uint8_t) DISCOVER_REQ;
		nullnet_buf = (uint8_t*) bro_msg;
		//nullnet_len = strlen(bro_msg)+1;
		nullnet_len = sizeof(uint8_t);
		NETSTACK_NETWORK.output(NULL);

		LOG_INFO("Sending BROADCAST \"%d\" " , (int) bro_msg);
		//LOG_INFO_LLADDR(&dest_addr);
		LOG_INFO_("\n");

	PROCESS_END();
}


