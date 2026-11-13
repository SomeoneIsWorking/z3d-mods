#include "game_library.hpp"

#include "common/string_util.h"
#include "core/loader/loader.h"
#include "core/loader/smdh.h"

#include <lucent/json.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace z3d::app {
namespace {

constexpr int kLargeIcon = 48;

const std::string &member(const lucent::json::Value &entry, std::string_view key) {
  const lucent::json::Value *value = entry.find(key);
  if (value == nullptr || value->string() == nullptr) {
    throw std::runtime_error("library entry wants a string \"" + std::string(key) + "\"");
  }
  return *value->string();
}

uint32_t rgba(uint16_t rgb565) {
  const auto expand = [](uint32_t value, int bits) {
    return (value << (8 - bits)) | (value >> (2 * bits - 8));
  };
  const uint32_t red = expand((rgb565 >> 11) & 0x1F, 5);
  const uint32_t green = expand((rgb565 >> 5) & 0x3F, 6);
  const uint32_t blue = expand(rgb565 & 0x1F, 5);
  return 0xFF000000U | (blue << 16) | (green << 8) | red;
}

void readSmdh(Game &game) {
  const std::string rom = game.setup.rom.string();
  const auto loader = Loader::GetLoader(rom);
  std::vector<u8> smdh;
  if (!loader || loader->ReadIcon(smdh) != Loader::ResultStatus::Success ||
      !Loader::IsValidSMDH(smdh)) {
    throw std::runtime_error("no icon in " + rom);
  }
  Loader::SMDH data{};
  std::memcpy(&data, smdh.data(), sizeof(data));
  game.title = Common::UTF16BufferToUTF8(data.GetShortTitle(Loader::SMDH::TitleLanguage::English));
  for (const u16 pixel : data.GetIcon(true)) {
    game.icon.push_back(rgba(pixel));
  }
  game.iconSize = kLargeIcon;
}

} // namespace

GameLibrary GameLibrary::load(const std::filesystem::path &path) {
  std::ifstream file(path);
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::string error;
  const auto document = lucent::json::parse(text, error);
  if (!document || document->array() == nullptr) {
    throw std::runtime_error("library " + path.string() + " is not a JSON array " + error);
  }
  GameLibrary library;
  for (const lucent::json::Value &entry : *document->array()) {
    Game game;
    game.setup.rom = member(entry, "rom");
    game.setup.layout = parseLayout(member(entry, "layout"));
    const lucent::json::Value *textures = entry.find("textures");
    game.setup.customTextures =
        textures != nullptr && textures->boolean() != nullptr && *textures->boolean();
    if (const lucent::json::Value *options = entry.find("options"); options != nullptr && options->string() != nullptr) {
      game.setup.options = *options->string();
    }
    readSmdh(game);
    library.games_.push_back(std::move(game));
  }
  if (library.games_.empty()) {
    throw std::runtime_error("library " + path.string() + " names no games");
  }
  return library;
}

} // namespace z3d::app
