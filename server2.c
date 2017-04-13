#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <pthread.h>

#include <libavutil/timestamp.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#include "segment.h"
#include "buffer.h"
#include "publisher.h"

#define BUFFER_SECS 30


struct ReadInfo {
    struct PublisherContext *pub;
    char *in_filename;
};

struct WriteInfo {
    struct PublisherContext *pub;
    int thread_id;
};

struct AcceptInfo {
    struct PublisherContext *pub;
    AVFormatContext *ifmt_ctx;
    const char *out_uri;
};

void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;
    printf("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}


void *read_thread(void *arg)
{
    struct ReadInfo *info = (struct ReadInfo*) arg;
    AVFormatContext *ifmt_ctx = NULL;
    char *in_filename = info->in_filename;
    int ret;
    int i;
    int video_idx;
    int id = 0;
    struct Segment *seg = NULL;
    int64_t pts, now, start;
    AVPacket pkt;
    AVStream *in_stream;
    AVRational tb;
    tb.num = 1;
    tb.den = AV_TIME_BASE;


    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0))) {
        fprintf(stderr, "Could not open stdin\n");
        goto end;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
        fprintf(stderr, "Could not get input stream info\n");
        goto end;
    }


    printf("Finding video stream.\n");

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        printf("Checking stream %d\n", i);
        AVStream *stream = ifmt_ctx->streams[i];
        printf("Got stream\n");
        AVCodecContext *avctx = avcodec_alloc_context3(NULL);
        if (!avctx)
            return NULL;
        ret = avcodec_parameters_to_context(avctx, stream->codecpar);
        if (ret < 0) {
            return NULL;
        }
        AVCodecParameters *params = stream->codecpar;
        printf("Got params\n");
        // Segfault here ↓
        enum AVMediaType type = params->codec_type;
        //if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        printf("Got type\n");
        if (type == AVMEDIA_TYPE_VIDEO) {
            video_idx = i;
            break;
        }
    }
    start = av_gettime_relative() - BUFFER_SECS * AV_TIME_BASE; // read first BUFFER seconds fast

    for (;;) {
        //printf("Reading packet\n");
        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0) {
            break;
        }
        in_stream = ifmt_ctx->streams[pkt.stream_index];
        if (pkt.pts == AV_NOPTS_VALUE) {
            pkt.pts = 0;
        }
        if (pkt.dts == AV_NOPTS_VALUE) {
            pkt.dts = 0;
        }
        pts = av_rescale_q(pkt.pts, in_stream->time_base, tb);
        now = av_gettime_relative() - start;

        //log_packet(ifmt_ctx, &pkt);
        while (pts > now) {
            usleep(1000);
            now = av_gettime_relative() - start;
        }

        if ((pkt.flags & AV_PKT_FLAG_KEY && pkt.stream_index == video_idx) || !seg) {
            if (seg) {
                segment_close(seg);
                buffer_push_segment(info->pub->buffer, seg);
                printf("New segment pushed.\n");
                publish(info->pub);
            }
            printf("starting new segment\n");
            segment_init(&seg, ifmt_ctx);
            seg->id = id++;
            printf("segment id = %d\n", seg->id);
        }
        //printf("writing frame\n");
        segment_ts_append(seg, pkt.dts, pkt.pts);
        ret = av_write_frame(seg->fmt_ctx, &pkt);
        av_packet_unref(&pkt);
        if (ret < 0) {
            printf("write frame failed\n");
        }

    }
    segment_close(seg);
    buffer_push_segment(info->pub->buffer, seg);
    printf("Finals segment pushed.\n");
    publish(info->pub);

end:

    avformat_close_input(&ifmt_ctx);
    printf("Freed buffer\n");


    /* close output */

    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
    }

    // signal to everyone else that the stream ended
    info->pub->shutdown = 1;

    return NULL;
}

void write_segment(struct Client *c)
{
    struct Segment *seg = buffer_peek_segment(c->buffer);
    int ret;
    int pkt_count = 0;
    if (seg) {
        AVFormatContext *fmt_ctx;
        AVIOContext *avio_ctx;
        AVPacket pkt;
        struct AVIOContextInfo info;
        client_set_state(c, BUSY);
        c->current_segment_id = seg->id;
        printf("Writing segment, size: %zu, id: %d, client id: %d ofmt_ctx: %p, pb: %p\n", seg->size, seg->id, c->id, c->ofmt_ctx, c->ofmt_ctx->pb); // 0: 0xbf0600 1: 0xbf0600 0x1edf340 0x1e5f600
        info.buf = seg->buf;
        info.left = seg->size;

        if (!(fmt_ctx = avformat_alloc_context())) {
            ret = AVERROR(ENOMEM);
            printf("NOMEM\n");
            return;
        }

        avio_ctx = avio_alloc_context(c->avio_buffer, AV_BUFSIZE, 0, &info, &segment_read, NULL, NULL);

        fmt_ctx->pb = avio_ctx;
        ret = avformat_open_input(&fmt_ctx, NULL, seg->ifmt, NULL);
        if (ret < 0) {
            fprintf(stderr, "Could not open input\n");
            return;
        }
        ret = avformat_find_stream_info(fmt_ctx, NULL);
        if (ret < 0) {
            fprintf(stderr, "Could not find stream information\n");
            return;
        }

        for (;;) {
            ret = av_read_frame(fmt_ctx, &pkt);
            if (ret < 0) {
                break;
            }
            //printf("read frame\n");
            pkt.dts = seg->ts[pkt_count];
            pkt.pts = seg->ts[pkt_count + 1];
            pkt_count += 2;
            //log_packet(fmt_ctx, &pkt);
            ret = av_write_frame(c->ofmt_ctx, &pkt);
            av_packet_unref(&pkt);
            if (ret < 0) {
                printf("write_frame to client failed, disconnecting...\n");
                avformat_close_input(&fmt_ctx);
                client_disconnect(c);
                return;
            }
            //printf("wrote frame to client\n");
        }
        avformat_close_input(&fmt_ctx);
        avformat_free_context(fmt_ctx);
        av_free(avio_ctx);
        buffer_drop_segment(c->buffer);
        client_set_state(c, WRITABLE);
    } else {
        buffer_set_state(c->buffer, WAIT);
    }
}

void *accept_thread(void *arg)
{
    struct AcceptInfo *info = (struct AcceptInfo*) arg;
    const char *out_uri = info->out_uri;
    char *status;
    char *method, *resource;
    AVIOContext *client;
    AVIOContext *server = NULL;
    AVFormatContext *ofmt_ctx;
    AVOutputFormat *ofmt;
    AVDictionary *options = NULL;
    AVDictionary *mkvoptions = NULL;
    AVStream *in_stream, *out_stream;
    AVCodecContext *codec_ctx;
    int ret, i, reply_code, handshake, return_status;

    if ((ret = av_dict_set(&options, "listen", "2", 0)) < 0) {
        fprintf(stderr, "Failed to set listen mode for server: %s\n", av_err2str(ret));
        return NULL;
    }

    if ((ret = av_dict_set_int(&options, "listen_timeout", 1000, 0)) < 0) {
        fprintf(stderr, "Failed to set listen timeout for server: %s\n", av_err2str(ret));
        return NULL;
    }

    if ((ret = av_dict_set_int(&options, "timeout", 20000, 0)) < 0) {
        fprintf(stderr, "Failed to set listen timeout for server: %s\n", av_err2str(ret));
        return NULL;
    }

    if ((ret = avio_open2(&server, out_uri, AVIO_FLAG_WRITE, NULL, &options)) < 0) {
        fprintf(stderr, "Failed to open server: %s\n", av_err2str(ret));
        return NULL;
    }

    for (;;) {
        if (info->pub->shutdown)
            break;
        status = publisher_gen_status_json(info->pub);
        printf(status);
        free(status);
        reply_code = 200;
        printf("Accepting new clients...\n");
        client = NULL;
        if ((ret = avio_accept(server, &client)) < 0) {
            printf("Error or timeout\n");
            printf("ret: %d\n", ret);
            continue;
        }
        //printf("No error or timeout\n");
        //printf("ret: %d\n", ret);


        // Append client to client list
        client->seekable = 0;
        return_status = 0;
        if ((ret = av_dict_set(&mkvoptions, "live", "1", 0)) < 0) {
            fprintf(stderr, "Failed to set live mode for matroska: %s\n", av_err2str(ret));
            continue;
        }
        if (publisher_reserve_client(info->pub)) {
            printf("No more slots free\n");
            reply_code = 503;
        }

        while ((handshake = avio_handshake(client)) > 0) {
            av_opt_get(client, "method", AV_OPT_SEARCH_CHILDREN, &method);
            av_opt_get(client, "resource", AV_OPT_SEARCH_CHILDREN, &resource);
            printf("method: %s resource: %s\n", method, resource);
            if (method && strlen(method) && strncmp("GET", method, 3)) {
                reply_code = 400;
            }
            if (resource && strlen(resource) && !strncmp("/status", resource, 7)) {
                return_status = 1;
            }
            free(method);
            free(resource);
        }

        if (handshake < 0) {
            reply_code = 400;
        }

        if ((ret = av_opt_set_int(client, "reply_code", reply_code, AV_OPT_SEARCH_CHILDREN)) < 0) {
            av_log(client, AV_LOG_ERROR, "Failed to set reply_code: %s.\n", av_err2str(ret));
            continue;
        }

        if (reply_code != 200) {
            publisher_cancel_reserve(info->pub);
            avio_close(client);
            continue;
        }

        if (return_status) {
            avio_close(client);
            continue;
        }

        avformat_alloc_output_context2(&ofmt_ctx, NULL, "matroska", NULL);
        printf("allocated new ofmt_ctx: %p\n", ofmt_ctx);

        if (!ofmt_ctx) {
            fprintf(stderr, "Could not create output context\n");
            continue;
        }
        ofmt_ctx->flags |= AVFMT_FLAG_GENPTS;
        ofmt = ofmt_ctx->oformat;
        ofmt->flags &= AVFMT_NOFILE;

        for (i = 0; i < info->ifmt_ctx->nb_streams; i++)
        {
            in_stream = info->ifmt_ctx->streams[i];
            codec_ctx = avcodec_alloc_context3(NULL);
            avcodec_parameters_to_context(codec_ctx, in_stream->codecpar);
            out_stream = avformat_new_stream(ofmt_ctx, codec_ctx->codec);
            avcodec_free_context(&codec_ctx);
            //avcodec_parameters_to_context(out_stream->codec, in_stream->codecpar);
            if (!out_stream) {
                fprintf(stdout, "Failed allocating output stream\n");
                continue;
            }
            ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
            if (ret < 0) {
                fprintf(stderr, "Failed to copy context from input to output stream codec context\n");
                continue;
            }
            av_dict_copy(&out_stream->metadata, in_stream->metadata, 0);
            printf("Allocated output stream.\n");
            /*out_stream->codec->codec_tag = 0;
            if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
                out_stream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER; */
        }

        ofmt_ctx->pb = client;
        ret = avformat_write_header(ofmt_ctx, &mkvoptions);
        if (ret < 0) {
            fprintf(stderr, "Error occurred when opening output output\n");
            continue;
        }
        publisher_add_client(info->pub, ofmt_ctx);
        ofmt_ctx = NULL;
        printf("Accepted new client! ofmt_ctx: %p pb: %p\n", ofmt_ctx, client);

    }

    avio_close(server);
    printf("Shut down http server.\n");

    return NULL;
}


void *write_thread(void *arg)
{
    struct WriteInfo *info = (struct WriteInfo*) arg;
    int i, nb_free;
    struct Client *c;
    for (;;) {
        nb_free = 0;
        usleep(500000);
        printf("Checking clients, thread: %d\n", info->thread_id);
        for (i = 0; i < MAX_CLIENTS; i++) {
            c = &info->pub->subscribers[i];
            //client_print(c);
            switch(c->state) {
            case WRITABLE:
                write_segment(c);

                if (info->pub->shutdown && info->pub->current_segment_id == c->current_segment_id) {
                    client_disconnect(c);
                }
                continue;
            case FREE:
                nb_free++;
                continue;
            default:
                continue;
            }
        }
        if (info->pub->shutdown && nb_free == MAX_CLIENTS)
            break;
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    struct ReadInfo rinfo;
    struct AcceptInfo ainfo;
    struct WriteInfo *winfos;
    struct PublisherContext *pub;
    int ret, i;
    pthread_t r_thread, a_thread;
    pthread_t *w_threads;

    AVFormatContext *ifmt_ctx = NULL;

    av_register_all();
    avformat_network_init();

    publisher_init(&pub);
    rinfo.in_filename = argv[1];
    rinfo.pub = pub;

    if ((ret = avformat_open_input(&ifmt_ctx, argv[1], 0, 0))) {
        fprintf(stderr, "Could not open stdin\n");
        return 1;
    }

    ainfo.out_uri = "http://0:8080";
    ainfo.ifmt_ctx = ifmt_ctx;
    ainfo.pub = pub;

    w_threads = (pthread_t*) malloc(sizeof(pthread_t) * pub->nb_threads);
    winfos = (struct WriteInfo*) malloc(sizeof(struct WriteInfo) * pub->nb_threads);

    //pthread_create(&a_thread, NULL, accept_thread, &ainfo);
    pthread_create(&r_thread, NULL, read_thread, &rinfo);
    for (i = 0; i < pub->nb_threads; i++) {
        winfos[i].pub = pub;
        winfos[i].thread_id = i;
        pthread_create(&w_threads[i], NULL, write_thread, &winfos[i]);
    }

    //write_thread(&winfo);
    accept_thread(&ainfo);
    //read_thread(&rinfo);


    pthread_join(r_thread, NULL);
    for (i = 0; i < pub->nb_threads; i++) {
        pthread_join(w_threads[i], NULL);
    }

    avformat_close_input(&ifmt_ctx);
    publisher_free(pub);
    free(pub->buffer);
    free(pub->fs_buffer);
    free(pub);
    return 0;

}
