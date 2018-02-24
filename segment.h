#ifndef SEGMENT_H
#define SEGMENT_H

#include <libavformat/avformat.h>
#include <pthread.h>

#define AV_BUFSIZE 4096

struct AVIOContextInfo {
    char *buf;
    int left;
};

struct Segment {
    char *buf;
    AVIOContext *io_ctx;
    AVFormatContext *fmt_ctx;
    AVInputFormat *ifmt;
    size_t size;
    int64_t *ts;
    size_t ts_len;
    //FILE *stream;
    int nb_read;
    unsigned char *avio_buffer;
    int id;
    pthread_mutex_t nb_read_lock;
};

void save_segment(struct Segment *seg, const char *filename);

void segment_free(struct Segment *seg);

void segment_ref(struct Segment *seg);

void segment_unref(struct Segment *seg);

void segment_ts_append(struct Segment *seg, int64_t dts, int64_t pts);

int segment_write(void *opaque, unsigned char *buf, int buf_size);

int segment_read(void *opaque, unsigned char *buf, int buf_size);

void segment_close(struct Segment *seg);

void segment_init(struct Segment **seg, AVFormatContext *fmt);


#endif // SEGMENT_H
