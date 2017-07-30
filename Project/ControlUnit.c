#include <stdio.h>
#include "contiki.h"
#include "sys/etimer.h"

#include "lib/list.h"
#include "lib/memb.h"

#include "dev/button-sensor.h"
#include "dev/leds.h"
#include "net/rime/rime.h"

#define MAX_RETRANSMISSIONS 5
#define NUM_NODES_ENTRIES 4

static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from){ }

static void broadcast_sent(struct broadcast_conn *c, int status, int num_tx){ }

static const struct broadcast_callbacks broadcast_call = {broadcast_recv, broadcast_sent}; //Be careful to the order
static struct broadcast_conn broadcast;
//----------------------------------------------------------
static void recv_runicast(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno)
{ 
	uint8_t mCode=*((uint8_t*)(packetbuf_dataptr()));

	if(mCode==4)
		printf("Average Temperature on the Door: %d C\n",*(uint8_t*)(packetbuf_dataptr()+1));//get the pointer to the data in the pkt
	else if(mCode==5)
		printf("Light in Lux on the Gate: %d Lux\n",*(uint8_t*)(packetbuf_dataptr()+1));//get the pointer to the data in the pkt
	else if(mCode==6)
		printf("Level of Water of the plant: %d /10\n",*(uint8_t*)(packetbuf_dataptr()+1));//get the pointer to the data in the pkt
	else if(mCode==7)
		printf(" Alert !! Low Level of Water of the plant: %d /10\n",*(uint8_t*)(packetbuf_dataptr()+1));//get the pointer to the data in the pkt
	
}


static void sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions){ }

static void timedout_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions){}

static const struct runicast_callbacks runicast_calls = {recv_runicast, sent_runicast, timedout_runicast};
static struct runicast_conn runicast;

PROCESS(button_process, "Button process");
PROCESS(runicast_process, "Countdown process");

//The button process start automaticcally at the beginning
AUTOSTART_PROCESSES(&button_process);


static volatile uint8_t n;
 


PROCESS_THREAD(button_process, ev, data){

	static struct etimer timer; //I declare the timer
	static int count = 0; //This is the counter for count the command inserted

	PROCESS_BEGIN();

	//This activate the button
	SENSORS_ACTIVATE(button_sensor);

	
	printf("1. Activate/Deactivate the alarm signal\n2. Luck/Unlock the gate\n3. Open (and close) the door and the gate\n4. Average of the last 5 internal temperatures valuse\n5. External light value	\n6. Level of wather of the plant\n");
		
	
	Loop: //Infinite Loop

		PROCESS_WAIT_EVENT(); //Wait for any event

		//Check the event type
		if(ev == sensors_event && data == &button_sensor){ //Wait until i press the button
			//printf("You pressed the button\n");
			if(count == 0) etimer_set(&timer, CLOCK_SECOND*4);
			else etimer_restart(&timer);
			count+=1;

		}else if(etimer_expired(&timer)){
			n = count;
			count=0; //reset count
			process_start(&runicast_process, NULL);
		
		}

	goto Loop;	
	

    PROCESS_END();
}


//This is the second protothread
PROCESS_THREAD(runicast_process, ev ,data){

	PROCESS_EXITHANDLER(runicast_close(&runicast));
	PROCESS_EXITHANDLER(broadcast_close(&broadcast));

	PROCESS_BEGIN();

	broadcast_open(&broadcast, 129, &broadcast_call);
	runicast_open(&runicast, 144, &runicast_calls);

	
		if(n == 1){

				//Send in broadcast to Node1 and Node2
				packetbuf_copyfrom(&n,1);
    			broadcast_send(&broadcast);
				
						
			//PROCESS_EXIT();
		}else if(n == 2){

			if(!runicast_is_transmitting(&runicast)) {

				linkaddr_t recv1;
				packetbuf_copyfrom(&n, 1);
				recv1.u8[0] = 2; //I send the message to the node 2.0
				recv1.u8[1] = 0;

								
				runicast_send(&runicast, &recv1, MAX_RETRANSMISSIONS);
			}
		}else if(n == 3){

			//Send in broadcast to Node1 and Node2
				packetbuf_copyfrom(&n,1);
    			broadcast_send(&broadcast);
		}else if(n == 4){
			if(!runicast_is_transmitting(&runicast)) {

				linkaddr_t recv1;
				packetbuf_copyfrom(&n, 1);
				recv1.u8[0] = 1; //I send the message to the node 1.0 to request Average Temperature
				recv1.u8[1] = 0;

								
				runicast_send(&runicast, &recv1, MAX_RETRANSMISSIONS);
			}
		}else if(n == 5){
			if(!runicast_is_transmitting(&runicast)) {

				linkaddr_t recv1;
				packetbuf_copyfrom(&n, 1);
				recv1.u8[0] = 2; //I send the message to the node 1.0 to request Ligth Value
				recv1.u8[1] = 0;

				runicast_send(&runicast, &recv1, MAX_RETRANSMISSIONS);
			}
		}else if(n == 6){
			if(!runicast_is_transmitting(&runicast)) {
				
				linkaddr_t recv1;
				packetbuf_copyfrom(&n, 1);
				recv1.u8[0] = 4; //I send the message to the node 1.0 to request Ligth Value
				recv1.u8[1] = 0;

				runicast_send(&runicast, &recv1, MAX_RETRANSMISSIONS);
			}
		}
		
		printf("1. Activate/Deactivate the alarm signal\n2. Luck/Unlock the gate\n3. Open (and close) the door and the gate\n4. Average of the last 5 internal temperatures valuse\n5. External light value	\n6. Level of wather of the plant\n");
		   	

	PROCESS_END();
}
