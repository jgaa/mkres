
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <ranges>
#include <algorithm>
#include <set>
#include <format>
#include <regex>

#include <boost/program_options.hpp>

#ifdef MKRES_WITH_GZIP
#   include<zlib.h>
#endif

using namespace std;
using namespace std::string_literals;

namespace mkres {

using path_t = filesystem::path;

template <class T, class V>
concept range_of = std::ranges::range<T> && std::is_same_v<V, std::ranges::range_value_t<T>>;

template <class T, class V>
concept input_range_of = std::ranges::input_range<T> && std::is_same_v<V, std::ranges::range_value_t<T>>;

struct Config {
    bool verbose = false;
    bool recurse = false;

    string res_name = "EmbeddedResource";
    std::string ns = "mkres";
    std::string filter;
    std::string exclude;
    std::string compression = "none";

    path_t destination = "out";
    vector<path_t> sources;
};

enum class CompressionState {
    INIT,
    COMPRESSING,
    INPUT_FINISHED,
    COMPRESSION_FINISHED,
};

// Formats one input stream to a span of bytes;

void format_data(ostream& out, input_range_of<byte> auto& in) {

    string_view seperator;
    auto col = 0;

    out << "{";

    for(const auto& b : in) {
        const auto ch = to_integer<uint8_t>(b);
        out << format("{}b({:0>2x})", seperator, ch);
        seperator = ",";

        if (++col > 20) {
            out << endl;
            col = 0;
        }
    }

    out << "}";
}

template <std::ranges::input_range R, size_t bufferLen = 1024 * 8>
class compress
{

public:
    using value_type = std::remove_cvref_t<std::ranges::range_value_t<R>>;

    // Forward iterator
    class Iterator {
    public:
        using value_type = compress::value_type;
        using difference_type = std::ptrdiff_t;
        using iterator_category = input_iterator_tag;
        //using pointer = const value_type *;
        using reference = value_type;

        Iterator() = default;

        Iterator(compress& owner, bool end)
            : owner_{&owner}, end_{end}
        {
            advance();
        }

        Iterator(const compress& owner, bool end)
            : end_{true}
        {
            assert(end);
        }

        Iterator(const Iterator& it) {
            assert(false); // dont use
        };

        Iterator(Iterator&& it) noexcept
            : owner_{it.owner_}, end_{it.end}
        {
            it.end_ = true; // May cause subtle bugs if the moved from object is used...
        }

        // NB: std::ranges::end() will not compile without this.
        //     I have not seen this documented anywhere!
        Iterator& operator = (const Iterator& it) noexcept {
            assert(it.end_); // dont use active iterators
        }

        Iterator& operator = (Iterator&& it) noexcept {
            owner_ = it.owner_;
            end_ = it.end_;
            it.end_ = true;
            return *this;
        }

        bool operator == (const Iterator& it) const noexcept {
            return end_ && it.end_;
        }

        value_type& operator * () const {
            throwIfEnd();
            assert(owner_);
            return *owner_->out_it_;
        }

        // ++T
        Iterator& operator++ () {
            advance();
            return *this;
        }

        // T++
        Iterator& operator++ (int) {
            advance();
            return *this;
        }

    private:

        void throwIfEnd() const {
            assert(!end_);
            if (end_) {
                throw runtime_error{"Cannot dereference iterator == end()"};
            }
        }

        void advance() {
            assert(owner_);
            if (owner_->out_it_ == owner_->out_end_) {
                if (!owner_->compressSome()) {
                    end_ = true;
                    return;
                }
                assert(owner_->out_it_ == owner_->out_buffer_.begin());
                return; // Don't adcance the iterator. It points at the start now.
            }

            if (owner_->out_it_ != owner_->out_end_) [[likely]] {
                owner_->out_it_++;
                return;
            }

            throw runtime_error{"compress::advance(): Past end()!"};
        }

        compress *owner_ = {};
        bool end_ = true;
    };

    compress() = default;

    explicit compress(R input)
        : input_{input}, it_{std::begin(input_)}
    {
        // https://itecnote.com/tecnote/setup-zlib-to-generate-gzipped-data/
        static constexpr int windowsBits = 15;
        static constexpr int GZIP_ENCODING = 16;

        if (deflateInit2(&strm_, Z_BEST_COMPRESSION, Z_DEFLATED,
                        windowsBits | GZIP_ENCODING,
                        9,
                        Z_DEFAULT_STRATEGY) != Z_OK) {
            throw runtime_error{"deflateInit2() failed"};
        }
    }

    auto begin() {
        return Iterator{*this, false};
    }

    auto end() {
        return Iterator{};
    }

private:
    void fillInputBufferFromInputRange() {
        auto out = in_buffer_.begin();
        bytes_in_input_ = 0;
        for(; it_ != input_.end(); ++it_, ++bytes_in_input_) {
            *out = *it_;
            ++it_;
            if (++out == in_buffer_.end()) {
                break;
            }
        }

        strm_.next_in = reinterpret_cast<uint8_t *>(in_buffer_.data());
        strm_.avail_in = bytes_in_input_;
    }

    void prepareOutputBuffer() {
        strm_.next_out = reinterpret_cast<uint8_t *>(out_buffer_.data());
        strm_.avail_out = out_buffer_.size();
        out_it_ = out_buffer_.begin();
    }

    bool compressSome() {
        if (state_ == CompressionState::COMPRESSION_FINISHED) {
            return false; // Nothing to do
        }

        if (state_ == CompressionState::INIT) {
            state_ = CompressionState::COMPRESSING;
        }

        if (state_ == CompressionState::COMPRESSING) {
            prepareOutputBuffer();
        }

        // Feed the compressor with input until we don't have any more.
        // Return when the output buffer is full or when we are done.
        while(true) {
            if (strm_.avail_in == 0 && state_ ==  CompressionState::COMPRESSING) {
                fillInputBufferFromInputRange();
            }

            auto op = Z_NO_FLUSH;
            if (state_ == CompressionState::INPUT_FINISHED) {
                op = Z_FINISH;
            }

            auto result = deflate(&strm_, op);

            if (result == Z_STREAM_END) {
                state_ = CompressionState::COMPRESSION_FINISHED;
                const auto bytes_in_buffer = out_buffer_.size() - strm_.avail_out;
                out_end_ = out_buffer_.begin() + bytes_in_buffer;
                return out_end_ != out_buffer_.begin();
            }

            if (result != Z_OK) {
                throw runtime_error{"deflate() failed"};
            }

            if (strm_.avail_out == 0) {
                out_end_ = out_buffer_.end(); // Assume the entire buffer was used.
                return true;
            }

            const bool last_batch = it_ == input_.end();
            if (state_ == CompressionState::COMPRESSING && strm_.avail_in == 0) {
                if (last_batch) {
                    state_ = CompressionState::INPUT_FINISHED;
                } else {
                    fillInputBufferFromInputRange();
                }
            }
        }
    }

    R input_;
    std::ranges::iterator_t<R> it_;
    z_stream strm_ = {};
    std::array<std::byte, bufferLen> in_buffer_;
    std::array<std::byte, bufferLen> out_buffer_;
    decltype(out_buffer_.begin()) out_it_ = out_buffer_.end();
    decltype(out_buffer_.end()) out_end_ = out_buffer_.end();
    size_t bytes_in_input_ = 0;
    CompressionState state_ = CompressionState::INIT;
};

void generate(const Config& config,  const range_of<pair<filesystem::path /* input path */, string  /* name/key */>> auto& inputs) {
    const auto ns = config.ns;
    const auto hdr_name = config.destination.string() + ".h";
    const auto impl_name = config.destination.string() + ".cpp";
    const auto res_name = config.res_name;
    const auto compressed = config.compression == "gzip" ? "true" : "false";

    ofstream impl(impl_name);
    ofstream hdr(hdr_name);
    //int count = 0;

    // Generate a simple header file

    hdr << format(R"(
// Generated by mkres version {}
// See: https://github.com/jgaa/mkres

#pragma once
#include <cstddef>
#include <span>
#include <string_view>
#include <string>
namespace {} {{

class {} {{
public:
    struct Data {{
        std::span<const std::byte> data;

        bool empty() const noexcept {{
            return data.empty();
        }}

        // Gets the entire buffer. Decompresses the data if it's compressed.
        std::string toString() const;
    }};

    static const Data& get(std::string_view key) noexcept;

    static constexpr bool isCompressed() noexcept {{
        return {};
    }}

    static constexpr std::string_view compression() noexcept {{
        return "{}";
    }}
}};
}} // namespace

)", MKRES_VERSION_STR, ns, res_name, compressed, config.compression);

    // Generate the implemetation file

/// =============================================================
/// Start of implementation

    impl << format(R"(

#include <algorithm>
#include "{}"

namespace {} {{

namespace {{

// Actual data
// (In their infinite wisdom, the C++ committee has decided that a container with std::byte cannot
//  be initialized with an initializer-list of chars or integers - each byte must be individually
//  constructed.)
#define b(ch) std::byte{{0x ## ch}}
)", hdr_name, ns/*, gen_keys(inputs)*/);

/// =============================================================
/// Data
///

    using formatter_t = function<void(ostream&, path_t)>;

    formatter_t formatter = [&config](ostream& out, const path_t& path) {

        ifstream data_stream(path, ios_base::in | ios_base::binary);
        data_stream.unsetf(ios_base::skipws);

        // compress ?

        auto input_range = ranges::istream_view<char>{data_stream}
                           | ranges::views::transform([](const auto ch) {
                                 return byte{static_cast<uint8_t>(ch)};
                             });

        out << " // " << path << endl;

        if (config.compression == "gzip") {
            auto compressor = compress(input_range);
            format_data(out, compressor);
        }

        format_data(out, input_range);
    };

    string_view delimiter;

    std::vector<std::pair<string_view /* key */, string /* var_name */>> data_names;

    // First, make one array for each file.
    // I have not found a simple constexpr construct to put it directly in the data array

    size_t count = 0;
    for (const auto& [data, key] : inputs) {

        auto name = format("data_{}", ++count);

        impl << format(R"(constexpr auto {} = std::to_array<const std::byte>()", name);
        formatter(impl, data);
        impl << ");" << endl;

        data_names.emplace_back(key, name);
    }

    impl << format(R"(

#undef b

using data_t = std::pair<std::string_view, {}::Data>;
constexpr auto data = std::to_array<data_t>({{)", res_name);

    delimiter = {};
    // Now, put the data-elements in an array so we can look it up from a key
    for(const auto& [key, name] : data_names) {
        impl << format(R"({}
    {{"{}", {{{}}}}})", delimiter, key, name);
        delimiter = ", ";
    }

    impl << "});" << endl;

/// =============================================================
/// Methods

impl << format(R"(

}} // anon namespace

const {}::Data& {}::get(std::string_view key) noexcept {{

    // C++20 dont't have an algorithm to search for a value in a sorted range.
    const data_t target{{key, {{}}}};
    const auto range = std::ranges::lower_bound(data, target, [](const auto& left, const auto& right) {{
        return left.first < right.first;
    }});

    if (range != data.end() && range->first == key) {{
        return range->second;
    }}

    static constexpr data_t empty;

    return empty.second;
}} // get()


std::string {}::Data::toString() const {{
    if (isCompressed()) {{
        return "compressed data...";
    }}

    const char *ptr = reinterpret_cast<const char *>(data.data());
    std::string str{{ptr, data.size()}};
    return str;
}}


}} // namespace
)", res_name, res_name, res_name);

}

class Scanner {
    using inputs_t = set<path_t>;
public:
    using named_inputs_t = map<path_t, vector<path_t>>;

    Scanner(const Config& conf)
        : conf_{conf} {

        auto apply = [&](auto& cfg, auto & var, const auto& name) {
            if (!cfg.empty()) {
                clog << "Applying " << name << ": " << cfg << endl;
                var.emplace(cfg);
            }
        };

        apply(conf_.filter, filter_, "filter");
        apply(conf_.exclude, exclude_, "negative filter (exclude)");
    }

    auto scan() {
        for(const auto& path : conf_.sources) {
            if (filesystem::is_directory(path)) {
                if (conf_.recurse) {
                    scanDir(path.parent_path(), path.filename());
                } else {
                    throw runtime_error{format(R"(The path "{}" is a directory! Use "--recursive" option to scan directories.)", path.string())};
                }
            } else {
                add(path.parent_path(), path.filename());
            }
        }

        return toRange();
    }

    auto count() const noexcept {
        return inputs_.size();
    }

private:
    void scanDir(const path_t& root, const path_t& path) {
        auto scan_path = root;
        scan_path /= path;

        if (conf_.verbose) {
            clog << "Scanning directory: " << scan_path << endl;
        }

        for(const auto& item : filesystem::directory_iterator{scan_path}) {
            const auto branch = item.path().filename();
            auto full_path = scan_path;
            full_path /= branch;

            auto relative_path = path;
            relative_path /= branch;

            if (filesystem::is_directory(full_path)) {
                scanDir(root, relative_path);
                continue;
            }

            add(root, relative_path);
        }
    }

    void add(const path_t& root, const path_t& path) {
        auto full_path = root;
        full_path /= path;

        const auto filtered = [&](const regex& filter, const auto& path) {
            if (regex_match(path.string(), filter)) {
                return true;
            }
            return false;
        };

        if ((filter_ && !filtered(*filter_, path))
            || (exclude_ && filtered(*exclude_, path))) {
            if (conf_.verbose) {
                clog << "- excluding: " << full_path << " (filter)" << endl;
            }
            return;
        }

        if (filesystem::is_regular_file(full_path)) {
            if (conf_.verbose) {
                clog << "Adding : " << full_path  << " as --> " << path << endl;
            }

            auto [_, added] = inputs_.emplace(full_path);
            if (added) {

                auto [_, added] = names_.emplace(path);
                if (!added) {
                    throw runtime_error{format(R"(The relative name "{}" in path "{}" was already used by another file. Relative paths are used as keys and must be unique!)",
                                               path.string(), root.string())};
                }

                named_inputs_[root].emplace_back(path);
            }
        } else {
            if (!filesystem::exists(full_path)) {
                throw runtime_error{format(R"(File or directory not found: "{}")", path.string())};
            }
            cerr << "*** Ignoring non-regular file: " << path << endl;
        }
    }

    vector<pair<path_t, string>> toRange() const {
        vector<pair<path_t, string>> inputs;
        for(const auto& [root, targets] : named_inputs_) {
            for(const auto& target : targets) {
                auto full_path = root;
                full_path /= target;
                inputs.emplace_back(full_path, target.string());
            }
        }

        ranges::sort(inputs, [](const auto& left, const auto& right) {
            return left.second < right.second;
        });
        return inputs;
    }

    const Config& conf_;
    inputs_t inputs_;
    inputs_t names_;
    named_inputs_t named_inputs_;
    optional<regex> filter_;
    optional<regex> exclude_;
};


} // namespace mkres


int main(const int argc, const char **argv) {
    namespace po = boost::program_options;
    po::options_description general("Options");
    mkres::Config config;
    bool help = false;
    bool version = false;

    general.add_options()
        ("help,h", po::bool_switch(&help), "Print help and exit")
        ("version", po::bool_switch(&version), "print version information and exit")
        ("verbose,v", po::bool_switch(&config.verbose),
         "Be verbose about what's being done")
        ("recurse,r", po::bool_switch(&config.recurse),
         "Recurse into directories")
        ("filter",
         po::value(&config.filter)->default_value(config.filter),
         "Filter the file-names to embed (regex)")
        ("exclude",
         po::value(&config.exclude)->default_value(config.exclude),
         "Exclude the the file-names to embed (regex)")
        ("destination,d",
         po::value(&config.destination)->default_value(config.destination),
         "Destination path/name. '.h' and '.cpp' is added to the destination file names, so just specify the name without extention.")
        ("compression,c",
          po::value(&config.compression)->default_value(config.compression),
         "Compression to use. 'none' or 'gzip'. If compressed, the application must decompress the data before it can be used.")
        ("namespace,n",
         po::value(&config.ns)->default_value(config.ns),
         "C++ Namespace to use for the embedded resource(s)")
        ("name,N",
         po::value(&config.res_name)->default_value(config.res_name),
         "Resource-name. This is the static constexpr name for the resource that you call from your code.")
        ;


    po::options_description source_files("hidden");
    source_files.add_options()
        ("input-files", po::value(&config.sources), "Input file or directory")
        ;

    po::positional_options_description input;
    input.add
        ("input-files", -1)
        ;

    const auto appname = filesystem::path(argv[0]).stem().string();
    po::options_description cmdline_options;
    cmdline_options.add(general).add(source_files);
    po::variables_map vm;
    try {
        po::store(po::command_line_parser(argc, argv).options(cmdline_options).positional(input).run(), vm);
        po::notify(vm);
    } catch (const std::exception& ex) {
        cerr << appname
             << " Failed to parse command-line arguments: " << ex.what() << endl;
        return -1;
    }

    if (help) {
        cout << appname << " [options] input-file ..." << endl;
        cout << general << endl;
        return -2;
    }

    if (version) {
        cout << appname << ' ' << MKRES_VERSION_STR << endl;
        return -3;
    }

    try {
        mkres::Scanner scanner{config};
        auto inputs = scanner.scan();
        clog << "Got " << scanner.count() << " items " << endl;
        generate(config, inputs);
    } catch(const exception& ex) {
        cerr << "Failed with exception! Message:  " << ex.what() << endl;
    }
}
