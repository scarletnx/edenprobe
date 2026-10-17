// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The reference decoder tools/vp9-jobs.py asks for the state the video family's jobs carry. It is
// FFmpeg's VP9 decoder, built from source by `tools/vp9-jobs.py build-ref`, and it reads the
// decoder's own context after every packet, so it has to be compiled against the same tree's
// internal headers. Nothing of it reaches the program: the script turns what this prints into
// source/vp9jobs.c, and this file stays a tool.
//
// Input, a file of packets, each a little-endian u32 size, one byte of what to do first and the
// bytes: 0 nothing, 1 tell the decoder the frame before was shown, 2 that it was hidden (what
// the engine's LastShowFrame bit says, written into the field FFmpeg reads it from). A size of
// zero with the byte 3 asks for the default tables instead. Output, one JSON object per line.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavcodec/avcodec.h"
#include "libavcodec/vp9data.h"
#include "libavcodec/vp9dec.h"
#include "libavutil/md5.h"

static void Hex(const char *name, const void *data, size_t size) {
    const uint8_t *p = data;
    printf("\"%s\":\"", name);
    for (size_t i = 0; i < size; ++i) {
        printf("%02x", p[i]);
    }
    printf("\"");
}

// Each plane's MD5 and the whole picture's, I420 cropped to the picture: how libvpx's .md5
// lists are made.
static void Md5s(const char *name, const AVFrame *f) {
    struct AVMD5 *whole = av_md5_alloc();
    uint8_t digest[4][16];
    av_md5_init(whole);
    for (int plane = 0; plane < 3; ++plane) {
        const int w = plane ? (f->width + 1) >> 1 : f->width;
        const int h = plane ? (f->height + 1) >> 1 : f->height;
        struct AVMD5 *one = av_md5_alloc();
        av_md5_init(one);
        for (int y = 0; y < h; ++y) {
            av_md5_update(one, f->data[plane] + (ptrdiff_t)y * f->linesize[plane], w);
            av_md5_update(whole, f->data[plane] + (ptrdiff_t)y * f->linesize[plane], w);
        }
        av_md5_final(one, digest[plane + 1]);
        av_free(one);
    }
    av_md5_final(whole, digest[0]);
    av_free(whole);
    printf("\"%s\":[", name);
    for (int i = 0; i < 4; ++i) {
        printf(i ? ",\"" : "\"");
        for (int j = 0; j < 16; ++j) {
            printf("%02x", digest[i][j]);
        }
        printf("\"");
    }
    printf("]");
}

static void Defaults(void) {
    printf("{\"defaults\":1,");
    Hex("p", &ff_vp9_default_probs, sizeof(ff_vp9_default_probs));
    printf(",");
    Hex("coef", ff_vp9_default_coef_probs, sizeof(ff_vp9_default_coef_probs));
    printf(",");
    Hex("kf_y", ff_vp9_default_kf_ymode_probs, sizeof(ff_vp9_default_kf_ymode_probs));
    printf(",");
    Hex("kf_uv", ff_vp9_default_kf_uvmode_probs, sizeof(ff_vp9_default_kf_uvmode_probs));
    printf(",");
    Hex("kf_partition", ff_vp9_default_kf_partition_probs,
        sizeof(ff_vp9_default_kf_partition_probs));
    printf("}\n");
}

// The probabilities the frame was decoded with: the saved context it started from with the
// compressed header's updates applied. Adaptation afterwards writes prob_ctx, never prob, so
// this is still the decoding state once the packet is done. The coefficient table keeps the
// three model probabilities of each eleven.
static void Probs(const VP9Context *s) {
    Hex("p", &s->prob.p, sizeof(s->prob.p));
    printf(",\"coef\":\"");
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 2; ++j)
            for (int k = 0; k < 2; ++k)
                for (int l = 0; l < 6; ++l)
                    for (int m = 0; m < 6; ++m)
                        for (int n = 0; n < 3; ++n)
                            printf("%02x", s->prob.coef[i][j][k][l][m][n]);
    printf("\",\"ctx\":[");
    for (int c = 0; c < 4; ++c) {
        printf(c ? ",{" : "{");
        Hex("p", &s->prob_ctx[c].p, sizeof(s->prob_ctx[c].p));
        printf(",");
        Hex("coef", s->prob_ctx[c].coef, sizeof(s->prob_ctx[c].coef));
        printf("}");
    }
    printf("]");
}

static void Header(const VP9Context *s) {
    const VP9BitstreamHeader *h = &s->s.h;
    printf("\"h\":{\"keyframe\":%d,\"invisible\":%d,\"errorres\":%d,\"intraonly\":%d,"
           "\"resetctx\":%d,\"refreshrefmask\":%d,\"highprecisionmvs\":%d,\"filtermode\":%d,"
           "\"allowcompinter\":%d,\"refreshctx\":%d,\"parallelmode\":%d,\"framectxid\":%d,"
           "\"use_last_frame_mvs\":%d,\"refidx\":[%d,%d,%d],\"signbias\":[%d,%d,%d],"
           "\"fixcompref\":%d,\"varcompref\":[%d,%d],\"level\":%d,\"sharpness\":%d,"
           "\"lf_enabled\":%d,\"lf_updated\":%d,\"lf_mode\":[%d,%d],\"lf_ref\":[%d,%d,%d,%d],"
           "\"yac_qi\":%d,\"ydc_qdelta\":%d,\"uvdc_qdelta\":%d,\"uvac_qdelta\":%d,"
           "\"lossless\":%d,\"txfmmode\":%d,\"comppredmode\":%d,"
           "\"log2_tile_cols\":%u,\"log2_tile_rows\":%u,\"tile_cols\":%u,\"tile_rows\":%u,"
           "\"uncompressed_header_size\":%d,\"compressed_header_size\":%d,",
           h->keyframe, h->invisible, h->errorres, h->intraonly, h->resetctx,
           h->refreshrefmask, h->highprecisionmvs, (int)h->filtermode, h->allowcompinter,
           h->refreshctx, h->parallelmode, h->framectxid, h->use_last_frame_mvs, h->refidx[0],
           h->refidx[1], h->refidx[2], h->signbias[0], h->signbias[1], h->signbias[2],
           h->fixcompref, h->varcompref[0], h->varcompref[1], h->filter.level,
           h->filter.sharpness, h->lf_delta.enabled, h->lf_delta.updated, h->lf_delta.mode[0],
           h->lf_delta.mode[1], h->lf_delta.ref[0], h->lf_delta.ref[1], h->lf_delta.ref[2],
           h->lf_delta.ref[3], h->yac_qi, h->ydc_qdelta, h->uvdc_qdelta, h->uvac_qdelta,
           h->lossless, (int)h->txfmmode, (int)h->comppredmode, h->tiling.log2_tile_cols,
           h->tiling.log2_tile_rows, h->tiling.tile_cols, h->tiling.tile_rows,
           h->uncompressed_header_size, h->compressed_header_size);
    printf("\"seg\":{\"enabled\":%d,\"temporal\":%d,\"absolute_vals\":%d,\"update_map\":%d,"
           "\"prob\":[%d,%d,%d,%d,%d,%d,%d],\"pred_prob\":[%d,%d,%d],\"feat\":[",
           h->segmentation.enabled, h->segmentation.temporal, h->segmentation.absolute_vals,
           h->segmentation.update_map, h->segmentation.prob[0], h->segmentation.prob[1],
           h->segmentation.prob[2], h->segmentation.prob[3], h->segmentation.prob[4],
           h->segmentation.prob[5], h->segmentation.prob[6], h->segmentation.pred_prob[0],
           h->segmentation.pred_prob[1], h->segmentation.pred_prob[2]);
    for (int i = 0; i < 8; ++i) {
        const typeof(h->segmentation.feat[0]) *f = &h->segmentation.feat[i];
        printf("%s[%d,%d,%d,%d,%d,%d,%d]", i ? "," : "", f->q_enabled, f->lf_enabled,
               f->ref_enabled, f->skip_enabled, f->q_val, f->lf_val, f->ref_val);
    }
    printf("]},\"last_keyframe\":%d,\"w\":%d,\"h\":%d,\"sb_cols\":%u,\"sb_rows\":%u}",
           s->last_keyframe, s->w, s->h, s->sb_cols, s->sb_rows);
}

static uint8_t *ReadAll(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(*size ? *size : 1);
    if (fread(data, 1, *size, f) != *size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    return data;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: vp9-ref <packets>\n");
        return 2;
    }
    size_t size;
    uint8_t *all = ReadAll(argv[1], &size);
    if (!all) {
        fprintf(stderr, "vp9-ref: cannot read %s\n", argv[1]);
        return 2;
    }
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_VP9);
    AVCodecContext *avctx = avcodec_alloc_context3(codec);
    // One thread: frame threading would decode ahead of the state read after each packet.
    avctx->thread_count = 1;
    avctx->thread_type = 0;
    if (avcodec_open2(avctx, codec, NULL) < 0) {
        fprintf(stderr, "vp9-ref: cannot open the decoder\n");
        return 2;
    }
    VP9Context *s = avctx->priv_data;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    size_t at = 0;
    for (int index = 0; at + 5 <= size; ++index) {
        const uint32_t n = all[at] | all[at + 1] << 8 | all[at + 2] << 16 | (uint32_t)all[at + 3] << 24;
        const int op = all[at + 4];
        at += 5;
        if (op == 3) {
            Defaults();
            continue;
        }
        if (at + n > size) {
            fprintf(stderr, "vp9-ref: packet %d runs past the file\n", index);
            return 2;
        }
        // The slots' sizes before the frame: what the engine's setup gives as the references'.
        int refs[8][2];
        for (int i = 0; i < 8; ++i) {
            refs[i][0] = s->s.refs[i].f ? s->s.refs[i].f->width : 0;
            refs[i][1] = s->s.refs[i].f ? s->s.refs[i].f->height : 0;
        }
        if (op == 1 || op == 2) {
            s->s.h.invisible = op == 2;
        }
        // show_existing_frame, the bit after the marker and the profile (one more bit for 3).
        int existing = 0;
        if (n > 0) {
            const int profile = (all[at] >> 5 & 1) | (all[at] >> 4 & 1) << 1;
            existing = all[at] >> (profile == 3 ? 2 : 3) & 1;
        }
        av_packet_unref(pkt);
        if (av_new_packet(pkt, (int)n) < 0) {
            return 2;
        }
        memcpy(pkt->data, all + at, n);
        at += n;
        int ret = avcodec_send_packet(avctx, pkt);
        printf("{\"i\":%d,\"ret\":%d,\"refs_before\":[", index, ret);
        for (int i = 0; i < 8; ++i) {
            printf("%s[%d,%d]", i ? "," : "", refs[i][0], refs[i][1]);
        }
        printf("],\"out\":[");
        int outs = 0;
        while (ret >= 0) {
            ret = avcodec_receive_frame(avctx, frame);
            if (ret < 0) {
                break;
            }
            printf(outs++ ? "," : "");
            printf("{");
            Md5s("md5", frame);
            printf("}");
            av_frame_unref(frame);
        }
        printf("],\"rret\":%d,", ret == AVERROR(EAGAIN) ? 0 : ret);
        // A show_existing_frame leaves the current frame where it was; a decoded one replaces it.
        const AVFrame *cur = s->s.frames[CUR_FRAME].tf.f;
        const int decoded = !existing && ret == AVERROR(EAGAIN) && cur;
        printf("\"decoded\":%d,", decoded);
        if (decoded) {
            Md5s("cur", cur);
            printf(",");
            Header(s);
            printf(",");
            Probs(s);
        } else {
            printf("\"cur\":null");
        }
        printf("}\n");
        fflush(stdout);
    }
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&avctx);
    free(all);
    return 0;
}
