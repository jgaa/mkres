#pragma once

#include <ranges>
#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <functional>
#include <span>
#include <cstdint>

#include<zlib.h>

// Generic transformig view template for

template <class T>
concept input_range_of_bytes =
    std::ranges::input_range<T>
    && sizeof(std::ranges::range_value_t<T>) == 1;


template <class R, class T = std::ranges::range_value_t<R>>
concept output_range_of_bytes =
    std::ranges::output_range<R, T>
    && sizeof(std::ranges::range_value_t<R>) == 1;

template<typename R>
concept output_buffer_range_of_bytes = output_range_of_bytes<R>
                                      && std::ranges::contiguous_range<R>
    ;

template<typename R, typename T = std::ranges::range_value_t<R>>
concept input_buffer_range_of_bytes = input_range_of_bytes<R>
                                      && std::ranges::contiguous_range<R>
    ;



// The compressor works with buffers of a known size, not iterators.
// This because zlib require C buffers for input and output.
template <typename T, typename Out = std::span<T>,
         std::invocable feedT = std::function<Out()>>
class GzipCompressor {
    enum class CompressionState {
        COMPRESSING,
        INPUT_FINISHED,
        COMPRESSION_FINISHED,
    };
public:

    GzipCompressor(Out out, feedT feed, bool gzip = true)
        : out_{out}, feed_{std::move(feed)} {

        // https://itecnote.com/tecnote/setup-zlib-to-generate-gzipped-data/
        static constexpr int windowsBits = 15;
        static constexpr int GZIP_ENCODING = 16;

        if (deflateInit2(&strm_, Z_BEST_COMPRESSION, Z_DEFLATED,
                         windowsBits | (gzip ? GZIP_ENCODING : 0),
                         9,
                         Z_DEFAULT_STRATEGY) != Z_OK) {
            throw std::runtime_error{"deflateInit2() failed"};
        }
    }

    // True if there might be more data
    std::span<T> next() {
        if (state_ == CompressionState::COMPRESSION_FINISHED) {
            return {}; // Nothing to do
        }

        if (state_ == CompressionState::COMPRESSING) {
            prepareOutput();
        }

        // Feed the compressor with input until we don't have any more.
        // Return when the output buffer is full or when we are done.
        while(true) {
            if (strm_.avail_in == 0 && state_ == CompressionState::COMPRESSING) {
                prepareInput();
            }

            const auto op = (state_ == CompressionState::INPUT_FINISHED)
                                ? Z_FINISH : Z_NO_FLUSH;

            const auto result = deflate(&strm_, op);

            if (result == Z_STREAM_END) {
                state_ = CompressionState::COMPRESSION_FINISHED;
                const auto bytes = out_.size() - strm_.avail_out;
                assert(bytes == strm_.total_out);
                return out_.subspan(0, bytes);
            }

            if (result != Z_OK) {
                throw std::runtime_error{"deflate() failed"};
            }

            if (strm_.avail_out == 0) {
                return out_;
            }

            if (state_ == CompressionState::COMPRESSING && strm_.avail_in == 0) {
                if (in_.empty()) {
                    state_ = CompressionState::INPUT_FINISHED;
                } else {
                    prepareInput();
                }
            }
        }
    }

private:
    void prepareOutput() {
        strm_.next_out = reinterpret_cast<uint8_t *>(out_.data());
        strm_.avail_out = out_.size();
    }

    void prepareInput() {
        assert(state_ == CompressionState::COMPRESSING);
        in_ = feed_();
        strm_.next_in = reinterpret_cast<uint8_t *>(in_.data());
        strm_.avail_in = in_.size();

        if (in_.empty()) {
            state_ = CompressionState::INPUT_FINISHED;
        }
    }

    CompressionState state_ = CompressionState::COMPRESSING;
    z_stream strm_ = {};
    Out out_;
    feedT feed_;
    decltype(feed_()) in_;
};



template <typename T, input_range_of_bytes R, size_t bufferLen, class P>
class Transformer
{
public:
    using value_type = std::remove_cvref_t<std::ranges::range_value_t<R>>;

    class Iterator {
    public:
        using value_type = Transformer::value_type;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::input_iterator_tag;
        using reference = value_type;

        Iterator() = default;

        Iterator(Transformer& parent)
            : end_{false}, parent_{&parent}
        {
            advance();
        }

        Iterator(const Iterator& it) {
            // this constructor does not make sense for
            // iterators that are not at end()
            assert(it.end_ == true);
        };

        Iterator(Iterator&& it) noexcept
            : end_{it.end_}, parent_{it.parent_}
        {
            it.end_ = true;
            it.parent_ = {};
        }

        ~Iterator() = default;

        // NB: std::ranges::end() will not compile without this.
        //     I have not seen this documented anywhere!
        Iterator& operator = (const Iterator& it) noexcept {
            assert(it.end_); // dont use active iterators
            return *this;
        }

        Iterator& operator = (Iterator&& it) noexcept {
            end_     = it.end_;
            parent_  = it.parent_;
            it.end_  = true;
            it.parent_ = {};
            return *this;
        }

        // We cannot have more than one non-end iterator, so
        // it only makes sense to test if both are at the end.
        bool operator == (const Iterator& it) const noexcept {
            return end_ && it.end_;
        }

        value_type operator * () const {
            throwIfEnd();
            assert(parent_);
            return parent_->value();
        }

        // ++T
        Iterator& operator++ () {
            advance();
            return *this;
        }

        // T++
        Iterator& operator++ (int) {
            assert(false && "use ++it");
            advance();
            return Iterator{}; // Prev itertor is no longer valid!
        }

    private:
        void advance() {
            throwIfEnd();
            assert(parent_);
            end_ = parent_->advance() == false;
        }

        void throwIfEnd() const {
            assert(!end_);
            if (end_) {
                throw std::runtime_error{"Cannot dereference iterator == end()"};
            }
        }

        bool end_ = true;
        Transformer *parent_{};
    };

    Transformer(R input)
        : input_{input}, it_{input_.begin()}
    {
    }

    // Only work once.
    auto begin() {
        if (!used_) {
            return Iterator{*this};
        }
        assert(false);
        used_ = true;
        return end();
    }

    auto end() {
        return Iterator{};
    }

    auto && feeder() {
        return [this] {
            return std::span<T>{};
        };
    }

private:
    // Move the iterator, over the data that is ready, forward.
    bool advance() {
        if (ready_it_ != ready_end_) {
            ++ready_it_;
        } else {
            auto ready_range = processor_.next();
            ready_it_ = ready_range.begin();
            ready_end_ = ready_range.end();
        }

        return ready_it_ != ready_end_;
    }

    value_type value() {
        assert(ready_it_ != ready_end_);
        return *ready_it_;
    }

    std::span<T> feed() {

        auto to = unprocessed_buffer_.begin();
        const auto to_end = unprocessed_buffer_.end();

        size_t count = 0;
        for(; it_ != input_.end() && to != to_end; ++it_, ++to, ++count) {
            *to = *it_;
        }

        return {unprocessed_buffer_.data(), count};
    }


    bool used_ = false;
    R input_;
    decltype(input_.begin()) it_;
    std::array<T, bufferLen> unprocessed_buffer_; //
    std::array<T, bufferLen> ready_buffer_;
    P processor_ = P{ready_buffer_, [this] {return feed();}};
    decltype(unprocessed_buffer_.begin()) up_it_ = unprocessed_buffer_.begin(); // cursor
    decltype(unprocessed_buffer_.end()) up_end = unprocessed_buffer_.end(); // cursor
    decltype(processor_.next().begin()) ready_it_;
    decltype(processor_.next().end()) ready_end_;
    size_t bytes_in_input_ = 0;

public:
    using rbuf_t = decltype(ready_buffer_);
};

template <input_range_of_bytes R, typename T=std::ranges::range_value_t<R>,
         size_t bufferLen = 1024 * 4>
using ZipRangeProcessor = Transformer<T, R, bufferLen, GzipCompressor<T>>;

void compress() {
    std::string_view input = "teste";

    std::string teste;

    auto range = ZipRangeProcessor<decltype(input)>{input};

    std::ranges::copy(range, std::back_inserter(teste));

}
