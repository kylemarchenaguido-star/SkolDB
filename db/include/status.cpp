#include <cstdio>

#include "status.h"

std::string status_to_string(const Status &s){
  std::string name;
  switch (s.code) {
    case Code::Ok:                name = "OK"; break;
    case Code::NotFound:          name = "NotFound"; break;
    case Code::Corruption:        name = "Corruption"; break;
    case Code::NotSupported:      name = "NotSupported"; break;
    case Code::InvalidArgument:   name = "InvalidArgument"; break;
    case Code::IOError:           name = "IOError"; break;
    default:                      name = "Unknown"; break;
  }
  return s.message.empty() ? name : name + ": " + s.message;
}
