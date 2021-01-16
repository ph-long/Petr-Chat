#ifndef JOB_H
#define JOB_H

#include "protocol.h"
#include "server.h"

typedef struct {
    int client_fd;
    petr_header * client_message;
    char * message;
} job;

#endif