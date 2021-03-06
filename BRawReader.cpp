/*
 * Copyright (c) 2022 WangBin <wbsecg1 at gmail.com>
 * braw plugin for libmdk
 */
#include "mdk/FrameReader.h"
#include "mdk/MediaInfo.h"
#include "mdk/VideoFrame.h"
#include "mdk/AudioFrame.h"
#include "BlackmagicRawAPI.h"
#include "ComPtr.h"
#if (__APPLE__ + 0)
# define BSTR CFStringRef
# include "cfptr.h"
#elif (__linux__ + 0)
# define BSTR const char*
#endif
#include <algorithm>
#include <atomic>
#include <iostream>

using namespace std;
using namespace Microsoft::WRL; //ComPtr
using namespace apple;

#define MS_ENSURE(f, ...) MS_CHECK(f, return __VA_ARGS__;)
#define MS_WARN(f) MS_CHECK(f)
#define MS_CHECK(f, ...)  do { \
        HRESULT __ms_hr__ = (f); \
        if (FAILED(__ms_hr__)) { \
            std::clog << #f "  ERROR@" << __LINE__ << __FUNCTION__ << ": (" << std::hex << __ms_hr__ << std::dec << ") " << std::error_code(__ms_hr__, std::system_category()).message() << std::endl << std::flush; \
            __VA_ARGS__ \
        } \
    } while (false)

MDK_NS_BEGIN

class BRawReader final : public FrameReader, public IBlackmagicRawCallback
{
public:
    BRawReader();
    ~BRawReader() override = default;
    const char* name() const override { return "BRAW"; }
    bool isSupported(const std::string& url, MediaType type) const override;
    void setTimeout(int64_t value, TimeoutCallback cb) override {}
    bool load() override;
    bool unload() override;
    void stop() override;
    bool seek(int64_t msec, SeekFlag flag = SeekFlag::Default, function<void(int64_t)> cb = nullptr) override;
    int64_t buffered(int64_t* bytes = nullptr, float* percent = nullptr) const override;

    // IBlackmagicRawCallback
    void ReadComplete(IBlackmagicRawJob* readJob, HRESULT result, IBlackmagicRawFrame* frame) override;
    void ProcessComplete(IBlackmagicRawJob* procJob, HRESULT result, IBlackmagicRawProcessedImage* processedImage) override;
    void DecodeComplete(IBlackmagicRawJob*, HRESULT) override {}
    void TrimProgress(IBlackmagicRawJob*, float) override {}
    void TrimComplete(IBlackmagicRawJob*, HRESULT) override {}
    void SidecarMetadataParseWarning(IBlackmagicRawClip*, CFStringRef, uint32_t, CFStringRef) override {}
    void SidecarMetadataParseError(IBlackmagicRawClip*, CFStringRef, uint32_t, CFStringRef) override {}
    void PreparePipelineComplete(void*, HRESULT) override {}
    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, LPVOID*) override { return E_NOTIMPL; }
    ULONG STDMETHODCALLTYPE AddRef(void) override { return 0; }
    ULONG STDMETHODCALLTYPE Release(void) override { return 0; }
private:
    struct UserData {
        uint64_t index;
    };

    ComPtr<IBlackmagicRawFactory> factory_;
    ComPtr<IBlackmagicRaw> codec_;
    ComPtr<IBlackmagicRawClip> clip_;
    int64_t duration_ = 0;
    int64_t frames_ = 0;
    atomic<uint64_t> index_ = 0;
};

void to(MediaInfo& info, ComPtr<IBlackmagicRawClip> clip, IBlackmagicRawMetadataIterator* i)
{
    info.format = "braw";
    // TODO: metadata

    info.streams = 1;
    VideoCodecParameters vcp;
    MS_WARN(clip->GetWidth((uint32_t*)&vcp.width));
    MS_WARN(clip->GetHeight((uint32_t*)&vcp.height));
    vcp.codec = "braw";
    //vcp.codec_tag =
    MS_WARN(clip->GetFrameRate(&vcp.frame_rate));
    VideoStreamInfo vsi;
    vsi.index = 0;
    MS_WARN(clip->GetFrameCount((uint64_t*)&vsi.frames));
    vsi.duration = vsi.frames * (1000.0 / vcp.frame_rate);
    vsi.codec = vcp;
    info.video.reserve(1);
    info.video.push_back(vsi);
    info.duration = vsi.duration;

    ComPtr<IBlackmagicRawClipAudio> audio;
    if (FAILED(clip->QueryInterface(IID_IBlackmagicRawClipAudio, &audio)))
        return;
    info.streams++;
    AudioCodecParameters acp;
    AudioStreamInfo asi;
    asi.index = 1;
    acp.codec = "pcm";
    MS_WARN(audio->GetAudioChannelCount((uint32_t*)&acp.channels));
    MS_WARN(audio->GetAudioSampleRate((uint32_t*)&acp.sample_rate));
    MS_WARN(audio->GetAudioSampleCount((uint64_t*)&asi.frames));
    uint32_t v = 0;
    MS_WARN(audio->GetAudioBitDepth(&v));
    switch (v) {
    case 8:
        acp.format = AudioFormat::SampleFormat::U8;
        break;
    case 16:
        acp.format = AudioFormat::SampleFormat::S16;
        break;
    case 24:
        acp.format = AudioFormat::SampleFormat::S24;
        break;
    case 32:
        acp.format = AudioFormat::SampleFormat::S32;
        break;
    }
    asi.duration = asi.frames * 1000 / acp.sample_rate;
    asi.codec = acp;
    info.audio.reserve(1);
    info.audio.push_back(asi);

    info.duration = std::max<int64_t>(info.duration, asi.duration);
}

PixelFormat to(BlackmagicRawResourceFormat fmt)
{
    switch (fmt)
    {
    case blackmagicRawResourceFormatRGBAU8: return PixelFormat::RGBA;
    case blackmagicRawResourceFormatBGRAU8: return PixelFormat::BGRA;
    case blackmagicRawResourceFormatRGBU16: return PixelFormat::RGB48LE;
    case blackmagicRawResourceFormatRGBAU16: return PixelFormat::RGBA64LE;
    case blackmagicRawResourceFormatBGRAU16: return PixelFormat::BGRA64LE;
    //case blackmagicRawResourceFormatRGBF32: return PixelFormat::RGBF;
    default:
        return PixelFormat::Unknown;
    }
}

BRawReader::BRawReader()
    : FrameReader()
{
#if (__APPLE__ + 0)
#ifdef _Pragma
_Pragma("weak CreateBlackmagicRawFactoryInstance")
#endif
    if (!CreateBlackmagicRawFactoryInstance) {
        clog << "BlackmagicRawAPI.framework is not found!" << endl;
        return;
    }
#endif
    factory_ = CreateBlackmagicRawFactoryInstance();
    if (!factory_) {
        clog << "BlackmagicRawAPI is not available!" << endl;
        return;
    }
}

bool BRawReader::isSupported(const std::string& url, MediaType type) const
{
    if (url.empty())
        return true;
    auto dot = url.rfind('.');
    if (dot == string::npos)
        return true;
    string s = url.substr(dot + 1);
    transform(s.begin(), s.end(), s.begin(), [](char c){
        return std::tolower(c);
    });
    return s == "braw";
}

bool BRawReader::load()
{
    if (!factory_)
        return false;
    MS_ENSURE(factory_->CreateCodec(&codec_), false);
    // TODO: bstr_ptr
    cfptr<CFStringRef> file = CFStringCreateWithCString(nullptr, url().data(), kCFStringEncodingUTF8);
    MS_ENSURE(codec_->OpenClip(file, &clip_), false);

    ComPtr<IBlackmagicRawMetadataIterator> mdit;
    MS_ENSURE(clip_->GetMetadataIterator(&mdit), false);
    MediaInfo info;
    to(info, clip_, mdit.Get());
    changed(info);
    clog << info << endl;
    duration_ = info.video[0].duration;
    frames_ = info.video[0].frames;

    update(MediaStatus::Loaded);

    MS_ENSURE(codec_->SetCallback(this), false);
    if (state() == State::Stopped) // start with pause
        update(State::Running);

    IBlackmagicRawJob* job = nullptr;
    MS_ENSURE(clip_->CreateJobReadFrame(index_, &job), false);
    auto data = new UserData();
    data->index = index_;
    job->SetUserData(data);
    MS_ENSURE(job->Submit(), false); // FIXME: user data leak

    return true;
}

bool BRawReader::unload()
{
    //codec->FlushJobs();
    // TODO: job->Abort()
    frames_ = 0;
    return true;
}

void BRawReader::stop()
{
    update(State::Stopped);
}

bool BRawReader::seek(int64_t msec, SeekFlag flag, std::function<void(int64_t)> cb)
{
    return false;
}

int64_t BRawReader::buffered(int64_t* bytes, float* percent) const
{
    return 0;
}

void BRawReader::ReadComplete(IBlackmagicRawJob* readJob, HRESULT result, IBlackmagicRawFrame* frame)
{ // immediately called
    ComPtr<IBlackmagicRawJob> job;
    job.Attach(readJob);
    uint64_t index = 0;
    UserData* data = nullptr;
    if (SUCCEEDED(readJob->GetUserData((void**)&data)) && data) {
        index = data->index;
        delete data;
    }
    MS_ENSURE(result);// TODO: stop?
    MS_ENSURE(frame->SetResourceFormat(blackmagicRawResourceFormatRGBAU8));
    IBlackmagicRawJob* decodeAndProcessJob = nullptr; // NOT ComPtr!
    MS_ENSURE(frame->CreateJobDecodeAndProcessFrame(nullptr, nullptr, &decodeAndProcessJob));
    job = decodeAndProcessJob;
    data = new UserData();
    data->index = index;
    decodeAndProcessJob->SetUserData(data);
    // will wait until submitted to gpu if using gpu decoder
    MS_ENSURE(decodeAndProcessJob->Submit());  // FIXME: user data leak
    //readJob->Release();
    // read the next frame.
}

void BRawReader::ProcessComplete(IBlackmagicRawJob* procJob, HRESULT result, IBlackmagicRawProcessedImage* processedImage)

{
    ComPtr<IBlackmagicRawJob> job;
    job.Attach(procJob);
    uint64_t index = 0;
    UserData* data = nullptr;
    if (SUCCEEDED(procJob->GetUserData((void**)&data)) && data) {
        index = data->index;
        delete data;
    }
    MS_ENSURE(result);// TODO: stop?
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int sizeBytes = 0;
    uint8_t const* imageData[1] = {};
    BlackmagicRawResourceFormat f;
    MS_ENSURE(processedImage->GetWidth(&width));
    MS_ENSURE(processedImage->GetHeight(&height));
    MS_ENSURE(processedImage->GetResourceSizeBytes(&sizeBytes));
    MS_ENSURE(processedImage->GetResource((void**)imageData));
    MS_ENSURE(processedImage->GetResourceFormat(&f));
    BlackmagicRawResourceType type;
    MS_ENSURE(processedImage->GetResourceType(&type));
    VideoFormat fmt = to(f);
    VideoFrame frame(width, height, fmt);
    frame.setBuffers(imageData);
    frame.setTimestamp(double(duration_ * index / frames_) / 1000.0);
    frame.setDuration((double)duration_/(double)frames_);
    frameAvailable(frame);
    // TODO: EOS frame
    if (index == frames_ - 1) {
        frameAvailable(VideoFrame().setTimestamp(TimestampEOS));
        return;
    }
    IBlackmagicRawJob* nextJob = nullptr;
    MS_ENSURE(clip_->CreateJobReadFrame(++index_, &nextJob));
    data = new UserData();
    data->index = index_;
    nextJob->SetUserData(data);
    MS_ENSURE(nextJob->Submit());  // FIXME: user data leak
}


void register_framereader_braw() {
    FrameReader::registerOnce("braw", []{return new BRawReader();});
}
namespace { // DCE
static const struct register_at_load_time_if_no_dce {
    inline register_at_load_time_if_no_dce() { register_framereader_braw();}
} s;
}
MDK_NS_END

MDK_API int mdk_plugin_load() {
    using namespace MDK_NS;
    register_framereader_braw();
    return abiVersion();
}