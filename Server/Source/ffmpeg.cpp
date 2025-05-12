#include "ffmpeg.hpp"
#include "common.hpp"

VideoMetadata get_video_metadata(const std::string& path) {
    AVFormatContext* fmt_ctx = nullptr;
    VideoMetadata meta;

    // Set larger probesize and analyzeduration
    AVDictionary* options = nullptr;
    av_dict_set(&options, "probesize", "10000000", 0); // 10MB
    av_dict_set(&options, "analyzeduration", "10000000", 0); // 10 seconds

    std::string format_name = get_format_from_filename(path);

    if (avformat_open_input(&fmt_ctx, path.c_str(), av_find_input_format(format_name.c_str()), &options) != 0) {
        av_dict_free(&options);
        return meta;
    }

    av_dict_free(&options);

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        avformat_close_input(&fmt_ctx);
        return meta;
    }

    for (unsigned int i = 0; i < fmt_ctx->nb_streams; ++i) {
        AVStream* stream = fmt_ctx->streams[i];
        AVCodecParameters* codecpar = stream->codecpar;

        if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            meta.width = codecpar->width;
            meta.height = codecpar->height;

            // Try to get duration from stream first
            if (stream->duration != AV_NOPTS_VALUE) {
                meta.duration = static_cast<int>(stream->duration * av_q2d(stream->time_base));
            }

            meta.valid = true;
            break;
        }
    }

    // If duration wasn't set from the stream, try format context
    if (meta.valid && meta.duration <= 0 && fmt_ctx->duration != AV_NOPTS_VALUE) {
        meta.duration = static_cast<int>(fmt_ctx->duration / AV_TIME_BASE);
    }

    avformat_close_input(&fmt_ctx);
    return meta;
}