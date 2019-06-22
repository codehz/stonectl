#pragma once
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <cstdio>
#include <errno.h>
#include <fcntl.h>
#include <filesystem>
#include <libtar.h>
#include <stdexcept>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <zlib.h>

#define CHUNK 16384

template <typename F> struct guard {
  F f;
  ~guard() { f(); }
};
template <typename F> guard(F)->guard<F>;

template <typename R, typename F> void degz(R eat, F feed) {
  z_stream zs{};
  int flush = 0;
  int ret   = inflateInit2(&zs, 16 + MAX_WBITS);
  if (ret != Z_OK) throw std::runtime_error("zlib stream init failed");
  guard zs_guard{ [&] { inflateEnd(&zs); } };
  char in[CHUNK], out[CHUNK];
  bool done = false;
  do {
    zs.avail_in = eat(in, CHUNK);
    if (zs.avail_in == 0) throw std::runtime_error("Compressed file is truncated");
    zs.next_in = (decltype(zs.next_in))in;
    do {
      zs.avail_out = CHUNK;
      zs.next_out  = (decltype(zs.next_out))out;
      ret          = inflate(&zs, flush);
      switch (ret) {
      case Z_OK: break;
      case Z_STREAM_END: done = true; break;
      case Z_NEED_DICT: throw std::runtime_error("Compressed file need dict");
      case Z_ERRNO: throw std::runtime_error(strerror(errno));
      case Z_STREAM_ERROR: throw std::runtime_error("Stream error");
      case Z_DATA_ERROR: throw std::runtime_error("Compressed file is corrupt");
      case Z_MEM_ERROR: throw std::runtime_error("Memory allocation failed");
      case Z_BUF_ERROR: throw std::runtime_error("Compressed file is truncated or otherwise corrupt");
      case Z_VERSION_ERROR: throw std::runtime_error("Compressed file version mismatched");
      default: throw std::runtime_error("Unknown error");
      }
      auto have = CHUNK - zs.avail_out;
      if (have) feed(out, have);
    } while (zs.avail_out == 0);
  } while (!done);
}

void untar(int infile, std::filesystem::path prefix, char const *name) {
  using namespace std::filesystem;
  TAR *tar{};
  auto ret = tar_fdopen(&tar, infile, "!.tar", NULL, O_RDONLY, 0, 0);
  if (ret != 0) throw std::runtime_error("tar stream init failed");
  guard tar_guard{ [&] { tar_close(tar); } };
  int i;
  while ((i = th_read(tar)) == 0) {
    path target = prefix / th_get_pathname(tar);
    printf("\r\033[2K[%-5s]Writing %s", name, target.c_str());
    fflush(stdout);
    if (exists(target) && is_regular_file(target)) remove(target);
    if (tar_extract_file(tar, target.string().data()) != 0) throw std::runtime_error(std::string("tar extract failed: ") + strerror(errno));
  }
  if (i != 1) throw std::runtime_error(std::string("tar extract failed: ") + strerror(errno));
}