/* This file will contain your solution. Modify it as you wish. */
#include <types.h>
#include <lib.h>
#include <synch.h>
#include "producerconsumer_driver.h"

/* Declare any variables you need here to keep track of and
   synchronise your bounded. A sample declaration of a buffer is shown
   below. It is an array of pointers to items.

   You can change this if you choose another implementation.
   However, you should not have a buffer bigger than BUFFER_SIZE
*/

data_item_t * item_buffer[BUFFER_SIZE];
struct semaphore *mutex;
struct semaphore *empty;
struct semaphore *full;

/* consumer_receive() is called by a consumer to request more data. It
   should block on a sync primitive if no data is available in your
   buffer. It should not busy wait! */

data_item_t * consumer_receive(void)
{
        data_item_t * item;

        P(full);
        P(mutex);
        item = item_buffer[0];
        for(int i = 0; i < BUFFER_SIZE - 1; i++)
        {
                item_buffer[i]=item_buffer[i + 1];
                if (item_buffer[i + 1] == NULL) break;
        }
        item_buffer[BUFFER_SIZE - 1] = NULL;
        V(mutex);
        V(empty);
        return item;
}

/* procucer_send() is called by a producer to store data in your
   bounded buffer.  It should block on a sync primitive if no space is
   available in your buffer. It should not busy wait!*/

void producer_send(data_item_t *item)
{
        P(empty);
        P(mutex);        
        int i = 0;
        while (i < BUFFER_SIZE) {
                if (item_buffer[i] == NULL) {
                        item_buffer[i] = item;
                        break;
                }
                i++;
        }
        V(mutex);
        V(full);
}




/* Perform any initialisation (e.g. of global data) you need
   here. Note: You can panic if any allocation fails during setup */

void producerconsumer_startup(void)
{
        //item_buffer[BUFFER_SIZE] = (data_item_t *)malloc(BUFFER_SIZE * sizeof(data_item_t *));
        // mutex - only allows one producer or consumer to access buffer at a time
        mutex = sem_create("mutex", 1); 
        if (mutex == NULL) {
                panic("producerconsumer: sem create failed (mutex)");
        }
        // counts number of empty slots - blocks producer if count goes to 0
        empty = sem_create("empty", BUFFER_SIZE);
        if (empty == NULL) {
                panic("producerconsumer: sem create failed (empty)");
        }
        // count number of taken slots - blocks consumer if = 0 (nothing to consume)
        full = sem_create("full", 0);
        if (full == NULL) {
                panic("producerconsumer: sem create failed (full)");
        }
     
}

/* Perform any clean-up you need here */
void producerconsumer_shutdown(void)
{
        sem_destroy(mutex);
        sem_destroy(empty);
        sem_destroy(full);
}
