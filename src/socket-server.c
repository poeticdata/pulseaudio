#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "socket-server.h"

struct socket_server {
    int fd;
    char *filename;

    void (*on_connection)(struct socket_server*s, struct iochannel *io, void *userdata);
    void *userdata;

    struct mainloop_source *mainloop_source;
};

static void callback(struct mainloop_source*src, int fd, enum mainloop_io_event event, void *userdata) {
    struct socket_server *s = userdata;
    struct iochannel *io;
    int nfd;
    assert(src && fd >= 0 && fd == s->fd && event == MAINLOOP_IO_EVENT_IN && s);

    if ((nfd = accept(fd, NULL, NULL)) < 0) {
        fprintf(stderr, "accept(): %s\n", strerror(errno));
        return;
    }

    if (!s->on_connection) {
        close(nfd);
        return;
    }

    io = iochannel_new(mainloop_source_get_mainloop(src), nfd, nfd);
    assert(io);
    s->on_connection(s, io, s->userdata);
}

struct socket_server* socket_server_new(struct mainloop *m, int fd) {
    struct socket_server *s;
    assert(m && fd >= 0);
    
    s = malloc(sizeof(struct socket_server));
    assert(s);
    s->fd = fd;
    s->filename = NULL;
    s->on_connection = NULL;
    s->userdata = NULL;

    s->mainloop_source = mainloop_source_new_io(m, fd, MAINLOOP_IO_EVENT_IN, callback, s);
    assert(s->mainloop_source);
    
    return s;
}

struct socket_server* socket_server_new_unix(struct mainloop *m, const char *filename) {
    int fd = -1;
    struct sockaddr_un sa;
    struct socket_server *s;
    
    assert(m && filename);

    if ((fd = socket(PF_LOCAL, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "socket(): %s\n", strerror(errno));
        goto fail;
    }

    sa.sun_family = AF_LOCAL;
    strncpy(sa.sun_path, filename, sizeof(sa.sun_path)-1);
    sa.sun_path[sizeof(sa.sun_path) - 1] = 0;

    if (bind(fd, (struct sockaddr*) &sa, SUN_LEN(&sa)) < 0) {
        fprintf(stderr, "bind(): %s\n", strerror(errno));
        goto fail;
    }

    if (listen(fd, 5) < 0) {
        fprintf(stderr, "listen(): %s\n", strerror(errno));
        goto fail;
    }

    s = socket_server_new(m, fd);
    assert(s);

    s->filename = strdup(filename);
    assert(s->filename);

    return s;
                                                                                                                                                                         
fail:
    if (fd >= 0)
        close(fd);

    return NULL;
}

struct socket_server* socket_server_new_ipv4(struct mainloop *m, uint32_t address, uint16_t port) {
    int fd = -1;
    struct sockaddr_in sa;
    int on = 1;

    assert(m && port);

    if ((fd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "socket(): %s\n", strerror(errno));
        goto fail;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
        fprintf(stderr, "setsockopt(): %s\n", strerror(errno));
    
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(address);

    if (bind(fd, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
        fprintf(stderr, "bind(): %s\n", strerror(errno));
        goto fail;
    }

    if (listen(fd, 5) < 0) {
        fprintf(stderr, "listen(): %s\n", strerror(errno));
        goto fail;
    }

    return socket_server_new(m, fd);
    
fail:
    if (fd >= 0)
        close(fd);

    return NULL;
}

void socket_server_free(struct socket_server*s) {
    assert(s);
    close(s->fd);

    if (s->filename) {
        unlink(s->filename);
        free(s->filename);
    }

    mainloop_source_free(s->mainloop_source);
    
    free(s);
}

void socket_server_set_callback(struct socket_server*s, void (*on_connection)(struct socket_server*s, struct iochannel *io, void *userdata), void *userdata) {
    assert(s);

    s->on_connection = on_connection;
    s->userdata = userdata;
}
