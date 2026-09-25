#pragma once
// CRC16/MODBUS —— 北京枢点 RS485 通讯协议校验算法。
// 协议文档未指明 CRC 算法，本实现经附件例帧实算验证：
//   AA 08 01 00 14 02 -> 76 ED
//   55 08 01 0B B9 01 -> 2F B1
// 线上字节序：低字节在前（CRCL），高字节在后（CRCH）。
#include <cstddef>
#include <cstdint>

namespace massage::core {

// 返回原始 CRC 值；写帧时 f[6] = 返回值低字节, f[7] = 高字节。
std::uint16_t crc16Modbus(const std::uint8_t* data, std::size_t len) noexcept;

}  // namespace massage::core
