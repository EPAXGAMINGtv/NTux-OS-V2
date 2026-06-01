#include <cpp/string.hpp>

#include <stdlib.h>
#include <string.h>

namespace ntux {
namespace cpp {

String::String() : data_(0), len_(0), cap_(0) {
    (void)ensure(1);
    if (data_) data_[0] = '\0';
}

String::String(const char* s) : data_(0), len_(0), cap_(0) {
    size_t n = s ? strlen(s) : 0;
    if (ensure(n + 1)) {
        if (n && s) memcpy(data_, s, n);
        data_[n] = '\0';
        len_ = n;
    }
}

String::String(const String& other) : data_(0), len_(0), cap_(0) {
    if (ensure(other.len_ + 1)) {
        if (other.len_) memcpy(data_, other.data_, other.len_);
        data_[other.len_] = '\0';
        len_ = other.len_;
    }
}

String& String::operator=(const String& other) {
    if (this == &other) return *this;
    if (!ensure(other.len_ + 1)) return *this;
    if (other.len_) memcpy(data_, other.data_, other.len_);
    data_[other.len_] = '\0';
    len_ = other.len_;
    return *this;
}

String::~String() {
    if (data_) free(data_);
    data_ = 0;
    len_ = 0;
    cap_ = 0;
}

bool String::ensure(size_t need) {
    if (need <= cap_) return true;
    size_t nc = cap_ ? cap_ : 16;
    while (nc < need) {
        if (nc > ((size_t)-1) / 2) return false;
        nc *= 2;
    }
    char* p = (char*)realloc(data_, nc);
    if (!p) return false;
    data_ = p;
    cap_ = nc;
    return true;
}

bool String::append(const char* s) {
    size_t add = s ? strlen(s) : 0;
    if (!ensure(len_ + add + 1)) return false;
    if (add) memcpy(data_ + len_, s, add);
    len_ += add;
    data_[len_] = '\0';
    return true;
}

const char* String::c_str() const {
    return data_ ? data_ : "";
}

size_t String::size() const {
    return len_;
}

} // namespace cpp
} // namespace ntux
