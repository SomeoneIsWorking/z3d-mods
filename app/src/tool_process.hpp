// One run of the tools (tools/z3dmods) as a child process: the lines it prints, and its refusal
// of the player's files.
#pragma once

#include <string>
#include <vector>

struct SDL_Process;

namespace z3d::app {

class ToolProcess {
public:
  ToolProcess() = default;
  // Ends a tool that is still running.
  ~ToolProcess();
  ToolProcess(const ToolProcess &) = delete;
  ToolProcess &operator=(const ToolProcess &) = delete;
  ToolProcess(ToolProcess &&) = delete;
  ToolProcess &operator=(ToolProcess &&) = delete;

  // `command` is the program and its arguments.
  void start(const std::vector<std::string> &command);
  // Takes in what the tool has printed; false once it has ended.
  [[nodiscard]] bool running();
  // Waits for the tool to end.
  void wait();
  // The ended tool's exit code.
  [[nodiscard]] int exitCode() const {
    return exitCode_;
  }
  // The last line the tool printed.
  [[nodiscard]] const std::string &lastLine() const {
    return lastLine_;
  }
  // Why the tool refused the player's files, in its own lines (those that start with "! ");
  // its last line when it ended some other way.
  [[nodiscard]] const std::string &failure() const {
    return refusal_.empty() ? lastLine_ : refusal_;
  }

private:
  void read();

  SDL_Process *process_ = nullptr;
  std::string pending_;
  std::string lastLine_;
  std::string refusal_;
  int exitCode_ = 0;
};

} // namespace z3d::app
