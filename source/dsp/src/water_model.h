// Deep Water values shared by the audio processor and Aquarium's display.
#pragma once

namespace water {
constexpr unsigned kSonarMs = 560;
constexpr float kSonarFeedback = 0.52f;
constexpr float kMotionMs = 120.0f;
constexpr float kBubbleCooldownMs = 90.0f;

inline float current_hz(float depth, float current)
{
    return (0.30f + 0.75f * current) * (1.0f - 0.5f * depth);
}

inline float stray_bubbles_hz(float amount)
{
    return amount * (0.6f + 7.0f * amount);
}
}  // namespace water
