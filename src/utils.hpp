#pragma once
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <chrono>
#include <curl/curl.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <rpcws.hpp>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/wait.h>

#include "decompress.hpp"

enum struct ProcessStatus {
  Waiting,
  Running,
  Stopped,
  Exited,
  Restarting,
};

NLOHMANN_JSON_SERIALIZE_ENUM(ProcessStatus, {
                                                { ProcessStatus::Waiting, "waiting" },
                                                { ProcessStatus::Running, "running" },
                                                { ProcessStatus::Stopped, "stoped" },
                                                { ProcessStatus::Exited, "exited" },
                                                { ProcessStatus::Restarting, "restarting" },
                                            });

struct RestartPolicy {
  bool enabled;
  int max;
  std::chrono::milliseconds reset_timer;
};

struct ProcessLaunchOptions {
  bool waitstop, pty;
  std::string root, cwd, log;
  std::vector<std::string> cmdline, env;
  std::map<std::string, std::string> mounts;
  RestartPolicy restart;
};

namespace nlohmann {
template <> struct adl_serializer<std::chrono::system_clock::time_point> {
  static void to_json(json &j, const std::chrono::system_clock::time_point &i) { j = std::chrono::system_clock::to_time_t(i); }
  static void from_json(const json &j, std::chrono::system_clock::time_point &i) { i = std::chrono::system_clock::from_time_t(j.get<std::time_t>()); }
};

template <class Rep, class Period> struct adl_serializer<std::chrono::duration<Rep, Period>> {
  static void to_json(json &j, const std::chrono::duration<Rep, Period> &i) { j = i.count(); }
  static void from_json(const json &j, std::chrono::duration<Rep, Period> &i) { i = std::chrono::duration<Rep, Period>(j.get<Rep>()); }
};
} // namespace nlohmann

inline void to_json(nlohmann::json &j, const RestartPolicy &i) {
  j["enabled"]     = i.enabled;
  j["max"]         = i.max;
  j["reset_timer"] = i.reset_timer;
}

inline void to_json(nlohmann::json &j, const ProcessLaunchOptions &i) {
  j["waitstop"] = i.waitstop;
  j["pty"]      = i.pty;
  j["cmdline"]  = i.cmdline;
  j["root"]     = i.root;
  j["cwd"]      = i.cwd;
  j["log"]      = i.log;
  j["env"]      = i.env;
  j["mounts"]   = i.mounts;
  j["restart"]  = i.restart;
}

enum class components {
  core,
  game,
  nsgod,
};

void update_progress();

template <components C> struct components_info {
  static int memfd() {
    static int fd = memfd_create(name(), MFD_CLOEXEC);
    return fd;
  }

  static FILE *memfile() {
    static FILE *file = fdopen(memfd(), "r+");
    return file;
  }

  static size_t &size() {
    static size_t val = 0;
    return val;
  }

  static size_t write_cb(char *data, size_t n, size_t l, void *userp) {
    fwrite(data, n, l, memfile());
    size() += n * l;
    return n * l;
  }

  static constexpr components tag = C;

  static char const *name() {
    switch (C) {
    case components::core: return "core";
    case components::game: return "game";
    case components::nsgod: return "nsgod";
    }
  }

  static char const *url() {
    switch (C) {
    case components::core: return "https://hertz.services/docker/codehz/stoneserver/0";
    case components::game: return "https://hertz.services/docker/codehz/mcbe/0";
    case components::nsgod: return "https://hertz.services/github/codehz/nsgod/latest/nsgod";
    }
  }

  static bool &enabled() {
    static bool val = false;
    return val;
  }

  static double &progress() {
    static double val = 0.0;
    return val;
  }
  static void print() {
    if (enabled()) printf("[%-5s]⇩%5.1lf%%", name(), progress());
  }

  static int xferinfo(void *p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    if (dltotal) progress() = ((double)dlnow / (double)dltotal * 100);
    update_progress();
    return 0;
  }

  static void add_transfer(CURLM *cm) {
    enabled() = true;
    CURL *eh  = curl_easy_init();
    curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(eh, CURLOPT_URL, url());
    curl_easy_setopt(eh, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(eh, CURLOPT_PRIVATE, &tag);
    curl_easy_setopt(eh, CURLOPT_XFERINFOFUNCTION, xferinfo);
    curl_easy_setopt(eh, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(eh, CURLOPT_ACCEPTTIMEOUT_MS, 10000L);
    curl_easy_setopt(eh, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
    curl_multi_add_handle(cm, eh);
  }

  static void extract() {
    using namespace std::filesystem;
    fflush(memfile());
    enabled() = false;

    if constexpr (C == components::nsgod) {
      printf("\r\033[2K[%-5s]Writing...(%4.1f MB)\n", name(), (double)size() / 1048576);
      path base = ".stone";
      create_directory(base);
      path target{ base / name() };
      int tfd   = ::open(target.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0755);
      off_t off = 0;
      sendfile(tfd, memfd(), &off, size());
      close(tfd);
      printf("[%-5s]Done.\n", name());
    } else {
      printf("\r\033[2K[%-5s]Extracting...\n", name());
      path base = path{ ".stone" } / name();
      create_directories(base);
      int temp = memfd_create(name(), O_RDWR);
      guard temp_guard{ [&] { close(temp); } };
      lseek(memfd(), 0, SEEK_SET);
      try {
        degz([&](auto buffer, auto size) { return read(memfd(), buffer, size); }, [&](auto buffer, auto size) { return write(temp, buffer, size); });
      } catch (std::exception &ex) {
        fprintf(stderr, "[%-5s]Failed to inflate: %s", name(), ex.what());
        return;
      }

      lseek(temp, 0, SEEK_SET);
      try {
        untar(temp, base.string().data());
      } catch (std::exception &ex) {
        fprintf(stderr, "[%-5s]Failed to extract: %s", name(), ex.what());
        return;
      }
      if constexpr (C == components::core) {
        create_directory(base / "proc");
        create_directory(base / "tmp");
        create_directory(base / "dev");
      }
      printf("[%-5s]Done.\n", name());
    }
  }
};

void update_progress() {
  printf("\r");
  components_info<components::core>::print();
  components_info<components::game>::print();
  components_info<components::nsgod>::print();
  fflush(stdout);
}

void start_nsgod(size_t) {
  pid_t pid;
  char const *const xarg[] = { "nsgod for stoneserver", nullptr };
  char const *const xenv[] = {
    "NSGOD_API=ws+unix://.stone/nsgod.socket",
    "NSGOD_LOCK=.stone/nsgod.lock",
    nullptr,
  };
  posix_spawn(&pid, ".stone/nsgod", nullptr, nullptr, (char *const *)xarg, (char *const *)xenv);
  wait(nullptr);
}