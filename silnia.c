#include <stdio.h>
#include <stdlib.h>
#include "cacti.h"

#define MSG_CREATE_NEXT 1
#define MSG_CHILD_CREATED 2

static void hello_zero(void **stateptr, size_t nbytes, void *data);
static void hello(void **stateptr, size_t nbytes, void *data);
static void create_next(void **stateptr, size_t nbytes, void *data);
static void child_created(void **stateptr, size_t nbytes, void *data);

static act_t prompts_zero[] = {hello_zero, create_next, child_created};
static act_t prompts[] = {hello, create_next, child_created};

static role_t role_zero = {3, prompts_zero};
static role_t role = {3, prompts};

static long n;

// Only for the first actor
static void hello_zero(void **stateptr, size_t nbytes, void *data) {
    (void)stateptr;
    (void)nbytes;
    (void)data;
}

static void hello(void **stateptr, size_t nbytes, void *data) {
    (void)stateptr;
    (void)nbytes;
    (void)data;
    
    // Informing my parent that I have been created
    message_t msg_child_created;
    msg_child_created.message_type = MSG_CHILD_CREATED;
    msg_child_created.data = (void*)actor_id_self();
    send_message((actor_id_t)data, msg_child_created);
}

// data contains a pair (k, fact)
// Creating the next actor
static void create_next(void **stateptr, size_t nbytes, void *data) {
    (void)nbytes;
    *stateptr = data;
    long k = ((long*)data)[0];
    long fact = ((long*)data)[1];

    if (k < n) {
        message_t msg_spawn;
        msg_spawn.message_type = MSG_SPAWN;
        msg_spawn.data = (void*)&role;
        send_message(actor_id_self(), msg_spawn);
    }
    else {
        printf("%ld\n", fact);
        message_t msg_die;
        msg_die.message_type = MSG_GODIE;
        send_message(actor_id_self(), msg_die);
        free(*stateptr);
    }
}

// Sending partial solution to the next actor (child)
static void child_created(void **stateptr, size_t nbytes, void *data) {
    (void)nbytes;
    (void)data;
    long k = ((long*)*stateptr)[0];
    long fact = ((long*)*stateptr)[1];
    free(*stateptr);

    message_t msg_next;
    msg_next.message_type = MSG_CREATE_NEXT;
    msg_next.data = malloc(sizeof(long) * 2);
    ((long*)msg_next.data)[0] = k + 1;
    ((long*)msg_next.data)[1] = fact * (k + 1);
    send_message((actor_id_t)data, msg_next);

    message_t msg_die;
    msg_die.message_type = MSG_GODIE;
    send_message(actor_id_self(), msg_die);
}

int main(){
    scanf("%ld", &n);
    long *base_case = malloc(sizeof(long) * 2);
    base_case[0] = 0;
    base_case[1] = 1;

    message_t msg;
    msg.message_type = MSG_CREATE_NEXT;
    msg.data = base_case;

    // Creating the first actor
    actor_id_t id;
    actor_system_create(&id, &role_zero);
    send_message(id, msg);
    actor_system_join(id);
    return 0;
}
