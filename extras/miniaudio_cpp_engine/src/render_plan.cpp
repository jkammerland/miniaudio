#include "miniaudio_cpp_engine/render_plan.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <utility>

namespace miniaudio::cpp_engine {

namespace {
void clear_suffix(planar_view output, std::uint32_t valid) noexcept {
    for (std::uint32_t channel = 0; channel < output.channels(); ++channel) {
        auto suffix = output.channel(channel).subspan(valid);
        std::fill(suffix.begin(), suffix.end(), 0.0f);
    }
}

auto valid_input(const stream_input& input, std::uint32_t quantum) noexcept -> bool {
    return input.samples.frames() == quantum && valid_stream_result(input.result, quantum) &&
           input.result.state != stream_state::failed;
}
} // namespace

void stream_node::process(process_context context, std::span<const const_planar_view> inputs,
                          planar_view output) noexcept {
    if (inputs.size() > max_node_inputs) {
        clear(output);
        return;
    }
    std::array<stream_input, max_node_inputs> streams{};
    for (std::size_t index = 0; index < inputs.size(); ++index)
        streams[index] = {inputs[index], {context.frame_count, stream_state::running}};
    clear(output);
    const auto result = process_stream(context, {streams.data(), inputs.size()}, output);
    if (!valid_stream_result(result, context.frame_count) || result.state == stream_state::failed)
        clear(output);
    else
        clear_suffix(output, result.valid_frames);
}

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

auto callback_source_node::process_stream(process_context context, std::span<const stream_input> inputs,
                                          planar_view output) noexcept -> stream_result {
    if (!inputs.empty()) {
        clear(output);
        return {0, stream_state::failed};
    }
    callback_(state_, context, output);
    return {output.frames(), stream_state::running};
}

finite_source_node::finite_source_node(channel_layout layout, void* state, finite_source_callback callback)
    : layout_(layout), state_(state), callback_(callback) {
    if (layout.empty() || callback == nullptr)
        throw std::invalid_argument("a finite source requires a layout and callback");
}

auto finite_source_node::process_stream(process_context context, std::span<const stream_input> inputs,
                                        planar_view output) noexcept -> stream_result {
    clear(output);
    if (!inputs.empty())
        return {0, stream_state::failed};
    const auto result = callback_(state_, context, output);
    if (result.valid_frames > output.frames()) {
        clear(output);
        return {0, stream_state::failed};
    }
    clear_suffix(output, result.valid_frames);
    return {result.eof ? result.valid_frames : output.frames(),
            result.eof ? stream_state::complete : stream_state::running};
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

auto gain_node::process_stream(process_context context, std::span<const stream_input> inputs,
                               planar_view output) noexcept -> stream_result {
    clear(output);
    if (inputs.size() != 1 || !valid_input(inputs.front(), context.frame_count))
        return {0, stream_state::failed};
    const auto input = inputs.front();
    for (std::uint32_t channel = 0; channel < output.channels(); ++channel)
        for (std::uint32_t frame = 0; frame < input.result.valid_frames; ++frame)
            output.channel_data(channel)[frame] = input.samples.channel_data(channel)[frame] * gain_;
    return input.result;
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

auto mixer_node::process_stream(process_context context, std::span<const stream_input> inputs,
                                planar_view output) noexcept -> stream_result {
    clear(output);
    if (inputs.size() != input_count_)
        return {0, stream_state::failed};
    stream_result result{0, stream_state::complete};
    for (const auto& input : inputs) {
        if (!valid_input(input, context.frame_count)) {
            clear(output);
            return {0, stream_state::failed};
        }
        result.valid_frames = std::max(result.valid_frames, input.result.valid_frames);
        if (input.result.state == stream_state::running)
            result.state = stream_state::running;
        for (std::uint32_t channel = 0; channel < output.channels(); ++channel)
            for (std::uint32_t frame = 0; frame < input.result.valid_frames; ++frame)
                output.channel_data(channel)[frame] += input.samples.channel_data(channel)[frame];
    }
    return result;
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

tail_free_effect_node::tail_free_effect_node(channel_layout input_layout, channel_layout output_layout, void* state,
                                             effect_callback callback)
    : input_layout_(input_layout), output_layout_(output_layout), state_(state), callback_(callback) {
    if (input_layout.empty() || output_layout.empty() || callback == nullptr)
        throw std::invalid_argument("a tail-free effect requires layouts and callback");
}

auto tail_free_effect_node::process_stream(process_context context, std::span<const stream_input> inputs,
                                           planar_view output) noexcept -> stream_result {
    clear(output);
    if (inputs.size() != 1 || !valid_input(inputs.front(), context.frame_count))
        return {0, stream_state::failed};
    const auto input = inputs.front();
    if (input.result.valid_frames != 0)
        callback_(state_, context, input.samples, output);
    clear_suffix(output, input.result.valid_frames);
    return input.result;
}

render_plan::render_plan(stream_contract contract, std::vector<operation> operations, node_id output, bool stream_processing)
    : contract_(contract), operations_(std::move(operations)), output_(output), stream_processing_(stream_processing) {}

render_plan::~render_plan() = default;

void render_plan::process(process_context context, planar_view output) noexcept {
    if (stream_processing_) {
        static_cast<void>(process_stream(context, output));
        return;
    }
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

auto render_plan::process_stream(process_context context, planar_view output) noexcept -> stream_result {
    if (!stream_processing_ || context.frame_count != contract_.quantum || context.sample_rate != contract_.sample_rate ||
        output.layout() != contract_.layout || output.frames() != contract_.quantum) {
        clear(output);
        terminal_state_ = stream_state::failed;
        return {0, terminal_state_};
    }
    if (terminal_state_ != stream_state::running) {
        clear(output);
        return {0, terminal_state_};
    }

    std::array<stream_input, max_node_inputs> inputs{};
    for (auto& operation : operations_) {
        clear(operation.output.view());
        if (operation.result.state == stream_state::complete || (draining_ && operation.inputs.empty())) {
            operation.result = {0, stream_state::complete};
            continue;
        }
        for (std::size_t index = 0; index < operation.inputs.size(); ++index) {
            const auto& producer = operations_[operation.inputs[index].value];
            inputs[index] = {producer.output.view(), producer.result};
        }
        operation.result = operation.stream_implementation->process_stream(
            context, {inputs.data(), operation.inputs.size()}, operation.output.view());
        if (!valid_stream_result(operation.result, contract_.quantum) || operation.result.state == stream_state::failed) {
            clear(output);
            terminal_state_ = stream_state::failed;
            return {0, terminal_state_};
        }
        clear_suffix(operation.output.view(), operation.result.valid_frames);
    }
    const auto result = operations_[output_.value].result;
    copy(operations_[output_.value].output.view(), output);
    terminal_state_ = result.state;
    return result;
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

auto render_plan_builder::compile_stream(node_id output) && -> std::unique_ptr<render_plan> {
    if (output.value >= operations_.size())
        throw std::invalid_argument("render plan output does not exist");
    if (operations_[output.value].implementation->output_layout() != contract_.layout)
        throw std::invalid_argument("render plan output layout does not match the stream");

    std::vector<bool> reachable(operations_.size());
    reachable[output.value] = true;
    std::size_t count = 0;
    for (auto index = operations_.size(); index-- > 0;) {
        if (!reachable[index])
            continue;
        operations_[index].stream_implementation = operations_[index].implementation->stream_interface();
        if (operations_[index].stream_implementation == nullptr)
            throw std::invalid_argument("stream graph contains a node with unknown EOF/tail behavior");
        ++count;
        for (const auto input : operations_[index].inputs)
            reachable[input.value] = true;
    }

    std::vector<render_plan::operation> compiled;
    compiled.reserve(count);
    std::vector<node_id> remap(operations_.size());
    for (std::size_t index = 0; index < operations_.size(); ++index) {
        if (!reachable[index])
            continue;
        auto& operation = operations_[index];
        for (auto& input : operation.inputs)
            input = remap[input.value];
        remap[index] = node_id{static_cast<std::uint32_t>(compiled.size())};
        compiled.push_back(std::move(operation));
    }
    return std::unique_ptr<render_plan>{new render_plan{contract_, std::move(compiled), remap[output.value], true}};
}

} // namespace miniaudio::cpp_engine
