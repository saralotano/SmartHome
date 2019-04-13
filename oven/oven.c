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
#define SEND_INTERVAL (8 * CLOCK_SECOND)
#define SEND_INTERVAL_BRO (13 * CLOCK_SECOND)





/*static linkaddr_t dest_addr = {{ 0x0000, 0x0012, 0x004b, 0x0000, 0x000f, 0x0082, 0x0000, 0x0002 }};
static int bro_period = 0;
static int uni_period = 0;*/
linkaddr_t basestation;


PROCESS(oven_proc, "oven_proc");

AUTOSTART_PROCESSES(&oven_proc);

void node_sync(){
	/*
	LOG_INFO("Received BROADCAST msg \"%d\" \n" , *(uint8_t *)data);
	nullnet_len = strlen("oven")+1;
	char ack[strlen("oven")+1];
	memcpy(ack,"oven",nullnet_len);
	LOG_INFO("Write ACK \"%s\" \b",ack);
	nullnet_buf = (uint8_t *)ack;
	NETSTACK_NETWORK.output(&basestation);
	*/

	uint8_t op = 23;
	char * content = "messaggio forno";
	char msg[strlen(content)+2];
	msg[0] = (char)op;
	nullnet_len = strlen(content)+2;

	memcpy(&msg[1],content,nullnet_len-1);
	nullnet_buf = (uint8_t *)msg;
	NETSTACK_NETWORK.output(&basestation);

	//char received_data[strlen((char *)content) + 1];
	//LOG_INFO("op is %d and content is \"%s\"\n",(int)op,content);
	return;
}

static void input_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest){
	LOG_INFO("input_callback\n");
	//LORENZO
	if(linkaddr_cmp(dest, &linkaddr_null)){
		//BROADCAST MSG received
		LOG_INFO("Broadcast received\n");
		basestation = *src;
		node_sync();
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

PROCESS_THREAD(oven_proc, ev, data){
	/*static struct etimer periodic_timer;
	static struct etimer broadcast_periodic_timer;
	static uint8_t * uni_msg;*/
	//sprintf(uni_msg, "Hello, I send you my msg at time: %d", uni_period);
	//sprintf(bro_msg, "Hello, I am an annoying spammer, time: %d", bro_period);

	PROCESS_BEGIN();

	LOG_INFO("prima di nullnet\n");
	nullnet_set_input_callback(input_callback);

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


