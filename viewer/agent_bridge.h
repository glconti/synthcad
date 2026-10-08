#pragma once
#include "project_contract.h"
#include <nlohmann/json.hpp>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>

namespace synthcad {
// The transport thread only reads published values or queues an action. All
// geometry, GPU and camera operations stay on the viewer's owning thread.
struct AgentAction {
  nlohmann::json request;
  std::promise<nlohmann::json> result;
  std::chrono::steady_clock::time_point deadline;
};
class AgentBridge {
 public:
  explicit AgentBridge(std::string session);
  void Publish(nlohmann::json snapshot, FileSnapshot files);
  nlohmann::json Handle(const nlohmann::json& request);
  std::vector<std::shared_ptr<AgentAction>> Drain();
  void Close();
 private:
  struct Expected {FileSnapshot files; std::string view;};
  std::string session_;
  std::mutex mutex_;
  std::condition_variable changed_;
  nlohmann::json snapshot_;
  FileSnapshot files_;
  std::map<std::string,Expected> expected_;
  std::deque<std::shared_ptr<AgentAction>> pending_;
  bool closed_=false;
};
}
