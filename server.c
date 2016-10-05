#include <stdio.h>
#include <unistd.h>

#include <pthread.h>

#include <libavutil/timestamp.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#include "server.h"

void print_buffer_stats(struct BufferContext *buffer)
{
    int pkts[BUFFERSIZE], i;
    struct AVPacketWrap *cur;
    for (i = 0; i < BUFFERSIZE; i++) {
        pkts[i] = 0;
        if(!buffer->pkts[i]) {
            printf("[%d]: %d ", i, 0);
            continue;
        }

        cur = buffer->pkts[i];

        while (cur->next) {
            pkts[i]++;
            cur = cur->next;
        }
        pkts[i]++;
        printf("[%d]: %d ", i, pkts[i]);
    }

    printf("\n");
}

int buffer_push_pkt(struct BufferContext *buffer, AVPacket *pkt)
{
    // No current packet to append to and current list is empty
    if (!buffer->pos && !buffer->pkts[buffer->cur_idx]) {
        // create list, set current packet, set
        buffer->pkts[buffer->cur_idx] = (struct AVPacketWrap*) malloc(sizeof(struct AVPacketWrap));
        buffer->pos = buffer->pkts[buffer->cur_idx];
        buffer->pos->next = NULL;
        buffer->pos->pkt = pkt;
        buffer->pos->hash = buffer->nb_idx / (BUFFERSIZE + 1);
        return 0;
    }

    if (!buffer->pos && buffer->pkts[buffer->cur_idx]) {
        buffer_clear_list(buffer, buffer->cur_idx);
        return buffer_push_pkt(buffer, pkt);
    }

    if (pkt->flags & AV_PKT_FLAG_KEY && pkt->stream_index == buffer->video_idx) {
        print_buffer_stats(buffer);
        buffer->cur_idx++;
        buffer->nb_idx++;
        if (buffer->cur_idx == BUFFERSIZE) {
            buffer->cur_idx = 0;
        }
        buffer->pos = NULL;
        return buffer_push_pkt(buffer, pkt);
    }

    buffer->pos->next = (struct AVPacketWrap*) malloc(sizeof(struct AVPacketWrap));
    buffer->pos = buffer->pos->next;
    buffer->pos->next = NULL;
    buffer->pos->pkt = pkt;
    buffer->pos->hash = buffer->nb_idx / (BUFFERSIZE + 1);
    return 0;
}


void buffer_clear_list(struct BufferContext *buffer, int i)
{
    struct AVPacketWrap *head = buffer->pkts[i];
    if (!head)
        return;
    do {
        av_packet_unref(head->pkt);
        free(head->pkt);
        free(head);
    } while ((head = head->next));
    buffer->pkts[i] = NULL;
}


int get_free_spot(struct ClientContext *clients)
{
    int i;
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].in_use) {
            return i;
        }
    }
    return -1;
}

void remove_client(struct ClientContext *list, int idx)
{
    printf("Trying to remove client\n");
    avio_close(list[idx].ofmt_ctx->pb);
    avformat_free_context(list[idx].ofmt_ctx);
    list[idx].in_use = 0;
    list[idx].buffer = NULL;
    printf("Removed client.\n");
}


void *accept_thread(void *arg)
{
    struct AcceptInfo *info = (struct AcceptInfo*) arg;
    struct BufferContext *buffer = info->buffer;
    AVIOContext *client;
    struct ClientContext *clients = info->clients;
    struct ClientContext *new_client;
    AVFormatContext *ofmt_ctx;
    AVOutputFormat *ofmt;
    AVStream *in_stream, *out_stream;
    AVCodecContext *codec_ctx;
    int ret, i, free_spot, reply_code;
    for (;;) {
        reply_code = 200;
        if ((ret = avio_accept(info->server, &client)) < 0)
            continue;
        // Append client to client list
        client->seekable = 0;
        free_spot = get_free_spot(clients);
        if (free_spot < 0) {
            printf("No more slots free\n");
            reply_code = 503;
        }
        if ((ret = av_opt_set_int(client, "reply_code", reply_code, AV_OPT_SEARCH_CHILDREN)) < 0) {
            av_log(client, AV_LOG_ERROR, "Failed to set reply_code: %s.\n", av_err2str(ret));
            continue;
        }
        while ((ret = avio_handshake(client)) > 0);
        if (ret < 0) {
            avio_close(client);
            continue;
        }

        if (reply_code == 503) {
            avio_close(client);
            continue;
        }

        avformat_alloc_output_context2(&ofmt_ctx, NULL, "matroska", NULL);

        if (!ofmt_ctx) {
            fprintf(stderr, "Could not create output context\n");
            continue;
        }
        ofmt_ctx->flags &= AVFMT_FLAG_GENPTS;
        ofmt = ofmt_ctx->oformat;
        ofmt->flags &= AVFMT_NOFILE;

        for (i = 0; i < info->ifmt_ctx->nb_streams; i++)
        {
            in_stream = info->ifmt_ctx->streams[i];
            codec_ctx = avcodec_alloc_context3(NULL);
            avcodec_parameters_to_context(codec_ctx, in_stream->codecpar);
            out_stream = avformat_new_stream(ofmt_ctx, codec_ctx->codec);
            if (!out_stream) {
                fprintf(stderr, "Failed allocating output stream\n");
                continue;
            }
            ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
            if (ret < 0) {
                fprintf(stderr, "Failed to copy context from input to output stream codec context\n");
                continue;
            }
            /*out_stream->codec->codec_tag = 0;
            if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
                out_stream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER; */
        }

        ofmt_ctx->pb = client;
        ret = avformat_write_header(ofmt_ctx, NULL);
        if (ret < 0) {
            fprintf(stderr, "Error occurred when opening output file\n");
            continue;
        }
        new_client = &clients[free_spot];
        new_client->in_use = 1;
        new_client->ofmt_ctx = ofmt_ctx;
        new_client->idx = buffer->cur_idx;
        new_client->nb_idx = buffer->nb_idx;
        new_client->buffer = buffer;
        printf("Accepted new client!\n");

    }

    return NULL;
}

void *write_gop_to_client(void *arg)
{
    struct WriteGOPInfo *info = (struct WriteGOPInfo*) arg;
    struct ClientContext *clients = info->clients;
    struct BufferContext *buffer = info->buffer;
    int i = info->client_index;
    struct AVPacketWrap *cur = buffer->pkts[clients[i].idx];
    AVPacket send_pkt;
    int ret;

    do {
        av_copy_packet(&send_pkt, cur->pkt);
        ret = av_interleaved_write_frame(clients[i].ofmt_ctx, &send_pkt);
        if (ret < 0) {
            fprintf(stderr, "Error muxing packet %s\n", av_err2str(ret));
            remove_client(clients, i);
            return NULL;
        }

        if (ret == 1) {
            remove_client(clients, i);
            return NULL;
        }
        //printf("Wrote interleaved frame\n");

    } while ((cur = cur->next));
    clients[i].idx++;
    clients[i].nb_idx++;
    if (clients[i].idx == BUFFERSIZE) {
        clients[i].idx = 0;
    }
    clients[i].in_use = 3;
    return (void*) info->info;
}

void *write_thread(void *arg)
{
    struct WriteInfo *info = (struct WriteInfo*) arg;
    struct BufferContext *buffer = info->buffer;
    struct ClientContext *clients = info->clients;
    struct WriteGOPInfo *gop_info;
    int i;

    printf("Going through clients.\n");
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].in_use)
            continue;
        printf("next client: %d\ngoing through packets %d\n", i, clients[i].idx);
        if (clients[i].nb_idx == buffer->nb_idx)
            continue;
        if (clients[i].in_use == 2)
            continue;
        if (clients[i].in_use == 3) {
            pthread_join(clients[i].write_thread, (void**) &gop_info);
            free(gop_info);
        }
        clients[i].in_use = 2;
        gop_info = (struct WriteGOPInfo*) malloc(sizeof(struct WriteGOPInfo));

        gop_info->buffer = buffer;
        gop_info->clients = clients;
        gop_info->client_index = i;
        gop_info->info = gop_info;
        printf("spawning new gop write thread\n");
        pthread_create(&clients[i].write_thread, NULL, write_gop_to_client, gop_info);


    }

    return NULL;
}

void *read_thread(void *arg)
{
    struct ReadInfo *info = (struct ReadInfo*) arg;
    AVFormatContext *ifmt_ctx = info->ifmt_ctx;
    struct BufferContext *buffer = info->buffer;
    AVPacket *pkt;
    int ret;
    int64_t pts, now;
    AVStream *in_stream;
    AVRational tb;
    tb.num = 1;
    tb.den = AV_TIME_BASE;
    for(;;) {
        //print_buffer_stats(buffer);
        pkt = (AVPacket*) malloc(sizeof(AVPacket));

        ret = av_read_frame(ifmt_ctx, pkt);
        if (ret < 0) {
            av_packet_unref(pkt);
            free(pkt);
            info->ret = ret;
            break;
        }
        in_stream = ifmt_ctx->streams[pkt->stream_index];
        pts = av_rescale_q(pkt->pts, in_stream->time_base, tb);
        now = av_gettime_relative() - info->start;
        //printf("now: %ld pts: %ld\n", now, pts);
        while (pts > now) {
            usleep(1000);
            now = av_gettime_relative() - info->start;
        }

        ret = buffer_push_pkt(buffer, pkt);

        if (ret < 0) {
            info->ret = ret;
            break;
        }
    }
    return NULL;
}
