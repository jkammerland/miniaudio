#pragma once

#include "audio_buffer.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace miniaudio::cpp_engine {

inline constexpr std::size_t max_node_inputs = 16;

struct stream_contract {
    channel_layout layout;
    std::uint32_t sample_rate{};
    std::uint32_t quantum{};

    [[nodiscard]] auto valid() const noexcept -> bool {
        return !layout.empty() && sample_rate != 0 && quantum != 0;
    }

    friend auto operator==(const stream_contract&, const stream_contract&) noexcept -> bool = default;
};

struct process_context {
    std::uint64_t first_frame{};
    std::uint32_t frame_count{};
    std::uint32_t sample_rate{};
};

class node {
  public:
    virtual ~node() = default;

    [[nodiscard]] virtual auto accepts_contract(const stream_contract&) const noexcept -> bool { return true; }
    [[nodiscard]] virtual auto output_layout() const noexcept -> const channel_layout& = 0;
    [[nodiscard]] virtual auto input_count() const noexcept -> std::uint32_t = 0;
    [[nodiscard]] virtual auto accepts_input(std::uint32_t index, const channel_layout& layout) const noexcept -> bool = 0;
    virtual void process(process_context context, std::span<const const_planar_view> inputs, planar_view output) noexcept = 0;
};

using source_callback = void (*)(void*, process_context, planar_view) noexcept;
using effect_callback = void (*)(void*, process_context, const_planar_view, planar_view) noexcept;

class callback_source_node final : public node {
  public:
    callback_source_node(channel_layout layout, void* state, source_callback callback);

    [[nodiscard]] auto output_layout() const noexcept -> const channel_layout& override { return layout_; }
    [[nodiscard]] auto input_count() const noexcept -> std::uint32_t override { return 0; }
    [[nodiscard]] auto accepts_input(std::uint32_t, const channel_layout&) const noexcept -> bool override { return false; }
    void process(process_context context, std::span<const const_planar_view> inputs, planar_view output) noexcept override;

  private:
    channel_layout layout_;
    void* state_{};
    source_callback callback_{};
};

class gain_node final : public node {
  public:
    gain_node(channel_layout layout, float gain) : layout_(layout), gain_(gain) {
        if (layout.empty())
            throw std::invalid_argument("a gain node requires a channel layout");
    }

    [[nodiscard]] auto output_layout() const noexcept -> const channel_layout& override { return layout_; }
    [[nodiscard]] auto input_count() const noexcept -> std::uint32_t override { return 1; }
    [[nodiscard]] auto accepts_input(std::uint32_t index, const channel_layout& layout) const noexcept -> bool override {
        return index == 0 && layout == layout_;
    }
    void process(process_context, std::span<const const_planar_view> inputs, planar_view output) noexcept override;

  private:
    channel_layout layout_;
    float gain_{};
};

class mixer_node final : public node {
  public:
    mixer_node(channel_layout layout, std::uint32_t input_count);

    [[nodiscard]] auto output_layout() const noexcept -> const channel_layout& override { return layout_; }
    [[nodiscard]] auto input_count() const noexcept -> std::uint32_t override { return input_count_; }
    [[nodiscard]] auto accepts_input(std::uint32_t index, const channel_layout& layout) const noexcept -> bool override {
        return index < input_count_ && layout == layout_;
    }
    void process(process_context, std::span<const const_planar_view> inputs, planar_view output) noexcept override;

  private:
    channel_layout layout_;
    std::uint32_t input_count_{};
};

class callback_effect_node final : public node {
  public:
    callback_effect_node(channel_layout input_layout, channel_layout output_layout, void* state, effect_callback callback);

    [[nodiscard]] auto output_layout() const noexcept -> const channel_layout& override { return output_layout_; }
    [[nodiscard]] auto input_count() const noexcept -> std::uint32_t override { return 1; }
    [[nodiscard]] auto accepts_input(std::uint32_t index, const channel_layout& layout) const noexcept -> bool override {
        return index == 0 && layout == input_layout_;
    }
    void process(process_context context, std::span<const const_planar_view> inputs, planar_view output) noexcept override;

  private:
    channel_layout input_layout_;
    channel_layout output_layout_;
    void* state_{};
    effect_callback callback_{};
};

struct node_id {
    std::uint32_t value{};
    friend auto operator==(node_id, node_id) noexcept -> bool = default;
};

class render_plan {
  public:
    render_plan(const render_plan&) = delete;
    auto operator=(const render_plan&) -> render_plan& = delete;
    render_plan(render_plan&&) noexcept = default;
    auto operator=(render_plan&&) noexcept -> render_plan& = default;
    ~render_plan();

    [[nodiscard]] auto contract() const noexcept -> const stream_contract& { return contract_; }
    void process(process_context context, planar_view output) noexcept;

  private:
    friend class render_plan_builder;

    struct operation {
        std::unique_ptr<node> implementation;
        std::vector<node_id> inputs;
        planar_buffer output;
    };

    render_plan(stream_contract contract, std::vector<operation> operations, node_id output);

    stream_contract contract_;
    std::vector<operation> operations_;
    node_id output_{};
};

class render_plan_builder {
  public:
    explicit render_plan_builder(stream_contract contract);

    [[nodiscard]] auto add(std::unique_ptr<node> implementation, std::span<const node_id> inputs = {}) -> node_id;
    [[nodiscard]] auto compile(node_id output) && -> std::unique_ptr<render_plan>;

  private:
    stream_contract contract_;
    std::vector<render_plan::operation> operations_;
};

} // namespace miniaudio::cpp_engine
