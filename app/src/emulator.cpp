#include "emulator.hpp"

#include "game_window.hpp"

#include "common/settings.h"
#include "core/arm/arm_interface.h"
#include "core/core.h"
#include "core/frontend/applets/default_applets.h"
#include "core/frontend/framebuffer_layout.h"
#include "core/frontend/image_interface.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/loader/loader.h"
#include "core/memory.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"

#include <lucent/log.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <string>

namespace z3d::app {
namespace {

// The longest a savestate waits on the guest's file I/O before giving up.
constexpr auto kAsyncDrainLimit = std::chrono::seconds(5);
std::shared_ptr<Kernel::Process> runningProcess(Core::System &system) {
  if (!system.IsPoweredOn()) {
    return nullptr;
  }
  return system.Kernel().GetCurrentProcess();
}

bool mapped(Core::System &system, const Kernel::Process &process, uint32_t address, uint32_t size) {
  if (size == 0 || address + size < address) {
    return false;
  }
  for (uint64_t page = address & ~Memory::CITRA_PAGE_MASK; page < uint64_t{address} + size;
       page += Memory::CITRA_PAGE_SIZE) {
    if (!system.Memory().IsValidVirtualAddress(process, static_cast<VAddr>(page))) {
      return false;
    }
  }
  return true;
}

} // namespace

// Guest stores to watched ranges, through the fork's memory write observer.
class WriteWatches {
public:
  explicit WriteWatches(Core::System &system) : system_(system) {
  }
  ~WriteWatches() {
    clear();
  }
  WriteWatches(const WriteWatches &) = delete;
  WriteWatches &operator=(const WriteWatches &) = delete;
  WriteWatches(WriteWatches &&) = delete;
  WriteWatches &operator=(WriteWatches &&) = delete;

  bool add(uint32_t address, uint32_t size) {
    const auto process = runningProcess(system_);
    if (!process || size == 0) {
      return false;
    }
    system_.Memory().RegisterWatchpoint(*process, address, size);
    ranges_.push_back({address, size});
    system_.Memory().SetWriteObserver([this](VAddr vaddr, u32 width, u64 value) {
      onWrite(vaddr, width, value);
    });
    return true;
  }

  void clear() {
    if (const auto process = runningProcess(system_)) {
      for (const Range &range : ranges_) {
        system_.Memory().UnregisterWatchpoint(*process, range.start, range.size);
      }
    }
    ranges_.clear();
    system_.Memory().SetWriteObserver({});
  }

  std::vector<WriteHit> take() {
    return std::exchange(hits_, {});
  }

private:
  struct Range {
    uint32_t start;
    uint32_t size;
  };

  void onWrite(VAddr vaddr, u32 width, u64 value) {
    for (const Range &range : ranges_) {
      if (vaddr < range.start + range.size && range.start < vaddr + width) {
        auto &cpu = system_.GetRunningCore();
        hits_.push_back({vaddr, width, value, cpu.GetPC(), cpu.GetReg(14)});
        return;
      }
    }
  }

  Core::System &system_;
  std::vector<Range> ranges_;
  std::vector<WriteHit> hits_;
};

Emulator::Emulator(const AppConfig &config, const std::filesystem::path &rom, GameWindow &window)
    : system_(Core::System::GetInstance()), window_(window), resolution_(config.resolution) {
  Frontend::RegisterDefaultApplets(system_);
  system_.RegisterImageInterface(std::make_shared<Frontend::ImageInterface>());
  const Core::System::ResultStatus status = system_.Load(window_, rom.string());
  if (status != Core::System::ResultStatus::Success) {
    const std::string details = system_.GetStatusDetails();
    throw std::runtime_error("cannot load " + rom.string() + " (status " +
                             std::to_string(static_cast<int>(status)) + ") " + details);
  }
  system_.RegisterCoreLoopThreadId();
  watches_ = std::make_unique<WriteWatches>(system_);
}

Emulator::~Emulator() {
  watches_.reset();
  system_.Shutdown();
}

void Emulator::step() {
  const Core::System::ResultStatus status = system_.RunLoop();
  if (status != Core::System::ResultStatus::Success) {
    throw std::runtime_error("core stopped (status " + std::to_string(static_cast<int>(status)) +
                             ") " + system_.GetStatusDetails());
  }
}

void Emulator::runFrames(uint64_t count, bool capture) {
  const uint64_t target = window_.framesShown() + count;
  while (window_.framesShown() < target) {
    if (capture && window_.framesShown() + 1 == target) {
      const Layout::FramebufferLayout layout = Layout::FrameLayoutFromResolutionScale(resolution_);
      captureRows_.assign(static_cast<size_t>(layout.width) * layout.height, 0);
      system_.GPU().Renderer().RequestScreenshot(
          captureRows_.data(),
          [this, layout](bool) {
            frame_.storeRows(std::move(captureRows_), {layout.width, layout.height});
          },
          layout);
      capture = false;
    }
    step();
    if (frameObserver_) {
      frameObserver_();
    }
  }
}

uint64_t Emulator::framesRun() const {
  return window_.framesShown();
}

bool Emulator::read(uint32_t address, std::span<uint8_t> out) const {
  const auto process = runningProcess(system_);
  const auto size = static_cast<uint32_t>(out.size());
  if (!process || !mapped(system_, *process, address, size)) {
    return false;
  }
  system_.Memory().ReadBlock(*process, address, out.data(), size);
  return true;
}

bool Emulator::write(uint32_t address, std::span<const uint8_t> data) {
  const auto process = runningProcess(system_);
  const auto size = static_cast<uint32_t>(data.size());
  if (!process || !mapped(system_, *process, address, size)) {
    return false;
  }
  system_.Memory().WriteBlock(*process, address, data.data(), size);
  return true;
}

std::vector<uint8_t> Emulator::unpatchedImage() const {
  std::vector<uint8_t> image;
  const auto process = runningProcess(system_);
  if (!process || system_.GetAppLoader().ReadCode(image) != Loader::ResultStatus::Success) {
    return {};
  }
  uint32_t end = kImageBase;
  while (mapped(system_, *process, end, Memory::CITRA_PAGE_SIZE)) {
    end += Memory::CITRA_PAGE_SIZE;
  }
  image.resize(std::max<size_t>(image.size(), end - kImageBase));
  return image;
}

void Emulator::codeChanged() {
  for (uint32_t core = 0; core < system_.GetNumCores(); ++core) {
    system_.GetCore(core).ClearInstructionCache();
  }
}

bool Emulator::drainAsync() {
  const auto start = std::chrono::steady_clock::now();
  while (system_.KernelRunning() && system_.Kernel().AreAsyncOperationsPending()) {
    if (std::chrono::steady_clock::now() - start > kAsyncDrainLimit) {
      lucent::warn("emulator", "guest file I/O still pending after {}s", kAsyncDrainLimit.count());
      return false;
    }
    step();
  }
  return true;
}

std::optional<std::vector<uint8_t>> Emulator::saveState() {
  if (!drainAsync()) {
    return std::nullopt;
  }
  try {
    return system_.SaveStateBuffer();
  } catch (const std::exception &error) {
    lucent::warn("emulator", "savestate failed: {}", error.what());
    return std::nullopt;
  }
}

bool Emulator::loadState(std::vector<uint8_t> state) {
  if (!drainAsync()) {
    return false;
  }
  watches_->clear();
  try {
    return system_.LoadStateBuffer(std::move(state));
  } catch (const std::exception &error) {
    lucent::warn("emulator", "loadstate failed: {}", error.what());
    return false;
  }
}

bool Emulator::watchWrites(uint32_t address, uint32_t size) {
  return watches_->add(address, size);
}

void Emulator::clearWatches() {
  watches_->clear();
}

std::vector<WriteHit> Emulator::takeHits() {
  return watches_->take();
}

} // namespace z3d::app
