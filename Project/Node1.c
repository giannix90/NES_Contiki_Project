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
  uint8_t temp;
  uint8_t leds_status; //this array og bits is used to save the previous statuo of leds
  bool alarm_signal; //1 or 0
  bool lock_gate;
  bool light_on;  
  bool during_automatic_close_gate_door;
};

struct enviroment* e=NULL;

static uint8_t temperatures[TEMPS_LEN]={BASE_TEMP,BASE_TEMP,BASE_TEMP,BASE_TEMP,BASE_TEMP};

// Declare a memory block.
MEMB(enviroment_mem,struct enviroment,1);
PROCESS(runicast_process, "Door Node");
PROCESS(automatically_close_door,"Automatically close door");
PROCESS(blink_led_process, "Blink led process");

AUTOSTART_PROCESSES(&runicast_process);

void _shift(uint8_t * t)
{
    int i=0;
    for(i=0;i<TEMPS_LEN-1;i++){
        t[i+1]=t[i];
    }
}

uint8_t get_temp()
{
    _shift(temperatures);
    int r = (rand() % 2)-1; //Between -1 and 1
    temperatures[0]=BASE_TEMP+r;
}

void save_led_status()
{
  //save the leds status
  e->leds_status=leds_get();
}

void restore_led_status()
{
  //switch off all leds
  leds_off(LEDS_ALL);
  //re-on the previous led state
  leds_on(e->leds_status);
}

/*------------Broadcast Callbacks implementation------------*/

static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{

    if(*(uint8_t*)packetbuf_dataptr()==1){
        e->alarm_signal=!e->alarm_signal;

        if(e->alarm_signal){

             if(!e->during_automatic_close_gate_door) //Only if i'm not during_automatic_close_gate_door otherwise i save blue led state
                save_led_status();
            else{
                //Stop other process during alarm
                process_exit(&automatically_close_door);
                e->during_automatic_close_gate_door=false;
            }
            //Launch the blinking_led_process
            process_start(&blink_led_process, NULL);
            //printf("Alarm Active\n");
        }else{
            //Kill the process
            process_exit(&blink_led_process);
            restore_led_status();
            //printf("Alarm not active\n");
        }
    }
    
    if(!e->alarm_signal && *(uint8_t*)packetbuf_dataptr()==3){
        process_start(&automatically_close_door, NULL);
    }        

}

static void broadcast_sent(struct broadcast_conn *c, int status, int num_tx){ }

static const struct broadcast_callbacks broadcast_call = {broadcast_recv, broadcast_sent}; //Be careful to the order
static struct broadcast_conn broadcast;

//-------------------------------------------------------

/*------------Unicast Callbacks implementation------------*/


static struct runicast_conn runicast;

static void recv_runicast(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno)
{
    /*
        N.B. With packetbuf_dataptr() i obtai the pointer to the received payload of message
    */
    //Only if the alarm is deactivated
    if(!e->alarm_signal && *(uint8_t*)packetbuf_dataptr()==4){
        if(!runicast_is_transmitting(&runicast)) {

                uint8_t buff[2];
                buff[0]=4;//Code for Response
                buff[1]=e->temp;
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
    PROCESS_EXITHANDLER(broadcast_close(&broadcast));

    PROCESS_BEGIN();

    //This activate the button
	SENSORS_ACTIVATE(button_sensor);

    //Open a connection
    runicast_open(&runicast, 144, &runicast_calls);
    broadcast_open(&broadcast, 129, &broadcast_call);

    memb_init(&enviroment_mem);
        
    e=memb_alloc(&enviroment_mem);
    e->alarm_signal=false;
    e->lock_gate=false;
    e->during_automatic_close_gate_door=false;
    e->light_on=false;
    e->temp=get_temp();
    
    etimer_set(&et,CLOCK_SECOND*10);//Set the timer for fetch the temperature

    //On the ligth of garden
    leds_on(LEDS_GREEN);

    Loop:

        PROCESS_WAIT_EVENT();

        if(ev == sensors_event && data == &button_sensor){ //Wait until i press the button
			if(!e->during_automatic_close_gate_door){
                leds_toggle(LEDS_RED);
                leds_toggle(LEDS_GREEN);
            }else{
                if(e->light_on)
                    e->leds_status=0x4;
                else
                    e->leds_status=0x2;
            }

		}else if(etimer_expired(&et)){

            e->temp=get_temp();
            
            etimer_reset(&et);
        }

    goto Loop;

    PROCESS_END();
    
}



PROCESS_THREAD(blink_led_process, ev, data)
{
    static struct etimer et;

    PROCESS_BEGIN();

    SENSORS_ACTIVATE(button_sensor);

    etimer_set(&et,CLOCK_SECOND*1);
    leds_off(LEDS_ALL);

    printf("Door sensor");

    Loop:

        PROCESS_WAIT_EVENT();

        if(etimer_expired(&et)){

            leds_toggle(LEDS_ALL);

            
            etimer_reset(&et);
        }

    goto Loop;

    PROCESS_END();
}



PROCESS_THREAD(automatically_close_door, ev, data)
{
    static struct etimer et;
    static struct etimer close_door;
    static struct etimer start_blink;

    PROCESS_BEGIN();

    save_led_status();
    

    e->during_automatic_close_gate_door=true;
    
    etimer_set(&start_blink,CLOCK_SECOND*14);
    
    //I wait until the countdown of 14 seconds start
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&start_blink));

    leds_off(LEDS_ALL);
    etimer_set(&et,CLOCK_SECOND*1);
    etimer_set(&close_door,CLOCK_SECOND*16);

    
    
    Loop:

        PROCESS_WAIT_EVENT();

        if(etimer_expired(&et)){

            leds_toggle(LEDS_BLUE);

            
            etimer_reset(&et);
        }else if(etimer_expired(&close_door)){
            //I restore the previous stete and i close the protothread
            restore_led_status();
            e->during_automatic_close_gate_door=false;
            PROCESS_EXIT();
        }

    goto Loop;

    PROCESS_END();
}