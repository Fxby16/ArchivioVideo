#pragma once

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavutil/avutil.h>
}

#include <string>

struct VideoMetadata {
    int duration = 0;
    int width = 0;
    int height = 0;
    bool valid = false;
};

extern VideoMetadata get_video_metadata(const std::string& path);