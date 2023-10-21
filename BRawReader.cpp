/*
 * Copyright (c) 2022-2023 WangBin <wbsecg1 at gmail.com>
 * braw plugin for libmdk
 */
// TODO: set frame attributes, read current index with attributes applied. AttrName.Range/List/ReadOnly. use forcc as name?
// hdr (gamma, gamut) attributes
// TODO: save sidecar, trim clip
//#define BRAW_MAJOR 2

#include "mdk/FrameReader.h"
#include "mdk/MediaInfo.h"
#include "mdk/VideoFrame.h"
#include "mdk/AudioFrame.h"
#include "BlackmagicRawAPI.h"
#include "BRawVideoBufferPool.h"
#include "ComPtr.h"
#include "BStr.h"
#include "Variant.h"
#include "base/Hash.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <mutex>
#include <thread>

using namespace std;
using namespace Microsoft::WRL; //ComPtr

#define MS_ENSURE(f, ...) MS_CHECK(f, return __VA_ARGS__;)
#define MS_WARN(f) MS_CHECK(f)
#define MS_CHECK(f, ...)  do { \
        const HRESULT __ms_hr__ = (f); \
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
    void PreparePipelineComplete(void*, HRESULT ret) override {
        MS_WARN(ret);
        clog << MDK_FUNCINFO << endl;
    }
    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, LPVOID*) override { return E_NOTIMPL; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 0; }
    ULONG STDMETHODCALLTYPE Release() override { return 0; }
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
        unordered_map<string,string> metadata;
        unordered_map<string,string> attributes;
    };

    ComPtr<IBlackmagicRawFactory> factory_;
    ComPtr<IBlackmagicRaw> codec_;
    ComPtr<IBlackmagicRawPipelineDevice> dev_;
    ComPtr<IBlackmagicRawResourceManager> resMgr_;
    ComPtr<IBlackmagicRawClip> clip_;
    void* processedRes_ = nullptr; // cpu readable(gpu writable?) in copy mode
    BlackmagicRawResourceType processedType_ = 0;
    BlackmagicRawPipeline pipeline_ = blackmagicRawPipelineCPU; // set by user only
    BlackmagicRawInterop interop_ = blackmagicRawInteropNone;
    string deviceName_; // opencl device can be NVIDIA(adapter name? nv does not work and slow?), gfx90c(amd?). cpu device can be AVX2, AVX, SSE 4.1
    PixelFormat format_ = PixelFormat::RGBA;
    int copy_ = 0; // copy gpu resources. only works for cuda pipeline, otherwise still copies.
    BlackmagicRawResolutionScale scale_ = blackmagicRawResolutionScaleFull; // higher fps if scaled
    uint32_t scaleToW_ = 0; // closest down scale to target width
    uint32_t scaleToH_ = 0;
    uint32_t threads_ = 0;
    int64_t duration_ = 0;
    int64_t frames_ = 0;
    atomic<int> seeking_ = 0;
    atomic<uint64_t> index_ = 0; // for stepping frame forward/backward

    NativeVideoBufferPoolRef pool_;
    mutex unload_mtx_;
    shared_ptr<bool> loaded_;
};

static void get_attributes(IBlackmagicRawClip* clip, function<void(const string&,const string&)>&& cb)
{
    ComPtr<IBlackmagicRawClipProcessingAttributes> a;
    MS_ENSURE(clip->QueryInterface(IID_IBlackmagicRawClipProcessingAttributes, &a));

    uint32_t count = 0;
    bool ro = false;
    if (SUCCEEDED(a->GetISOList(nullptr, &count, &ro))) { // from metadata?
        vector<uint32_t> iso(count);
        a->GetISOList(&iso[0], &count, &ro);
        string vals;
        for (auto i : iso)
            vals += std::to_string(i) + ',';
        cb("ISOList", vals);
    }

    VARIANT val;
    VariantInit(&val);
    VARIANT valMin, valMax;
    VariantInit(&valMin);
    VariantInit(&valMax);
    for (auto i : {
        blackmagicRawClipProcessingAttributeColorScienceGen          , //= /* 'csgn' */ 0x6373676E,	// u16
        blackmagicRawClipProcessingAttributeGamma                    , //= /* 'gama' */ 0x67616D61,	// string
        blackmagicRawClipProcessingAttributeGamut                    , //= /* 'gamt' */ 0x67616D74,	// string
        blackmagicRawClipProcessingAttributeToneCurveContrast        , //= /* 'tcon' */ 0x74636F6E,	// float
        blackmagicRawClipProcessingAttributeToneCurveSaturation      , //= /* 'tsat' */ 0x74736174,	// float
        blackmagicRawClipProcessingAttributeToneCurveMidpoint        , //= /* 'tmid' */ 0x746D6964,	// float
        blackmagicRawClipProcessingAttributeToneCurveHighlights      , //= /* 'thih' */ 0x74686968,	// float
        blackmagicRawClipProcessingAttributeToneCurveShadows         , //= /* 'tsha' */ 0x74736861,	// float
        blackmagicRawClipProcessingAttributeToneCurveVideoBlackLevel , //= /* 'tvbl' */ 0x7476626C,	// u16
        blackmagicRawClipProcessingAttributeToneCurveBlackLevel      , //= /* 'tblk' */ 0x74626C6B,	// float
        blackmagicRawClipProcessingAttributeToneCurveWhiteLevel      , //= /* 'twit' */ 0x74776974,	// float
        blackmagicRawClipProcessingAttributeHighlightRecovery        , //= /* 'hlry' */ 0x686C7279,	// u16
        blackmagicRawClipProcessingAttributeAnalogGainIsConstant     , //= /* 'agic' */ 0x61676963,	// u16
        blackmagicRawClipProcessingAttributeAnalogGain               , //= /* 'gain' */ 0x6761696E,	// float
        blackmagicRawClipProcessingAttributePost3DLUTMode            , //= /* 'lutm' */ 0x6C75746D,	// string
        blackmagicRawClipProcessingAttributeEmbeddedPost3DLUTName    , //= /* 'emln' */ 0x656D6C6E,	// string
        blackmagicRawClipProcessingAttributeEmbeddedPost3DLUTTitle   , //= /* 'emlt' */ 0x656D6C74,	// string
        blackmagicRawClipProcessingAttributeEmbeddedPost3DLUTSize    , //= /* 'emls' */ 0x656D6C73,	// u16
        blackmagicRawClipProcessingAttributeEmbeddedPost3DLUTData    , //= /* 'emld' */ 0x656D6C64,	// float array, size*size*size*3 elements
        blackmagicRawClipProcessingAttributeSidecarPost3DLUTName     , //= /* 'scln' */ 0x73636C6E,	// string
        blackmagicRawClipProcessingAttributeSidecarPost3DLUTTitle    , //= /* 'sclt' */ 0x73636C74,	// string
        blackmagicRawClipProcessingAttributeSidecarPost3DLUTSize     , //= /* 'scls' */ 0x73636C73,	// u16
        blackmagicRawClipProcessingAttributeSidecarPost3DLUTData     , //= /* 'scld' */ 0x73636C64,	// float array, size*size*size*3 elements
        blackmagicRawClipProcessingAttributeGamutCompressionEnable   , //= /* 'gace' */ 0x67616365	// u16, 0=disabled, 1=enabled
    }) {
        if (SUCCEEDED(a->GetClipAttributeList(i, nullptr, &count, &ro))) {
            vector<VARIANT> vars(count);
            a->GetClipAttributeList(i, &vars[0], &count, &ro);
            string vals;
            for (const auto& v : vars) {
                vals += to_string(v) + ',';
            }
            cb(FOURCC_name(i) + ".list", vals);
        } else if (SUCCEEDED(a->GetClipAttributeRange(i, &valMin, &valMax, &ro))) {
            const auto vals = to_string(valMin) + '+' + to_string(valMax);
            cb(FOURCC_name(i) + ".range", vals);
        }
        if (SUCCEEDED(a->GetClipAttribute(i, &val))) {
            auto v = to_string(val);
            if (!v.empty())
                cb(FOURCC_name(i), v);
        }
    }
}

static void get_attributes(IBlackmagicRawFrame* frame, unordered_map<string,string>& md)
{
    ComPtr<IBlackmagicRawFrameProcessingAttributes> a;
    MS_ENSURE(frame->QueryInterface(IID_IBlackmagicRawFrameProcessingAttributes, &a));
    VARIANT val;
    VariantInit(&val);
    VARIANT valMin, valMax;
    VariantInit(&valMin);
    VariantInit(&valMax);
    uint32_t count = 0;
    bool ro = false;
    for (auto i : {
        blackmagicRawFrameProcessingAttributeWhiteBalanceKelvin      , //= /* 'wbkv' */ 0x77626B76,	// u32
        blackmagicRawFrameProcessingAttributeWhiteBalanceTint        , //= /* 'wbtn' */ 0x7762746E,	// s16
        blackmagicRawFrameProcessingAttributeExposure                , //= /* 'expo' */ 0x6578706F,	// float
        blackmagicRawFrameProcessingAttributeISO                     , //= /* 'fiso' */ 0x6669736F,	// u32. GetISOList or GetFrameAttributeList
        blackmagicRawFrameProcessingAttributeAnalogGain              , //= /* 'agpf' */ 0x61677066	// float
    }) {
        if (SUCCEEDED(a->GetFrameAttributeList(i, nullptr, &count, &ro))) {
            vector<VARIANT> vars(count);
            a->GetFrameAttributeList(i, &vars[0], &count, &ro);
            string vals;
            for (const auto& v : vars) {
                vals += to_string(v) + ',';
            }
            md.emplace(FOURCC_name(i) + ".list", vals);
        } else if (SUCCEEDED(a->GetFrameAttributeRange(i, &valMin, &valMax, &ro))) {
            const auto vals = to_string(valMin) + '+' + to_string(valMax);
            md.emplace(FOURCC_name(i) + ".range", vals);
            //clog << FOURCC_name(i) + ".range: " + vals << endl;
        }
        if (SUCCEEDED(a->GetFrameAttribute(i, &val))) {
            auto v = to_string(val);
            if (!v.empty())
                md.emplace(FOURCC_name(i), v);
            //clog << FOURCC_name(i) + " = " + v << endl;
        }
    }
}

static void read_metadata(IBlackmagicRawMetadataIterator* i, unordered_map<string,string>& md)
{
    if (!i)
        return;
    BRawStr key;
    while (SUCCEEDED(i->GetKey(&key))) {
        VARIANT val;
        VariantInit(&val);
        if (FAILED(i->GetData(&val)))
            break;
        auto v = to_string(val);
        if (!v.empty())
            md.emplace(BStr::to_string(key), v);
        VariantClear(&val);
        i->Next();
    //CFRelease(key);// FIXME: leak detected in xcode in 3.1 sdk
    }
    //for (const auto& [k, v] : md)
    //    clog << k << " = " << v << endl;
}

void to(MediaInfo& info, const ComPtr<IBlackmagicRawClip>& clip)
{
    info.format = "braw";

    ComPtr<IBlackmagicRawMetadataIterator> mdit;
    if (SUCCEEDED(clip->GetMetadataIterator(&mdit)))
        read_metadata(mdit.Get(), info.metadata);

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
    switch (fmt) {
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
    switch (fmt) {
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


BRawReader::BRawReader()
    : FrameReader()
{
    factory_ = CreateBlackmagicRawFactoryInstance();
    if (!factory_) {
        clog << "BlackmagicRawAPI is not available!" << endl;
        return;
    }

}

bool BRawReader::load()
{
    if (!factory_)
        return false;
    MS_ENSURE(factory_->CreateCodec(&codec_), false);
    MS_ENSURE(codec_->SetCallback(this), false);
    ComPtr<IBlackmagicRawConfiguration> config;
    MS_ENSURE(codec_->QueryInterface(IID_IBlackmagicRawConfiguration, (void**)&config), false);
#if (BRAW_MAJOR + 0) >= 3
    BRawStr ver;
    if (SUCCEEDED(config->GetVersion(&ver)))
        clog << "IBlackmagicRawConfiguration Version: " << BStr::to_string(ver) << endl;
#endif
    parseDecoderOptions();


    setupPipeline();

    if (dev_) {
        MS_ENSURE(config->SetFromDevice(dev_.Get()), false); // ~ dev-GetPipeline(ctx,cmdQ) + cfg->SetPipeline(ctx, cmdQ)
        MS_WARN(codec_->PreparePipelineForDevice(dev_.Get(), nullptr)); // speed-up the 1st frame
    }
    if (threads_ > 0)
        MS_ENSURE(config->SetCPUThreads(threads_), false);

    ComPtr<IBlackmagicRawConfigurationEx> configEx;
    MS_ENSURE(codec_->QueryInterface(IID_IBlackmagicRawConfigurationEx, (void**)&configEx), false);
    MS_ENSURE(configEx->GetResourceManager(&resMgr_), false);
    BlackmagicRawInstructionSet instruction;
    MS_ENSURE(configEx->GetInstructionSet(&instruction), false);
    clog << "BlackmagicRawInstructionSet: " << FOURCC_name(instruction) << endl;

    BStr file(url().data());
    MS_ENSURE(codec_->OpenClip(file.get(), &clip_), false);

    loaded_ = make_shared<bool>();

    MediaEvent e{};
    e.category = "decoder.video";
    e.detail = "braw";
    dispatchEvent(e);

    if (scaleToW_ > 0 || scaleToH_ > 0) {
        ComPtr<IBlackmagicRawClipResolutions> res;
        MS_ENSURE(clip_->QueryInterface(IID_IBlackmagicRawClipResolutions, &res), false);
        uint32_t retW = 0, retH = 0;
        res->GetClosestScaleForResolution(scaleToW_, scaleToH_
#if (BRAW_MAJOR + 0) < 3
            , false
#endif
            , &scale_);
        clog << "desired resolution: " << scaleToW_ << "x" << scaleToH_ << ", result: " << retW << "x" << retH << " scale: " << FOURCC_name(scale_) << endl;
        uint32_t count = 0;
        MS_WARN(res->GetResolutionCount(&count));
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t w = 0, h = 0;
            MS_ENSURE(res->GetResolution(i, &w, &h), false);
            clog << "supported resolution " << w << "x" << h << endl;
        }
    }

    MediaInfo info;
    to(info, clip_);
    info.video[0].codec.format = format_;
    clog << info << endl;
    duration_ = info.video[0].duration;
    frames_ = info.video[0].frames;

    changed(info); // may call seek for player.prepare(), duration_, frames_ and SetCallback() must be ready
    update(MediaStatus::Loaded);

    get_attributes(clip_.Get(), [=](const string& k, const string& v){
        setProperty(k, v);
    });
    updateBufferingProgress(0);

    if (state() == State::Stopped) // start with pause
        update(State::Running);

    if (seeking_ == 0 && !readAt(0)) // prepare(pos) will seek in changed(MediaInfo)
        return false;

    return true;
}

bool BRawReader::unload()
{
    {
        lock_guard lock(unload_mtx_);
        update(MediaStatus::Unloaded);
    }
    if (!codec_) {
        update(State::Stopped);
        return false;
    }
    // TODO: job->Abort();
    codec_->FlushJobs(); // must wait all jobs to safe release
    frameAvailable(VideoFrame().setTimestamp(TimestampEOS)); // clear vo frames
    if (processedRes_) {
        BlackmagicRawPipeline pipeline;
        void* context = nullptr;
        void* cmdQueue = nullptr;
        MS_WARN(dev_->GetPipeline(&pipeline, &context, &cmdQueue));
        MS_WARN(resMgr_->ReleaseResource(context, cmdQueue, processedRes_, processedType_));
        processedRes_ = nullptr;
    }
    loaded_.reset();
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
        index = (uint64_t)clamp<int64_t>((int64_t)index_ + msec, 0, frames_ - 1);
    }
    seeking_++;
    clog << seeking_ << " Seek to index: " << index << " from " << index_<< endl;
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

    ComPtr<IBlackmagicRawMetadataIterator> mit;
    if (SUCCEEDED(frame->GetMetadataIterator(&mit)))
        read_metadata(mit.Get(), data->metadata);
    get_attributes(frame, data->attributes);

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
    index_ = index; // update index_ before seekComplete because pending seek may be executed in seekCompleted
    if (seekId > 0 && seekWaitFrame) {
        seeking_--;
        lock_guard lock(unload_mtx_);
        if (test_flag(mediaStatus() & MediaStatus::Loaded)) {
            if (seeking_ > 0/* && seekId == 0*/) { // ?
                seekComplete(duration_ * index / frames_, seekId); // may create a new seek
                clog << "ProcessComplete drop @" << index << endl;
                return;
            }
            seekComplete(duration_ * index / frames_, seekId); // may create a new seek
        }
    }

    MS_ENSURE(result);// TODO: stop?
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t sizeBytes = 0;
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


    const VideoFormat fmt = to(f);
    VideoFrame frame(width, height, fmt);

    if (type != blackmagicRawResourceTypeBufferCPU) {
        void* context = nullptr;
        void* cmdQueue = nullptr;
        MS_ENSURE(processedImage->GetResourceContextAndCommandQueue(&context, &cmdQueue));
        if (copy_ || type == blackmagicRawResourceTypeBufferOpenCL || !pool_) {
#if !defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__)
            // iOS: -[MTLToolsResource validateCPUWriteable]:135: failed assertion `resourceOptions (0x20) specify MTLResourceStorageModePrivate, which is not CPU accessible.'
            MS_WARN(resMgr_->GetResourceHostPointer(context, cmdQueue, res, type, (void**)&imageData[0])); // metal can get host ptr?
#endif
            if (!imageData[0]) { // cuda, ocl
                if (!processedRes_) {
                    processedType_ = type;
                    clog << "try CPU readable GPU writable memory for " << FOURCC_name(type) << endl;
                    MS_ENSURE(resMgr_->CreateResource(context, cmdQueue, sizeBytes, type, blackmagicRawResourceUsageReadCPUWriteGPU, &processedRes_));
                    MS_WARN(resMgr_->GetResourceHostPointer(context, cmdQueue, processedRes_, type, (void**)&imageData[0])); // why host ptr is null?
                    if (!imageData[0]) {
                        clog << "try CPU readable CPU writable memory for " << FOURCC_name(type) << endl;
                        MS_WARN(resMgr_->ReleaseResource(context, cmdQueue, processedRes_, processedType_));
                        MS_ENSURE(resMgr_->CreateResource(context, cmdQueue, sizeBytes, type, blackmagicRawResourceUsageReadCPUWriteCPU, &processedRes_)); // processed image is on cpu readable memory?
                    }
                }
                MS_WARN(resMgr_->GetResourceHostPointer(context, cmdQueue, processedRes_, type, (void**)&imageData[0]));
                if (imageData[0])
                    MS_ENSURE(resMgr_->CopyResource(context, cmdQueue, res, type, processedRes_, type, sizeBytes, false));
            }
    // TODO: less copy via [MTLBuffer newBufferWithBytesNoCopy:length:options:deallocator:] from VideoFrame.buffer(0)
            //clog << FOURCC_name(type) << " processedRes_ " << processedRes_ << " res " << res << " imageData: " << (void*)imageData[0] << endl;
            if (imageData[0])
                frame.setBuffers(imageData);
        }
    } else { // cpu
        imageData[0] = (uint8_t*)res;
        frame.setBuffers(imageData);
    }
    if (!imageData[0]) {
        if (type == blackmagicRawResourceTypeBufferCUDA) {
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
            weak_ptr<bool> wp = loaded_;
            auto nativeBuf = pool_->getBuffer(&cuframe, [=]{
                auto sp = wp.lock();
                if (!sp)
                    return;
                processedImage->Release(); // invalid if braw objects are destroyed in unload()?
            });
            if (copy_) {
                NativeVideoBuffer::MapParameter mp;
                mp.width[0] = width;
                mp.height[0] = height;
                mp.stride[0] = mp.stride[1] = cuframe.stride[0];
                auto ma = static_cast<NativeVideoBuffer::MemoryArray*>(nativeBuf->map(NativeVideoBuffer::Type::HostMemory, &mp));
                frame.setBuffers((const uint8_t **)ma->data, mp.stride);
            } else {
                frame.setNativeBuffer(nativeBuf);
            }
        } else {
            BRawVideoBuffers bb{};
            bb.width = width;
            bb.height = height;
            bb.format = fmt.format();
            bb.image = processedImage;
            bb.bytes = sizeBytes;
            bb.gpuResource = res;
            bb.type = type;
            bb.device = dev_.Get();
            bb.resMgr = resMgr_.Get();
            //auto dev = bb.device;
            //auto resMgr = bb.resMgr;
            //dev->AddRef();
            //resMgr->AddRef();
            processedImage->AddRef();
            auto nativeBuf = pool_->getBuffer(&bb, [=]{
                //dev->Release();
                //resMgr->Release();
                processedImage->Release(); // invalid if braw objects are destroyed in unload()?
            });
            frame.setNativeBuffer(nativeBuf);
        }
    }

    frame.setTimestamp(double(duration_ * index / frames_) / 1000.0);
    frame.setDuration((double)duration_/(double)frames_ / 1000.0);

    lock_guard lock(unload_mtx_);
    // FIXME: stop playback in onFrame() callback results in dead lock in braw(FlushJobs will wait this function finished)
    if (seekId > 0) {
        frameAvailable(VideoFrame(fmt).setTimestamp(frame.timestamp()));
    }
    bool accepted = frameAvailable(frame); // false: out of loop range and begin a new loop
    if ((index == frames_ - 1 && seeking_ == 0 && accepted) || !test_flag(mediaStatus() & MediaStatus::Loaded)) {
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
        clog << name << " braw pipeline: " << FOURCC_name(pipeline) << ", interop: " << FOURCC_name(interop) << endl;
        if (!found)
            found = !pipeline_ || (pipeline == pipeline_ && interop_ == interop);
        if (!best || best == blackmagicRawPipelineCPU)
            best = pipeline;
        else if (best == blackmagicRawPipelineOpenCL && pipeline != blackmagicRawPipelineCPU)
            best = pipeline;
    } while (pit->Next() == S_OK);
    auto pipeline_selected = pipeline_;
    if (!pipeline_selected)
        pipeline_selected = best;
    if (!found) {
        clog << "braw pipeline not found" << endl;
        return false;
    }

    ComPtr<IBlackmagicRawPipelineDeviceIterator> it;
    MS_ENSURE(factory_->CreatePipelineDeviceIterator(pipeline_selected, interop_, &it), false);
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
        clog << "braw pipeline: " << FOURCC_name(pipeline) << ", interop: " << FOURCC_name(interop) << ", device: '" << name << "' - " << dev.Get();
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
        clog << "No device found for pipeline " << FOURCC_name(pipeline_selected) << " + interop " << FOURCC_name(interop_) << " + device " << deviceName_ << endl;
        return false;
    }

    if (pipeline_selected == blackmagicRawPipelineOpenCL && copy_ == 0) {
        clog << "OpenCL does not support 0-copy, copy mode will be used" << endl;
    }

    ComPtr<IBlackmagicRawOpenGLInteropHelper> interop;
    MS_ENSURE(dev_->GetOpenGLInteropHelper(&interop), false);
    BlackmagicRawResourceFormat bestFormat;
    MS_ENSURE(interop->GetPreferredResourceFormat(&bestFormat), false);
    clog << "GetPreferredResourceFormat: " << to(bestFormat) << endl;

    if (pipeline_selected == blackmagicRawPipelineCUDA)
        pool_ = NativeVideoBufferPool::create("CUDA"); // better support d3d11/opengl/opengles
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
    const auto k = detail::fnv1ah32::hash(key);
    switch (k) {
    case "format"_svh:
        format_ = VideoFormat::fromName(val.data());
        return;
    case "threads"_svh:
        threads_ = stoi(val);
        return;
    case "gpu"_svh:
    case "pipeline"_svh: {
        if ("auto"sv == val) { // metal > cuda > opencl > cpu
            pipeline_ = 0;
        } else if ("metal" == val) {
            pipeline_ = blackmagicRawPipelineMetal;
        } else if ("opencl" == val) {
            pipeline_ = blackmagicRawPipelineOpenCL; // interop must be none
        } else if ("cuda" == val) {
            pipeline_ = blackmagicRawPipelineCUDA;
        } else {
            pipeline_ = blackmagicRawPipelineCPU;
        }
    }
        return;
    case "interop"_svh:
        interop_ = val == "opengl" ? blackmagicRawInteropOpenGL : blackmagicRawInteropNone;
        return;
    case "device"_svh:
        deviceName_ = val;
        transform(deviceName_.begin(), deviceName_.end(), deviceName_.begin(), [](unsigned char c){ return std::tolower(c);});
        return;
    case "copy"_svh:
        copy_ = stoi(val);
        return;
    case "scale"_svh:
    case "size"_svh: { // widthxheight or width(height=width)
        if (val.find('x') != string::npos) { // closest scale to target resolution
            char* s = nullptr;
            scaleToW_ = strtoul(val.data(), &s, 10);
            if (s && s[0] == 'x')
                scaleToH_ = strtoul(s + 1, nullptr, 10);
        } else if (val.find("1/") == 0) {
            const auto s = atoi(&val[2]);
            if (s >= 6) {
                scale_ = blackmagicRawResolutionScaleEighth;
            } else if (s >= 3) {
                scale_ = blackmagicRawResolutionScaleQuarter;
            } else if (s > 1) {
                scale_ = blackmagicRawResolutionScaleHalf;
            } else {
                scale_ = blackmagicRawResolutionScaleFull;
            }
        } else {
            scaleToW_ = strtoul(val.data(), nullptr, 10);
            scaleToH_ = scaleToW_;
        }
    }
        return;
    case "decoder"_svh:
    case "video.decoder"_svh:
        parse(val.data());
        return;
    }
    // TODO: if property is a clip attribute and exists, guess VARIANT then SetClipAttribute(). if not exist, insert only
}


void register_framereader_braw() {
    FrameReader::registerOnce("BRAW", []{return new BRawReader();}, {{"braw"}});
}
MDK_NS_END

// project name must be braw or mdk-braw
MDK_PLUGIN(braw) {
    using namespace MDK_NS;
    register_framereader_braw();
    return MDK_ABI_VERSION;
}