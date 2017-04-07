#include "publisher.h"

void client_print(struct Client *c)
{
    printf("State: ");
    switch(c->buffer->state) {
    case WRITABLE:
        printf("WRITABLE");
        break;
    case WAIT:
        printf("WAIT");
        break;
    case BUSY:
        printf("BUSY");
        break;
    case BUFFER_FULL:
        printf("BUFFER_FULL");
        break;
    case FREE:
        printf("FREE");
        break;
    case RESERVED:
        printf("RESERVED");
        break;
    default:
        printf("LOL");
        break;
    }
    printf("\n");
}


void publisher_init(struct PublisherContext **pub)
{
    int i;
    *pub = (struct PublisherContext*) malloc(sizeof(struct PublisherContext));
    (*pub)->nb_threads = 4;
    (*pub)->buffer = (struct Buffer*) malloc(sizeof(struct Buffer));
    buffer_init((*pub)->buffer);
    for(i = 0; i < MAX_CLIENTS; i++) {
        struct Client *c = &(*pub)->subscribers[i];
        c->buffer = (struct Buffer*) malloc(sizeof(struct Buffer));
        buffer_init(c->buffer);
        c->id = -1;
    }

    return;
}

int publisher_reserve_client(struct PublisherContext *pub)
{
    int i;
    for(i = 0; i < MAX_CLIENTS; i++) {
        switch(pub->subscribers[i].buffer->state) {
        case FREE:
            buffer_set_state(pub->subscribers[i].buffer, RESERVED);
            return 0;
        default:
            continue;
        }
    }
    return 1;
}

void publisher_add_client(struct PublisherContext *pub, AVFormatContext *ofmt_ctx)
{
    int i, j;
    struct Segment *prebuffer_seg;
    for(i = 0; i < MAX_CLIENTS; i++) {
        switch(pub->subscribers[i].buffer->state) {
        case RESERVED:
            pub->subscribers[i].ofmt_ctx = ofmt_ctx;
            pub->subscribers[i].avio_buffer = (unsigned char*) av_malloc(AV_BUFSIZE);
            buffer_set_state(pub->subscribers[i].buffer, WRITABLE);
            /*for (j = BUFFER_SEGMENTS; j > 0; j--) {
                if ((prebuffer_seg = buffer_get_segment_at(pub->buffer, pub->buffer->write - j))) {
                    buffer_push_segment(pub->subscribers[i].buffer, prebuffer_seg);
                    printf("pushed prebuffer segment.\n");
                }
            } */
        default:
            continue;
        }
    }
}


void publisher_free(struct PublisherContext *pub)
{
    int i;
    buffer_free(pub->buffer);
    for(i = 0; i < MAX_CLIENTS; i++) {
        struct Client *c = &pub->subscribers[i];
        buffer_free(c->buffer);
    }
    return;
}

void publish(struct PublisherContext *pub)
{
    int i;
    struct Segment *seg = buffer_peek_segment(pub->buffer);
    if (seg) {
        for (i = 0; i < MAX_CLIENTS; i++) {
            switch(pub->subscribers[i].buffer->state) {
            case BUFFER_FULL:
                fprintf(stderr, "Warning: dropping segment for client %d\n", i);
                continue;
            case WAIT:
            case WRITABLE:
                buffer_push_segment(pub->subscribers[i].buffer, seg);
                continue;
            default:
                continue;

            }
        }
    }
    //if (pub->buffer->nb_segs == BUFFER_SEGMENTS) {
        buffer_drop_segment(pub->buffer);
        printf("Dropping segment from prebuffer buffer\n.");
    //}
}
