#include "core/crc16.h"

namespace massage::core {

std::uint16_t crc16Modbus(const std::uint8_t* data, std::size_t len) noexcept {
  std::uint16_t crc = 0xFFFF;
  for (std::size_t i = 0; i < len; ++i) {
    crc = static_cast<std::uint16_t>(crc ^ data[i]);
    for (int bit = 0; bit < 8; ++bit) {
      if (crc & 0x0001) {
        crc = static_cast<std::uint16_t>((crc >> 1) ^ 0xA001);
      } else {
        crc = static_cast<std::uint16_t>(crc >> 1);
      }
    }
  }
  return crc;
}

}  // namespace massage::core
