#pragma once 

#include <string>
#include <utility>

#include "slice.h"

enum class Code {
  Ok = 0,
  NotFound = 1,
  Corruption = 2,
  NotSupported = 3,
  InvalidArgument = 4,
  IOError = 5
};



struct [[nodiscard]] Status {
  Code code = Code::Ok;
  std::string message;
};

inline Status status_ok() {
  return {};
}

inline Status status_notfound(std::string msg) {
  return { Code::NotFound, std::move(msg)};
}

inline Status status_corruption(std::string msg) {
  return { Code::Corruption, std::move(msg)};
}

inline Status status_notsupported(std::string msg) {
  return { Code::NotSupported, std::move(msg)};
}

inline Status status_invalidargument(std::string msg) {
  return { Code::InvalidArgument, std::move(msg)};
}

inline Status status_ioerror(std::string msg) {
  return { Code::IOError, std::move(msg)};
}

inline bool is_ok(const Status& s) {
  return s.code == Code::Ok;
}

std::string status_to_string(const Status& s);

