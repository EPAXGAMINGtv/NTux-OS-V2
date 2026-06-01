#ifndef NTUX_LIBCPP_STRING_HPP
#define NTUX_LIBCPP_STRING_HPP

#include <stddef.h>

namespace ntux {
namespace cpp {

class String {
public:
    String();
    explicit String(const char* s);
    String(const String& other);
    String& operator=(const String& other);
    ~String();

    bool append(const char* s);
    const char* c_str() const;
    size_t size() const;

private:
    bool ensure(size_t need);
    char* data_;
    size_t len_;
    size_t cap_;
};

} // namespace cpp
} // namespace ntux

#endif
