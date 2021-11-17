#include <pthread.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <signal.h>
#include "cacti.h"

//// Message queue ------------------------------------------------------------

typedef struct message_node {
    message_t message;
    struct message_node *next;
} message_node_t;

typedef struct message_queue {
    message_node_t *head;
    message_node_t *tail;
    size_t size;
} message_queue_t;

static void init_message_node(message_node_t **node_ptr, message_t message) {
    *node_ptr = malloc(sizeof(message_node_t));
    message_node_t *node = *node_ptr;
    node->message = message;
    node->next = NULL;
}

static void free_message_node(message_node_t *node) {
    free(node);
}

static void init_message_queue(message_queue_t **queue_ptr) {
    *queue_ptr = malloc(sizeof(message_queue_t));
    message_queue_t *queue = *queue_ptr;
    queue->head = NULL;
    queue->tail = NULL;
    queue->size = 0;
}

static void insert_message(message_queue_t *queue, message_t message) {
    if (queue->size > 0) {
        // Non-empty queue
        init_message_node(&queue->tail->next, message);
        queue->tail = queue->tail->next;
    }
    else {
        // Empty queue
        init_message_node(&queue->tail, message);
        queue->head = queue->tail;
    }
    queue->size++;
}

static message_t pop_message(message_queue_t *queue) {
    message_t msg = queue->head->message;
    message_node_t *head_to_free = queue->head;
    queue->head = queue->head->next;
    free_message_node(head_to_free);

    // if the queue becomes empty
    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    queue->size--;
    return msg;
}

static void free_message_queue(message_queue_t *queue) {
    while (queue->size > 0) {
        pop_message(queue);
    }
    free(queue);
}

//// Actor --------------------------------------------------------------------

typedef struct actor {
    actor_id_t id;
    role_t *role;

    message_queue_t *queue;
    void *state;

    bool is_on_queue;
    bool is_dead;
} actor_t;

static void init_actor(actor_t **actor_ptr, actor_id_t id, role_t *role) {
    *actor_ptr = malloc(sizeof(actor_t));
    actor_t *actor = *actor_ptr;

    actor->id = id;
    actor->role = role;

    init_message_queue(&actor->queue);
    actor->state = NULL;

    actor->is_on_queue = false;
    actor->is_dead = false;
}

static void free_actor(actor_t *actor) {
    free_message_queue(actor->queue);
    free(actor);
}

//// Actor queue --------------------------------------------------------------

typedef struct actor_node {
    actor_t *actor;
    struct actor_node *next;
} actor_node_t;

typedef struct actor_queue {
    actor_node_t *head;
    actor_node_t *tail;
    size_t size;
} actor_queue_t;

static void init_actor_node(actor_node_t **node_ptr, actor_t *actor) {
    *node_ptr = malloc(sizeof(actor_node_t));
    actor_node_t *node = *node_ptr;
    node->actor = actor;
    node->next = NULL;
}

static void free_actor_node(actor_node_t *node) {
    free(node);
}

static void init_actor_queue(actor_queue_t **queue_ptr) {
    *queue_ptr = malloc(sizeof(actor_queue_t));
    actor_queue_t *queue = *queue_ptr;
    queue->head = NULL;
    queue->tail = NULL;
    queue->size = 0;
}

static void insert_actor(actor_queue_t *queue, actor_t *actor) {
    if (!actor->is_on_queue) {
        if (queue->size > 0) {
            // Non-empty queue
            init_actor_node(&queue->tail->next, actor);
            queue->tail = queue->tail->next;
        }
        else {
            // Empty queue
            init_actor_node(&queue->tail, actor);
            queue->head = queue->tail;
        }
        queue->size++;
    }
}

static actor_t *pop_actor(actor_queue_t *queue) {
    actor_t *actor = queue->head->actor;
    actor_node_t *head_to_free = queue->head;
    queue->head = queue->head->next;
    free_actor_node(head_to_free);

    // if the queue becomes empty
    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    queue->size--;
    return actor;
}

static void free_actor_queue(actor_queue_t *queue) {
    while (queue->size > 0) {
        pop_actor(queue);
    }
    free(queue);
}

//// Actor System -------------------------------------------------------------

typedef struct actor_system {
    pthread_mutex_t lock;
    pthread_cond_t empty_cond;

    actor_queue_t *queue;
    size_t no_of_actors;
    size_t no_of_finished_actors;

    bool spawning_possible;
    actor_t *actors[CAST_LIMIT];
    pthread_t threads[POOL_SIZE];
    actor_id_t thread_to_id[POOL_SIZE];
} actor_system_t;

static void free_actor_system(actor_system_t *actor_system) {
    for (size_t i = 0; i < actor_system->no_of_actors; i++) {
        free_actor(actor_system->actors[i]);
    }
    free_actor_queue(actor_system->queue);
    free(actor_system);
}

static actor_system_t *actor_system;

//// Predefined messages handling ---------------------------------------------

static void handle_death(actor_t *actor) {
    pthread_mutex_lock(&actor_system->lock);
    actor->is_dead = true;
    pthread_mutex_unlock(&actor_system->lock);
}

// data - pointer to role_t
static void handle_spawn(actor_t *actor, void *data) {
    actor_id_t new_id;
    pthread_mutex_lock(&actor_system->lock);
    if (actor_system->no_of_actors < CAST_LIMIT && actor_system->spawning_possible) {
        init_actor(&actor_system->actors[actor_system->no_of_actors], actor_system->no_of_actors, data);
        new_id = actor_system->no_of_actors++;
        pthread_mutex_unlock(&actor_system->lock);

        message_t msg_hello;
        msg_hello.message_type = MSG_HELLO;
        msg_hello.data = (void *) actor->id;
        send_message(new_id, msg_hello);
        return;
    }
    pthread_mutex_unlock(&actor_system->lock);
}

//// Worker threads -----------------------------------------------------------

static void *worker_thread(void *arg) {
    (void)arg;
    while (true) {
        actor_t *actor = NULL;
        pthread_mutex_lock(&actor_system->lock);

        // Wait until the queue is not empty (or all the actors have finished)
        while (actor_system->queue->size == 0 &&
               actor_system->no_of_actors != actor_system->no_of_finished_actors) {
            pthread_cond_wait(&actor_system->empty_cond, &actor_system->lock);
        }

        // Ending the thread if all actors have finished work
        if (actor_system->no_of_finished_actors >= actor_system->no_of_actors &&
            actor_system->queue->size == 0) { // The second condition is necessary for sigint handling
            pthread_mutex_unlock(&actor_system->lock);
            pthread_cond_signal(&actor_system->empty_cond);
            return NULL;
        }

        // Getting the message to handle
        actor = pop_actor(actor_system->queue);
        message_t msg = pop_message(actor->queue);
        pthread_mutex_unlock(&actor_system->lock);

        // Remembering the actor id for the thread
        pthread_t tid = pthread_self();
        for (int i = 0; i < POOL_SIZE; i++) {
            if (actor_system->threads[i] == tid) {
                actor_system->thread_to_id[i] = actor->id;
                break;
            }
        }

        // Handling the message
        switch (msg.message_type) {
            case MSG_GODIE:
                handle_death(actor);
                break;
            case MSG_SPAWN:
                handle_spawn(actor, msg.data);
                break;
            default:
                actor->role->prompts[msg.message_type](&actor->state, msg.nbytes, msg.data);
        }

        pthread_mutex_lock(&actor_system->lock);
        actor->is_on_queue = false;

        // Putting the actor back in the queue
        if (actor->queue->size > 0) {
            insert_actor(actor_system->queue, actor);
            actor->is_on_queue = true;
            pthread_cond_signal(&actor_system->empty_cond);
        }

        // Checking whether the actor is finished
        if (actor->is_dead && actor->queue->size == 0) {
            actor_system->no_of_finished_actors++;
        }
        pthread_mutex_unlock(&actor_system->lock);
    }
}

//// Signal handling ----------------------------------------------------------

static void sigint_handler() {
    actor_system->spawning_possible = false;
    for (size_t i = 0; i < actor_system->no_of_actors; i++) {
        actor_system->actors[i]->is_dead = true;
        if (!actor_system->actors[i]->is_on_queue) {
            actor_system->no_of_finished_actors++;
        }
    }
    pthread_cond_signal(&actor_system->empty_cond);
    for (int i = 0; i < POOL_SIZE; i++) {
        pthread_join(actor_system->threads[i], NULL);
    }
    exit(1);
}

//// Header implementation ----------------------------------------------------

actor_id_t actor_id_self() {
    pthread_t tid = pthread_self();
    for (int i = 0; i < POOL_SIZE; i++) {
        if (actor_system->threads[i] == tid) {
            return actor_system->thread_to_id[i];
        }
    }
    return 0;
}

int actor_system_create(actor_id_t *actor, role_t *const role) {
    // Creating actor_system and its pthread objects
    if ((actor_system = malloc(sizeof(actor_system_t))) == NULL) {
        return -1;
    }
    pthread_mutex_init(&actor_system->lock, NULL);
    pthread_cond_init(&actor_system->empty_cond, NULL);

    // Creating the first actor and the actor queue
    *actor = 0;
    init_actor(&actor_system->actors[0], *actor, role);

    init_actor_queue(&actor_system->queue);
    actor_system->no_of_actors = 1;
    actor_system->no_of_finished_actors = 0;
    actor_system->spawning_possible = true;

    struct sigaction sigint_handling;
    sigint_handling.sa_handler = sigint_handler;
    sigaction(SIGINT, &sigint_handling, NULL);

    // Starting the worker threads
    for (int i = 0; i < POOL_SIZE; i++) {
        pthread_create(&actor_system->threads[i], NULL, worker_thread, NULL);
    }

    message_t msg_hello;
    msg_hello.message_type = MSG_HELLO;
    send_message(0, msg_hello);
    return 0;
}

void actor_system_join(actor_id_t actor) {
    // Checking whether actor is a member of the actor system
    pthread_mutex_lock(&actor_system->lock);
    if ((size_t)actor >= actor_system->no_of_actors) {
        pthread_mutex_unlock(&actor_system->lock);
        return;
    }
    pthread_mutex_unlock(&actor_system->lock);

    // Joining worker threads
    for (int i = 0; i < POOL_SIZE; i++) {
        pthread_join(actor_system->threads[i], NULL);
    }

    // Freeing the allocated memory
    free_actor_system(actor_system);
}

int send_message(actor_id_t actor, message_t message) {
    pthread_mutex_lock(&actor_system->lock);
    if ((size_t)actor >= actor_system->no_of_actors) {
        pthread_mutex_unlock(&actor_system->lock);
        return -2; // If the actor doesn't exist
    }
    actor_t *receiver_actor = actor_system->actors[actor];
    if (!receiver_actor->is_dead && receiver_actor->queue->size < ACTOR_QUEUE_LIMIT) {
        // Inserting the message into the actor's message queue
        insert_message(receiver_actor->queue, message);
        insert_actor(actor_system->queue, receiver_actor);
        receiver_actor->is_on_queue = true;

        // Informing the worker threads about a new task
        pthread_cond_signal(&actor_system->empty_cond);
        pthread_mutex_unlock(&actor_system->lock);
        return 0;
    }
    else if (receiver_actor->is_dead) {
        pthread_mutex_unlock(&actor_system->lock);
        return -1; // If the actor is dead
    }
    else {
        pthread_mutex_unlock(&actor_system->lock);
        return -3; // If the actor's message queue is full
    }
}
