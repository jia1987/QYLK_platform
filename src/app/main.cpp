// @srs SRS-004 SRS-005 SRS-006 SRS-018 SRS-054 SRS-055 SRS-092
// 台式按摩仪多设备控制软件 —— 应用入口。
// 默认模拟总线（进程内 MockBus）；--serial <port> 使用真实 485（需 Qt SerialPort）。
#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <memory>

#include "app_config.h"
#include "app_core.h"
#include "audit/audit_log.h"
#include "audit/json_mini.h"
#include "mock_bus.h"
#ifdef APP_HAS_SERIALPORT
#include "serial_transport.h"
#endif

#ifndef APP_VERSION
#define APP_VERSION "0.3.0"
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
    // 模拟总线：默认按原型演示 L2 未接入、其余 5 头在位。
    // MASSAGE_MOCK_PRESENT="0" 等可覆盖在位地址表（维护/烧录流程演练用：
    // 如只留地址 0 的新头，模拟换头场景）
    std::vector<std::uint8_t> present;
    const QByteArray mockEnv = qgetenv("MASSAGE_MOCK_PRESENT");
    if (!mockEnv.isEmpty()) {
      const QList<QByteArray> parts = mockEnv.split(',');
      for (const QByteArray& p : parts)
        present.push_back(static_cast<std::uint8_t>(p.toUInt()));
    } else {
      for (const app::SlotConfig& s : cfg.slotList)
        if (s.slotId != QLatin1String("L2")) present.push_back(s.addr);
    }
    mock = std::make_unique<app::MockBus>(present);
    transport = mock.get();
  }

  // ---- M4a 审计追踪（决策 D13–D22）：打开失败不阻止治疗（fail-operational），
  // AuditLog 自动降级 NDJSON 兜底并定期重试恢复 ----
  audit::SystemClock auditClock;
  audit::AuditLog auditLog;
  {
    audit::AuditConfig acfg;
    if (cfg.auditDbPath.isEmpty()) {
      const QString dir =
          QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
      QDir().mkpath(dir);
      acfg.dbPath = (dir + QStringLiteral("/audit.db")).toStdString();
    } else {
      acfg.dbPath = cfg.auditDbPath.toStdString();
    }
    acfg.quotaMb = cfg.auditQuotaMb;
    acfg.appVersion = APP_VERSION;
    // deviceId 待 T3 板卡冻结后填 SN（当前 DEV-UNSET）
    if (!auditLog.open(acfg, auditClock))
      qWarning("审计 DB 打开失败 —— 已降级文件兜底（不阻止治疗，D13）: %s",
               acfg.dbPath.c_str());
  }

  app::AppCore core(&cfg, *transport, realBus, &auditLog);
  if (mock)
    mock->replySink = [&core](const core::Frame& f) { core.deliverReply(f); };
#ifdef APP_HAS_SERIALPORT
  if (serial) {
    serial->replySink = [&core](const core::Frame& f) { core.deliverReply(f); };
    serial->errorSink = [&auditLog](const QString& e) {
      qWarning("串口错误: %s", qPrintable(e));
      auditLog.log(audit::cat::Fault, audit::ev::SerialError, {}, 0, 0,
                   std::string("{") + audit::jstr("error", e.toStdString()) + "}");
    };
  }
#endif

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("App"), &core);
  engine.load(QUrl(QStringLiteral("qrc:/main.qml")));  // qml.qrc 位于 qml/，前缀 /
  if (engine.rootObjects().isEmpty()) {
    auditLog.close();
    return -1;
  }
  const int rc = QGuiApplication::exec();
  auditLog.close();  // 正常退出：SESSION_END（未走到这里 = 崩溃 → 哨兵补记 D19）
  return rc;
}
