#include <stdio.h>
#include "segment.h"
#include <pthread.h>

#include <libavutil/opt.h>


void save_segment(struct Segment *seg, const char *filename)
{
    FILE *out = fopen(filename, "w");
    fwrite(seg->buf, seg->size, 1, out);
    fclose(out);
}

void segment_free(struct Segment *seg)
{
    printf("Freeing segment\n");
    free(seg->buf);
    free(seg->ts);
}

void segment_ref(struct Segment *seg)
{
    pthread_mutex_lock(&seg->nb_read_lock);
    seg->nb_read++;
    printf("  ref Readers: %d\n", seg->nb_read);
    pthread_mutex_unlock(&seg->nb_read_lock);
}

void segment_unref(struct Segment *seg)
{
    pthread_mutex_lock(&seg->nb_read_lock);
    seg->nb_read--;
    pthread_mutex_unlock(&seg->nb_read_lock);
    printf("unref Readers: %d\n", seg->nb_read);
    if (seg->nb_read == 0) {
        segment_free(seg);
    }
}

void segment_ts_append(struct Segment *seg, int64_t dts, int64_t pts)
{
    seg->ts = (int64_t*) realloc(seg->ts, sizeof(int64_t) * 2  * (seg->ts_len + 2));
    seg->ts[seg->ts_len] = dts;
    seg->ts[seg->ts_len + 1] = pts;
    seg->ts_len += 2;
    return;
}

int segment_write(void *opaque, unsigned char *buf, int buf_size)
{
    struct Segment *seg = (struct Segment*) opaque;
    //return fwrite(buf, buf_size, 1, seg->stream);
    seg->size += buf_size;
    seg->buf = (char*) realloc(seg->buf, seg->size);
    //printf("buf:%p size:%zu\n", seg->buf, seg->size);
    memcpy(seg->buf + seg->size - buf_size, buf, buf_size);
    return buf_size;
}

/*int segment_read(void *opaque, unsigned char *buf, int buf_size)
{
    struct Segment *seg = (struct Segment*) opaque;
    return fread(buf, buf_size, 1, seg->stream);
} */

int segment_read(void *opaque, unsigned char *buf, int buf_size)
{
    struct AVIOContextInfo *info = (struct AVIOContextInfo*) opaque;
    buf_size = buf_size < info->left ? buf_size : info->left;

    //printf("buf:%p left:%d\n", info->buf, info->left);
    /* copy internal buffer data to buf */
    memcpy(buf, info->buf, buf_size);
    info->buf  += buf_size;
    info->left -= buf_size;
    return buf_size;
}


/*int64_t segment_seek(void *opaque, int64_t offset, int whence)
{
    struct Segment *seg = (struct Segment*) opaque;
    return fseek(seg->stream, offset, whence);
}
*/

void segment_close(struct Segment *seg)
{
    //avio_flush(seg->io_ctx);
    av_write_trailer(seg->fmt_ctx);
/*    if (seg->io_ctx)
        avio_close(seg->io_ctx);
*/

    //fclose(seg->stream);
}

void segment_init(struct Segment **seg_p, AVFormatContext *fmt)
{
    int ret;
    int i;
    AVStream *in_stream, *out_stream;
    AVCodecContext *codec_ctx;
    struct Segment *seg = (struct Segment*) malloc(sizeof(struct Segment));

    seg->ifmt = fmt->iformat;
    seg->fmt_ctx = NULL;
    seg->nb_read = 0;
    seg->size = 0;
    seg->ts = NULL;
    seg->ts_len = 0;
    seg->buf = NULL;
    seg->avio_buffer = (unsigned char*) av_malloc(AV_BUFSIZE);
    pthread_mutex_init(&seg->nb_read_lock, NULL);
    //(*seg)->stream = open_memstream(&(*seg)->buf, &(*seg)->size);
    seg->io_ctx = avio_alloc_context(seg->avio_buffer, AV_BUFSIZE, 1, seg, NULL, &segment_write, NULL);
    seg->io_ctx->seekable = 0;
    avformat_alloc_output_context2(&seg->fmt_ctx, NULL, "matroska", NULL);
    if ((ret = av_opt_set_int(seg->fmt_ctx, "flush_packets", 1, AV_OPT_SEARCH_CHILDREN)) < 0) {
        printf("Could not set flush_packets!\n");
    }

    seg->fmt_ctx->flags |= AVFMT_FLAG_GENPTS;
    seg->fmt_ctx->oformat->flags &= AVFMT_NOFILE;

    printf("Initializing segment\n");

    for (i = 0; i < fmt->nb_streams; i++) {
        in_stream = fmt->streams[i];
        //enum AVMediaType type = in_stream->codecpar->codec_type;
        //if (type == AVMEDIA_TYPE_VIDEO || type == AVMEDIA_TYPE_AUDIO || type == AVMEDIA_TYPE_SUBTITLE) {
            codec_ctx = avcodec_alloc_context3(NULL);
            avcodec_parameters_to_context(codec_ctx, in_stream->codecpar);
            out_stream = avformat_new_stream(seg->fmt_ctx, codec_ctx->codec);
            //avcodec_parameters_to_context(out_stream->codec, in_stream->codecpar);
            //av_free(codec_ctx);
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
    //    }
    }

    seg->fmt_ctx->pb = seg->io_ctx;
    ret = avformat_write_header(seg->fmt_ctx, NULL);
    avio_flush(seg->io_ctx);
    if (ret < 0) {
        segment_close(seg);
        fprintf(stderr, "Error occured while writing header: %s\n", av_err2str(ret));
    }

    printf("Initialized segment.\n");
    av_dump_format(seg->fmt_ctx, 0, "(memory)", 1);

    *seg_p = seg;

    return;
}
