#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "cacti.h"

#define MSG_CALCULATE 1
#define MSG_CHILD_CREATED 2
#define MSG_ROW_FINISHED 3

static void hello_main(void **stateptr, size_t nbytes, void *data);
static void hello_children(void **stateptr, size_t nbytes, void *data);
static void child_created(void **stateptr, size_t nbytes, void *data);
static void row_finished(void **stateptr, size_t nbytes, void *data);
static void calculate(void **stateptr, size_t nbytes, void *data);

static act_t prompts_main[] = {hello_main, calculate, child_created, row_finished};
static act_t prompts_children[] = {hello_children, calculate};

static role_t role_main = {4, prompts_main};
static role_t role_child = {2, prompts_children};

static int n, k;
static int *M;
static int *T;
static int *ids;
static int *sums;

static void hello_main(void **stateptr, size_t nbytes, void *data) {
    (void)stateptr;
    (void)nbytes;
    (void)data;
    
    // An array of actor ids and sums
    ids = malloc(sizeof(actor_id_t) * k);
    sums = calloc(n, sizeof(sums));
    ids[0] = actor_id_self();

    // Creating more actors for calculations
    message_t msg_spawn;
    msg_spawn.message_type = MSG_SPAWN;
    msg_spawn.data = (void*)&role_child;
    for (int i = 1; i < k; i++) {
        send_message(actor_id_self(), msg_spawn);
    }
}

static void hello_children(void **stateptr, size_t nbytes, void *data) {
    (void)stateptr;
    (void)nbytes;
    (void)data;
    
    // Informing my parent (the main actor) that I've been created
    message_t msg_created;
    msg_created.message_type = MSG_CHILD_CREATED;
    msg_created.data = (void*)actor_id_self();
    send_message(ids[0], msg_created);
}

// One of my children has been created
// data is the id of the created child
static void child_created(void **stateptr, size_t nbytes, void *data) {
    (void)stateptr;
    (void)nbytes;
    static int no_of_actors = 1; // Only main initially
    ids[no_of_actors] = (actor_id_t)data;
    if (++no_of_actors < k) {
        return;
    }

    // Summation of rows
    for (int y = 0; y < n; y++) {
        message_t msg_calculate;
        msg_calculate.message_type = MSG_CALCULATE;
        msg_calculate.data = malloc(sizeof(int) * 3); // Coordinates and partial solution
        ((int*)msg_calculate.data)[0] = 0;
        ((int*)msg_calculate.data)[1] = y;
        ((int*)msg_calculate.data)[2] = 0;
        send_message(ids[0], msg_calculate);
    }
}

// For the main actor, after a row has been finished
static void row_finished(void **stateptr, size_t nbytes, void *data) {
    (void)stateptr;
    (void)nbytes;
    (void)data;
    static int no_of_finished_rows = 0;
    if (++no_of_finished_rows < n) {
        return;
    }

    // Printing sums
    for (int y = 0; y < n; y++) {
        printf("%d\n", sums[y]);
    }

    // Killing all actors
    for (int x = 0; x < k; x++) {
        message_t msg_die;
        msg_die.message_type = MSG_GODIE;
        send_message(ids[x], msg_die);
    }
}

// Calculate and send to next actor or to the main actor
static void calculate(void **stateptr, size_t nbytes, void *data) {
    (void)stateptr;
    (void)nbytes;
    int x = ((int*)data)[0];
    int y = ((int*)data)[1];
    int sum = ((int*)data)[2];
    free(data);
    usleep(T[y * k + x] * 1000);
    sum += M[y * k + x];

    if (x < k - 1) {
        message_t msg_calculate;
        msg_calculate.message_type = MSG_CALCULATE;
        msg_calculate.data = malloc(sizeof(int) * 3);
        ((int*)msg_calculate.data)[0] = x + 1;
        ((int*)msg_calculate.data)[1] = y;
        ((int*)msg_calculate.data)[2] = sum;
        send_message(ids[x + 1], msg_calculate);
    }
    else {
        message_t msg_finished;
        msg_finished.message_type = MSG_ROW_FINISHED;
        sums[y] = sum;
        send_message(ids[0], msg_finished);
    }
}

int main() {
    // Reading the input
    scanf("%d%d", &n, &k);
    M = malloc(sizeof(int) * k * n);
    T = malloc(sizeof(int) * k * n);
    for (int i = 0; i < n * k; i++) {
        scanf("%d%d", &M[i], &T[i]);
    }

    // Creating the first actor
    actor_id_t id;
    actor_system_create(&id, &role_main);
    actor_system_join(id);

    // Freeing the memory
    free(M);
    free(T);
    free(ids);
    free(sums);
    return 0;
}
