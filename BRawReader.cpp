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
#include "BStr.h"
#include <algorithm>
#include <atomic>
#include <iostream>
#include <thread>

using namespace std;
using namespace Microsoft::WRL; //ComPtr
// GPU: CreatePipeline/DeviceIterator

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
    bool seekTo(int64_t msec, SeekFlag flag, int id) override;
    int64_t buffered(int64_t* bytes = nullptr, float* percent = nullptr) const override;

    // IBlackmagicRawCallback
    void ReadComplete(IBlackmagicRawJob* readJob, HRESULT result, IBlackmagicRawFrame* frame) override;
    void ProcessComplete(IBlackmagicRawJob* procJob, HRESULT result, IBlackmagicRawProcessedImage* processedImage) override;
    void DecodeComplete(IBlackmagicRawJob*, HRESULT) override {}
    void TrimProgress(IBlackmagicRawJob*, float info) override {}
    void TrimComplete(IBlackmagicRawJob*, HRESULT) override {}
    void SidecarMetadataParseWarning(IBlackmagicRawClip*, BRawStr fileName, uint32_t lineNumber, BRawStr info) override {}
    void SidecarMetadataParseError(IBlackmagicRawClip*, BRawStr fileName, uint32_t lineNumber, BRawStr info) override {}
    void PreparePipelineComplete(void*, HRESULT) override {}
    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, LPVOID*) override { return E_NOTIMPL; }
    ULONG STDMETHODCALLTYPE AddRef(void) override { return 0; }
    ULONG STDMETHODCALLTYPE Release(void) override { return 0; }
private:
    bool readAt(uint64_t index);
    struct UserData {
        uint64_t index = 0;
        int seekId = 0;
        bool seekWaitFrame = true;
    };

    ComPtr<IBlackmagicRawFactory> factory_;
    ComPtr<IBlackmagicRaw> codec_;
    ComPtr<IBlackmagicRawClip> clip_;
    int64_t duration_ = 0;
    int64_t frames_ = 0;
    atomic<int> seeking_ = 0;
    atomic<uint64_t> index_ = 0; // for stepping frame forward/backward
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
    transform(s.begin(), s.end(), s.begin(), [](unsigned char c){
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
    BStr file(url().data());
    MS_ENSURE(codec_->OpenClip(file.get(), &clip_), false);
    MS_ENSURE(codec_->SetCallback(this), false);

    ComPtr<IBlackmagicRawMetadataIterator> mdit;
    MS_ENSURE(clip_->GetMetadataIterator(&mdit), false);
    MediaInfo info;
    to(info, clip_, mdit.Get());
    clog << info << endl;
    duration_ = info.video[0].duration;
    frames_ = info.video[0].frames;

    changed(info); // may call seek for player.prepare(), duration_, frames_ and SetCallback() must be ready
    update(MediaStatus::Loaded);

    updateBufferingProgress(0);

    if (state() == State::Stopped) // start with pause
        update(State::Running);

    MediaEvent e{};
    e.category = "decoder.video";
    e.detail = "braw";
    dispatchEvent(e);

    if (!readAt(0))
        return false;

    return true;
}

bool BRawReader::unload()
{
    update(MediaStatus::Unloaded);
    if (!codec_) {
        update(State::Stopped);
        return false;
    }
    // TODO: job->Abort();
    codec_->FlushJobs(); // must wait all jobs to safe release
    codec_.Reset();
    clip_.Reset();
    frames_ = 0;
    update(State::Stopped);
    return true;
}

bool BRawReader::seekTo(int64_t msec, SeekFlag flag, int id)
{
    if (!clip_)
        return false;
    // TODO: cancel running decodeProcessJob
    // TODO: seekCompelete if error later
    auto index = std::min<uint64_t>((frames_ - 1) * msec / duration_, frames_ - 1);;
    if (test_flag(flag, SeekFlag::FromNow|SeekFlag::Frame)) {
        if (msec == 0) {
            seekComplete(duration_ * index_ / frames_, id);
            return true;
        }
        index = clamp<uint64_t>(index_ + msec, 0, frames_ - 1);
    }
    seeking_++;
    clog << seeking_ << " Seek to index: " << index << endl;
    updateBufferingProgress(0);
    IBlackmagicRawJob* job = nullptr;
    MS_ENSURE(clip_->CreateJobReadFrame(index, &job), false);
    auto data = new UserData();
    data->index = index;
    data->seekId = id;
    data->seekWaitFrame = !test_flag(flag & SeekFlag::IOCompleteCallback);
    job->SetUserData(data);
    MS_ENSURE(job->Submit(), false); // FIXME: user data leak
    return true;
}

int64_t BRawReader::buffered(int64_t* bytes, float* percent) const
{
    return 0;
}

void BRawReader::ReadComplete(IBlackmagicRawJob* readJob, HRESULT result, IBlackmagicRawFrame* frame)
{ // immediately called
    updateBufferingProgress(100);
    ComPtr<IBlackmagicRawJob> job;
    job.Attach(readJob);
    uint64_t index = 0;
    int seekId = 0;
    bool seekWaitFrame = true;
    UserData* data = nullptr;
    if (SUCCEEDED(readJob->GetUserData((void**)&data)) && data) {
        index = data->index;
        seekId = data->seekId;
        seekWaitFrame = data->seekWaitFrame;
        delete data;
    }
    if (seekId > 0 && !seekWaitFrame) {
        seeking_--;
        seekComplete(duration_ * index / frames_, seekId);
    }
    MS_ENSURE(result);// TODO: stop?

    if (index == frames_ - 1) {
        update(MediaStatus::Loaded|MediaStatus::End); // Options::ContinueAtEnd
    }

    MS_ENSURE(frame->SetResourceFormat(blackmagicRawResourceFormatRGBAU8));
    IBlackmagicRawJob* decodeAndProcessJob = nullptr; // NOT ComPtr!
    MS_ENSURE(frame->CreateJobDecodeAndProcessFrame(nullptr, nullptr, &decodeAndProcessJob));
    job = decodeAndProcessJob;
    data = new UserData();
    data->index = index;
    data->seekId = seekId;
    data->seekWaitFrame = seekWaitFrame;
    decodeAndProcessJob->SetUserData(data);
    // will wait until submitted to gpu if using gpu decoder
    MS_ENSURE(decodeAndProcessJob->Submit());  // FIXME: user data leak
}

void BRawReader::ProcessComplete(IBlackmagicRawJob* procJob, HRESULT result, IBlackmagicRawProcessedImage* processedImage)
{
    ComPtr<IBlackmagicRawJob> job;
    job.Attach(procJob);
    uint64_t index = 0;
    int seekId = 0;
    bool seekWaitFrame = true;
    UserData* data = nullptr;
    if (SUCCEEDED(procJob->GetUserData((void**)&data)) && data) {
        index = data->index;
        seekId = data->seekId;
        seekWaitFrame = data->seekWaitFrame;
        delete data;
    }
    if (seekId > 0 && seekWaitFrame) {
        seeking_--;
        if (seeking_ > 0 && seekId == 0) {
            seekComplete(duration_ * index / frames_, seekId); // may create a new seek
            clog << "ProcessComplete drop @" << index << endl;
            return;
        }
        seekComplete(duration_ * index / frames_, seekId); // may create a new seek
    }
    index_ = index;

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

    if (seekId > 0) {
        frameAvailable(VideoFrame(fmt).setTimestamp(frame.timestamp()));
    }
    frameAvailable(frame);

    if (index == frames_ - 1) {
        frameAvailable(VideoFrame().setTimestamp(TimestampEOS));
        if (!test_flag(options() & Options::ContinueAtEnd)) {
            thread([=]{ unload(); }).detach(); // unload() in current thread will result in dead lock
        }
        return;
    }
    // frameAvailable() will wait in pause state, and return when seeking, do not read the next index
    if (seeking_ == 0 && state() == State::Running && test_flag(mediaStatus() & MediaStatus::Loaded)) // seeking_ > 0: new seek created by seekComplete when continuously seeking
        readAt(index + 1);
}

bool BRawReader::readAt(uint64_t index)
{
    if (!test_flag(mediaStatus(), MediaStatus::Loaded))
        return false;
    if (!clip_)
        return false;
    IBlackmagicRawJob* nextJob = nullptr;
    MS_ENSURE(clip_->CreateJobReadFrame(index, &nextJob), false);
    auto data = new UserData();
    data->index = index;
    nextJob->SetUserData(data);
    MS_ENSURE(nextJob->Submit(), false);  // FIXME: user data leak
    return true;
}


void register_framereader_braw() {
    FrameReader::registerOnce("braw", []{return new BRawReader();});
}
MDK_NS_END

extern "C" MDK_API int mdk_plugin_load() {
    using namespace MDK_NS;
    register_framereader_braw();
    return abiVersion();
}