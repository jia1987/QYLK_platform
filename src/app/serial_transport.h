#pragma once
// QSerialPort 传输适配（ITransport 实现 + StreamFramer 收帧）。
// 仅在 Qt SerialPort 模块可用时编译（CMake: APP_HAS_SERIALPORT）。
// 目标板 Ubuntu 20.04：apt install libqt5serialport5-dev；
// Windows 开发机：Qt 维护工具勾选 "Qt Serial Port" 附加库。
#ifdef APP_HAS_SERIALPORT

#include <QObject>
#include <QSerialPort>

#include "core/bus_scheduler.h"
#include "core/frame_codec.h"

namespace massage::app {

class SerialTransport : public QObject, public core::ITransport {
  Q_OBJECT
 public:
  explicit SerialTransport(QObject* parent = nullptr);
  ~SerialTransport() override;

  bool open(const QString& portName, qint32 baud);
  void close();
  bool isOpen() const;
  QString errorString() const;

  // ITransport
  bool send(const core::Frame& f) override;

  // 校验通过的完整应答帧（接 AppCore::deliverReply）
  std::function<void(const core::Frame&)> replySink;
  // 串口级错误（拔线等）
  std::function<void(const QString&)> errorSink;

 private:
  QSerialPort port_;
  core::StreamFramer framer_;
};

}  // namespace massage::app

#endif  // APP_HAS_SERIALPORT
