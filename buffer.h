#ifndef BUFFER_H
#define BUFFER_H

#include <pthread.h>
#include "segment.h"

#define MAX_SEGMENTS 16

enum State {FREE, RESERVED, WAIT, WRITABLE, BUSY, BUFFER_FULL};


struct Buffer {
    struct Segment *buffer[MAX_SEGMENTS]; // keeps MAX_SEGMENTS elements to buffer
    int read; // segment to read from
    int write; // segment to write to
    int nb_segs; // number of segments currently in buffer
    enum State state; // NOTE: set to WRITABLE after every freeing of a buffer, as it may be set to BUFFER_FULL in case of a full buffer
    pthread_mutex_t state_lock;
};


int buffer_wrap(int x);

void buffer_set_state(struct Buffer *buffer, enum State state);

void buffer_init(struct Buffer *buffer);

void buffer_push_segment(struct Buffer *buffer, struct Segment *seg);

struct Segment *buffer_peek_segment(struct Buffer *buffer);

void buffer_drop_segment(struct Buffer *buffer);

struct Segment *buffer_pop_segment(struct Buffer *buffer);

struct Segment *buffer_get_segment_at(struct Buffer *buffer, int pos);

void buffer_free(struct Buffer *buffer);



#endif // BUFFER_H
