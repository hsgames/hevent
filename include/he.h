#ifndef HE_H
#define HE_H

#include <time.h>

#define HE_OK 0
#define HE_ERR -1

#define HE_NONE 0
#define HE_READABLE 1
#define HE_WRITABLE 2

#define HE_NOTUSED(V) ((void) V)

#ifdef __cplusplus
extern "C" {
#endif

struct he_event_loop;

typedef void he_file_proc(struct he_event_loop *event_loop, 
    int fd, void *client_data, int mask);
typedef int he_update_proc(struct he_event_loop *event_loop, 
    void *client_data);

typedef struct he_file_event {
    int mask;
    he_file_proc *rfile_proc;
    he_file_proc *wfile_proc;
    void *client_data;
} he_file_event;

typedef struct he_fired_event {
    int fd;
    int mask;
} he_fired_event;

typedef struct he_update_info {
    long when_sec;
    long when_ms;
    long long update_ms;
    time_t last_time;
    he_update_proc *proc;
    void *client_data;
} he_update_info;

typedef struct he_event_loop {
    int setsize;
    he_file_event *events;
    he_fired_event *fired;
    he_update_info ui;
    int stop;
    void *apidata;
} he_event_loop;

he_event_loop *he_create_event_loop(int setsize, long long update_ms,
    he_update_proc *proc, void *client_data);
void he_delete_event_loop(he_event_loop *event_loop);
void he_stop(he_event_loop *event_loop);
int he_create_file_event(he_event_loop *event_loop, int fd, int mask,
        he_file_proc *proc, void *client_data);
void he_delete_file_event(he_event_loop *event_loop, int fd, int mask);
int he_process_events(he_event_loop *event_loop);
void he_main(he_event_loop *event_loop);

#ifdef __cplusplus
}
#endif

#endif
