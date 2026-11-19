/*
 * prodcons.c - Producer-consumer problem with a circular buffer.
 * Synchronization uses POSIX semaphores (empty/full counts) and a mutex
 * that protects the critical section.
 * OS concepts: Unit 3 - Process synchronization, critical section, mutual
 * exclusion, semaphores.
 */
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int *buffer;
static int capacity;
static int total_items;
static int in = 0;
static int out = 0;

static sem_t empty_slots;   /* counts free slots   */
static sem_t full_slots;    /* counts filled slots */
static sem_t mutex;         /* mutual exclusion    */

static void *producer(void *arg)
{
    (void)arg;
    for (int i = 1; i <= total_items; i++) {
        sem_wait(&empty_slots);      /* wait for a free slot */
        sem_wait(&mutex);            /* enter critical section */

        buffer[in] = i;
        printf("Producer: produced item %d at slot %d\n", i, in);
        in = (in + 1) % capacity;

        sem_post(&mutex);            /* leave critical section */
        sem_post(&full_slots);       /* signal one more item */
        usleep(200000);
    }
    return NULL;
}

static void *consumer(void *arg)
{
    (void)arg;
    for (int i = 1; i <= total_items; i++) {
        sem_wait(&full_slots);       /* wait for an item */
        sem_wait(&mutex);            /* enter critical section */

        int item = buffer[out];
        printf("Consumer: consumed item %d from slot %d\n", item, out);
        out = (out + 1) % capacity;

        sem_post(&mutex);            /* leave critical section */
        sem_post(&empty_slots);      /* signal one more free slot */
        usleep(500000);
    }
    return NULL;
}

int main(void)
{
    pthread_t prod, cons;

    printf("Enter the size of the circular buffer: ");
    if (scanf("%d", &capacity) != 1 || capacity <= 0) {
        fprintf(stderr, "Buffer size must be a positive number.\n");
        return 1;
    }
    printf("Enter the number of items to produce: ");
    if (scanf("%d", &total_items) != 1 || total_items <= 0) {
        fprintf(stderr, "Number of items must be a positive number.\n");
        return 1;
    }

    buffer = malloc(capacity * sizeof(int));
    if (!buffer) {
        perror("malloc");
        return 1;
    }

    sem_init(&empty_slots, 0, capacity);
    sem_init(&full_slots, 0, 0);
    sem_init(&mutex, 0, 1);

    pthread_create(&prod, NULL, producer, NULL);
    pthread_create(&cons, NULL, consumer, NULL);
    pthread_join(prod, NULL);
    pthread_join(cons, NULL);

    sem_destroy(&empty_slots);
    sem_destroy(&full_slots);
    sem_destroy(&mutex);
    free(buffer);
    return 0;
}
