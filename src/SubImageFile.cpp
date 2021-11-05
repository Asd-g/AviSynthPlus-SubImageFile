#include <string>
#include <vector>
#include <cassert>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

#include "avisynth.h"

constexpr int64_t unused_colour = static_cast<int64_t>(1) << 42;

typedef struct Subtitle
{
    std::vector<AVPacket> packets;
    int start_frame;
    int end_frame; // Actually first frame where subtitle is not displayed.
} Subtitle;

int findSubtitleIndex(int frame, const std::vector<Subtitle>& subtitles)
{
    for (size_t i = 0; i < subtitles.size(); ++i)
        if (subtitles[i].start_frame <= frame && frame < subtitles[i].end_frame)
            return i;

    return -1;
}

void makePaletteGray(uint32_t* palette)
{
    for (int i = 0; i < AVPALETTE_COUNT; ++i)
    {
        uint32_t g = (((palette[i] >> 16) & 0xff) + ((palette[i] >> 8) & 0xff) + (palette[i] & 0xff)) / 3;
        palette[i] = ((palette[i] >> 24) << 24) | (g << 16) | (g << 8) | g;
    }
}

bool isSupportedCodecID(AVCodecID codec_id)
{
    return codec_id == AV_CODEC_ID_DVD_SUBTITLE || codec_id == AV_CODEC_ID_HDMV_PGS_SUBTITLE;
}

/* multiplies and divides a rational number, such as a frame duration, in place and reduces the result */
AVS_FORCEINLINE void muldivRational(int64_t* num, int64_t* den, int64_t mul, int64_t div)
{
    /* do nothing if the rational number is invalid */
    if (!*den)
        return;

    /* nobody wants to accidentally divide by zero */
    assert(div);

    *num *= mul;
    *den *= div;
    int64_t a = *num;
    int64_t b = *den;
    while (b != 0)
    {
        int64_t t = a;
        a = b;
        b = t % b;
    }
    if (a < 0)
        a = -a;
    *num /= a;
    *den /= a;
}

int timestampToFrameNumber(int64_t pts, const AVRational& time_base, int64_t fpsnum, int64_t fpsden)
{
    int64_t num = time_base.num;
    int64_t den = time_base.den;

    muldivRational(&num, &den, fpsnum, fpsden);

    muldivRational(&num, &den, pts, 1);

    return static_cast<int>(num / den);
}

class SubImageFile : public GenericVideoFilter
{
    PVideoFrame blank_rgb;
    PVideoFrame blank_alpha;
    PVideoFrame last_frame;
    int last_subtitle;
    std::vector<Subtitle> subtitles;
    std::vector<int> palette;
    bool gray;
    bool info;
    bool flatten;
    AVCodecContext* avctx;
    VideoInfo vi1;
    int palette_size;
    std::string desc;

public:
    SubImageFile(PClip _child, const char* file, int id, const std::vector<int>& palette_, bool gray_, bool info_, bool flatten_, IScriptEnvironment* env)
        : GenericVideoFilter(_child), palette(palette_), gray(gray_), info(info_), flatten(flatten_)
    {
        palette_size = (palette[0] == -1) ? 0 : palette.size();
        vi.pixel_type = VideoInfo::CS_RGBP8;

        av_log_set_level(AV_LOG_PANIC);

        int ret = 0;

        AVFormatContext* fctx = nullptr;

        try {
            ret = avformat_open_input(&fctx, file, nullptr, nullptr);
            if (ret < 0)
                throw std::string("avformat_open_input failed: ");

            ret = avformat_find_stream_info(fctx, NULL);
            if (ret < 0)
                throw std::string("avformat_find_stream_info failed: ");
        }
        catch (const std::string& e) {
            char av_error[AV_ERROR_MAX_STRING_SIZE] = { 0 };
            std::string error;

            if (!av_strerror(ret, av_error, AV_ERROR_MAX_STRING_SIZE))
                error = av_error;
            else
                error = strerror(0);

            if (fctx)
                avformat_close_input(&fctx);

            env->ThrowError(("SubImageFile: " + e + error).c_str());
        }

        if (fctx->iformat->name != std::string("vobsub") && fctx->iformat->name != std::string("sup"))
        {
            avformat_close_input(&fctx);
            env->ThrowError("SubImageFile: unsupported file format.");
        }

        if (fctx->nb_streams == 0)
        {
            avformat_close_input(&fctx);
            env->ThrowError("SubImageFile: no streams found.");
        }

        int stream_index = -1;

        try {
            if (id > -1)
            {
                for (unsigned i = 0; i < fctx->nb_streams; ++i)
                {
                    if (fctx->streams[i]->id == id)
                    {
                        stream_index = i;
                        break;
                    }
                }

                if (stream_index == -1)
                    throw std::string("there is no stream with the chosen id.");

                if (!isSupportedCodecID(fctx->streams[stream_index]->codecpar->codec_id))
                    throw std::string("selected stream has unsupported format.");
            }
            else {
                for (unsigned i = 0; i < fctx->nb_streams; ++i)
                {
                    if (isSupportedCodecID(fctx->streams[i]->codecpar->codec_id))
                    {
                        stream_index = i;
                        break;
                    }
                }

                if (stream_index == -1)
                    throw std::string("no supported subtitle streams found.");
            }

            for (unsigned i = 0; i < fctx->nb_streams; ++i)
                if ((int)i != stream_index)
                    fctx->streams[i]->discard = AVDISCARD_ALL;

            AVCodecID codec_id = fctx->streams[stream_index]->codecpar->codec_id;

            const AVCodec* decoder = avcodec_find_decoder(codec_id);
            if (!decoder)
                throw std::string("failed to find decoder for '") + avcodec_get_name(codec_id) + "'.";

            avctx = avcodec_alloc_context3(decoder);
            if (!avctx)
                throw std::string("failed to allocate AVCodecContext.");

            int extradata_size = fctx->streams[stream_index]->codecpar->extradata_size;
            if (extradata_size)
            {
                avctx->extradata_size = extradata_size;
                avctx->extradata = static_cast<uint8_t*>(av_mallocz(extradata_size + AV_INPUT_BUFFER_PADDING_SIZE));
                memcpy(avctx->extradata, fctx->streams[stream_index]->codecpar->extradata, extradata_size);
            }

            ret = avcodec_open2(avctx, decoder, nullptr);
            if (ret < 0)
                throw std::string("failed to open AVCodecContext.");
        }
        catch (const std::string& e) {           
            avformat_close_input(&fctx);

            if (avctx)
                avcodec_free_context(&avctx);

            env->ThrowError(("SubImageFile: " + e).c_str());
        }

        vi.width = fctx->streams[stream_index]->codecpar->width;
        vi.height = fctx->streams[stream_index]->codecpar->height;

        Subtitle current_subtitle = { };

        AVPacket packet;
        av_init_packet(&packet);

        AVSubtitle avsub;

        while (av_read_frame(fctx, &packet) == 0)
        {
            if (packet.stream_index != stream_index)
            {
                av_packet_unref(&packet);
                continue;
            }

            int got_avsub = 0;

            AVPacket decoded_packet = packet;

            ret = avcodec_decode_subtitle2(avctx, &avsub, &got_avsub, &decoded_packet);
            if (ret < 0)
            {
                av_packet_unref(&packet);
                continue;
            }

            if (got_avsub)
            {
                const AVRational& time_base = fctx->streams[stream_index]->time_base;

                if (avsub.num_rects)
                {
                    current_subtitle.packets.push_back(packet);

                    int64_t start_time = current_subtitle.packets.front().pts;
                    if (fctx->streams[stream_index]->codecpar->codec_id == AV_CODEC_ID_DVD_SUBTITLE)
                    {
                        start_time += avsub.start_display_time;

                        current_subtitle.end_frame = timestampToFrameNumber(packet.pts + avsub.end_display_time, time_base, vi.fps_numerator, vi.fps_denominator);
                        // If it doesn't say when it should end, display it until the next one.
                        if (avsub.end_display_time == 0)
                            current_subtitle.end_frame = 0;
                    }

                    current_subtitle.start_frame = timestampToFrameNumber(start_time, time_base, vi.fps_numerator, vi.fps_denominator);

                    subtitles.push_back(current_subtitle);
                    current_subtitle.packets.clear();
                }
                else
                {
                    Subtitle& previous_subtitle = subtitles.back();
                    if (subtitles.size()) // The first AVSubtitle may be empty.
                        previous_subtitle.end_frame = timestampToFrameNumber(current_subtitle.packets.front().pts, time_base, vi.fps_numerator, vi.fps_denominator);

                    for (auto p : current_subtitle.packets)
                        av_packet_unref(&p);

                    current_subtitle.packets.clear();

                    av_packet_unref(&packet);
                }

                avsubtitle_free(&avsub);
            }
            else
                current_subtitle.packets.push_back(packet);
        }

        if (subtitles.size() == 0)
        {
            avformat_close_input(&fctx);

            if (avctx)
                avcodec_free_context(&avctx);

            env->ThrowError("SubImageFile: no usable subtitle pictures found.");
        }

        // Sometimes there is no AVSubtitle with num_rects = 0 in between two AVSubtitles with num_rects > 0 (PGS).
        // Sometimes end_display_time is 0 (VOBSUB).
        // In such cases end_frame is 0, so we correct it.
        for (size_t i = 0; i < subtitles.size(); ++i)
        {
            if (subtitles[i].end_frame == 0)
            {
                if (i < subtitles.size() - 1)
                    subtitles[i].end_frame = subtitles[i + 1].start_frame;
                else
                    subtitles[i].end_frame = vi.num_frames;
            }
        }

        blank_rgb = env->NewVideoFrame(vi);
        vi1 = vi;
        vi1.pixel_type = VideoInfo::CS_Y8;
        blank_alpha = env->NewVideoFrame(vi1);

        const int planes[3] = { PLANAR_R, PLANAR_G, PLANAR_B };
        for (int i = 0; i < 4; ++i)
        {
            uint8_t* ptr = (i < 3) ? blank_rgb->GetWritePtr(planes[i]) : blank_alpha->GetWritePtr();
            int stride = (i < 3) ? blank_rgb->GetPitch(planes[i]) : blank_alpha->GetPitch();

            for (int y = 0; y < vi.height; ++y)
            {
                memset(ptr, 0, vi.width);

                ptr += stride;
            }
        }

        last_subtitle = INT_MIN;

        if (flatten)
            vi.num_frames = static_cast<int>(subtitles.size());

        if (info)
        {
            desc = "Supported subtitle streams:\n";

            for (unsigned i = 0; i < fctx->nb_streams; i++)
            {
                AVCodecID codec_id = fctx->streams[i]->codecpar->codec_id;

                if (!isSupportedCodecID(codec_id))
                    continue;

                char stream_id[100] = { 0 };
                snprintf(stream_id, 99, "0x%x", fctx->streams[i]->id);

                desc += "Id: ";
                desc += stream_id;

                AVDictionaryEntry* language = av_dict_get(fctx->streams[i]->metadata, "language", nullptr, AV_DICT_MATCH_CASE);
                if (language) {
                    desc += ", language: ";
                    desc += language->value;
                }

                const int width = fctx->streams[stream_index]->codecpar->width;
                const int height = fctx->streams[stream_index]->codecpar->height;

                desc += ", size: ";
                desc += std::to_string(width);
                desc += "x";
                desc += std::to_string(height);

                desc += ", type: ";
                desc += avcodec_get_name(codec_id);

                desc += "\n";
            }

            desc.pop_back();
        }
    }

    int __stdcall SetCacheHints(int cachehints, int frame_range)
    {
        return cachehints == CACHE_GET_MTMODE ? MT_MULTI_INSTANCE : 0;
    }

    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env)
    {
        const int subtitle_index = (flatten) ? n : findSubtitleIndex(n, subtitles);

        if (subtitle_index == last_subtitle)
            return last_frame;

        PVideoFrame rgb = env->NewVideoFrame(vi);
        PVideoFrame alpha = env->NewVideoFrame(vi1);

        const int planes[4] = { PLANAR_R, PLANAR_G, PLANAR_B };
        for (int i = 0; i < 3; ++i)
            env->BitBlt(rgb->GetWritePtr(planes[i]), rgb->GetPitch(planes[i]), blank_rgb->GetReadPtr(planes[i]), blank_rgb->GetPitch(planes[i]), blank_rgb->GetRowSize(planes[i]), blank_rgb->GetHeight(planes[i]));

        env->BitBlt(alpha->GetWritePtr(), alpha->GetPitch(), blank_alpha->GetReadPtr(), blank_alpha->GetPitch(), blank_alpha->GetRowSize(), blank_alpha->GetHeight());

        if (subtitle_index > -1)
        {
            if (avctx->codec_id == AV_CODEC_ID_HDMV_PGS_SUBTITLE &&
                last_subtitle != subtitle_index - 1)
            {
                // Random access in PGS doesn't quite work without decoding some previous subtitles.
                // 5 was not enough. 10 seems to work.
                for (int s = std::max(0, subtitle_index - 10); s < subtitle_index; ++s)
                {
                    const Subtitle& sub = subtitles[s];

                    int got_subtitle = 0;

                    AVSubtitle avsub;

                    for (size_t i = 0; i < sub.packets.size(); ++i)
                    {
                        AVPacket packet = sub.packets[i];

                        avcodec_decode_subtitle2(avctx, &avsub, &got_subtitle, &packet);

                        if (got_subtitle)
                            avsubtitle_free(&avsub);
                    }
                }
            }

            last_subtitle = subtitle_index;

            const Subtitle& sub = subtitles[subtitle_index];

            int got_subtitle = 0;

            AVSubtitle avsub;

            for (size_t i = 0; i < sub.packets.size(); ++i)
            {
                AVPacket packet = sub.packets[i];

                if (avcodec_decode_subtitle2(avctx, &avsub, &got_subtitle, &packet) < 0)
                    env->ThrowError("SubImageFile: failed to decode subtitle.");

                if (got_subtitle && i < sub.packets.size() - 1)
                    env->ThrowError("SubImageFile: got subtitle sooner than expected.");
            }

            if (!got_subtitle)
                env->ThrowError("SubImageFile: got no subtitle after decoding all the packets.");

            if (avsub.num_rects == 0)
                env->ThrowError("SubImageFile: got subtitle with num_rects=0.");

            for (unsigned r = 0; r < avsub.num_rects; ++r)
            {
                AVSubtitleRect* rect = avsub.rects[r];

                if (rect->w <= 0 || rect->h <= 0 || rect->type != SUBTITLE_BITMAP)
                    continue;

                uint8_t** rect_data = rect->data;
                int* rect_linesize = rect->linesize;

                uint32_t palette[AVPALETTE_COUNT];
                memcpy(palette, rect_data[1], AVPALETTE_SIZE);
                for (size_t i = 0; i < palette_size; ++i)
                    if (SubImageFile::palette[i] != unused_colour)
                        palette[i] = SubImageFile::palette[i];

                if (gray)
                    makePaletteGray(palette);

                const uint8_t* input = rect_data[0];

                uint8_t* dst_a = alpha->GetWritePtr();
                uint8_t* dst_r = rgb->GetWritePtr(PLANAR_R);
                uint8_t* dst_g = rgb->GetWritePtr(PLANAR_G);
                uint8_t* dst_b = rgb->GetWritePtr(PLANAR_B);
                const int stride = rgb->GetPitch();

                dst_a += rect->y * stride + rect->x;
                dst_r += rect->y * stride + rect->x;
                dst_g += rect->y * stride + rect->x;
                dst_b += rect->y * stride + rect->x;

                for (int y = 0; y < rect->h; ++y)
                {
                    for (int x = 0; x < rect->w; ++x)
                    {
                        uint32_t argb = palette[input[x]];

                        dst_a[x] = (argb >> 24) & 0xff;
                        dst_r[x] = (argb >> 16) & 0xff;
                        dst_g[x] = (argb >> 8) & 0xff;
                        dst_b[x] = argb & 0xff;
                    }

                    input += rect_linesize[0];
                    dst_a += stride;
                    dst_r += stride;
                    dst_g += stride;
                    dst_b += stride;
                }
            }

            avsubtitle_free(&avsub);
        }

        env->propSetFrame(env->getFramePropsRW(rgb), "_Alpha", alpha, 0);

        if (info)
            env->propSetData(env->getFramePropsRW(rgb), "text", desc.c_str(), desc.size(), 0);

        if (subtitle_index > -1)
            last_frame = rgb;

        return rgb;
    }
};

AVSValue __cdecl Create_SubImageFile(AVSValue args, void* user_data, IScriptEnvironment* env)
{
    std::vector<int> palette;

    if (args[3].Defined())
    {
        if (args[3].ArraySize() > AVPALETTE_COUNT)
            env->ThrowError(("SubImageFile: the palette can have at most" + std::to_string(AVPALETTE_COUNT) + " elements").c_str());

        for (int i = 0; i < args[3].ArraySize(); ++i)
        {
            if (args[3][i].AsInt() < 0 || (args[3][i].AsInt() > UINT32_MAX && args[3][i].AsInt() != unused_colour))
                env->ThrowError(("SubImageFile: palette[" + std::to_string(i) + "] has an invalid value.").c_str());
            else
                palette.emplace_back(args[3][i].AsInt());
        }
    }
    else
        palette.emplace_back(-1);

    return new SubImageFile(
        args[0].AsClip(),
        args[1].AsString(),
        args[2].AsInt(-1),
        palette,
        args[4].AsBool(false),
        args[5].AsBool(false),
        args[6].AsBool(false),
        env);
}

const AVS_Linkage* AVS_linkage;

extern "C" __declspec(dllexport)
const char* __stdcall AvisynthPluginInit3(IScriptEnvironment * env, const AVS_Linkage* const vectors)
{
    AVS_linkage = vectors;

    env->AddFunction("SubImageFile", "cs[id]i[palette]i*[gray]b[info]b[flatten]b", Create_SubImageFile, 0);
    return "SubImageFile";
}
