#include "frame_interpolation.hpp"

#include "emulator.hpp"

#include "core/section_replay.h"

#include <lucent/json.h>
#include <lucent/log.h>

#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace z3d::app {

FrameInterpolation::FrameInterpolation(Emulator &emulator, const std::filesystem::path &manifest)
    : emulator_(emulator) {
  std::ifstream file(manifest);
  const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::string error;
  const auto document = lucent::json::parse(json, error);
  const lucent::json::Value *table = document ? document->find("interpolation") : nullptr;
  if (table == nullptr) {
    return;
  }
  const lucent::json::Value *site = table->find("call_site");
  if (site == nullptr || !site->integer()) {
    throw std::runtime_error("\"interpolation\" wants \"call_site\"");
  }
  callSite_ = static_cast<uint32_t>(*site->integer());
}

FrameInterpolation::~FrameInterpolation() {
  setMode(Mode::Off);
}

bool FrameInterpolation::setMode(Mode mode) {
  if (!callSite_) {
    return mode == Mode::Off;
  }
  Core::SectionReplay &replay = emulator_.sectionReplay();
  if (mode == Mode::Off) {
    replay.Uninstall();
  } else if (mode_ == Mode::Off && !replay.Install(*callSite_, [this](Core::SectionReplay &) {
               onSection();
             })) {
    return false;
  }
  mode_ = mode;
  return true;
}

bool FrameInterpolation::setCallSite(uint32_t site) {
  if (mode_ != Mode::Off) {
    return false;
  }
  callSite_ = site;
  return true;
}

void FrameInterpolation::onSection() {
  Core::SectionReplay &replay = emulator_.sectionReplay();
  const Core::SectionReplay::Census &census = replay.SectionCensus();
  ++totals_.sections;
  totals_.sectionPages += census.pages;
  totals_.sectionMicroseconds += census.host_microseconds;
  totals_.sectionHeldReads += census.held_reads;
  for (const Core::SectionReplay::Svc &svc : census.svcs) {
    std::ostringstream key;
    key << std::hex << svc.number << '@' << svc.pc;
    if (!svc.service.empty()) {
      key << ':' << svc.service << ':' << svc.header;
    }
    ++totals_.svcs[key.str()];
  }
  for (const VAddr page : replay.SectionPages()) {
    ++totals_.pageRegions[page >> 20];
  }
  if (mode_ != Mode::Probe) {
    return;
  }
  std::vector<Core::SectionReplay::Difference> differences;
  const Core::SectionReplay::Result result = replay.Replay({}, [&] {
    differences = replay.DiffWithSection();
  });
  ++totals_.replays;
  totals_.replayMicroseconds += result.host_microseconds;
  if (!result.done) {
    ++totals_.refused;
    totals_.lastRefusal = result.refusal;
    return;
  }
  totals_.instructions += result.instructions;
  totals_.replayPages += result.pages;
  totals_.replayHeldReads += result.held_reads;
  bool identical = true;
  for (const Core::SectionReplay::Difference &difference : differences) {
    if (!difference.replayed) {
      totals_.windowOnly[difference.page] += difference.bytes;
      continue;
    }
    identical = false;
    ++totals_.differentPages;
    totals_.differentBytes += difference.bytes;
    auto &page = totals_.differentByPage[difference.page];
    page.first += difference.bytes;
    page.second = difference.first;
    if (totals_.firstDifference.empty()) {
      std::ostringstream text;
      text << std::hex << difference.page + difference.first << '+' << std::dec << difference.bytes;
      totals_.firstDifference = text.str();
    }
  }
  if (identical) {
    ++totals_.identical;
  }
}

std::string FrameInterpolation::takeReport() {
  std::ostringstream report;
  report << "sections=" << totals_.sections << " section_pages=" << totals_.sectionPages
         << " section_us=" << totals_.sectionMicroseconds
         << " replay_us=" << totals_.replayMicroseconds
         << " section_held_reads=" << totals_.sectionHeldReads
         << " replay_held_reads=" << totals_.replayHeldReads << " svcs=";
  for (const auto &[svc, count] : totals_.svcs) {
    report << svc << '=' << count << ',';
  }
  report << " regions=";
  for (const auto &[megabyte, count] : totals_.pageRegions) {
    report << std::hex << (megabyte << 20) << std::dec << ':' << count << ',';
  }
  report << " window_only=";
  for (const auto &[page, bytes] : totals_.windowOnly) {
    report << std::hex << page << std::dec << ':' << bytes << ',';
  }
  report << " replays=" << totals_.replays << " refused=" << totals_.refused;
  if (!totals_.lastRefusal.empty()) {
    report << " (" << totals_.lastRefusal << ')';
  }
  report << " instructions=" << totals_.instructions << " replay_pages=" << totals_.replayPages
         << " identical=" << totals_.identical << " different_pages=" << totals_.differentPages
         << " different_bytes=" << totals_.differentBytes;
  if (!totals_.firstDifference.empty()) {
    report << " first=" << totals_.firstDifference << " pages=";
    for (const auto &[page, bytes] : totals_.differentByPage) {
      report << std::hex << page + bytes.second << std::dec << ':' << bytes.first << ',';
    }
  }
  totals_ = {};
  return report.str();
}

} // namespace z3d::app
