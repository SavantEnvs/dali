// Fuzzes DALI's webdataset tar reader (dali/operators/reader/loader/webdataset/tar_utils.cc)
// through a real StdFileStream over the fuzz input.
#include <fcntl.h>
#include <unistd.h>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include "dali/operators/reader/loader/webdataset/tar_utils.h"
#include "dali/util/std_file.h"

namespace {
int g_fd = -1;
std::string g_path;

bool EnsureTempFile() {
  if (g_fd >= 0)
    return true;
  const char *tmp = getenv("TMPDIR");
  g_path = std::string(tmp && *tmp ? tmp : "/tmp") + "/dali_tar_fuzz_XXXXXX";
  std::vector<char> templ(g_path.begin(), g_path.end());
  templ.push_back('\0');
  g_fd = mkstemp(templ.data());
  if (g_fd < 0)
    return false;
  g_path = templ.data();
  unlink(g_path.c_str());
  g_path = "/proc/self/fd/" + std::to_string(g_fd);
  return true;
}

void Walk(dali::detail::TarArchive &tar) {
  std::vector<int64_t> headers;
  uint8_t buf[4096];
  int entries = 0;
  while (!tar.EndOfArchive() && entries++ < 4096) {
    headers.push_back(tar.TellArchive());
    (void)tar.GetFileName();
    (void)tar.GetFileType();
    (void)tar.HeaderSize();
    size_t remaining = tar.GetFileSize();
    while (remaining && !tar.EndOfFile()) {
      size_t n = tar.Read(buf, sizeof(buf));
      if (n == 0)
        break;
      remaining -= n < remaining ? n : remaining;
    }
    if (!tar.NextFile())
      break;
  }
  if (!headers.empty())
    tar.SeekArchive(headers[headers.size() / 2]);
  (void)tar.GetFileName();
  (void)tar.ReadFile();
}
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (!EnsureTempFile())
    return -1;
  if (ftruncate(g_fd, 0) != 0 || (size && pwrite(g_fd, data, size, 0) != static_cast<ssize_t>(size)))
    return -1;
  try {
    auto stream = std::make_unique<dali::StdFileStream>(g_path);
    dali::detail::TarArchive tar(std::move(stream));
    Walk(tar);
  } catch (const std::exception &) {
  }
  return 0;
}
