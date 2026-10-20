#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <span>

namespace pt::audio {

// Windows Spatial Sound uses listener-space coordinates: +X right, +Y up and -Z forward.
class SpatialAudioSink {
public:
    virtual ~SpatialAudioSink() = default;
    // False means that the endpoint has no dynamic object left this period; the caller keeps the voice in the bed.
    virtual bool SubmitMono(uint32_t voice_id, uint32_t frame_offset, std::span<const float> samples,
                            const glm::vec3& listener_relative_position) = 0;
};

}  // namespace pt::audio
