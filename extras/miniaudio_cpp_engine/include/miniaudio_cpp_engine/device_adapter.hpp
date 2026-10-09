#pragma once

#include "plan_exchange.hpp"
#include "quantum_adapter.hpp"

#include "miniaudio.h"

#include <optional>

namespace miniaudio::cpp_engine {

struct playback_device_config {
    stream_contract contract;
    std::optional<ma_backend> backend;
    std::uint32_t period_size_in_frames{};
};

class playback_device {
  public:
    playback_device(plan_exchange& exchange, playback_device_config config);
    playback_device(const playback_device&) = delete;
    auto operator=(const playback_device&) -> playback_device& = delete;
    ~playback_device();

    auto initialize() noexcept -> ma_result;
    auto start() noexcept -> ma_result;
    auto stop() noexcept -> ma_result;
    void uninitialize() noexcept;

    [[nodiscard]] auto initialized() const noexcept -> bool { return device_initialized_; }
    [[nodiscard]] auto counters_after_stop() const noexcept -> render_counters { return renderer_.counters(); }

  private:
    static void data_callback(ma_device* device, void* output, const void* input, ma_uint32 frame_count) noexcept;
    void render(void* output, ma_uint32 frame_count) noexcept;

    plan_exchange& exchange_;
    playback_device_config config_;
    quantum_output_adapter renderer_;
    render_plan* active_plan_{};
    ma_context context_{};
    ma_device device_{};
    bool context_initialized_{};
    bool device_initialized_{};
};

} // namespace miniaudio::cpp_engine
