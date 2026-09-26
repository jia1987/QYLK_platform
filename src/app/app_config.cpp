#include "app_config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QStringList>

namespace massage::app {

QString AppConfig::configPath() {
  const QString dir =
      QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  return dir + QStringLiteral("/config.json");
}

AppConfig AppConfig::defaults() {
  AppConfig c;
  // 默认槽位绑定（决策 D2：地址→槽位→类型 由配置维护）
  const QStringList ids = {"L1", "L2", "L3", "R1", "R2", "R3"};
  const QStringList types = {QStringLiteral("局部治疗头"),
                             QStringLiteral("手法治疗头"),
                             QStringLiteral("靠垫治疗头")};
  for (int i = 0; i < ids.size(); ++i)
    c.slotList.push_back(SlotConfig{ids[i], static_cast<quint8>(i + 1),
                                 types[i % types.size()]});
  // 决策 D10 修正后预设表（量程 10–50Hz）
  c.presets = {
      {15, 0, 20}, {30, 1, 30}, {40, 2, 20},
      {50, 0, 15}, {50, 1, 10}, {50, 0, 5},
  };
  return c;
}

AppConfig AppConfig::loadOrCreate() {
  AppConfig c = defaults();
  QFile f(configPath());
  if (!f.exists()) {
    c.save();
    return c;
  }
  if (!f.open(QIODevice::ReadOnly)) return c;
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
  f.close();
  if (!doc.isObject()) return c;
  const QJsonObject root = doc.object();

  c.serialPort = root.value(QStringLiteral("serialPort")).toString(c.serialPort);
  c.baudRate = root.value(QStringLiteral("baudRate")).toInt(c.baudRate);
  c.hospital = root.value(QStringLiteral("hospital")).toString();
  c.department = root.value(QStringLiteral("department")).toString();
  c.pinSalt = root.value(QStringLiteral("pinSalt")).toString();
  c.pinHash = root.value(QStringLiteral("pinHash")).toString();
  c.auditDbPath = root.value(QStringLiteral("auditDbPath")).toString();
  c.auditQuotaMb = root.value(QStringLiteral("auditQuotaMb")).toInt(c.auditQuotaMb);
  c.currentOperatorId = static_cast<qint64>(
      root.value(QStringLiteral("currentOperatorId")).toDouble(0));

  const QJsonArray slotsArr = root.value(QStringLiteral("slots")).toArray();
  if (slotsArr.size() == c.slotList.size()) {
    for (int i = 0; i < slotsArr.size(); ++i) {
      const QJsonObject s = slotsArr[i].toObject();
      c.slotList[i].slotId = s.value(QStringLiteral("slotId")).toString(c.slotList[i].slotId);
      c.slotList[i].addr = static_cast<quint8>(s.value(QStringLiteral("addr")).toInt(c.slotList[i].addr));
      c.slotList[i].type = s.value(QStringLiteral("type")).toString(c.slotList[i].type);
    }
  }
  const QJsonArray presetsArr = root.value(QStringLiteral("presets")).toArray();
  if (presetsArr.size() == c.presets.size()) {
    for (int i = 0; i < presetsArr.size(); ++i) {
      const QJsonObject p = presetsArr[i].toObject();
      c.presets[i].freqHz = p.value(QStringLiteral("freqHz")).toInt(c.presets[i].freqHz);
      c.presets[i].mode = p.value(QStringLiteral("mode")).toInt(c.presets[i].mode);
      c.presets[i].timeMin = p.value(QStringLiteral("timeMin")).toInt(c.presets[i].timeMin);
    }
  }
  return c;
}

bool AppConfig::save() const {
  const QString path = configPath();
  QDir().mkpath(QFileInfo(path).absolutePath());

  // 注意：局部变量不可命名为 slots（与 Qt 宏冲突）
  QJsonArray slotsArr;
  for (const SlotConfig& s : slotList) {
    QJsonObject o;
    o[QStringLiteral("slotId")] = s.slotId;
    o[QStringLiteral("addr")] = s.addr;
    o[QStringLiteral("type")] = s.type;
    slotsArr.append(o);
  }
  QJsonArray presetsArr;
  for (const PresetConfig& p : presets) {
    QJsonObject o;
    o[QStringLiteral("freqHz")] = p.freqHz;
    o[QStringLiteral("mode")] = p.mode;
    o[QStringLiteral("timeMin")] = p.timeMin;
    presetsArr.append(o);
  }
  QJsonObject root;
  root[QStringLiteral("serialPort")] = serialPort;
  root[QStringLiteral("baudRate")] = baudRate;
  root[QStringLiteral("hospital")] = hospital;
  root[QStringLiteral("department")] = department;
  root[QStringLiteral("pinSalt")] = pinSalt;
  root[QStringLiteral("pinHash")] = pinHash;
  root[QStringLiteral("auditDbPath")] = auditDbPath;
  root[QStringLiteral("auditQuotaMb")] = auditQuotaMb;
  root[QStringLiteral("currentOperatorId")] = static_cast<double>(currentOperatorId);
  root[QStringLiteral("slots")] = slotsArr;
  root[QStringLiteral("presets")] = presetsArr;

  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  f.close();
  return true;
}

}  // namespace massage::app
