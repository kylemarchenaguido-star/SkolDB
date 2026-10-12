#include <algorithm>
#include <string>

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
