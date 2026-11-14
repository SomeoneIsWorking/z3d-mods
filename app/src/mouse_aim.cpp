#include "mouse_aim.hpp"

#include "emulator.hpp"
#include "game_window.hpp"

#include <lucent/json.h>

#include <SDL3/SDL_rect.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>

namespace z3d::app {
namespace {

// The HID gyroscope's units per degree a second (GetGyroscopeLowRawToDpsCoefficient).
constexpr double kUnitsPerDps = 14.375;
// The reader turns its direction by a hundredth of the rate per sample (reader +0x850).
constexpr double kSamplesPerSecond = 100.0;
// A sample's largest turn. The reader turns its direction to first order, which is exact for a
// small turn; a faster mouse is finished over the next samples.
constexpr double kMaxStep = 6.0;
// focus.rot's units per degree.
constexpr double kBinaryAngle = 65536.0 / 360.0;
// The view this far from where the 3DS points it is held at a limit of the look.
constexpr double kLimitSlack = 0.1;
// Samples without a turn after which the reader has taken every sample: it takes up to 32 a read.
constexpr uint32_t kSettled = 32;
// A zero-drift radius that matches no sample: the reader's mean of like samples is the sample.
constexpr float kNoDriftRadius = -1.0F;

uint32_t address(const lucent::json::Value &aim, std::string_view key) {
  const lucent::json::Value *value = aim.find(key);
  if (value == nullptr || !value->integer()) {
    throw std::runtime_error("\"aim\" wants \"" + std::string(key) + "\"");
  }
  return static_cast<uint32_t>(*value->integer());
}

template <typename T> std::array<uint8_t, sizeof(T)> bytesOf(const T &value) {
  std::array<uint8_t, sizeof(T)> bytes{};
  std::memcpy(bytes.data(), &value, sizeof(T));
  return bytes;
}

} // namespace

MouseAim::MouseAim(GameWindow &window, Emulator &emulator, PadInput &pad, const std::filesystem::path &manifest)
    : window_(window), emulator_(emulator), pad_(pad) {
  std::ifstream file(manifest);
  const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::string error;
  const auto document = lucent::json::parse(json, error);
  const lucent::json::Value *aim = document ? document->find("aim") : nullptr;
  if (aim == nullptr) {
    return;
  }
  layout_ = Layout{address(*aim, "reader"),     address(*aim, "angles"),      address(*aim, "valid"),
                   address(*aim, "yaw_scale"),  address(*aim, "pitch_scale"), address(*aim, "motion_off"),
                   address(*aim, "play"),       address(*aim, "player"),      address(*aim, "look_flags"),
                   address(*aim, "look_active"), address(*aim, "look_base"),  address(*aim, "focus_rot"),
                   address(*aim, "direction"),  address(*aim, "filters"),     address(*aim, "drift_radius"),
                   address(*aim, "drift_bias"), address(*aim, "scale")};
  pad_.setGyroSource(this);
  emulator_.onFrame([this] {
    update();
  });
}

MouseAim::~MouseAim() {
  if (layout_) {
    pad_.setGyroSource(nullptr);
    window_.captureMouse(false);
  }
}

template <typename T> std::optional<T> MouseAim::read(uint32_t address) const {
  std::array<uint8_t, sizeof(T)> bytes{};
  if (!emulator_.read(address, bytes)) {
    return std::nullopt;
  }
  T value{};
  std::memcpy(&value, bytes.data(), sizeof(T));
  return value;
}

void MouseAim::move(double dx, double dy) {
  if (!active_) {
    return;
  }
  // A turn on y looks left and a turn on x looks down.
  const double pitch = dy * kDegreesPerCount / pitchScale_;
  const double yaw = -dx * kDegreesPerCount / yawScale_;
  // The view does not follow a turn further past a limit, so the 3DS does not make it.
  if (pitch * past_[0] <= 0.0) {
    pitch_ += pitch;
  }
  if (yaw * past_[1] <= 0.0) {
    yaw_ += yaw;
  }
}

std::array<float, 3> MouseAim::takeSample() {
  std::array<double, 3> turn{};
  const bool pitchNow = pitch_ != 0.0 && (yaw_ == 0.0 || !yawNext_);
  if (pitchNow) {
    const double step = std::clamp(pitch_, -kMaxStep, kMaxStep);
    pitch_ -= step;
    turn[0] = step;
    // The 3DS turns around its x, so world up turns the other way on its axes.
    const double angle = -step * std::numbers::pi / 180.0;
    up_ = {up_[0], up_[1] * std::cos(angle) - up_[2] * std::sin(angle),
           up_[1] * std::sin(angle) + up_[2] * std::cos(angle)};
  } else if (yaw_ != 0.0) {
    const double step = std::clamp(yaw_, -kMaxStep, kMaxStep);
    yaw_ -= step;
    turn = {up_[0] * step, up_[1] * step, up_[2] * step};
  }
  yawNext_ = pitchNow;
  std::array<float, 3> rate{};
  bool turned = false;
  for (size_t axis = 0; axis < turn.size(); ++axis) {
    const double wanted = carry_[axis] + turn[axis] * kSamplesPerSecond * kUnitsPerDps;
    const double units = std::clamp(std::round(wanted), -32767.0, 32767.0);
    carry_[axis] = wanted - units;
    rate[axis] = static_cast<float>(units / kUnitsPerDps);
    turned = turned || units != 0.0;
  }
  still_ = turned ? 0 : still_ + 1;
  ++pastFor_;
  return rate;
}

std::optional<uint32_t> MouseAim::exactReader() {
  const std::optional<uint32_t> reader = read<uint32_t>(layout_->reader);
  if (!reader || *reader == 0) {
    return std::nullopt;
  }
  // Zero play, zero drift, the accelerometer's revise and axis rotation: one byte each.
  const std::optional<uint32_t> filters = read<uint32_t>(*reader + layout_->filters);
  const std::optional<float> radius = read<float>(*reader + layout_->driftRadius);
  if (!filters || !radius) {
    return std::nullopt;
  }
  if (*filters != 0 || *radius != kNoDriftRadius) {
    const std::array<double, 3> scale{1.0, 1.0, 1.0};
    const bool written = emulator_.write(*reader + layout_->filters, bytesOf(uint32_t{0})) &&
                         emulator_.write(*reader + layout_->driftRadius, bytesOf(kNoDriftRadius)) &&
                         emulator_.write(*reader + layout_->driftBias, bytesOf(std::array<float, 3>{})) &&
                         emulator_.write(*reader + layout_->scale, bytesOf(scale));
    if (!written) {
      return std::nullopt;
    }
  }
  return reader;
}

std::optional<uint32_t> MouseAim::lookingPlayer() const {
  const std::optional<uint8_t> valid = read<uint8_t>(layout_->valid);
  const std::optional<uint8_t> off = read<uint8_t>(layout_->motionOff);
  const std::optional<uint32_t> play = read<uint32_t>(layout_->play);
  if (!valid || *valid == 0 || !off || *off != 0 || !play || *play == 0) {
    return std::nullopt;
  }
  const std::optional<uint32_t> player = read<uint32_t>(*play + layout_->player);
  if (!player || *player == 0) {
    return std::nullopt;
  }
  const std::optional<uint32_t> flags = read<uint32_t>(*player + layout_->lookFlags);
  if (!flags || (*flags & layout_->lookActive) != layout_->lookActive) {
    return std::nullopt;
  }
  return player;
}

std::array<double, 2> MouseAim::pastLimits(uint32_t player) const {
  const auto angles = read<std::array<float, 2>>(layout_->angles);
  const auto base = read<std::array<int16_t, 2>>(player + layout_->lookBase);
  const auto focus = read<std::array<int16_t, 2>>(player + layout_->focusRot);
  if (!angles || !base || !focus) {
    return {};
  }
  const std::array<double, 2> scales{pitchScale_, yawScale_};
  std::array<double, 2> past{};
  for (size_t axis = 0; axis < past.size(); ++axis) {
    // The look is the base plus the 3DS's angle times the scale, held inside its limits.
    const double asked = (*base)[axis] + (*angles)[axis] * scales[axis] * kBinaryAngle;
    const double held = std::remainder(asked - (*focus)[axis], 65536.0) / kBinaryAngle;
    past[axis] = std::abs(held) > kLimitSlack ? held / scales[axis] : 0.0;
  }
  return past;
}

void MouseAim::update() {
  const std::optional<uint32_t> reader = exactReader();
  const std::optional<float> yawScale = read<float>(layout_->yawScale);
  const std::optional<float> pitchScale = read<float>(layout_->pitchScale);
  const std::optional<uint32_t> player = lookingPlayer();
  active_ = reader && yawScale && *yawScale > 0.0F && pitchScale && *pitchScale > 0.0F && player;
  window_.captureMouse(active_);
  const SDL_FPoint moved = window_.takeMouseMotion();
  if (!active_) {
    pitch_ = 0.0;
    yaw_ = 0.0;
    past_ = {};
    pastFor_ = 0;
    return;
  }
  yawScale_ = *yawScale;
  pitchScale_ = *pitchScale;
  const std::array<double, 2> past = pastLimits(*player);
  if (std::abs(past[0] - past_[0]) > kLimitSlack || std::abs(past[1] - past_[1]) > kLimitSlack) {
    pastFor_ = 0;
  }
  past_ = past;
  if (still_ >= kSettled) {
    // Rows are the 3DS's axes in the world, whose up is y.
    if (const auto rows = read<std::array<float, 9>>(*reader + layout_->direction)) {
      up_ = {(*rows)[1], (*rows)[4], (*rows)[7]};
    }
    // The game has read every sample: the 3DS comes back to the limits it went past, once.
    if (pastFor_ >= kSettled && (past_[0] != 0.0 || past_[1] != 0.0)) {
      pitch_ -= past_[0];
      yaw_ -= past_[1];
      still_ = 0;
    }
  }
  move(moved.x, moved.y);
}

} // namespace z3d::app
