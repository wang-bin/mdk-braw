/*
 * Copyright (c) 2022 WangBin <wbsecg1 at gmail.com>
 * braw plugin for libmdk
 */
// TODO: metadata, attributes
#include "mdk/FrameReader.h"
#include "mdk/MediaInfo.h"
#include "mdk/VideoFrame.h"
#include "mdk/AudioFrame.h"
#include "BlackmagicRawAPI.h"
#include "BRawVideoBufferPool.h"
#include "ComPtr.h"
#include "BStr.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <thread>
#include <sstream>

using namespace std;
using namespace Microsoft::WRL; //ComPtr

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
    void PreparePipelineComplete(void*, HRESULT) override {
        clog << MDK_FUNCINFO << endl;
    }
    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, LPVOID*) override { return E_NOTIMPL; }
    ULONG STDMETHODCALLTYPE AddRef(void) override { return 0; }
    ULONG STDMETHODCALLTYPE Release(void) override { return 0; }
protected:
    void onPropertyChanged(const std::string& /*key*/, const std::string& /*value*/) override;
private:
    bool setupPipeline();
    bool readAt(uint64_t index);
    void parseDecoderOptions();
    void setDecoderOption(const char* key, const char* val);

    struct UserData {
        uint64_t index = 0;
        int seekId = 0;
        bool seekWaitFrame = true;
    };

    ComPtr<IBlackmagicRawFactory> factory_;
    ComPtr<IBlackmagicRawPipelineDevice> dev_;
    ComPtr<IBlackmagicRawResourceManager> resMgr_;
    ComPtr<IBlackmagicRaw> codec_;
    ComPtr<IBlackmagicRawClip> clip_;
    BlackmagicRawPipeline pipeline_ = blackmagicRawPipelineCPU; // TODO: 0 auto
    BlackmagicRawInterop interop_ = blackmagicRawInteropNone;
    string deviceName_; // opencl device can be NVIDIA(adapter name?), gfx90c(amd?). cpu device can be AVX2, AVX, SSE 4.1
    PixelFormat format_ = PixelFormat::RGBA;
    int copy_ = 0; // copy gpu resources
    BlackmagicRawResolutionScale scale_ = blackmagicRawResolutionScaleFull; // higher fps if scaled
    uint32_t scaleToW_ = 0; // closest down scale to target width
    uint32_t scaleToH_ = 0;
    uint32_t threads_ = 0;
    int64_t duration_ = 0;
    int64_t frames_ = 0;
    atomic<int> seeking_ = 0;
    atomic<uint64_t> index_ = 0; // for stepping frame forward/backward

    NativeVideoBufferPoolRef pool_;
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
    case blackmagicRawResourceFormatRGBAU8: return PixelFormat::RGBA; // "rgba"
    case blackmagicRawResourceFormatBGRAU8: return PixelFormat::BGRA; // "bgra"
    case blackmagicRawResourceFormatRGBU16: return PixelFormat::RGB48LE; // "rgb48le" NOT RECOMMENDED! 3 channel formats are not directly supported by gpu
    case blackmagicRawResourceFormatRGBAU16: return PixelFormat::RGBA64LE; // "rgba64le"
    case blackmagicRawResourceFormatBGRAU16: return PixelFormat::BGRA64LE; // "bgra64le"
    case blackmagicRawResourceFormatRGBU16Planar: return PixelFormat::RGBP16LE; // "rgbp16le"
    //case blackmagicRawResourceFormatRGBF32: return PixelFormat::RGBF32LE;
    case blackmagicRawResourceFormatRGBF32Planar: return PixelFormat::RGBPF32LE; // "rgbpf32le"
    case blackmagicRawResourceFormatBGRAF32: return PixelFormat::BGRAF32LE; // "bgraf32le"
    default:
        return PixelFormat::Unknown;
    }
}

BlackmagicRawResourceFormat from(PixelFormat fmt)
{
    switch (fmt)
    {
    case PixelFormat::RGBA: return blackmagicRawResourceFormatRGBAU8;
    case PixelFormat::BGRA: return blackmagicRawResourceFormatBGRAU8;
    case PixelFormat::RGB48LE: return blackmagicRawResourceFormatRGBU16; // NOT RECOMMENDED! 3 channel formats are not directly supported by gpu
    case PixelFormat::RGBA64LE: return blackmagicRawResourceFormatRGBAU16;
    case PixelFormat::BGRA64LE: return blackmagicRawResourceFormatBGRAU16;
    case PixelFormat::RGBP16LE: return blackmagicRawResourceFormatRGBU16Planar;
    //case PixelFormat::RGBF32LE: return blackmagicRawResourceFormatRGBF32;
    case PixelFormat::RGBPF32LE: return blackmagicRawResourceFormatRGBF32Planar;
    case PixelFormat::BGRAF32LE: return blackmagicRawResourceFormatBGRAF32;
    default:
        return blackmagicRawResourceFormatRGBAU8;
    }
}

string fourcc_to_str(uint32_t fcc)
{
    stringstream ss;
    ss << std::hex << fcc << std::dec;
    char t[] = { '\'', char((fcc>>24)&0xff), char((fcc>>16)&0xff),char((fcc>>8)&0xff),  char(fcc & 0xff), '\''};
    for (auto i : t)
        if (!i)
            return ss.str();
    return {t, std::size(t)};
}

BRawReader::BRawReader()
    : FrameReader()
{
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
    ComPtr<IBlackmagicRawConfiguration> config;
    MS_ENSURE(codec_->QueryInterface(IID_IBlackmagicRawConfiguration, (void**)&config), false);

    parseDecoderOptions();

    if (threads_ > 0)
        MS_ENSURE(config->SetCPUThreads(threads_), false);

    setupPipeline();

    if (dev_)
        MS_ENSURE(config->SetFromDevice(dev_.Get()), false);

    ComPtr<IBlackmagicRawConfigurationEx> configEx;
    MS_ENSURE(codec_->QueryInterface(IID_IBlackmagicRawConfigurationEx, (void**)&configEx), false);
    MS_ENSURE(configEx->GetResourceManager(&resMgr_), false);
    BlackmagicRawInstructionSet instruction;
    MS_ENSURE(configEx->GetInstructionSet(&instruction), false);
    clog << "BlackmagicRawInstructionSet: " << fourcc_to_str(instruction) << endl;

    // TODO: bstr_ptr
    BStr file(url().data());
    MS_ENSURE(codec_->OpenClip(file.get(), &clip_), false);
    MS_ENSURE(codec_->SetCallback(this), false);

    MediaEvent e{};
    e.category = "decoder.video";
    e.detail = "braw";
    dispatchEvent(e);

    if (scaleToW_ > 0 || scaleToH_ > 0) {
        ComPtr<IBlackmagicRawClipResolutions> res;
        MS_ENSURE(clip_->QueryInterface(IID_IBlackmagicRawClipResolutions, &res), false);
        MS_ENSURE(res->GetClosestScaleForResolution(scaleToW_, scaleToH_, false, &scale_), false);
        uint32_t retW = 0, retH = 0;
        MS_ENSURE(res->GetClosestResolutionForScale(scale_, &retW, &retH), false);
        clog << "desired resolution: " << scaleToW_ << "x" << scaleToH_ << ", result: " << retW << "x" << retH << " scale: " << fourcc_to_str(scale_) << endl;
        uint32_t count = 0;
        MS_WARN(res->GetResolutionCount(&count));
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t w = 0, h = 0;
            MS_ENSURE(res->GetResolution(i, &w, &h), false);
            clog << "supported resolution " << w << "x" << h << endl;
        }
    }

    ComPtr<IBlackmagicRawMetadataIterator> mdit;
    MS_ENSURE(clip_->GetMetadataIterator(&mdit), false);
    MediaInfo info;
    to(info, clip_, mdit.Get());
    info.video[0].codec.format = format_;
    clog << info << endl;
    duration_ = info.video[0].duration;
    frames_ = info.video[0].frames;

    changed(info); // may call seek for player.prepare(), duration_, frames_ and SetCallback() must be ready
    update(MediaStatus::Loaded);

    updateBufferingProgress(0);

    if (state() == State::Stopped) // start with pause
        update(State::Running);

    if (seeking_ == 0 && !readAt(0)) // prepare(pos) will seek in changed(MediaInfo)
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
    if (msec > duration_) // msec can be INT64_MAX, avoid overflow
        msec = duration_;
    const auto dt = (duration_ + frames_ - 1) / frames_;
    auto index = std::min<uint64_t>(frames_ * (msec + dt) / duration_, frames_ - 1);
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
    MS_ENSURE(job->Submit(), (delete data, false));
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

    MS_WARN(frame->SetResolutionScale(scale_));
    MS_ENSURE(frame->SetResourceFormat(from(format_)));
    IBlackmagicRawJob* decodeAndProcessJob = nullptr; // NOT ComPtr!
    MS_ENSURE(frame->CreateJobDecodeAndProcessFrame(nullptr, nullptr, &decodeAndProcessJob));
    job = decodeAndProcessJob;
    data = new UserData();
    data->index = index;
    data->seekId = seekId;
    data->seekWaitFrame = seekWaitFrame;
    decodeAndProcessJob->SetUserData(data);
    // will wait until submitted to gpu if using gpu decoder
    MS_ENSURE(decodeAndProcessJob->Submit(), delete data);
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
    uint8_t const* imageData[3] = {};
    void* res = nullptr;
    BlackmagicRawResourceFormat f;

    MS_ENSURE(processedImage->GetWidth(&width));
    MS_ENSURE(processedImage->GetHeight(&height));
    MS_ENSURE(processedImage->GetResourceSizeBytes(&sizeBytes));
    MS_ENSURE(processedImage->GetResource(&res));
    MS_ENSURE(processedImage->GetResourceFormat(&f));
    BlackmagicRawResourceType type;
    MS_ENSURE(processedImage->GetResourceType(&type));


    VideoFormat fmt = to(f);
    VideoFrame frame(width, height, fmt);

    if (type != blackmagicRawResourceTypeBufferCPU) {
        BlackmagicRawPipeline pipeline;
        void* context = nullptr;
        void* cmdQueue = nullptr;
        MS_ENSURE(dev_->GetPipeline(&pipeline, &context, &cmdQueue));
        if ((copy_ || !pool_) && pipeline_ != blackmagicRawPipelineCUDA) { // GetResourceHostPointer does not work for cuda?
    // TODO: less copy via [MTLBuffer newBufferWithBytesNoCopy:length:options:deallocator:] from VideoFrame.buffer(0)
            MS_WARN(resMgr_->GetResourceHostPointer(context, cmdQueue, res, type, (void**)&imageData[0]));
            frame.setBuffers(imageData);
        } else if (pipeline_ == blackmagicRawPipelineCUDA) {
#if defined(__x86_64) || defined(AMD64) || defined(_M_AMD64) || (_LP64+0) > 0 || defined(__aarch64__)
typedef unsigned long long CUdeviceptr;
#else
typedef unsigned int CUdeviceptr;
#endif
            struct {
                CUdeviceptr cuptr[3]{};
                int stride[3]{};
                PixelFormat pixfmt;
            } cuframe{};
            cuframe.pixfmt = fmt;
            cuframe.cuptr[0] = (CUdeviceptr)res;
            cuframe.stride[0] = fmt.bytesPerLine(width, 0);
            for (int i = 1; i < fmt.planeCount(); ++i) {
                cuframe.cuptr[i] = CUdeviceptr(cuframe.cuptr[i-1] + cuframe.stride[i-1] * fmt.height(height, i-1));
                cuframe.stride[i] = fmt.bytesPerLine(width, i);
            }
            processedImage->AddRef();
            auto nativeBuf = pool_->getBuffer(&cuframe, [=]{
                processedImage->Release();
            });
            if (copy_) {
                NativeVideoBuffer::MapParameter mp;
                mp.width[0] = width;
                mp.height[0] = height;
                mp.stride[0] = mp.stride[1] = cuframe.stride[0];
                auto ma = static_cast<NativeVideoBuffer::MemoryArray*>(nativeBuf->map(NativeVideoBuffer::Type::HostMemory, &mp));
                frame.setBuffers((const uint8_t **)ma->data, mp.stride); // TODO: no copy, frame.toHost()
            } else {
                frame.setNativeBuffer(nativeBuf);
            }
        } else {
            BRawVideoBuffers bb{};
            bb.width = width;
            bb.height = height;
            bb.format = fmt.format();
            bb.gpuResource = res;
            bb.type = type;
            bb.device = dev_.Get();
            bb.device->AddRef();
            bb.resMgr = resMgr_.Get();
            bb.resMgr->AddRef();
            auto dev = bb.device;
            auto resMgr = bb.resMgr;
            auto nativeBuf = pool_->getBuffer(&bb, [=]{
                dev->Release();
                resMgr->Release();
            });
            frame.setNativeBuffer(nativeBuf);
        }
    } else { // cpu
        imageData[0] = (uint8_t*)res;
        frame.setBuffers(imageData);
    }

    frame.setTimestamp(double(duration_ * index / frames_) / 1000.0);
    frame.setDuration((double)duration_/(double)frames_ / 1000.0);
    if (seekId > 0) {
        frameAvailable(VideoFrame(fmt).setTimestamp(frame.timestamp()));
    }
    bool accepted = frameAvailable(frame); // false: out of loop range and begin a new loop
    if (index == frames_ - 1 && seeking_ == 0 && accepted) {
        accepted = frameAvailable(VideoFrame().setTimestamp(TimestampEOS));
        if (accepted && !test_flag(options() & Options::ContinueAtEnd)) {
            thread([=]{ unload(); }).detach(); // unload() in current thread will result in dead lock
        }
        return;
    }
    // frameAvailable() will wait in pause state, and return when seeking, do not read the next index
    if (accepted && seeking_ == 0 && state() == State::Running && test_flag(mediaStatus() & MediaStatus::Loaded)) // seeking_ > 0: new seek created by seekComplete when continuously seeking
        readAt(index + 1);
}

bool BRawReader::setupPipeline()
{
    if (pipeline_ == blackmagicRawPipelineCPU && interop_ == blackmagicRawInteropNone && deviceName_.empty())
        return true;
    ComPtr<IBlackmagicRawPipelineIterator> pit;
    MS_ENSURE(factory_->CreatePipelineIterator(interop_, &pit), false);
    BlackmagicRawPipeline best = 0; // metal > cuda > opencl > cpu
    bool found = false;
    do {
        BlackmagicRawPipeline pipeline;
        BRawStr nameb = nullptr;
        string name = "?";
        if (SUCCEEDED(pit->GetName(&nameb))) // "CPU" or "GPU"
            name = BStr::to_string(nameb);
        MS_WARN(pit->GetPipeline(&pipeline));
        BlackmagicRawInterop interop;
        MS_WARN(pit->GetInterop(&interop));
        clog << name << " braw pipeline: " << fourcc_to_str(pipeline) << ", interop: " << fourcc_to_str(interop) << endl;
        if (!found)
            found = !pipeline_ || (pipeline == pipeline_ && interop_ == interop);
        if (!best || best == blackmagicRawPipelineCPU)
            best = pipeline;
        else if (best == blackmagicRawPipelineOpenCL && pipeline != blackmagicRawPipelineCPU)
            best = pipeline;
    } while (pit->Next() == S_OK);
    if (!pipeline_)
        pipeline_ = best;
    if (!found) {
        clog << "braw pipeline not found" << endl;
        return false;
    }

    ComPtr<IBlackmagicRawPipelineDeviceIterator> it;
    MS_ENSURE(factory_->CreatePipelineDeviceIterator(pipeline_, interop_, &it), false); // TODO: none interop
    do {
        BlackmagicRawInterop interop = 0;
        BlackmagicRawPipeline pipeline = blackmagicRawPipelineCPU;
        ComPtr<IBlackmagicRawPipelineDevice> dev;
        MS_WARN(it->GetPipeline(&pipeline));
        MS_WARN(it->GetInterop(&interop));
        MS_WARN(it->CreateDevice(&dev)); // maybe E_FAIL
        string name = "?";
        if (dev) {
            BRawStr nameb = nullptr;
            if (SUCCEEDED(dev->GetName(&nameb)))
                name = BStr::to_string(nameb);
        }
        clog << "braw pipeline: " << fourcc_to_str(pipeline) << ", interop: " << fourcc_to_str(interop) << ", device: " << name << " - " << dev.Get();
        if (!deviceName_.empty()) {
            transform(name.begin(), name.end(), name.begin(), [](unsigned char c){ return std::tolower(c);});
            if (name.find(deviceName_) != string::npos) {
                dev_ = dev;
                clog << ". Selected";
            }
        } else if (!dev_) { // select the 1st device
            dev_ = dev;
            clog << ". Selected";
        }
        clog << endl;
    } while (it->Next() == S_OK); // crash if pipeline + interop is not supported

    if (!dev_) {
        clog << "No device found for pipeline " << fourcc_to_str(pipeline_) << " + interop " << fourcc_to_str(interop_) << " + device " << deviceName_ << endl;
        return false;
    }

    ComPtr<IBlackmagicRawOpenGLInteropHelper> interop;
    MS_ENSURE(dev_->GetOpenGLInteropHelper(&interop), false);
    BlackmagicRawResourceFormat bestFormat;
    MS_ENSURE(interop->GetPreferredResourceFormat(&bestFormat), false);
    clog << "GetPreferredResourceFormat: " << to(bestFormat) << endl;

    codec_->PreparePipelineForDevice(dev_.Get(), nullptr);
    if (pipeline_ == blackmagicRawPipelineCUDA)
        pool_ = NativeVideoBufferPool::create("CUDA"); // better support d3d11/opengl/opengles
    else
        pool_ = NativeVideoBufferPool::create("BRAW"); // seems not work with cuda and opencl
    return true;
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
    MS_ENSURE(nextJob->Submit(), (delete data, false));
    return true;
}

void BRawReader::parseDecoderOptions()
{
    // decoder: name:key1=val1:key2=val2
    for (const auto& i : decoders(MediaType::Video)) {
        if (auto colon = i.find(':'); colon != string::npos) {
            if (string_view(i).substr(0, colon) == name()) {
                parse(i.data() + colon);
                return;
            }
        }
    }
}

void BRawReader::onPropertyChanged(const std::string& key, const std::string& val)
{
    if ("format" == key) {
        format_ = VideoFormat::fromName(val.data());
    } else if ("threads" == key) {
        threads_ = stoi(val);
    } else if ("gpu" == key || "pipeline" == key) {
        if ("auto"sv == val) { // metal > cuda > opencl > cpu
            pipeline_ = 0;
        } else if ("metal" == val) {
            pipeline_ = blackmagicRawPipelineMetal;
        } else if ("opencl" == val) {
            pipeline_ = blackmagicRawPipelineOpenCL; // interop must be none on mac. black host image?
        } else if ("cuda" == val) {
            pipeline_ = blackmagicRawPipelineCUDA;
        } else {
            pipeline_ = blackmagicRawPipelineCPU;
        }
    } else if ("interop" == key) {
        if (val == "opengl")
            interop_ = blackmagicRawInteropOpenGL;
        else
            interop_ = blackmagicRawInteropNone;
    } else if ("device" == key) {
        deviceName_ = val;
        transform(deviceName_.begin(), deviceName_.end(), deviceName_.begin(), [](unsigned char c){ return std::tolower(c);});
    } else if ("copy" == key) {
        copy_ = stoi(val);
    } else if ("scale" == key || "size" == key) { // widthxheight or width(height=width)
        if (val.find('x') != string::npos) { // closest scale to target resolution
            char* s = nullptr;
            scaleToW_ = strtoul(val.data(), &s, 10);
            if (s && s[0] == 'x')
                scaleToH_ = strtoul(s + 1, nullptr, 10);
        } else {
            scaleToW_ = strtoul(val.data(), nullptr, 10);
            scaleToH_ = scaleToW_;
        }
    }
}


void register_framereader_braw() {
    FrameReader::registerOnce("braw", []{return new BRawReader();});
}
MDK_NS_END

extern "C" MDK_API int mdk_plugin_load() {
    using namespace MDK_NS;
    register_framereader_braw();
    register_native_buffer_pool_braw();
    return abiVersion();
}