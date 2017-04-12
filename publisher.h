#ifndef PUBLISHER_H
#define PUBLISHER_H

#include <libavformat/avformat.h>
#include "buffer.h"

#define MAX_CLIENTS 16
#define BUFFER_SEGMENTS 10

struct Client {
    struct AVFormatContext *ofmt_ctx;
    struct Buffer *buffer;
    unsigned char *avio_buffer;
    enum State state;
    pthread_mutex_t state_lock;
    int id;
    int current_segment_id;
};


struct PublisherContext {
    struct Client subscribers[MAX_CLIENTS];
    struct Buffer *buffer;
    struct Buffer *fs_buffer; // fast start buffer;
    int nb_threads;
    int current_segment_id;
};


void client_print(struct Client *c);

void client_disconnect(struct Client *c);

void client_set_state(struct Client *c, enum State state);


void publisher_init(struct PublisherContext **pub);

int publisher_reserve_client(struct PublisherContext *pub);

void publisher_cancel_reserve(struct PublisherContext *pub);

void publisher_add_client(struct PublisherContext *pub, AVFormatContext *ofmt_ctx);

void publisher_free(struct PublisherContext *pub);

void publish(struct PublisherContext *pub);

char *publisher_gen_status_json(struct PublisherContext *pub);

#endif // PUBLISHER_H
