// Fuzzes DALI's URI parser (dali/util/uri.cc).
#include <cstdint>
#include <exception>
#include <string>
#include "dali/util/uri.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  std::string s(reinterpret_cast<const char *>(data), size);
  for (auto opts : {dali::URI::Default, dali::URI::AllowNonEscaped}) {
    dali::URI uri = dali::URI::Parse(s, opts);
    if (!uri.valid())
      continue;
    try {
      size_t total = uri.scheme().size() + uri.authority().size() + uri.scheme_authority().size() +
                     uri.path().size() + uri.scheme_authority_path().size() + uri.query().size() +
                     uri.path_and_query().size() + uri.fragment().size();
      if (total > 8 * (size + 1) + 8)
        __builtin_trap();
    } catch (const std::exception &) {
    }
  }
  return 0;
}
