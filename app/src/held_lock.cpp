#include "held_lock.hpp"

#include "section_memory.hpp"

namespace z3d::app {

bool HeldLock::release(Core::SectionReplay &replay, uint32_t sectionTls,
                       std::vector<Core::SectionReplay::Input> &out) const {
  const auto event = readStart<int32_t>(replay, addresses_.lock + addresses_.event);
  const auto owner = readStart<uint32_t>(replay, addresses_.lock + addresses_.owner);
  if (event >= 0 || owner == sectionTls) {
    return false;
  }
  // The holder's leave: no owner, no entries, the event set with nobody waiting.
  out.push_back(replayInput<int32_t>(addresses_.lock + addresses_.event, 1));
  out.push_back(replayInput<uint32_t>(addresses_.lock + addresses_.owner, 0));
  out.push_back(replayInput<uint32_t>(addresses_.lock + addresses_.count, 0));
  return true;
}

} // namespace z3d::app
