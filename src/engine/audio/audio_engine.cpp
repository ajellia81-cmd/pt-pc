#include "engine/audio/audio_engine.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <thread>
#include <string>

#include "engine/audio/channel_layout.h"
#include "engine/core/log.h"
#include "engine/platform/sdl_diag.h"

#if defined(_WIN32)
#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <spatialaudioclient.h>
#include <windows.h>
#include <wrl/client.h>
#endif

namespace pt::audio {

#if defined(_WIN32)
// Owns a native Windows Spatial Sound stream. Dynamic objects carry game emitters, while two static objects retain
// the legacy stereo bed (music, UI and effect returns). Windows performs the endpoint-specific Atmos/Sonic rendering.
class WindowsSpatialOutput final : public SpatialAudioSink {
public:
    bool Open(uint32_t rate, AudioOutput::RenderFunction render) {
        if (rate != 48000 || !render) return false;
        render_ = std::move(render);
        const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(init) && init != RPC_E_CHANGED_MODE) return false;
        com_initialized_ = SUCCEEDED(init);
        Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
            FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device_)) ||
            FAILED(device_->Activate(__uuidof(ISpatialAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client_.GetAddressOf())))) {
            Close(); return false;
        }
        Microsoft::WRL::ComPtr<IAudioFormatEnumerator> formats;
        if (FAILED(client_->GetSupportedAudioObjectFormatEnumerator(&formats))) { Close(); return false; }
        UINT32 count = 0;
        if (FAILED(formats->GetCount(&count))) { Close(); return false; }
        for (UINT32 i = 0; i < count; ++i) {
            WAVEFORMATEX* candidate = nullptr;
            if (SUCCEEDED(formats->GetFormat(i, &candidate)) && candidate && candidate->nSamplesPerSec == rate &&
                candidate->nChannels == 1 && candidate->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
                format_ = *candidate;
                CoTaskMemFree(candidate);
                break;
            }
            if (candidate) CoTaskMemFree(candidate);
        }
        if (format_.nSamplesPerSec != rate) { Close(); return false; }
        UINT32 max_dynamic = 0;
        if (FAILED(client_->GetMaxDynamicObjectCount(&max_dynamic)) || max_dynamic == 0) { Close(); return false; }
        max_dynamic_ = std::min<UINT32>(max_dynamic, 32);
        event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        stop_ = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        if (!event_ || !stop_) { Close(); return false; }
        SpatialAudioObjectRenderStreamActivationParams params{};
        params.ObjectFormat = &format_;
        params.StaticObjectTypeMask = static_cast<AudioObjectType>(AudioObjectType_FrontLeft | AudioObjectType_FrontRight);
        params.MinDynamicObjectCount = 0;
        params.MaxDynamicObjectCount = max_dynamic_;
        params.Category = AudioCategory_GameEffects;
        params.EventHandle = event_;
        PROPVARIANT activation{};
        activation.vt = VT_BLOB;
        activation.blob.cbSize = sizeof(params);
        activation.blob.pBlobData = reinterpret_cast<BYTE*>(&params);
        if (FAILED(client_->ActivateSpatialAudioStream(&activation, IID_PPV_ARGS(&stream_)))) { Close(); return false; }
        if (FAILED(stream_->ActivateSpatialAudioObject(AudioObjectType_FrontLeft, &bed_left_)) ||
            FAILED(stream_->ActivateSpatialAudioObject(AudioObjectType_FrontRight, &bed_right_))) { Close(); return false; }
        dynamic_.resize(max_dynamic_);
        for (auto& object : dynamic_) {
            if (FAILED(stream_->ActivateSpatialAudioObject(AudioObjectType_Dynamic, &object))) { Close(); return false; }
        }
        if (FAILED(stream_->Start())) { Close(); return false; }
        running_ = true;
        worker_ = std::thread([this] { Pump(); });
        LogInfo("audio: native Windows Spatial Sound stream active ({} dynamic objects)", max_dynamic_);
        return true;
    }

    void Close() {
        running_ = false;
        if (stop_) SetEvent(stop_);
        if (worker_.joinable()) worker_.join();
        if (stream_) stream_->Stop();
        dynamic_.clear(); bed_left_.Reset(); bed_right_.Reset(); stream_.Reset(); client_.Reset(); device_.Reset(); render_ = {};
        if (event_) { CloseHandle(event_); event_ = nullptr; }
        if (stop_) { CloseHandle(stop_); stop_ = nullptr; }
        if (com_initialized_) { CoUninitialize(); com_initialized_ = false; }
    }

    ~WindowsSpatialOutput() override { Close(); }

    bool SubmitMono(std::span<const float> samples, const glm::vec3& position) override {
        // BeginUpdatingAudioObjects grants only this many dynamic buffers for the current render period.
        if (next_dynamic_ >= dynamic_buffers_.size()) return false;
        DynamicBuffer& buffer = dynamic_buffers_[next_dynamic_++];
        if (!buffer.samples) return false;
        const size_t frames = buffer.frames;
        float* out = buffer.samples;
        const size_t copied = std::min(samples.size(), frames);
        std::memcpy(out, samples.data(), copied * sizeof(float));
        std::fill(out + copied, out + frames, 0.0f);
        buffer.object->SetPosition(position.x, position.y, position.z);
        buffer.object->SetVolume(1.0f);
        return true;
    }

private:
    void Pump() {
        // The stream was activated in an MTA. Every worker thread that uses its COM interfaces must also join it.
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) {
            running_ = false;
            return;
        }
        const bool uninitialize = SUCCEEDED(apartment);
        const HANDLE waits[2] = {event_, stop_};
        while (WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0 && running_) {
            UINT32 available = 0, frames = 0;
            if (FAILED(stream_->BeginUpdatingAudioObjects(&available, &frames)) || frames == 0) break;
            next_dynamic_ = 0;
            BYTE* left = nullptr; BYTE* right = nullptr; UINT32 left_bytes = 0, right_bytes = 0;
            if (FAILED(bed_left_->GetBuffer(&left, &left_bytes)) || FAILED(bed_right_->GetBuffer(&right, &right_bytes)) ||
                !left || !right || left_bytes < frames * sizeof(float) || right_bytes < frames * sizeof(float)) {
                stream_->EndUpdatingAudioObjects();
                break;
            }
            std::memset(left, 0, left_bytes); std::memset(right, 0, right_bytes);
            const size_t dynamic_count = std::min<size_t>(available, dynamic_.size());
            dynamic_buffers_.resize(dynamic_count);
            for (size_t i = 0; i < dynamic_count; ++i) {
                BYTE* bytes = nullptr; UINT32 byte_count = 0;
                if (SUCCEEDED(dynamic_[i]->GetBuffer(&bytes, &byte_count)) && bytes) {
                    std::memset(bytes, 0, byte_count);
                    dynamic_buffers_[i] = {dynamic_[i].Get(), reinterpret_cast<float*>(bytes), byte_count / sizeof(float)};
                } else {
                    dynamic_buffers_[i] = {};
                }
            }
            bed_.assign(static_cast<size_t>(frames) * 2, 0.0f);
            render_(bed_.data(), frames, 2, this);
            auto* l = reinterpret_cast<float*>(left); auto* r = reinterpret_cast<float*>(right);
            for (UINT32 i = 0; i < frames; ++i) { l[i] = bed_[i * 2]; r[i] = bed_[i * 2 + 1]; }
            dynamic_buffers_.clear();
            if (FAILED(stream_->EndUpdatingAudioObjects())) break;
        }
        running_ = false;
        if (uninitialize) CoUninitialize();
    }

    AudioOutput::RenderFunction render_;
    struct DynamicBuffer { ISpatialAudioObject* object = nullptr; float* samples = nullptr; size_t frames = 0; };
    Microsoft::WRL::ComPtr<IMMDevice> device_;
    Microsoft::WRL::ComPtr<ISpatialAudioClient> client_;
    Microsoft::WRL::ComPtr<ISpatialAudioObjectRenderStream> stream_;
    Microsoft::WRL::ComPtr<ISpatialAudioObject> bed_left_, bed_right_;
    std::vector<Microsoft::WRL::ComPtr<ISpatialAudioObject>> dynamic_;
    std::vector<DynamicBuffer> dynamic_buffers_;
    WAVEFORMATEX format_{};
    HANDLE event_ = nullptr, stop_ = nullptr;
    std::thread worker_;
    std::atomic<bool> running_{false};
    size_t next_dynamic_ = 0;
    UINT32 max_dynamic_ = 0;
    std::vector<float> bed_;
    bool com_initialized_ = false;
};
#endif

AudioOutput::AudioOutput() = default;

AudioOutput::~AudioOutput() {
    Close();
}

bool AudioOutput::Open(uint32_t sample_rate, bool surround, bool spatial_audio, RenderFunction render) {
    Close();
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        LogError("audio: SDL audio init failed (surround requested: {}): {} (audio drivers built in: {})", surround,
                 SDL_GetError(), SdlCompiledAudioDrivers());
        return false;
    }
    render_ = std::move(render);
#if defined(_WIN32)
    if (spatial_audio) {
        spatial_output_ = std::make_unique<WindowsSpatialOutput>();
        if (spatial_output_->Open(sample_rate, render_)) {
            channels_ = 2;
            return true;
        }
        spatial_output_.reset();
        LogWarn("audio: Windows Spatial Sound unavailable on the default endpoint; using normal output");
    }
#else
    if (spatial_audio) LogInfo("audio: Dolby Atmos requested on a non-Windows build; using normal output");
#endif
    SDL_AudioSpec preferred_spec{};
    const bool queried = SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &preferred_spec, nullptr);
    const int preferred_channels = queried ? preferred_spec.channels : -1;
    if (!queried) {
        LogWarn("audio: could not query preferred playback format (surround requested: {}): {}", surround, SDL_GetError());
    }
    LogInfo("audio: opening playback (surround requested: {}, preferred channels: {})", surround, preferred_channels);

    std::string last_error;
    const auto probes = AudioOutputProbeOrder(preferred_channels, surround);
    for (const uint32_t probe_channels : probes) {
        if (probe_channels == 0) break;

        const SDL_AudioSpec requested_spec{SDL_AUDIO_F32, static_cast<int>(probe_channels), static_cast<int>(sample_rate)};
        const SDL_AudioDeviceID device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &requested_spec);
        if (!device) {
            last_error = SDL_GetError();
            LogWarn("audio: {}-channel device open failed (surround requested: {}, preferred channels: {}): {}",
                    probe_channels, surround, preferred_channels, last_error);
            continue;
        }

        SDL_AudioSpec opened_spec{};
        if (!SDL_GetAudioDeviceFormat(device, &opened_spec, nullptr)) {
            last_error = SDL_GetError();
            LogWarn("audio: could not query opened {}-channel device (surround requested: {}): {}", probe_channels,
                    surround, last_error);
            SDL_CloseAudioDevice(device);
            continue;
        }

        const uint32_t app_channels = AudioOutputChannelsForDevice(opened_spec.channels, surround);
        if (surround && probe_channels > 2 && app_channels == 2) {
            LogInfo("audio: {}-channel request opened as {} channels; probing next layout", probe_channels,
                    opened_spec.channels);
            SDL_CloseAudioDevice(device);
            continue;
        }

        const SDL_AudioSpec app_spec{SDL_AUDIO_F32, static_cast<int>(app_channels), static_cast<int>(sample_rate)};
        SDL_AudioStream* stream = SDL_CreateAudioStream(&app_spec, nullptr);
        if (!stream) {
            last_error = SDL_GetError();
            LogWarn("audio: could not create {}-channel stream for {}-channel device: {}", app_channels,
                    opened_spec.channels, last_error);
            SDL_CloseAudioDevice(device);
            continue;
        }
        if (!SDL_BindAudioStream(device, stream)) {
            last_error = SDL_GetError();
            LogWarn("audio: could not bind {}-channel stream to {}-channel device: {}", app_channels,
                    opened_spec.channels, last_error);
            SDL_DestroyAudioStream(stream);
            SDL_CloseAudioDevice(device);
            continue;
        }

        SDL_AudioSpec bound_src{};
        SDL_AudioSpec bound_dst{};
        if (!SDL_GetAudioStreamFormat(stream, &bound_src, &bound_dst)) {
            last_error = SDL_GetError();
            LogWarn("audio: could not query bound stream format: {}", last_error);
            SDL_UnbindAudioStream(stream);
            SDL_DestroyAudioStream(stream);
            SDL_CloseAudioDevice(device);
            continue;
        }
        const uint32_t bound_app_channels = AudioOutputChannelsForDevice(bound_dst.channels, surround);
        if (surround && probe_channels > 2 && bound_app_channels == 2) {
            LogInfo("audio: bound {}-channel stream has {} output channels; probing next layout", app_channels,
                    bound_dst.channels);
            SDL_UnbindAudioStream(stream);
            SDL_DestroyAudioStream(stream);
            SDL_CloseAudioDevice(device);
            continue;
        }
        if (bound_src.channels != static_cast<int>(bound_app_channels)) {
            const SDL_AudioSpec adjusted_src{SDL_AUDIO_F32, static_cast<int>(bound_app_channels), static_cast<int>(sample_rate)};
            if (!SDL_SetAudioStreamFormat(stream, &adjusted_src, nullptr)) {
                last_error = SDL_GetError();
                LogWarn("audio: could not match stream input to {} actual output channels: {}", bound_app_channels,
                        last_error);
                SDL_UnbindAudioStream(stream);
                SDL_DestroyAudioStream(stream);
                SDL_CloseAudioDevice(device);
                continue;
            }
        }
        if (!SDL_SetAudioStreamGetCallback(stream, Callback, this)) {
            last_error = SDL_GetError();
            LogWarn("audio: could not install playback callback: {}", last_error);
            SDL_UnbindAudioStream(stream);
            SDL_DestroyAudioStream(stream);
            SDL_CloseAudioDevice(device);
            continue;
        }

        device_id_ = device;
        stream_ = stream;
        channels_ = bound_app_channels;
        if (!SDL_ResumeAudioDevice(device_id_)) {
            last_error = SDL_GetError();
            LogError("audio: could not resume playback device (surround requested: {}, preferred channels: {}, requested channels: {}, actual channels: {}, renderer channels: {}): {}",
                     surround, preferred_channels, probe_channels, bound_dst.channels, channels_, last_error);
            Close();
            LogSdlAudioDevices(false);
            return false;
        }
        LogInfo("audio: playback at {} Hz (surround requested: {}, preferred channels: {}, requested channels: {}, actual channels: {}, renderer channels: {})",
                sample_rate, surround, preferred_channels, probe_channels, bound_dst.channels, channels_);
        LogSdlAudioDevices(false);
        return true;
    }

    LogError("audio: cannot open playback device (surround requested: {}, preferred channels: {}, last error: {})", surround,
             preferred_channels, last_error.empty() ? SDL_GetError() : last_error);
    render_ = {};
    channels_ = 2;
    LogSdlAudioDevices(false);
    return false;
}

void AudioOutput::Close() {
    if (spatial_output_) {
        spatial_output_->Close();
        spatial_output_.reset();
    }
    if (stream_) {
        SDL_UnbindAudioStream(stream_);
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
    if (device_id_ != 0) {
        SDL_CloseAudioDevice(device_id_);
        device_id_ = 0;
    }
    render_ = {};
    channels_ = 2;
}

void AudioOutput::Callback(void* user, SDL_AudioStream* stream, int additional, int) {
    auto* output = static_cast<AudioOutput*>(user);
    if (additional <= 0 || !output->render_) {
        return;
    }
    const uint32_t channels = output->channels_;
    const uint32_t frames = static_cast<uint32_t>(additional) / (channels * sizeof(float));
    if (frames == 0) {
        return;
    }
    output->buffer_.resize(static_cast<size_t>(frames) * channels);
    output->render_(output->buffer_.data(), frames, channels, nullptr);
    SDL_PutAudioStreamData(stream, output->buffer_.data(), static_cast<int>(output->buffer_.size() * sizeof(float)));
}

}
