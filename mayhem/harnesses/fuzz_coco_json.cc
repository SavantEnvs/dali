// Fuzzes DALI's LookaheadParser (dali/pipeline/util/lookahead_parser.h), the JSON reader
// behind the COCO reader's annotation parsing, by walking arbitrary JSON the way
// coco_loader.cc does (EnterObject/NextObjectKey/GetX/SkipX/EnterArray/NextArrayValue).
#include <cstdint>
#include <cstring>
#include <vector>
#include "dali/pipeline/util/lookahead_parser.h"

using dali::detail::LookaheadParser;

namespace {
constexpr int kMaxDepth = 64;

void Walk(LookaheadParser &p, int depth, bool use_skip) {
  if (depth > kMaxDepth) {
    p.SkipValue();
    return;
  }
  switch (p.PeekType()) {
    case rapidjson::kObjectType:
      if (use_skip && depth % 2 == 1) {
        p.SkipObject();
        break;
      }
      if (!p.EnterObject())
        break;
      while (const char *key = p.NextObjectKey()) {
        (void)strlen(key);
        Walk(p, depth + 1, use_skip);
      }
      break;
    case rapidjson::kArrayType:
      if (use_skip && depth % 2 == 1) {
        p.SkipArray();
        break;
      }
      if (!p.EnterArray())
        break;
      while (p.NextArrayValue())
        Walk(p, depth + 1, use_skip);
      break;
    case rapidjson::kStringType:
      if (const char *str = p.GetString())
        (void)strlen(str);
      break;
    case rapidjson::kNumberType:
      if (p.PeekValue() && p.PeekValue()->IsInt())
        (void)p.GetInt();
      else
        (void)p.GetDouble();
      break;
    case rapidjson::kTrueType:
    case rapidjson::kFalseType:
      (void)p.GetBool();
      break;
    case rapidjson::kNullType:
      p.GetNull();
      break;
    default:
      p.SkipValue();
      break;
  }
}
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  for (int mode = 0; mode < 2; mode++) {
    std::vector<char> buf(data, data + size);
    buf.push_back('\0');
    LookaheadParser parser(buf.data());
    if (!parser.IsValid())
      return 0;
    Walk(parser, 0, mode == 1);
    (void)parser.IsValid();
  }
  return 0;
}
