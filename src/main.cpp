
#include <string>
#include <vector>
#include <iostream>
#include <filesystem>

#include <boost/program_options.hpp>

using namespace std;
namespace mkres {

using path_t = filesystem::path;

struct Config {
    bool verbose = false;
    bool recurse = false;

    std::string filter;
    std::string compression = "none";

    path_t destination = "out";
    vector<path_t> sources;
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
        ("verbose", po::bool_switch(&config.verbose),
         "Be verbose about what's being done")
        ("recurse,r", po::bool_switch(&config.recurse),
         "Recurse into directories")
        ("filter",
         po::value(&config.filter)->default_value(config.filter),
         "Filter the file-names to embed (regex)")
        ("destination,d",
         po::value(&config.destination),
         "Destination path/name. '.h' and '.cpp' is added to the destination file names, so just specify the name without extention.")
        ("compression,c",
         po::value(&config.compression),
         "Compression to use. 'none' or 'gzip'. If compressed, the application must decompress the data before it can be used.")
        ;

    po::options_description hidden("Hidden options", -1);
    hidden.add_options()
        ("input-file", po::value(&config.sources), "input file")
        ;

    const auto appname = filesystem::path(argv[0]).stem().string();
    po::options_description cmdline_options;
    cmdline_options.add(general).add(hidden);
    po::variables_map vm;
    try {
        po::store(po::command_line_parser(argc, argv).options(cmdline_options).run(), vm);
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

}
