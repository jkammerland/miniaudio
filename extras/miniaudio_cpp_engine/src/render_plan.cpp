#include "miniaudio_cpp_engine/render_plan.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <utility>

namespace miniaudio::cpp_engine {

callback_source_node::callback_source_node(channel_layout layout, void* state, source_callback callback)
    : layout_(layout), state_(state), callback_(callback) {
    if (layout.empty() || callback == nullptr)
        throw std::invalid_argument("a callback source requires a layout and callback");
}

void callback_source_node::process(process_context context, std::span<const const_planar_view> inputs,
                                   planar_view output) noexcept {
    assert(inputs.empty());
    static_cast<void>(inputs);
    callback_(state_, context, output);
}

void gain_node::process(process_context, std::span<const const_planar_view> inputs, planar_view output) noexcept {
    assert(inputs.size() == 1);
    const auto input = inputs.front();
    for (std::uint32_t channel = 0; channel < output.channels(); ++channel) {
        const auto source = input.channel(channel);
        auto destination = output.channel(channel);
        for (std::uint32_t frame = 0; frame < output.frames(); ++frame)
            destination[frame] = source[frame] * gain_;
    }
}

mixer_node::mixer_node(channel_layout layout, std::uint32_t input_count)
    : layout_(layout), input_count_(input_count) {
    if (layout.empty() || input_count == 0 || input_count > max_node_inputs)
        throw std::invalid_argument("a mixer requires 1..16 same-layout inputs");
}

void mixer_node::process(process_context, std::span<const const_planar_view> inputs, planar_view output) noexcept {
    assert(inputs.size() == input_count_);
    clear(output);
    for (const auto& input : inputs) {
        for (std::uint32_t channel = 0; channel < output.channels(); ++channel) {
            const auto source = input.channel(channel);
            auto destination = output.channel(channel);
            for (std::uint32_t frame = 0; frame < output.frames(); ++frame)
                destination[frame] += source[frame];
        }
    }
}

callback_effect_node::callback_effect_node(channel_layout input_layout, channel_layout output_layout, void* state,
                                           effect_callback callback)
    : input_layout_(input_layout), output_layout_(output_layout), state_(state), callback_(callback) {
    if (input_layout.empty() || output_layout.empty() || callback == nullptr)
        throw std::invalid_argument("a callback effect requires layouts and callback");
}

void callback_effect_node::process(process_context context, std::span<const const_planar_view> inputs,
                                   planar_view output) noexcept {
    assert(inputs.size() == 1);
    callback_(state_, context, inputs.front(), output);
}

render_plan::render_plan(stream_contract contract, std::vector<operation> operations, node_id output)
    : contract_(contract), operations_(std::move(operations)), output_(output) {}

render_plan::~render_plan() = default;

void render_plan::process(process_context context, planar_view output) noexcept {
    assert(context.frame_count == contract_.quantum);
    assert(context.sample_rate == contract_.sample_rate);
    assert(output.layout() == contract_.layout);
    assert(output.frames() == contract_.quantum);

    if (context.frame_count != contract_.quantum || context.sample_rate != contract_.sample_rate ||
        output.layout() != contract_.layout || output.frames() != contract_.quantum) {
        clear(output);
        return;
    }

    std::array<const_planar_view, max_node_inputs> inputs{};
    for (auto& operation : operations_) {
        assert(operation.inputs.size() <= inputs.size());
        for (std::size_t index = 0; index < operation.inputs.size(); ++index)
            inputs[index] = operations_[operation.inputs[index].value].output.view();

        operation.implementation->process(
            context, std::span<const const_planar_view>{inputs.data(), operation.inputs.size()}, operation.output.view());
    }

    copy(operations_[output_.value].output.view(), output);
}

render_plan_builder::render_plan_builder(stream_contract contract) : contract_(contract) {
    if (!contract.valid())
        throw std::invalid_argument("a render contract requires layout, sample rate, and quantum");
}

auto render_plan_builder::add(std::unique_ptr<node> implementation, std::span<const node_id> inputs) -> node_id {
    if (!implementation)
        throw std::invalid_argument("cannot add a null node");
    if (!implementation->accepts_contract(contract_))
        throw std::invalid_argument("node rejected the render stream contract");
    if (inputs.size() != implementation->input_count() || inputs.size() > max_node_inputs)
        throw std::invalid_argument("node input count does not match its contract");

    for (std::size_t index = 0; index < inputs.size(); ++index) {
        if (inputs[index].value >= operations_.size())
            throw std::invalid_argument("node inputs must refer to earlier nodes");
        if (!implementation->accepts_input(static_cast<std::uint32_t>(index),
                                           operations_[inputs[index].value].implementation->output_layout()))
            throw std::invalid_argument("node rejected an input channel layout");
    }

    const auto id = node_id{static_cast<std::uint32_t>(operations_.size())};
    auto copied_inputs = std::vector<node_id>{inputs.begin(), inputs.end()};
    auto output = planar_buffer{implementation->output_layout(), contract_.quantum};
    operations_.push_back({std::move(implementation), std::move(copied_inputs), std::move(output)});
    return id;
}

auto render_plan_builder::compile(node_id output) && -> std::unique_ptr<render_plan> {
    if (output.value >= operations_.size())
        throw std::invalid_argument("render plan output does not exist");
    if (operations_[output.value].implementation->output_layout() != contract_.layout)
        throw std::invalid_argument("render plan output layout does not match the stream");
    return std::unique_ptr<render_plan>{new render_plan{contract_, std::move(operations_), output}};
}

} // namespace miniaudio::cpp_engine
