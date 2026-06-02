#include <sys/process.h>
#include <mm/kmalloc.h>
#include <lib/string.h>

uint32_t process_get_current_pid(void) {
    return 0;
}

process_fd_socket_t *process_socket_create(void) {
    process_fd_socket_t *sock = (process_fd_socket_t *)kmalloc(sizeof(process_fd_socket_t));
    if (!sock) return NULL;
    memset(sock, 0, sizeof(process_fd_socket_t));
    wait_queue_init(&sock->accept_waitq);
    wait_queue_init(&sock->rx_waitq);
    return sock;
}

void process_socket_release(process_fd_socket_t *sock) {
    if (!sock) return;
    kfree(sock);
}
