#ifndef SERVER_H
#define SERVER_H

#include <libavformat/avformat.h>

#define BUFFERSIZE 16
#define MAX_CLIENTS 1024

struct AVPacketWrap {
    AVPacket *pkt;
    int hash;
    struct AVPacketWrap *next;
};


struct BufferContext {
    struct AVPacketWrap *pkts[BUFFERSIZE];
    int cur_idx;
    int nb_idx;
    int video_idx;
    struct AVPacketWrap *pos;
};


struct ReadInfo {
    AVFormatContext *ifmt_ctx;
    struct BufferContext *buffer;
    int ret;
    int64_t start;
};

struct WriteInfo {
    struct BufferContext *buffer;
    struct ClientContext *clients;
};

struct WriteGOPInfo {
    struct BufferContext *buffer;
    struct ClientContext *clients;
    struct WriteGOPInfo *info;
    int client_index;
};

struct ClientContext {
    int in_use;
    int idx;
    int nb_idx;
    struct BufferContext *buffer;
    AVFormatContext *ofmt_ctx;
    pthread_t write_thread;
};

struct AcceptInfo {
    AVFormatContext *ifmt_ctx;
    AVIOContext *server;
    struct BufferContext *buffer;
    struct ClientContext *clients;
};


void print_buffer_stats(struct BufferContext *buffer);
int buffer_push_pkt(struct BufferContext *buffer, AVPacket *pkt);
void buffer_clear_list(struct BufferContext *buffer, int i);

int get_free_spot(struct ClientContext *clients);
void remove_client(struct ClientContext *list, int idx);

void *accept_thread(void *arg);
void *write_gop_to_client(void *arg);
void *write_thread(void *arg);
void *read_thread(void *arg);


#endif
