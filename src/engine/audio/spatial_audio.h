#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <span>

namespace pt::audio {

// The mixer owns the samples for the duration of SubmitMono.  Windows Spatial Sound takes coordinates in listener space:
// +X right, +Y up and +Z in front.  Keeping this interface free of Windows headers lets the normal SDL backends stay intact.
class SpatialAudioSink {
public:
    virtual ~SpatialAudioSink() = default;
    // Returns false when the endpoint has no remaining dynamic object for this render period; callers then retain
    // the voice in the conventional bed instead of silently dropping it.
    virtual bool SubmitMono(std::span<const float> samples, const glm::vec3& listener_relative_position) = 0;
};

}  // namespace pt::audio
