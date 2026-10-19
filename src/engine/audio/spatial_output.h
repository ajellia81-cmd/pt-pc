#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "engine/audio/spatial_audio.h"

namespace pt::audio {

class SpatialOutput : public SpatialAudioSink {
public:
    using RenderFunction = std::function<void(float* interleaved, uint32_t frames, uint32_t channels)>;
    using SpatialRenderFunction = std::function<void(float* interleaved, uint32_t frames, uint32_t channels, SpatialAudioSink* sink)>;

    SpatialOutput();
    ~SpatialOutput();
    SpatialOutput(const SpatialOutput&) = delete;
    SpatialOutput& operator=(const SpatialOutput&) = delete;

    bool Open(uint32_t sample_rate, RenderFunction render, SpatialRenderFunction spatial_render = {});
    void Close();
    bool SubmitMono(uint32_t voice_id, uint32_t frame_offset, std::span<const float> mono, const glm::vec3& position) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
