// Fuzzes DALI's generic audio decoder (dali/operators/decoder/audio/generic_decoder.cc),
// the libsndfile wrapper behind the AudioDecoder operator, on in-memory encoded audio.
#include <cstdint>
#include <exception>
#include <memory>
#include <vector>
#include "dali/operators/decoder/audio/generic_decoder.h"

namespace {
constexpr int64_t kMaxFrames = 1 << 18;
constexpr int64_t kChunkFrames = 1024;

template <typename T>
void DecodeAs(const uint8_t *data, size_t size) {
  auto decoder = dali::make_generic_audio_decoder();
  auto meta = decoder->Open(dali::make_cspan(reinterpret_cast<const char *>(data), size));
  if (meta.channels <= 0 || meta.channels > 256 || meta.length < 0)
    return;
  std::vector<T> out(kChunkFrames * meta.channels);
  int64_t total = 0;
  while (total < kMaxFrames) {
    auto n = decoder->template DecodeFrames<T>(out.data(), kChunkFrames);
    if (n <= 0)
      break;
    total += n;
    if (total > kMaxFrames / 2)
      decoder->SeekFrames(meta.length / 2, SEEK_SET);
  }
  decoder->SeekFrames(0, SEEK_SET);
  auto n = decoder->template Decode<T>(dali::make_span(out));
  (void)n;
  decoder->Close();
}
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size == 0)
    return -1;
  try {
    DecodeAs<float>(data, size);
    DecodeAs<int16_t>(data, size);
    DecodeAs<int32_t>(data, size);
  } catch (const std::exception &) {
  }
  return 0;
}
