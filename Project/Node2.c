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
#define AVERAGE_LUX 35
#define MAX_RETRANSMISSIONS 5

//This is the global state
struct enviroment {
  uint8_t temp;
  uint8_t leds_status; //this array og bits is used to save the previous statuo of leds
  bool alarm_signal; //1 or 0
  bool lock_gate;
  bool during_automatic_close_gate_door;
};

struct enviroment* e=NULL;

// Declare a memory block.
MEMB(enviroment_mem,struct enviroment,1);

PROCESS(runicast_process, "Gate Node");
PROCESS(automatically_close_gate,"Automatically close gate");
PROCESS(blink_led_process, "Blink led process");

AUTOSTART_PROCESSES(&runicast_process);

uint8_t get_light()
{
    int r = (rand() % 2)-1; //Between -1 and 1
    return (uint8_t)AVERAGE_LUX+r;
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
                process_exit(&automatically_close_gate);
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
        process_start(&automatically_close_gate, NULL);
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
    if(!e->alarm_signal && *(uint8_t*)packetbuf_dataptr()==2){
        //Flip the value
        e->lock_gate=!e->lock_gate;

        if(e->lock_gate){

            if(!e->during_automatic_close_gate_door){
                //Lock the gate
                leds_on(LEDS_RED); 
                leds_off(LEDS_GREEN);  
            }else{
                e->leds_status=0x2;
            }
            
        }else{
            if(!e->during_automatic_close_gate_door){
                //Unloc the gate
                leds_on(LEDS_GREEN);
                leds_off(LEDS_RED);
            }else{
                e->leds_status=0x4;
            }
        }
    }else if(!e->alarm_signal && *(uint8_t*)packetbuf_dataptr()==5){
        if(!runicast_is_transmitting(&runicast)) {

                uint8_t buff[2];
                buff[0]=5;//Code for Response
                buff[1]=get_light();
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
    PROCESS_EXITHANDLER(runicast_close(&runicast));
    PROCESS_EXITHANDLER(broadcast_close(&broadcast));

    PROCESS_BEGIN();
    //Open a connection
    runicast_open(&runicast, 144, &runicast_calls);
    broadcast_open(&broadcast, 129, &broadcast_call);

    memb_init(&enviroment_mem);
        
    e=memb_alloc(&enviroment_mem);
    e->alarm_signal=false;
    e->lock_gate=false;
    e->during_automatic_close_gate_door=false;
    
    //Unlock the gate
    leds_on(LEDS_GREEN);
    
    
    printf("Gate sensor");

    //Receiver node do nothing ==> wait for ever
    PROCESS_WAIT_EVENT_UNTIL(0);
        
    
    
    PROCESS_END();
}



PROCESS_THREAD(blink_led_process, ev, data)
{
    static struct etimer et;

    PROCESS_BEGIN();

    SENSORS_ACTIVATE(button_sensor);

    etimer_set(&et,CLOCK_SECOND*1);
    leds_off(LEDS_ALL);


    Loop:
        
        PROCESS_WAIT_EVENT();

        if(etimer_expired(&et)){

            leds_toggle(LEDS_ALL);

            
            etimer_reset(&et);
        }

    goto Loop;

    PROCESS_END();
}

PROCESS_THREAD(automatically_close_gate, ev, data)
{
    static struct etimer et;
    static struct etimer close_gate;

    PROCESS_BEGIN();

    etimer_set(&et,CLOCK_SECOND*1);
    etimer_set(&close_gate,CLOCK_SECOND*16);

    save_led_status();
    leds_off(LEDS_ALL);
    e->during_automatic_close_gate_door=true;

    Loop:

        PROCESS_WAIT_EVENT();

        if(etimer_expired(&et)){

            leds_toggle(LEDS_BLUE);

            
            etimer_reset(&et);
        }else if(etimer_expired(&close_gate)){
            //I restore the previous stete and i close the protothread
            restore_led_status();
            
            e->during_automatic_close_gate_door=false;
            PROCESS_EXIT();
        }

    goto Loop;

    PROCESS_END();
}