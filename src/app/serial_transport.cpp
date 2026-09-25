#ifdef APP_HAS_SERIALPORT
#include "serial_transport.h"

namespace massage::app {

SerialTransport::SerialTransport(QObject* parent) : QObject(parent) {
  connect(&port_, &QSerialPort::readyRead, this, [this] {
    const QByteArray data = port_.readAll();
    framer_.feed(reinterpret_cast<const std::uint8_t*>(data.constData()),
                 static_cast<std::size_t>(data.size()));
    core::Frame f;
    while (framer_.next(f))
      if (replySink) replySink(f);
  });
  connect(&port_, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialError) {
    if (errorSink) errorSink(port_.errorString());
  });
}

SerialTransport::~SerialTransport() { close(); }

bool SerialTransport::open(const QString& portName, qint32 baud) {
  port_.setPortName(portName);
  port_.setBaudRate(baud);           // 协议：57600（可定制）
  port_.setDataBits(QSerialPort::Data8);
  port_.setParity(QSerialPort::NoParity);
  port_.setStopBits(QSerialPort::OneStop);
  port_.setFlowControl(QSerialPort::NoFlowControl);
  return port_.open(QIODevice::ReadWrite);
}

void SerialTransport::close() {
  if (port_.isOpen()) port_.close();
}

bool SerialTransport::isOpen() const { return port_.isOpen(); }

QString SerialTransport::errorString() const { return port_.errorString(); }

bool SerialTransport::send(const core::Frame& f) {
  if (!port_.isOpen()) return false;
  const qint64 n = port_.write(reinterpret_cast<const char*>(f.data()),
                               static_cast<qint64>(f.size()));
  return n == static_cast<qint64>(f.size());
}

}  // namespace massage::app

#endif  // APP_HAS_SERIALPORT
