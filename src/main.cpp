#define API_MODE 1
#include <CLI/CLI.hpp>
#include <condition_variable>
#include <curl/curl.h>
#include <editline.h>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <rpcws.hpp>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <thread>

#include <stone-api/Chat.h>
#include <stone-api/Command.h>
#include <stone-api/Core.h>

#include "utils.hpp"

using namespace rpcws;

auto ep = std::make_shared<epoll>();
bool debug_mode;

RPC::Client &nsgod() {
  static RPC::Client client{ std::make_unique<client_wsio>("ws+unix://.stone/nsgod.socket", ep) };
  return client;
}

template <typename F> void handle_fail(F f) {
  try {
    f();
  } catch (std::exception &ex) {
    std::cerr << ex.what() << std::endl;
    ep->shutdown();
    exit(EXIT_FAILURE);
  }
}

template <> void handle_fail<std::exception_ptr>(std::exception_ptr e) {
  if (e) handle_fail([&] { std::rethrow_exception(e); });
}

template <typename T, T... ch> std::enable_if_t<std::is_same_v<T, char>, std::string &> operator""_str() {
  static std::string var;
  return var;
}

template <typename T, T... ch> std::enable_if_t<std::is_same_v<T, char>, bool &> operator""_flag() {
  static bool var;
  return var;
}

template <typename T, T... ch> std::enable_if_t<std::is_same_v<T, char>, std::vector<std::string> &> operator""_vstr() {
  static std::vector<std::string> var;
  return var;
}

const auto service_name_validator = CLI::Validator(
    [](std::string &input) -> std::string {
      if (input.size() == 0 || input.find_first_of('.') != std::string::npos) return "invalid name";
      return {};
    },
    "PROFILE", "profile name");

namespace fs = std::filesystem;

int main(int argc, char **argv) {
  using namespace std::chrono;
  CLI::App app{ "stoneserver manager" };
  app.set_help_all_flag("--help-all");
  app.require_subcommand(-1);
  app.require_subcommand(1);
  auto check = app.add_subcommand("check", "check current installation");
  check->callback([] {
    fs::path base{ ".stone" };
    if (!fs::is_directory(base)) {
      std::cerr << "Not installed at all" << std::endl;
      exit(EXIT_FAILURE);
    }
    if (!fs::is_regular_file(base / "nsgod")) {
      std::cerr << "nsgod (process manager) is not installed" << std::endl;
      exit(EXIT_FAILURE);
    }
    if (!fs::is_directory(base / "core") || !fs::is_regular_file(base / "core" / "run" / "stone")) {
      std::cerr << "StoneServer core is not installed" << std::endl;
      exit(EXIT_FAILURE);
    }
    if (!fs::is_directory(base / "game") || !fs::is_regular_file(base / "game" / "libs" / "libminecraftpe.so")) {
      std::cerr << "Minecraft (bedrock edition) is not installed" << std::endl;
      exit(EXIT_FAILURE);
    }
    std::cout << "Seems all components is installed" << std::endl;
  });
  auto install = app.add_subcommand("install", "install stoneserver");
  static std::vector<components> install_components;
  install->add_option("components", install_components, "install components")
      ->transform(CLI::Transformer(std::map<std::string, components>{
          { "core", components::core },
          { "game", components::game },
          { "nsgod", components::nsgod },
      }));
  install->callback([] {
    curl_global_init(CURL_GLOBAL_ALL);
    CURLM *cm = curl_multi_init();
    curl_multi_setopt(cm, CURLMOPT_MAXCONNECTS, (long)10);
    if (install_components.size() == 0) {
      install_components.emplace_back(components::core);
      install_components.emplace_back(components::game);
      install_components.emplace_back(components::nsgod);
    }
    for (components comp : install_components) {
      switch (comp) {
      case components::core: components_info<components::core>::add_transfer(cm); break;
      case components::game: components_info<components::game>::add_transfer(cm); break;
      case components::nsgod: components_info<components::nsgod>::add_transfer(cm); break;
      }
    }
    int still_alive = 1;
    int msgs_left   = -1;
    do {
      curl_multi_perform(cm, &still_alive);
      CURLMsg *msg;
      while ((msg = curl_multi_info_read(cm, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
          CURL *e = msg->easy_handle;
          components *pcomp;
          curl_easy_getinfo(e, CURLINFO_PRIVATE, &pcomp);
          curl_multi_remove_handle(cm, e);
          curl_easy_cleanup(e);
          switch (*pcomp) {
          case components::core: components_info<components::core>::extract(msg->data.result); break;
          case components::game: components_info<components::game>::extract(msg->data.result); break;
          case components::nsgod: components_info<components::nsgod>::extract(msg->data.result); break;
          }
        } else {
          std::cerr << msg->msg << std::endl;
        }
      }
      if (still_alive) curl_multi_wait(cm, NULL, 0, 1000, NULL);
    } while (still_alive);
    curl_multi_cleanup(cm);
    curl_global_cleanup();
  });
  auto start = app.add_subcommand("start", "start service");
  start->add_option("service", "start-service"_str, "target service to start")->required()->check(CLI::ExistingDirectory & service_name_validator);
  start->preparse_callback(start_nsgod);
  start->callback([] {
    handle_fail([] {
      nsgod()
          .start()
          .then<promise<json>>([] {
            ProcessLaunchOptions options{
              .waitstop = true,
              .pty = true,
              .root = fs::absolute(".stone/core"),
              .cwd = "/run",
              .log = fs::absolute(fs::path("start-service"_str) / "stone.log"),
              .cmdline = { "./stone" },
              .env = { "STONE_DEBUG=1", "UPSTART_JOB=stoneserver", "HOME=/run/data", },
              .mounts = {
                {"run/game", fs::absolute(".stone/game")},
                {"run/data", fs::absolute("start-service"_str)},
                {"dev", "/dev"},
                {"proc", "/proc"},
                {"tmp", "/tmp"},
              },
              .restart = RestartPolicy{
                .enabled = true,
                .max = 5,
                .reset_timer = 1min,
              },
            };
            return nsgod().call("start", json::object({
                                             { "service", "start-service"_str },
                                             { "options", options },
                                         }));
          })
          .then([](json ret) {
            std::cout << ret.dump(2) << std::endl;
            ep->shutdown();
          })
          .fail(handle_fail<std::exception_ptr>);
    });
    ep->wait();
  });
  auto ps = app.add_subcommand("ps", "list running services");
  ps->callback([] {
    handle_fail([] {
      nsgod()
          .start()
          .then<promise<json>>([] { return nsgod().call("status", json::object({})); })
          .then([](json ret) {
            for (auto [k, v] : ret.items()) { std::cout << k << "\t" << v["status"] << std::endl; }
            ep->shutdown();
          })
          .fail(handle_fail<std::exception_ptr>);
    });
    ep->wait();
  });
  auto stop = app.add_subcommand("stop", "kill service(s)");
  stop->add_option("service", "stop-service"_vstr, "target service(s) to stop")->required()->check(CLI::ExistingDirectory & service_name_validator)->expected(-1);
  stop->add_flag("--restart,!--no-restart", "stop-restart"_flag, "restart service after killed");
  stop->add_flag("--force", "stop-force"_flag, "force stop service(SIGKILL)");
  stop->callback([] {
    handle_fail([] {
      nsgod()
          .start()
          .then<promise<void>>([] {
            return promise<void>::map_all(std::vector<std::string>{ "stop-service"_vstr }, [](std::string const &input) -> promise<void> {
              return nsgod().call("status", json::object({ { "service", input } })).then<void>([](json ret) { return; });
            });
          })
          .then<promise<void>>([] {
            return promise<void>::map_all(std::vector<std::string>{ "stop-service"_vstr }, [](std::string const &input) -> promise<void> {
              return nsgod()
                  .call("kill", json::object({
                                    { "service", input },
                                    { "signal", SIGTERM },
                                    { "restart", "stop-restart"_flag ? 1 : -1 },
                                }))
                  .then<void>([](json ret) { return; });
            });
          })
          .then([] {
            std::cout << "sent SIGTERM signal to "
                      << "stop-service"_vstr.size() << " service(s)" << std::endl;
            ep->shutdown();
          })
          .fail(handle_fail<std::exception_ptr>);
    });
    ep->wait();
  });
  auto ping = app.add_subcommand("ping-daemon", "ping daemon");
  ping->callback([] {
    handle_fail([] {
      nsgod()
          .start()
          .then<promise<json>>([] { return nsgod().call("ping", json::object({})); })
          .then([](auto) {
            std::cout << "daemon is running" << std::endl;
            ep->shutdown();
          })
          .fail(handle_fail<std::exception_ptr>);
    });
    ep->wait();
  });
  auto kill = app.add_subcommand("kill-daemon", "stop all services and kill the daemon");
  kill->callback([] {
    handle_fail([] {
      nsgod()
          .start()
          .then<promise<json>>([] { return nsgod().call("shutdown", json::object({})); })
          .then([](auto) {
            std::cout << "daemon is shutdown" << std::endl;
            ep->shutdown();
          })
          .fail(handle_fail<std::exception_ptr>);
    });
    ep->wait();
  });
  auto attach = app.add_subcommand("attach", "attach to service's command interface");
  attach->add_option("service", "attach-service"_str, "target service name")->required()->check(CLI::ExistingDirectory & service_name_validator);
  attach->add_option("--as", "attach-executor"_str, "executor name")->default_val("stonectl");
  attach->callback([] {
    handle_fail([] {
      using namespace api;

      static auto prompt = "attach-service"_str + "> ";

      endpoint() = std::make_unique<RPC::Client>(std::make_unique<client_wsio>("ws+unix://" + "attach-service"_str + "/api.socket", ep));
      static CoreService core;
      static CommandService command;
      static ChatService chat;

      constexpr auto wrapped_output = +[](std::string const &data) {
        if (data.length() == 0) return;

        char *saved_line;
        int saved_point;
        saved_point = rl_point;
        saved_line  = strndup(rl_line_buffer, rl_end);
        guard free_line{ [&] { free(saved_line); } };
        rl_set_prompt("");
        rl_forced_update_display();
        std::cout << data;
        rl_set_prompt(prompt.c_str());
        rl_insert_text(saved_line);
        rl_point = saved_point;
        rl_forced_update_display();
      };

      endpoint()->start().then([&] {
        struct termios term;
        tcgetattr(STDIN_FILENO, &term);
        term.c_lflag &= ~ICANON;
        term.c_cc[VTIME] = 1;
        tcsetattr(STDIN_FILENO, TCSANOW, &term);

        rl_callback_handler_install(prompt.c_str(), [](char *line) {
          if (line == nullptr) {
            ep->shutdown();
            return;
          }
          guard line_guard{ [&] { free(line); } };
          if (line[0] == '/')
            command.execute({ "attach-executor"_str, line }).then(wrapped_output).fail(handle_fail<std::exception_ptr>);
          else
            chat.send({ "attach-executor"_str, line });
        });

        core.log >> [](LogEntry const &entry) {
          std::stringstream ss;
          ss << print_level(entry.level) << " [" << entry.tag << "] " << entry.content << "\033[0m\n";
          wrapped_output(ss.str());
        };

        ep->add(EPOLLIN, STDIN_FILENO, ep->reg([](epoll_event const &e) {
          if (e.events & EPOLLERR || e.events & EPOLLHUP) {
            std::cout << "bye!" << std::endl;
            ep->shutdown();
            return;
          }
          int nread;
          ioctl(STDIN_FILENO, FIONREAD, &nread);
          if (nread <= 0) { ep->shutdown(); }
          rl_callback_read_char();
        }));
      });
      ep->wait();
    });
  });
  CLI11_PARSE(app, argc, argv);
}