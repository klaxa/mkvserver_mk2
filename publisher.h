#ifndef PUBLISHER_H
#define PUBLISHER_H

#include <libavformat/avformat.h>
#include "buffer.h"

#define MAX_CLIENTS 5
#define BUFFER_SEGMENTS 3

struct Client {
    struct AVFormatContext *ofmt_ctx;
    struct Buffer *buffer;
    unsigned char *avio_buffer;
    int id;
};


struct PublisherContext {
    struct Client subscribers[MAX_CLIENTS];
    struct Buffer *buffer;
    int nb_threads;
};


void client_print(struct Client *c);

void publisher_init(struct PublisherContext **pub);

int publisher_reserve_client(struct PublisherContext *pub);

void publisher_add_client(struct PublisherContext *pub, AVFormatContext *ofmt_ctx);

void publisher_free(struct PublisherContext *pub);

void publish(struct PublisherContext *pub);

#endif // PUBLISHER_H
