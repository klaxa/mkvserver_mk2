#include "buffer.h"

int buffer_wrap(int x)
{
    return x % MAX_SEGMENTS;
}

void buffer_set_state(struct Buffer *buffer, enum State state)
{
    pthread_mutex_lock(&buffer->state_lock);
    buffer->state = state;
    pthread_mutex_unlock(&buffer->state_lock);
}

void buffer_init(struct Buffer *buffer)
{
    int i;
    buffer->read = 0;
    buffer->write = 0;
    buffer->nb_segs = 0;
    pthread_mutex_init(&buffer->state_lock, NULL);
    buffer_set_state(buffer, FREE);
    for (i = 0; i < MAX_SEGMENTS; i++) {
        buffer->buffer[i] = NULL;
    }

}

void buffer_push_segment(struct Buffer *buffer, struct Segment *seg)
{
    int ridx = buffer_wrap(buffer->read);
    int widx = buffer_wrap(buffer->write);
    if ((buffer->read != buffer->write) && (ridx == widx)) {
        fprintf(stderr, "Warning: dropping segment %d\n", seg->id);
        buffer_set_state(buffer, BUFFER_FULL);
        return;
    }
    buffer->buffer[widx] = seg;
    buffer->write++;
    buffer->nb_segs++;
    buffer_set_state(buffer, WRITABLE);
    segment_ref(seg);
}

struct Segment *buffer_peek_segment(struct Buffer *buffer)
{
    int idx = buffer_wrap(buffer->read);
    if (buffer->read < buffer->write) {
        struct Segment *seg = buffer->buffer[idx];
        return seg;
    }
    return NULL;
}

void buffer_drop_segment(struct Buffer *buffer)
{
    int idx = buffer_wrap(buffer->read);
    if (buffer->read < buffer->write) {
        struct Segment *seg = buffer->buffer[idx];
        buffer->read++;
        buffer->nb_segs--;
        segment_unref(seg);
        buffer->buffer[idx] = NULL;
        buffer_set_state(buffer, WRITABLE);
    }
}

struct Segment *buffer_pop_segment(struct Buffer *buffer)
{
    struct Segment *seg = buffer_peek_segment(buffer);
    buffer_drop_segment(buffer);
    return seg;
}

struct Segment *buffer_get_segment_at(struct Buffer *buffer, int pos)
{
    int idx = buffer_wrap(pos);
    if (pos < buffer->write && pos >= buffer->read - MAX_SEGMENTS && pos >= 0) {
        return buffer->buffer[idx];
    }
    return NULL;
}

void buffer_free(struct Buffer *buffer)
{
    while (buffer_pop_segment(buffer));
}
