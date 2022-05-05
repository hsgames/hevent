#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "he.h"
#include "he_epoll.c"

static void he_get_time(long *seconds, long *milliseconds)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    *seconds = tv.tv_sec;
    *milliseconds = tv.tv_usec / 1000;
}

static void he_add_milliseconds_to_now(long long milliseconds, long *sec, long *ms) 
{
    long cur_sec, cur_ms, when_sec, when_ms;

    he_get_time(&cur_sec, &cur_ms);
    when_sec = cur_sec + milliseconds / 1000;
    when_ms = cur_ms + milliseconds % 1000;
    if (when_ms >= 1000) {
        when_sec++;
        when_ms -= 1000;
    }
    *sec = when_sec;
    *ms = when_ms;
}

he_event_loop *he_create_event_loop(int setsize, long long update_ms,
    he_update_proc *proc, void *client_data) 
{
    he_event_loop *event_loop;
    int i;

    if ((event_loop = malloc(sizeof(*event_loop))) == NULL) goto err;
    event_loop->events = malloc(sizeof(he_file_event) * setsize);
    event_loop->fired = malloc(sizeof(he_fired_event) * setsize);
    if (event_loop->events == NULL || event_loop->fired == NULL) goto err;
    he_add_milliseconds_to_now(update_ms, &event_loop->ui.when_sec, &event_loop->ui.when_ms);
    event_loop->ui.update_ms = update_ms;
    event_loop->ui.last_time = time(NULL);
    event_loop->ui.proc = proc;
    event_loop->ui.client_data = client_data;
    event_loop->setsize = setsize;
    event_loop->stop = 0;
    if (he_api_create(event_loop) == -1) goto err;
    for (i = 0; i < setsize; i++)
        event_loop->events[i].mask = HE_NONE;
    return event_loop;

err:
    if (event_loop) {
        free(event_loop->events);
        free(event_loop->fired);
        free(event_loop);
    }
    return NULL;
}

void he_delete_event_loop(he_event_loop *event_loop) 
{
    he_api_free(event_loop);
    free(event_loop->events);
    free(event_loop->fired);
    free(event_loop);
}

void he_stop(he_event_loop *event_loop) 
{
    event_loop->stop = 1;
}

int he_create_file_event(he_event_loop *event_loop, int fd, int mask,
    he_file_proc *proc, void *client_data)
{
    if (fd >= event_loop->setsize) {
        errno = ERANGE;
        return HE_ERR;
    }
    he_file_event *fe = &event_loop->events[fd];

    if (he_api_add_event(event_loop, fd, mask) == -1)
        return HE_ERR;
    fe->mask |= mask;
    if (mask & HE_READABLE) fe->rfile_proc = proc;
    if (mask & HE_WRITABLE) fe->wfile_proc = proc;
    fe->client_data = client_data;
    return HE_OK;
}

void he_delete_file_event(he_event_loop *event_loop, int fd, int mask)
{
    if (fd >= event_loop->setsize) return;
    he_file_event *fe = &event_loop->events[fd];
    if (fe->mask == HE_NONE) return;
    he_api_del_event(event_loop, fd, mask);
    fe->mask = fe->mask & (~mask);
}

static int he_process_update(he_event_loop *event_loop) 
{
    int processed = 0;
    long now_sec, now_ms;
 
    he_get_time(&now_sec, &now_ms);
    if (now_sec > event_loop->ui.when_sec ||
        (now_sec == event_loop->ui.when_sec && now_ms >= event_loop->ui.when_ms)) {
        he_add_milliseconds_to_now(event_loop->ui.update_ms, 
            &event_loop->ui.when_sec, &event_loop->ui.when_ms);
        if (event_loop->ui.proc)
            processed += event_loop->ui.proc(event_loop, event_loop->ui.client_data);
    }

    return processed;  
}

int he_process_events(he_event_loop *event_loop)
{
    int processed = 0, numevents;
    int j;
    long now_sec, now_ms;
    time_t now = time(NULL);

    if (now < event_loop->ui.last_time) {
        event_loop->ui.when_sec = 0;
        event_loop->ui.when_ms = 0;
    }
    event_loop->ui.last_time = now;

    he_get_time(&now_sec, &now_ms);
    long long ms =
        (event_loop->ui.when_sec - now_sec) * 1000 +
        event_loop->ui.when_ms - now_ms;
    if (ms < 0) ms = 0;

    numevents = he_api_poll(event_loop, ms);

    processed += he_process_update(event_loop);

    for (j = 0; j < numevents; j++) {
        he_file_event *fe = &event_loop->events[event_loop->fired[j].fd];
        int mask = event_loop->fired[j].mask;
        int fd = event_loop->fired[j].fd;
        int fired = 0;

        if (fe->mask & mask & HE_READABLE) {
            fe->rfile_proc(event_loop, fd, fe->client_data, mask);
            fired++;
        }
        if (fe->mask & mask & HE_WRITABLE) {
            if (!fired || fe->wfile_proc != fe->rfile_proc) {
                fe->wfile_proc(event_loop, fd, fe->client_data, mask);
                fired++;
            }
        }
        processed++;
    }

    return processed;
}

void he_main(he_event_loop *event_loop) {
    event_loop->stop = 0;
    while (!event_loop->stop) {
        he_process_events(event_loop);
    }
}
