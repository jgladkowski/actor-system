#ifndef CACTI_H
#define CACTI_H

#include <stddef.h>

typedef long message_type_t;

// Predefined message types
#define MSG_SPAWN (message_type_t)0x06057a6e // The actor that receives SPAWN creates a new actor
                                             // with a role (an array of message handlers) specified in the message
                                             // and becomes its parent
                                             // SPAWN's handler is predefined and can't be changed
#define MSG_GODIE (message_type_t)0x60bedead // The actor that is sent GODIE stops receiving
                                             // any further messages
                                             // GODIE's handler is predefined and can't be changed
#define MSG_HELLO (message_type_t)0x0        // This message is sent to each newly created actor
                                             // and is used for providing the actor with its parent's id
                                             // HELLO has no predefined handler and always must be implemented

#ifndef ACTOR_QUEUE_LIMIT
#define ACTOR_QUEUE_LIMIT 1024               // Max number of messages waiting in actor's queue
#endif

#ifndef CAST_LIMIT
#define CAST_LIMIT 1048576                   // Max number of actors in the system
#endif

#ifndef POOL_SIZE
#define POOL_SIZE 3                          // How many threads are used in the system
#endif

typedef struct message
{
    message_type_t message_type; // Actors will behave differently upon 
                                 // receiving messages with different types.
                                 // Actor's behaviour is defined by role_t.
    size_t nbytes;  // Length of the message
    void *data;     // Raw pointer to the bytes of the message.
} message_t;

// Actors have unique identifiers for communication purposes
typedef long actor_id_t;

// Gives the id of the actor that is using the current thread
actor_id_t actor_id_self();

// Type representing a message handler function
typedef void (*const act_t)(void **stateptr, size_t nbytes, void *data);

typedef struct role
{
    size_t nprompts;        // Number of handled message types
    act_t *prompts;         // Pointers to message handlers
} role_t;

// Creates the actor system along with the first actor.
// First actor's behaviour is defined by the role argument.
// Sets *actor to the id of the first actor.
// Returns 0 on success or a negative value otherwise.
// ------------------------------------------------------------
// IMPORTANT: It is assumed that at most one actor system exists at a given time.
// Therefore actor_system_create shouldn't be called again before actor_system_join finishes.
int actor_system_create(actor_id_t *actor, role_t *const role);

// Waits until the actor system that the provided actor belongs to finishes work.
void actor_system_join(actor_id_t actor);

// Sends a message to an actor
int send_message(actor_id_t actor, message_t message);

#endif
