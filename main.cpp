#include <iostream>

extern "C" {
#include <libavformat/avformat.h>
}

AVFormatContext *src = nullptr;
AVFormatContext *dst = nullptr;

AVCodecContext *audioEncoder = nullptr;

AVCodecContext *videoEncoder = nullptr;

int dst_audio_stream_index = -1;

int dst_video_stream_index = -1;

AVCodecContext *audioDecoder = nullptr;

AVCodecContext *videoDecoder = nullptr;

const char *error2str(int code) {
    static char buf[1024];
    av_make_error_string(buf, 1024, code);
    return buf;
}

int main() {
    av_log_set_callback(nullptr);
    int ret = 0;
    if ((ret = avformat_open_input(&src, "../test_data/input.flv", nullptr, nullptr)) < 0) {
        printf("fail to open input:%s\n", error2str(ret));
        return ret;
    }
    if ((ret = avformat_find_stream_info(src, nullptr)) < 0) {
        printf("fail to find stream ifno:%s\n", error2str(ret));
        return ret;
    }
    int audio_stream_index = -1;
    int video_stream_index = -1;
    for (int i = 0; i < src->nb_streams; i++) {
        if (src->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
        } else if (src->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
        }
    }


    if (video_stream_index == -1 && video_stream_index == -1) {
        printf("input  video has no any streams");
        return -1;
    }
    const char *output_file = "./output.mp4";
    ret = avformat_alloc_output_context2(&dst, nullptr, nullptr, output_file);
    if (ret < 0) {
        printf("fail to alloc format contexts\n");
        return ret;
    }

    if (!(dst->flags & AVFMT_NOFILE)) {
        ret = avio_open2(&dst->pb, output_file, AVIO_FLAG_WRITE, nullptr, nullptr);
    }

    if (video_stream_index != -1) {

        videoDecoder = avcodec_alloc_context3(nullptr);
        avcodec_parameters_to_context(videoDecoder, src->streams[video_stream_index]->codecpar);

        ret = avcodec_open2(videoDecoder, avcodec_find_decoder(src->streams[video_stream_index]->codecpar->codec_id),
                            nullptr);


        AVCodec *videoCodec = avcodec_find_encoder(src->streams[video_stream_index]->codecpar->codec_id);
        if (!videoCodec) {
            printf("do not support this encoder\n");
            return -2;
        }
        videoEncoder = avcodec_alloc_context3(videoCodec);
        avcodec_parameters_to_context(videoEncoder, src->streams[video_stream_index]->codecpar);
        videoEncoder->time_base = {1, 30};
        if (dst->oformat->flags & AVFMT_GLOBALHEADER) {
            videoEncoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        AVDictionary *param = nullptr;
        av_dict_set(&param, "tune", "zerolatency", 0);
        av_dict_set(&param, "preset", "ultrafast", 0);

        ret = avcodec_open2(videoEncoder, videoCodec, &param);
        if (ret < 0) {
            printf("fail to open encoder:%s,because %s\n", videoCodec->name, error2str(ret));
            return ret;
        }

        AVStream *stream = avformat_new_stream(dst, nullptr);
        if (stream == nullptr) {
            printf("fail to add audio stream\n");
            return ret;
        }
        ret = avcodec_parameters_from_context(stream->codecpar, videoEncoder);
        if (ret < 0) {
            printf("fail to copy parameters from videoCodecContext\n");
            return ret;
        }
        dst_video_stream_index = stream->index;

    }

    if (audio_stream_index != -1) {
        audioDecoder = avcodec_alloc_context3(nullptr);
        avcodec_parameters_to_context(audioDecoder, src->streams[audio_stream_index]->codecpar);
        ret = avcodec_open2(audioDecoder, avcodec_find_decoder(src->streams[audio_stream_index]->codecpar->codec_id),
                            nullptr);

        AVCodec *audioCodec = avcodec_find_encoder(src->streams[audio_stream_index]->codecpar->codec_id);
        if (!audioCodec) {
            printf("do not support this encoder\n");
            return -2;
        }
        audioEncoder = avcodec_alloc_context3(audioCodec);
        avcodec_parameters_to_context(audioEncoder, src->streams[audio_stream_index]->codecpar);

        if (dst->oformat->flags & AVFMT_GLOBALHEADER) {
            audioEncoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }


        ret = avcodec_open2(audioEncoder, audioCodec, nullptr);
        if (ret < 0) {
            printf("fail to open encoder:%s,because %s\n", audioCodec->name, error2str(ret));
            return ret;
        }
        AVStream *stream = avformat_new_stream(dst, nullptr);
        if (stream == nullptr) {
            printf("fail to add audio stream\n");
            return ret;
        }
        ret = avcodec_parameters_from_context(stream->codecpar, audioEncoder);
        if (ret < 0) {
            printf("fail to copy parameters from audioCodecContext\n");
            return ret;
        }
        dst_audio_stream_index = stream->index;
    }

    ret = avformat_write_header(dst, nullptr);
    if (ret < 0) {
        printf("fail to write header :%s\n", error2str(ret));
    }

    AVPacket *decode_packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();


    AVPacket *encode_packet = av_packet_alloc();

    while (av_read_frame(src, decode_packet) >= 0) {
        if (decode_packet->stream_index == audio_stream_index) {
            avcodec_send_packet(audioDecoder, decode_packet);
            while (avcodec_receive_frame(audioDecoder, frame) >= 0) {
                avcodec_send_frame(audioEncoder, frame);
                double pts = decode_packet->pts * av_q2d(src->streams[audio_stream_index]->time_base);
                while (avcodec_receive_packet(audioEncoder, encode_packet) >= 0) {
                    av_packet_rescale_ts(encode_packet, src->streams[audio_stream_index]->time_base,
                                         dst->streams[dst_audio_stream_index]->time_base);
                    encode_packet->stream_index = dst_audio_stream_index;
                    ret = av_interleaved_write_frame(dst, encode_packet);
                    if (ret < 0) {
                        printf("fail to write audio frame:%s\n", error2str(ret));
                    } else {
                        printf("write audio video frame success\n");
                    }
                }
            }
        } else if (decode_packet->stream_index == video_stream_index) {
            avcodec_send_packet(videoDecoder, decode_packet);
            while (avcodec_receive_frame(videoDecoder, frame) == 0) {
                avcodec_send_frame(videoEncoder, frame);
                while (avcodec_receive_packet(videoEncoder, encode_packet) >= 0) {
                    av_packet_rescale_ts(encode_packet, src->streams[video_stream_index]->time_base,
                                         dst->streams[dst_video_stream_index]->time_base);
                    encode_packet->stream_index = dst_video_stream_index;
                    ret = av_interleaved_write_frame(dst, encode_packet);
                    if (ret < 0) {
                        printf("fail to write video frame:%s\n", error2str(ret));
                    } else {
                        printf("write video video frame success\n");
                    }
                }
            }
        }
        av_packet_unref(decode_packet);
    }

    printf("flush---audio\n");
    while (avcodec_send_frame(audioEncoder, nullptr) != 0) {
        while (avcodec_receive_frame(audioDecoder, frame) >= 0) {
            avcodec_send_frame(audioEncoder, frame);
            while (avcodec_receive_packet(audioEncoder, encode_packet) >= 0) {
                av_packet_rescale_ts(encode_packet, src->streams[audio_stream_index]->time_base,
                                     dst->streams[dst_audio_stream_index]->time_base);
                encode_packet->stream_index = dst_audio_stream_index;
                ret = av_interleaved_write_frame(dst, encode_packet);
                if (ret < 0) {
                    printf("fail to write audio frame:%s\n", error2str(ret));
                } else {
                    printf("write video audio frame success\n");
                }
            }
        }
    }


    printf("flush---video\n");
    while (avcodec_send_frame(videoEncoder, nullptr) != 0) {
        while (avcodec_receive_packet(videoEncoder, encode_packet) >= 0) {
            av_packet_rescale_ts(encode_packet, src->streams[video_stream_index]->time_base,
                                 dst->streams[dst_video_stream_index]->time_base);
            encode_packet->stream_index = dst_video_stream_index;
            ret = av_interleaved_write_frame(dst, encode_packet);
            if (ret < 0) {
                printf("fail to write video frame:%s\n", error2str(ret));
            } else {
                printf("write video video frame success\n");
            }
        }
    }

    ret = av_write_trailer(dst);
    if (ret < 0) {
        printf("fail to write trailer :%s\n", error2str(ret));
    } else {
        printf("write trailer success\n");
    }

    if (!(dst->flags & AVFMT_NOFILE)) {
        avio_close(dst->pb);
    }
    avformat_close_input(&src);
    avcodec_free_context(&audioDecoder);
    avcodec_free_context(&videoDecoder);

    avformat_free_context(dst);
    avcodec_free_context(&audioEncoder);
    avcodec_free_context(&videoEncoder);

    av_packet_free(&decode_packet);
    av_packet_free(&encode_packet);
    av_frame_free(&frame);
    return 0;
}
