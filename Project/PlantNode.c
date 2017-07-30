#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include "contiki.h"
#include "lib/memb.h"
#include "net/rime/rime.h"
#include "sys/etimer.h"
#include "dev/leds.h"
#include "dev/button-sensor.h"

#define LEDS_BLUE 1
#define LEDS_RED 2
#define LEDS_GREEN 4
#define TEMPS_LEN 5
#define BASE_TEMP 25
#define MAX_RETRANSMISSIONS 5

//This is the global state
struct enviroment {
  uint8_t water_level;
};

struct enviroment* e=NULL;

// Declare a memory block.
MEMB(enviroment_mem,struct enviroment,1);
PROCESS(runicast_process, "Plant Node");

AUTOSTART_PROCESSES(&runicast_process);



/*------------Unicast Callbacks implementation------------*/


static struct runicast_conn runicast;

static void recv_runicast(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno)
{
    /*
        N.B. With packetbuf_dataptr() i obtai the pointer to the received payload of message
    */
    //Only if the alarm is deactivated
    if( *(uint8_t*)packetbuf_dataptr()==6){
        if(!runicast_is_transmitting(&runicast)) {

                uint8_t buff[2];
                buff[0]=6;//Code for Response
                buff[1]=e->water_level;
				linkaddr_t recv1;
				packetbuf_copyfrom(buff, 2);
				recv1.u8[0] = 3; //I send the message with Temp to the node 3.0 CU
				recv1.u8[1] = 0;
				
				runicast_send(&runicast, &recv1, MAX_RETRANSMISSIONS);
		}
    }
}

static void sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions){}


static void timedout_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions){}

static const struct runicast_callbacks runicast_calls = {recv_runicast, sent_runicast, timedout_runicast};





PROCESS_THREAD(runicast_process, ev, data)
{
    static struct etimer et;

    PROCESS_EXITHANDLER(runicast_close(&runicast));

    PROCESS_BEGIN();

    //This activate the button
	SENSORS_ACTIVATE(button_sensor);

    //Open a connection
    runicast_open(&runicast, 144, &runicast_calls);

    memb_init(&enviroment_mem);
        
    e=memb_alloc(&enviroment_mem);
    e->water_level=10;
    
    etimer_set(&et,CLOCK_SECOND*7200);//Set the timer for fetch the water Level 
    //Every 2 hours i decrease one level

    //On the ligth of garden
    leds_on(LEDS_GREEN);

    Loop:

        PROCESS_WAIT_EVENT();

        if(ev == sensors_event && data == &button_sensor){ //Wait until i press the button
			
			leds_on(LEDS_GREEN);
            leds_off(LEDS_RED);
            e->water_level=10;

		}else if(etimer_expired(&et)){
            
            if(e->water_level>0)
                e->water_level--;

            if(e->water_level<=3){

                leds_off(LEDS_RED);

                //Low Water Level
                if(!runicast_is_transmitting(&runicast)) {

                    uint8_t buff[2];
                    buff[0]=7;//Code for Alarm Low Water Level
                    buff[1]=e->water_level;
                    linkaddr_t recv1;
                    packetbuf_copyfrom(buff, 2);
                    recv1.u8[0] = 3; //I send the message with Temp to the node 3.0 CU
                    recv1.u8[1] = 0;
                    
                    runicast_send(&runicast, &recv1, MAX_RETRANSMISSIONS);
		        }
            }
            
            etimer_reset(&et);
        }

    goto Loop;

    PROCESS_END();
    
}
