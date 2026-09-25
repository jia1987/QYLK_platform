// 台式按摩仪多设备控制软件 —— 应用入口。
// 默认模拟总线（进程内 MockBus）；--serial <port> 使用真实 485（需 Qt SerialPort）。
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <memory>

#include "app_config.h"
#include "app_core.h"
#include "mock_bus.h"
#ifdef APP_HAS_SERIALPORT
#include "serial_transport.h"
#endif

using namespace massage;

int main(int argc, char* argv[]) {
  QGuiApplication qtApp(argc, argv);
  qtApp.setApplicationName(QStringLiteral("massage-controller"));
  qtApp.setOrganizationName(QStringLiteral("shudot"));
  QQuickStyle::setStyle(QStringLiteral("Basic"));

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("三维螺旋振动治疗系统 · 多治疗头控制"));
  parser.addHelpOption();
  QCommandLineOption serialOpt(
      QStringLiteral("serial"),
      QStringLiteral("真实 485 串口设备（如 COM6 或 /dev/massage485）；缺省为模拟总线"),
      QStringLiteral("port"));
  QCommandLineOption baudOpt(QStringLiteral("baud"),
                             QStringLiteral("波特率（默认 57600）"),
                             QStringLiteral("baud"));
  parser.addOption(serialOpt);
  parser.addOption(baudOpt);
  parser.process(qtApp);

  app::AppConfig cfg = app::AppConfig::loadOrCreate();
  if (parser.isSet(serialOpt)) cfg.serialPort = parser.value(serialOpt);
  if (parser.isSet(baudOpt)) cfg.baudRate = parser.value(baudOpt).toInt();

  // ---- 传输选择 ----
  std::unique_ptr<app::MockBus> mock;
#ifdef APP_HAS_SERIALPORT
  std::unique_ptr<app::SerialTransport> serial;
#endif
  core::ITransport* transport = nullptr;
  bool realBus = false;

  if (!cfg.serialPort.isEmpty()) {
#ifdef APP_HAS_SERIALPORT
    serial = std::make_unique<app::SerialTransport>();
    if (serial->open(cfg.serialPort, cfg.baudRate)) {
      transport = serial.get();
      realBus = true;
    } else {
      qWarning("串口 %s 打开失败: %s（回退到模拟总线）",
               qPrintable(cfg.serialPort), qPrintable(serial->errorString()));
      serial.reset();
    }
#else
    qWarning("本机 Qt 未安装 SerialPort 模块，无法使用 --serial（回退到模拟总线）");
#endif
  }
  if (!transport) {
    // 模拟总线：按原型演示 L2 未接入，其余 5 头在位
    std::vector<std::uint8_t> present;
    for (const app::SlotConfig& s : cfg.slotList)
      if (s.slotId != QLatin1String("L2")) present.push_back(s.addr);
    mock = std::make_unique<app::MockBus>(present);
    transport = mock.get();
  }

  app::AppCore core(&cfg, *transport, realBus);
  if (mock)
    mock->replySink = [&core](const core::Frame& f) { core.deliverReply(f); };
#ifdef APP_HAS_SERIALPORT
  if (serial) {
    serial->replySink = [&core](const core::Frame& f) { core.deliverReply(f); };
    serial->errorSink = [](const QString& e) { qWarning("串口错误: %s", qPrintable(e)); };
  }
#endif

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("App"), &core);
  engine.load(QUrl(QStringLiteral("qrc:/main.qml")));  // qml.qrc 位于 qml/，前缀 /
  if (engine.rootObjects().isEmpty()) return -1;
  return QGuiApplication::exec();
}
