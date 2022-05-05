#include <sys/epoll.h>

typedef struct he_api_state {
    int epfd;
    struct epoll_event *events;
} he_api_state;

static int he_api_create(he_event_loop *event_loop) 
{
    he_api_state *state = malloc(sizeof(he_api_state));

    if (!state) return -1;
    state->events = malloc(sizeof(struct epoll_event) * event_loop->setsize);
    if (!state->events) {
        free(state);
        return -1;
    }
    state->epfd = epoll_create(1024);
    if (state->epfd == -1) {
        free(state->events);
        free(state);
        return -1;
    }
    event_loop->apidata = state;
    return 0;
}

static void he_api_free(he_event_loop *event_loop) 
{
    he_api_state *state = event_loop->apidata;

    close(state->epfd);
    free(state->events);
    free(state);
}

static int he_api_add_event(he_event_loop *event_loop, int fd, int mask) 
{
    he_api_state *state = event_loop->apidata;
    struct epoll_event ee = {0};
    int op = event_loop->events[fd].mask == HE_NONE ?
            EPOLL_CTL_ADD : EPOLL_CTL_MOD;

    ee.events = 0;
    mask |= event_loop->events[fd].mask;
    if (mask & HE_READABLE) ee.events |= EPOLLIN;
    if (mask & HE_WRITABLE) ee.events |= EPOLLOUT;
    ee.data.fd = fd;
    if (epoll_ctl(state->epfd, op, fd, &ee) == -1) return -1;
    return 0;
}

static void he_api_del_event(he_event_loop *event_loop, int fd, int delmask) 
{
    he_api_state *state = event_loop->apidata;
    struct epoll_event ee = {0};
    int mask = event_loop->events[fd].mask & (~delmask);

    ee.events = 0;
    if (mask & HE_READABLE) ee.events |= EPOLLIN;
    if (mask & HE_WRITABLE) ee.events |= EPOLLOUT;
    ee.data.fd = fd;
    if (mask != HE_NONE) {
        epoll_ctl(state->epfd, EPOLL_CTL_MOD, fd, &ee);
    } else {
        epoll_ctl(state->epfd, EPOLL_CTL_DEL, fd, &ee);
    }
}

static int he_api_poll(he_event_loop *event_loop, int timeout) 
{
    he_api_state *state = event_loop->apidata;
    int retval, numevents = 0;

    retval = epoll_wait(state->epfd, state->events, event_loop->setsize, timeout);
    if (retval > 0) {
        int j;

        numevents = retval;
        for (j = 0; j < numevents; j++) {
            int mask = 0;
            struct epoll_event *e = state->events + j;

            if (e->events & EPOLLIN) mask |= HE_READABLE;
            if (e->events & EPOLLOUT) mask |= HE_WRITABLE;
            if (e->events & EPOLLERR) mask |= HE_WRITABLE | HE_READABLE;
            if (e->events & EPOLLHUP) mask |= HE_WRITABLE | HE_READABLE;
            event_loop->fired[j].fd = e->data.fd;
            event_loop->fired[j].mask = mask;
        }
    }
    return numevents;
}
