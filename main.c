#include <stdio.h>
#include <unistd.h>

#include <pthread.h>

#include <libavutil/timestamp.h>
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

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
};

struct WriteInfo {
    struct BufferContext *buffer;
    struct ClientContext *clients;
    AVFormatContext *ifmt_ctx;
};

struct ClientContext {
    int in_use;
    int idx;
    int nb_idx;
    struct BufferContext *buffer;
    AVFormatContext *ofmt_ctx;
};

struct AcceptInfo {
    AVFormatContext *ifmt_ctx;
    AVIOContext *server;
    struct BufferContext *buffer;
    struct ClientContext *clients;
};

int client_next_frame(struct ClientContext *cc) {
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

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           tag,
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
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

void remove_client(struct ClientContext *list, int idx)
{
    printf("Trying to remove client\n");
        avio_closep(&list[idx].ofmt_ctx->pb);
        avformat_free_context(list[idx].ofmt_ctx);
        list[idx].in_use = 0;
        list[idx].buffer = NULL;
        printf("Removed client.\n");
}

void *write_thread(void *arg)
{
    struct WriteInfo *info = (struct WriteInfo*) arg;
    struct BufferContext *buffer = info->buffer;
    struct ClientContext *clients = info->clients;
    struct AVPacketWrap *cur;
    struct AVPacket send_pkt;
    //AVStream *out_stream, *in_stream;
//    AVPacket pkt;
    int ret, i;

    printf("Going through clients.\n");
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].in_use)
            continue;
        printf("next client: %d\ngoing through packets %d\n", i, clients[i].idx);
        if (clients[i].nb_idx == buffer->nb_idx)
            continue;
        cur = buffer->pkts[clients[i].idx];
        do {
            printf("next packet\n");
            //in_stream = info->ifmt_ctx->streams[cur->pkt->stream_index];
            //out_stream = clients[i].ofmt_ctx->streams[cur->pkt->stream_index];
/*            av_copy_packet(&pkt, cur->pkt);

            pkt.pts = av_rescale_q_rnd(cur->pkt->pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
            pkt.dts = av_rescale_q_rnd(cur->pkt->dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
            pkt.duration = av_rescale_q(cur->pkt->duration, in_stream->time_base, out_stream->time_base);
            pkt.pos = -1;
*/
            av_copy_packet(&send_pkt, cur->pkt);
            ret = av_interleaved_write_frame(clients[i].ofmt_ctx, &send_pkt);
            if (ret < 0) {
                fprintf(stderr, "Error muxing packet %s\n", av_err2str(ret));
                remove_client(clients, i);
                break;
            }

            if (ret == 1) {
                remove_client(clients, i);
                break;
            }
            printf("Wrote interleaved frame\n");

        } while ((cur = cur->next));
        clients[i].idx++;
        clients[i].nb_idx++;
        if (clients[i].idx == BUFFERSIZE) {
            clients[i].idx = 0;
        }
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
    for(;;) {
        print_buffer_stats(buffer);
        pkt = (AVPacket*) malloc(sizeof(AVPacket));

        ret = av_read_frame(ifmt_ctx, pkt);
        if (ret < 0) {
            av_packet_unref(pkt);
            free(pkt);
            info->ret = ret;
            break;
        }

        ret = buffer_push_pkt(buffer, pkt);

        if (ret < 0) {
            info->ret = ret;
            break;
        }
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    AVOutputFormat *ofmt = NULL;
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    struct BufferContext buffer_ctx;
    const char *in_filename = "pipe:0";
    const char *out_uri = "http://127.0.0.1:8080";
    int ret, i;
    AVDictionary *options = NULL;
    AVIOContext *server = NULL;
    struct ClientContext clients[MAX_CLIENTS] = {0};
    struct ReadInfo read_info;
    struct AcceptInfo accept_info;
    struct WriteInfo write_info;
    pthread_t r_thread, a_thread, w_thread;

    if (argc > 1) {
        in_filename = argv[1];
    }

    printf("opening %s\n", in_filename);

    for (i = 0; i < BUFFERSIZE; i++) {
        buffer_ctx.pkts[i] = NULL;
    }
    buffer_ctx.cur_idx = 0;
    buffer_ctx.nb_idx = 0;
    buffer_ctx.pos = NULL;


    av_register_all();
    avformat_network_init();

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
            return 1;
        ret = avcodec_parameters_to_context(avctx, stream->codecpar);
        if (ret < 0) {
            return 1;
        }
        avcodec_free_context(&avctx);
        AVCodecParameters *params = stream->codecpar;
        printf("Got params\n");
        // Segfault here ↓
        enum AVMediaType type = params->codec_type;
        //if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        printf("Got type\n");
        if (type == AVMEDIA_TYPE_VIDEO) {
            buffer_ctx.video_idx = i;
            break;
        }
    }

    //av_dump_format(ifmt_ctx, 0, in_filename, 0);
    printf("Video stream id: %d\n", buffer_ctx.video_idx);

    if ((ret = av_dict_set(&options, "listen", "2", 0)) < 0) {
        fprintf(stderr, "Failed to set listen mode for server: %s\n", av_err2str(ret));
        return ret;
    }
    if ((ret = avio_open2(&server, out_uri, AVIO_FLAG_WRITE, NULL, &options)) < 0) {
        fprintf(stderr, "Failed to open server: %s\n", av_err2str(ret));
        return ret;
    }

    read_info.ifmt_ctx = ifmt_ctx;
    read_info.buffer = &buffer_ctx;
    accept_info.server = server;
    accept_info.clients = clients;
    accept_info.buffer = &buffer_ctx;
    accept_info.ifmt_ctx = ifmt_ctx;
    write_info.buffer = &buffer_ctx;
    write_info.clients = clients;
    write_info.ifmt_ctx = ifmt_ctx;

    pthread_create(&r_thread, NULL, read_thread, &read_info);
    pthread_create(&a_thread, NULL, accept_thread, &accept_info);
    for (;;) {
        //pthread_create(&w_thread, NULL, write_thread, &write_info);
        write_thread(&write_info);
        usleep(500000);
        //pthread_join(w_thread, NULL);
    }


    pthread_join(r_thread, NULL);
    //read_thread(&read_info);

end:

    avformat_close_input(&ifmt_ctx);
    avio_close(server);


    /* close output */
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);
    for (i = 0; i < BUFFERSIZE; i++) {
        buffer_clear_list(&buffer_ctx, i);
    }

    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }

    return 0;
}
