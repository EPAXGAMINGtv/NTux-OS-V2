#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <wait_queue.h>

typedef struct process_fd_socket {
    void *pcb;
    int domain;
    int is_connected;
    int is_bound;
    int is_listening;
    int tcp_closed;
    int tcp_connect_done;
    int tcp_connect_error;
    void *recv_queue;
    wait_queue_head_t accept_waitq;
    wait_queue_head_t rx_waitq;
    struct process_fd_socket *accept_queue[16];
    int accept_queue_count;
} process_fd_socket_t;

process_fd_socket_t *process_socket_create(void);
void process_socket_release(process_fd_socket_t *sock);

#endif
