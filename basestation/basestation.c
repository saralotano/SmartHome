#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include <string.h>
#include <stdio.h>
#include "sys/log.h"
#include "sys/clock.h"
#include "parameters.h"
#include "sys/ctimer.h"
#include "os/dev/serial-line.h"
//#include "arch/cpu/cc26x0-cc13x0/dev/cc26xx-uart.h"
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
static linkaddr_t window_addr;
static struct ctimer broad_timer;
bool oven_sync = false;
bool window_sync = false;
bool ovenBusy = false, windowBusy = false;
int num_sync_nodes = 0;				//
PROCESS(basestation_proc, "basestation");

AUTOSTART_PROCESSES(&basestation_proc);


static void synchNode(char* received_data,const linkaddr_t *src){

		if(strcmp(received_data, "oven")== 0 && !oven_sync){
			oven_addr = *src;
			LOG_INFO("Oven address : ");
			LOG_INFO_LLADDR(src);
			LOG_INFO_("\n");
			oven_sync = true;
			num_sync_nodes++;
		}
		else if(strcmp(received_data, "window")==0 && !window_sync){
			window_addr = *src;
			LOG_INFO("Window address : ");
			LOG_INFO_LLADDR(src);
			LOG_INFO_("\n");
			window_sync = true;
			num_sync_nodes++;
		}
	

	if(num_sync_nodes == NUMBER_NODES){
			LOG_INFO("numero di nodi sincronizzati raggiunto \n");
			ctimer_stop(&broad_timer);
	}
}

static void input_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest){

	uint8_t op = *(uint8_t *)data;
	char* content = ((char*)data)+1;
	char received_data[strlen((char *)content) + 1];
	if(len == strlen((char *)data) + 1) 
		memcpy(&received_data, content, strlen((char *)content) + 1);

	else{
	
		LOG_INFO("errore lunghezza messaggio ricevuto \n");
		return;	
	}

	switch(op){

		case DISCOVER_RESP:
			synchNode(received_data,src);
			break;

		default:
			LOG_INFO("op non riconosciuto \n");
			return;
	}
	
}


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



void handle_serial_line(char* data){
	if(!ovenBusy && !windowBusy){
		LOG_INFO("nessuna delle due occupata \n");
		if(!strcmp(data,"oven") && oven_sync){
			printf("Inserire temperatura espressa in gradi centigradi e durata della cottura espressa in minuti separati da una virgola, come segue: 180,25\n");			
			ovenBusy = true;
			return;
		}
		if(!strcmp(data,"window") && window_sync){
			LOG_INFO("riconosco window\n");
			windowBusy = true;
			return;
		}

		LOG_INFO("dispositivo sconosciuto \n");
		return;
	}

	if(ovenBusy){
		LOG_INFO("dentro ovenBusy \n");
		sendMsg(START_OVEN,&oven_addr,data);
	}

}

void discoverNodes(){

	sendMsg(DISCOVER_REQ,NULL,NULL);
}

PROCESS_THREAD(basestation_proc, ev, data){

	PROCESS_BEGIN();

	//cc26xx_uart_set_input(serial_line_input_byte);
	//serial_line_init();

	nullnet_set_input_callback(input_callback);

	ctimer_set(&broad_timer,2*CLOCK_SECOND,discoverNodes,NULL);

	while(1){
		PROCESS_WAIT_EVENT_UNTIL(ev == serial_line_event_message);
		printf("received: %s\n",(char*)data);
		handle_serial_line((char*)data);
	}


	PROCESS_END();
}


