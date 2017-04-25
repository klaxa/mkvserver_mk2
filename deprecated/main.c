#include <stdio.h>
#include <unistd.h>

#include <pthread.h>

#include <libavutil/time.h>

#include "server.h"

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
    buffer_ctx.eos = 0;
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

    if ((ret = av_dict_set_int(&options, "listen_timeout", 500, 0)) < 0) {
        fprintf(stderr, "Failed to set listen timeout for server: %s\n", av_err2str(ret));
        return ret;
    }

    if ((ret = av_dict_set_int(&options, "timeout", 20000, 0)) < 0) {
        fprintf(stderr, "Failed to set listen timeout for server: %s\n", av_err2str(ret));
        return ret;
    }

    if ((ret = avio_open2(&server, out_uri, AVIO_FLAG_WRITE, NULL, &options)) < 0) {
        fprintf(stderr, "Failed to open server: %s\n", av_err2str(ret));
        return ret;
    }

    read_info.ifmt_ctx = ifmt_ctx;
    read_info.buffer = &buffer_ctx;
    read_info.start = av_gettime_relative();
    accept_info.server = server;
    accept_info.clients = clients;
    accept_info.buffer = &buffer_ctx;
    accept_info.ifmt_ctx = ifmt_ctx;
    write_info.buffer = &buffer_ctx;
    write_info.clients = clients;

    pthread_create(&r_thread, NULL, read_thread, &read_info);
    pthread_create(&w_thread, NULL, write_thread, &write_info);
    //pthread_create(&a_thread, NULL, accept_thread, &accept_info);

    accept_thread(&accept_info);
    printf("Joined a_thread\n");

    pthread_join(r_thread, NULL);
    printf("Joined r_thread\n");
    pthread_join(w_thread, NULL);
    printf("Joined w_thread\n");
    //pthread_join(a_thread, NULL);

end:

    avformat_close_input(&ifmt_ctx);
    avio_close(server);

    free_buffer(&buffer_ctx);
    printf("Freed buffer\n");


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
