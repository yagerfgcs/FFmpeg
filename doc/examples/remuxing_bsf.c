/*
 * Copyright (c) 2013 Stefano Sabatini
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * libavformat/libavcodec demuxing bsfing and muxing API example.
 *
 * Remux streams from one container format to another and use bsf to filter streams.
 * @example remuxing_bsf.c
 */

#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>

static char s_errbuff[500];
static char *ff_err2str(int errRet){
  av_strerror(errRet, s_errbuff, 500);
  return s_errbuff;
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

static int filter_vbsf(AVBSFContext* bsf_context, AVStream *st, AVPacket *pkt) {
  if(!bsf_context){
    return -1;
  }

  int ret = 0;
  AVPacket filter_pkt;
  AVPacket filtered_pkt;

  memset(&filter_pkt, 0, sizeof(filter_pkt));
  memset(&filtered_pkt, 0, sizeof(filtered_pkt));

  if ((ret = av_packet_ref(&filter_pkt, pkt)) < 0) {
    fprintf(stderr, "Fail to ref input pkt");
    return -1;
  }

  if ((ret = av_bsf_send_packet(bsf_context, &filter_pkt)) < 0) {
    fprintf(stderr, "Fail to send packet, %s", ff_err2str(ret));
    av_packet_unref(&filter_pkt);
    return -1;
  }

  if ((ret = av_bsf_receive_packet(bsf_context, &filtered_pkt)) < 0) {
    fprintf(stderr, "Fail to receive packet, %s", ff_err2str(ret));
    av_packet_unref(&filter_pkt);
    return -1;
  }

  av_packet_unref(&filter_pkt);
  av_packet_unref(pkt);
  av_packet_move_ref(pkt, &filtered_pkt);
  return 0;
}

int main(int argc, char **argv)
{
    AVOutputFormat *ofmt = NULL;
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    AVCodecParameters *in_codecpar = NULL;
    AVCodecParameters *out_codecpar = NULL;
    AVPacket pkt;
    const char *in_filename, *out_filename;
    int ret, i;
    int stream_index = 0;
    int *stream_mapping = NULL;
    int stream_mapping_size = 0;

    if (argc < 3) {
        printf("usage: %s input output\n"
               "API example program to remux a media file with libavformat and libavcodec.\n"
               "Use bsf h264_mp4toannexb,dump_extra to filter streams.\n"
               "The output format is guessed according to the file extension.\n"
               "\n", argv[0]);
        return 1;
    }

    in_filename  = argv[1];
    out_filename = argv[2];

    // open input.
    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0) {
        fprintf(stderr, "Could not open input file '%s'", in_filename);
        goto end;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information");
        goto end;
    }

    av_dump_format(ifmt_ctx, 0, in_filename, 0);

    // init bsfs.
    AVBSFContext *vbsf_context = NULL;
    const char *bsf_annexb_name = NULL;
    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVCodecParameters *codecpar = ifmt_ctx->streams[i]->codecpar;
        if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            in_codecpar = codecpar;
            if(codecpar->codec_id == AV_CODEC_ID_H264) {
                bsf_annexb_name = "h264_mp4toannexb";
            } else if (in_codecpar->codec_id == AV_CODEC_ID_HEVC) {
                bsf_annexb_name = "hevc_mp4toannexb";
            } else {
                bsf_annexb_name = NULL;
            }
        }
    }
    
    // example 1. init only one h264_mp4toannexb or bsf.
    // const AVBitStreamFilter *bsf_annexb = NULL;
    // if (bsf_annexb_name) {
    //   bsf_annexb = av_bsf_get_by_name(bsf_annexb_name);
    //   if (!bsf_annexb) {
    //     fprintf(stderr, "Fail to get bsf, %s", bsf_annexb_name);
    //     goto end;
    //   }
    // }

    // ret = av_bsf_alloc(bsf_annexb, &vbsf_context);
    // if (ret) {
    //   fprintf(stderr, "Fail to alloc bsf context");
    //   goto end;
    // }

    // // copy codec parameters to bsf and init bsf.
    // ret = avcodec_parameters_copy(vbsf_context->par_in, in_codecpar);
    // if (ret < 0) {
    //   fprintf(stderr, "Fail to copy bsf parameters, %s", ff_err2str(ret));

    //   av_bsf_free(&vbsf_context);
    //   vbsf_context = NULL;
    //   goto end;
    // }

    // ret = av_bsf_init(vbsf_context);
    // if (ret < 0) {
    //   fprintf(stderr, "Fail to init bsf, %s", ff_err2str(ret));

    //   av_bsf_free(&vbsf_context);
    //   vbsf_context = NULL;
    //   goto end;
    // }

    // example 2. init some bsfs in list.
    AVBSFList *bsf_list = av_bsf_list_alloc();
    if(bsf_annexb_name){
      ret = av_bsf_list_append2(bsf_list, bsf_annexb_name, NULL);
      if (ret < 0) {
        fprintf(stderr, "Fail to list append bsf:%s, %s", bsf_annexb_name, ff_err2str(ret));

        av_bsf_free(&vbsf_context);
        vbsf_context = NULL;
        av_bsf_list_free(&bsf_list);
        bsf_list = NULL;
        goto end;
      }
    }
    
    ret = av_bsf_list_append2(bsf_list, "dump_extra", NULL);
    if (ret < 0) {
      fprintf(stderr, "Fail to list append bsf:dump_extra, %s", ff_err2str(ret));

      av_bsf_free(&vbsf_context);
      vbsf_context = NULL;
      av_bsf_list_free(&bsf_list);
      bsf_list = NULL;
      goto end;
    }

    ret = av_bsf_list_finalize(&bsf_list, &vbsf_context);
    if (ret < 0) {
      fprintf(stderr, "Fail to lsit finalize bsf, %s", ff_err2str(ret));

      av_bsf_free(&vbsf_context);
      vbsf_context = NULL;
      av_bsf_list_free(&bsf_list);
      bsf_list = NULL;
      goto end;
    }

    // copy codec parameters to bsf and init bsf.
    ret = avcodec_parameters_copy(vbsf_context->par_in, in_codecpar);
    if (ret < 0) {
      fprintf(stderr, "Fail to copy bsf parameters, %s", ff_err2str(ret));

      av_bsf_free(&vbsf_context);
      vbsf_context = NULL;
      av_bsf_list_free(&bsf_list);
      bsf_list = NULL;
      goto end;
    }

    ret = av_bsf_init(vbsf_context);
    if (ret < 0) {
      fprintf(stderr, "Fail to init bsf, %s", ff_err2str(ret));

      av_bsf_free(&vbsf_context);
      vbsf_context = NULL;
      av_bsf_list_free(&bsf_list);
      bsf_list = NULL;
      goto end;
    }

    av_bsf_list_free(&bsf_list);
    bsf_list = NULL;

    // open output.
    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, out_filename);
    if (!ofmt_ctx) {
        fprintf(stderr, "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    stream_mapping_size = ifmt_ctx->nb_streams;
    stream_mapping = av_mallocz_array(stream_mapping_size, sizeof(*stream_mapping));
    if (!stream_mapping) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    ofmt = ofmt_ctx->oformat;

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *out_stream;
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;

        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            stream_mapping[i] = -1;
            continue;
        }

        stream_mapping[i] = stream_index++;

        out_stream = avformat_new_stream(ofmt_ctx, NULL);
        if (!out_stream) {
            fprintf(stderr, "Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        // video use vbsf_context->par_out, audio use in_codecpar.
        if(in_codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
          ret = avcodec_parameters_copy(out_stream->codecpar, vbsf_context->par_out);
        }else{
          ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        }
        
        if (ret < 0) {
            fprintf(stderr, "Failed to copy codec parameters\n");
            goto end;
        }
        out_stream->codecpar->codec_tag = 0;
    }
    av_dump_format(ofmt_ctx, 0, out_filename, 1);

    if (!(ofmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open output file '%s'", out_filename);
            goto end;
        }
    }

    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        goto end;
    }

    while (1) {
        AVStream *in_stream, *out_stream;

        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0)
            break;

        in_stream  = ifmt_ctx->streams[pkt.stream_index];
        if (pkt.stream_index >= stream_mapping_size ||
            stream_mapping[pkt.stream_index] < 0) {
            av_packet_unref(&pkt);
            continue;
        }

        pkt.stream_index = stream_mapping[pkt.stream_index];
        out_stream = ofmt_ctx->streams[pkt.stream_index];
        log_packet(ifmt_ctx, &pkt, "in");

        /* copy packet */
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;
        log_packet(ofmt_ctx, &pkt, "out");

        // filter bsf packet for video stream.
        if(in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
          ret = filter_vbsf(vbsf_context, in_stream, &pkt);
          if(ret < 0){
            fprintf(stderr, "Fail to filter bsf packet");
            break;
          }
        }
        
        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error muxing packet\n");
            break;
        }
        av_packet_unref(&pkt);
    }

    av_write_trailer(ofmt_ctx);
end:

    avformat_close_input(&ifmt_ctx);

    /* close output */
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);

    av_freep(&stream_mapping);

    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }

    return 0;
}
