# mkres
Utility to embed files in C++ projects

In some projects, I find it simpler to embed files in the binary,
then making an "install" procedure where files must be copied
to some location on the destination system, and then found and
used by the application.

For example; the [nsblast](https://github.com/jgaa/nsblast) name server
has an embedded HTTP server to support a REST API. I have added
an option to also enable a swagger interface to this API. 

The HTTP server fetches the JavaScript files
and the rest of the swagger content, directly from memory
in the server. The content is transformed from normal files
on the disk, to C++ code (mostly static arrays with data)
by *mkres*.

I have used various simpler versions of this program in various projects in the past.
When I needed to add a React based Web User Interface to nsblast, I needed
to recursively add files with non-predictable names - and support the target paths
in the HTTP server. The files also become quite large, so I realized that I needed to
compress them. As a result, I created this new project to serve all my needs for
embedded files in one code base.

## What is does

*Mkres* is a small command line utility written in C++ that can read one
or more files on the local file system, and generate C++ code that
embeds the content of the files to C++ code. It also provides a simple
interface (static C++ class) to lookup the content by name.

**Features:**
- Generate C++ code representing the content of one or more files as static const C++ data.
- It accept files identified by their path on the command line.
- It accept directories, which it will scan recursively and add the content with keys matching the file-names in the directory tree
- Positive filter. You can specify a regex for files to add when adding directories.
- Negative filter. You can specify a regex for files to exclude when working with directories.
- Compression. You can use *gzip* compression for the content to save space. The content can be accessed by the application in it's compressed form, or automatically decompressed and used as strings.
- You can specify C++ namespace (`--namespace`) for the generated code
- You can specify the C++ class (`--name`) for the generated code


## Requirements
- C++20 compatible compiler. Tested with g++-13 and clang++-17.
- zlib if compression is enabled (CMake option).
- Boost library (program_options). The generated code does not require libboost.

## Usage:
```
./mkres -h
mkres [options] input-file ...
Options:
  -h [ --help ]                         Print help and exit
  --version                             print version information and exit
  -v [ --verbose ]                      Be verbose about what's being done
  -r [ --recurse ]                      Recurse into directories
  --filter arg                          Filter the file-names to embed (regex)
  --exclude arg                         Exclude the the file-names to embed 
                                        (regex)
  -d [ --destination ] arg (="out")     Destination path/name. '.h' and '.cpp' 
                                        is added to the destination file names,
                                        so just specify the name without 
                                        extention.
  -c [ --compression ] arg (=none)      Compression to use. 'none' or 'gzip'. 
                                        If compressed, the application must 
                                        decompress the data before it can be 
                                        used.
  -n [ --namespace ] arg (=mkres)       C++ Namespace to use for the embedded 
                                        resource(s)
  -N [ --name ] arg (=EmbeddedResource) Resource-name. This is the static 
                                        constexpr name for the resource that 
                                        you call from your code.
```


## Example

This code is from nsblast.

**Using mkres in a CMake project:**
```cmake
ExternalProject_Add(mkres
    PREFIX "${EXTERNAL_PROJECTS_PREFIX}"
    GIT_REPOSITORY "https://github.com/jgaa/mkres.git"
    GIT_TAG "main"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${EXTERNAL_PROJECTS_INSTALL_PREFIX}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_GENERATOR='${CMAKE_GENERATOR}'
        -DCMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}'
)

set(MKRES "${EXTERNAL_PROJECTS_PREFIX}/installed/bin/mkres")
```

**Generating code swagger files**
```cmake
add_custom_command(
    COMMAND ${MKRES} --verbose --compression gzip --namespace nsblast::lib::embedded --name Swagger --destination swagger_res --exclude '.*\\.map' ${NSBLAST_ROOT}/swagger/*
    DEPENDS ${NSBLAST_ROOT}/swagger/index.html ${NSBLAST_ROOT}/swagger/swagger.yaml mkres
    OUTPUT swagger_res.cpp swagger_res.h
    COMMENT "Embedding swagger..."
    )

    set(LIB_SWAGGER_FILES swagger_res.cpp swagger_res.h)

    add_custom_target(swagger_res ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/swagger_res.cpp)

```

***The C++ interface to the embedded data***
```C++ This is the generated header file
// Generated by mkres version 0.1.0
// See: https://github.com/jgaa/mkres

#pragma once
#include <cstddef>
#include <span>
#include <string_view>
#include <string>
namespace nsblast::lib::embedded {

class Swagger {
public:
    struct Data {
        const std::span<const std::byte> data;
        const size_t origLen{};

        bool empty() const noexcept {
            return data.empty();
        }

        // Gets the entire buffer. Decompresses the data if it's compressed.
        std::string toString() const;
    };

    static const Data& get(std::string_view key) noexcept;

    static constexpr bool isCompressed() noexcept {
        return true;
    }

    static constexpr std::string_view compression() noexcept {
        return "gzip";
    }
};
} // namespace


```

**Using the swagger files in the embedded HTTP server**

This is real code. The interesting line in the template is:
```C++
    if (const auto& data = T::get(t); !data.empty()) {
```
Here the target the user has requested, for example `index.html` is
looked up in the generated code. 

```C++

template <typename T>
class EmbeddedResHandler : public RequestHandler {
public:
    explicit EmbeddedResHandler(std::string prefix)
        : prefix_{std::move(prefix)} {}

    Response onReqest(const Request& req) override {
        auto t = std::string_view{req.target};

        if (const auto& data = T::get(t); !data.empty()) {
            std::filesystem::path served = prefix_;
            served /= t;
            return {200, "OK", data.toString(), served.string()};
        }

        return {404, "Document not found"};
    }

private:
    const std::string prefix_;
};

....

    const string_view swagger_path = "/api/swagger";
    LOG_INFO << "Enabling Swagger at " << swagger_path;
    
    http_->addRoute(swagger_path,
                    make_shared<EmbeddedResHandler<lib::embedded::Swagger>>("/api/swagger"));

```

