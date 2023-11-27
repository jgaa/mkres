
#include <string>
#include <vector>
#include <iostream>
#include <filesystem>

namespace mkres {

using namespace std;

using path_t = filesystem::path;

struct Config {
    bool verbose = false;
    bool recurse = false;

    std::string filter;

    path_t destination;
    vector<path_t> sources;
};


} // namespace mkres
