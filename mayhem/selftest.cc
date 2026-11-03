// Known-answer tests for the DALI components fuzzed by mayhem/harnesses. Built by build.sh with the
// project's normal flags; mayhem/test.sh runs it and checks every PASS line.
#include <unistd.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "dali/operators/decoder/audio/generic_decoder.h"
#include "dali/operators/reader/loader/webdataset/tar_utils.h"
#include "dali/pipeline/util/lookahead_parser.h"
#include "dali/util/std_file.h"
#include "dali/util/uri.h"

static int g_fail = 0;
static void Report(const char *name, bool ok) {
  printf("%s %s\n", ok ? "PASS" : "FAIL", name);
  if (!ok)
    g_fail++;
}

static void TestUri() {
  auto u = dali::URI::Parse("s3://bucket/path/to/obj.tar?x=1&y=2#frag");
  Report("uri_valid", u.valid());
  Report("uri_scheme", u.valid() && u.scheme() == "s3");
  Report("uri_authority", u.valid() && u.authority() == "bucket");
  Report("uri_path", u.valid() && u.path() == "/path/to/obj.tar");
  Report("uri_query", u.valid() && u.query() == "x=1&y=2");
  Report("uri_fragment", u.valid() && u.fragment() == "frag");
  Report("uri_invalid", !dali::URI::Parse("http://exa mple/a b").valid());
}

static void TestJson() {
  std::string doc =
      R"({"images":[{"id":7,"file_name":"a.jpg","width":640.5},{"id":9,"file_name":"b.jpg","width":10}],)"
      R"("flag":true,"skipme":{"k":[1,2,3]}})";
  std::vector<char> buf(doc.begin(), doc.end());
  buf.push_back('\0');
  dali::detail::LookaheadParser p(buf.data());
  std::vector<int> ids;
  std::vector<std::string> names;
  double width0 = 0;
  bool flag = false, skipped = false;
  bool ok = p.EnterObject();
  while (const char *key = p.NextObjectKey()) {
    if (!strcmp(key, "images")) {
      p.EnterArray();
      while (p.NextArrayValue()) {
        p.EnterObject();
        while (const char *k = p.NextObjectKey()) {
          if (!strcmp(k, "id")) ids.push_back(p.GetInt());
          else if (!strcmp(k, "file_name")) names.push_back(p.GetString());
          else if (!strcmp(k, "width") && ids.size() == 1) width0 = p.GetDouble();
          else p.SkipValue();
        }
      }
    } else if (!strcmp(key, "flag")) {
      flag = p.GetBool();
    } else {
      p.SkipValue();
      skipped = true;
    }
  }
  Report("json_walk_valid", ok && p.IsValid());
  Report("json_ids", ids.size() == 2 && ids[0] == 7 && ids[1] == 9);
  Report("json_names", names.size() == 2 && names[0] == "a.jpg" && names[1] == "b.jpg");
  Report("json_double", width0 == 640.5);
  Report("json_flag_skip", flag && skipped);
  char bad[] = "{\"a\": [1, 2,";
  dali::detail::LookaheadParser bp(bad);
  bp.EnterObject();
  bp.NextObjectKey();
  bp.EnterArray();
  while (bp.NextArrayValue()) bp.GetInt();
  Report("json_truncated_invalid", !bp.IsValid());
}

static void PutOctal(char *dst, size_t len, uint64_t v) {
  snprintf(dst, len, "%0*llo", static_cast<int>(len - 1), static_cast<unsigned long long>(v));
}

static std::string TarEntry(const std::string &name, const std::string &content) {
  std::string out(512, '\0');
  char *h = &out[0];
  memcpy(h, name.data(), name.size());
  PutOctal(h + 100, 8, 0644);
  PutOctal(h + 108, 8, 0);
  PutOctal(h + 116, 8, 0);
  PutOctal(h + 124, 12, content.size());
  PutOctal(h + 136, 12, 0);
  memset(h + 148, ' ', 8);
  h[156] = '0';
  memcpy(h + 257, "ustar  ", 8);
  unsigned sum = 0;
  for (int i = 0; i < 512; i++) sum += static_cast<unsigned char>(h[i]);
  snprintf(h + 148, 7, "%06o", sum);
  h[154] = '\0';
  h[155] = ' ';
  out += content;
  out.append((512 - content.size() % 512) % 512, '\0');
  return out;
}

static void TestTar() {
  std::string payload_b(1000, 'x');
  for (size_t i = 0; i < payload_b.size(); i++) payload_b[i] = 'a' + i % 26;
  std::string tar = TarEntry("a.txt", "hello") + TarEntry("dir/b.bin", payload_b) + std::string(1024, '\0');
  const char *tmpdir = getenv("TMPDIR");
  std::string tmpl = std::string(tmpdir && *tmpdir ? tmpdir : "/tmp") + "/dali_selftest_XXXXXX";
  char *path = &tmpl[0];
  int fd = mkstemp(path);
  bool wrote = fd >= 0 && write(fd, tar.data(), tar.size()) == static_cast<ssize_t>(tar.size());
  if (fd >= 0) close(fd);
  Report("tar_written", wrote);
  std::vector<std::string> names;
  std::vector<size_t> sizes;
  std::vector<std::string> contents;
  bool types_ok = true;
  try {
    dali::detail::TarArchive archive(std::make_unique<dali::StdFileStream>(path));
    while (!archive.EndOfArchive()) {
      names.push_back(archive.GetFileName());
      sizes.push_back(archive.GetFileSize());
      types_ok &= archive.GetFileType() == dali::detail::TarArchive::ENTRY_FILE;
      std::string c(archive.GetFileSize(), '\0');
      size_t n = archive.Read(&c[0], c.size());
      c.resize(n);
      contents.push_back(c);
      if (!archive.NextFile()) break;
    }
  } catch (const std::exception &e) {
    printf("tar exception: %s\n", e.what());
  }
  unlink(path);
  Report("tar_names", names.size() == 2 && names[0] == "a.txt" && names[1] == "dir/b.bin");
  Report("tar_sizes", sizes.size() == 2 && sizes[0] == 5 && sizes[1] == 1000);
  Report("tar_types", types_ok);
  Report("tar_contents", contents.size() == 2 && contents[0] == "hello" && contents[1] == payload_b);
}

static void Le16(std::string &s, uint16_t v) { s.append(reinterpret_cast<char *>(&v), 2); }
static void Le32(std::string &s, uint32_t v) { s.append(reinterpret_cast<char *>(&v), 4); }

static void TestAudio() {
  const int N = 16;
  std::string wav = "RIFF";
  Le32(wav, 36 + N * 2);
  wav += "WAVEfmt ";
  Le32(wav, 16);
  Le16(wav, 1);
  Le16(wav, 1);
  Le32(wav, 8000);
  Le32(wav, 16000);
  Le16(wav, 2);
  Le16(wav, 16);
  wav += "data";
  Le32(wav, N * 2);
  for (int i = 0; i < N; i++) Le16(wav, static_cast<uint16_t>(static_cast<int16_t>(i * 1000 - 8000)));
  auto dec = dali::make_generic_audio_decoder();
  auto meta = dec->Open(dali::make_cspan(wav.data(), wav.size()));
  Report("audio_meta", meta.length == N && meta.sample_rate == 8000 && meta.channels == 1);
  std::vector<int16_t> pcm(N);
  auto n = dec->DecodeFrames<int16_t>(pcm.data(), N);
  bool pcm_ok = n == N;
  for (int i = 0; i < N && pcm_ok; i++) pcm_ok = pcm[i] == i * 1000 - 8000;
  Report("audio_pcm16", pcm_ok);
  dec->SeekFrames(0, SEEK_SET);
  std::vector<float> f(N);
  n = dec->DecodeFrames<float>(f.data(), N);
  bool f_ok = n == N;
  for (int i = 0; i < N && f_ok; i++) f_ok = std::fabs(f[i] - (i * 1000 - 8000) / 32768.0f) < 1e-6f;
  Report("audio_float", f_ok);
  bool threw = false;
  try {
    auto bad = dali::make_generic_audio_decoder();
    std::string junk = "not audio at all, just some text";
    bad->Open(dali::make_cspan(junk.data(), junk.size()));
  } catch (const std::exception &) {
    threw = true;
  }
  Report("audio_rejects_junk", threw);
}

int main() {
  TestUri();
  TestJson();
  TestTar();
  TestAudio();
  printf("SELFTEST DONE failed=%d\n", g_fail);
  return g_fail ? 1 : 0;
}
