#pragma once
// 应用配置持久化（JSON，决策 D10）：槽位绑定、预设、医院信息、PIN、串口。
#include <QString>
#include <QVector>

namespace massage::app {

struct SlotConfig {
  QString slotId;  // L1..R3
  quint8 addr = 0; // 485 地址（绑定界面维护）
  QString type;    // 局部治疗头 / 手法治疗头 / 靠垫治疗头
};

struct PresetConfig {
  int freqHz = 30;
  int mode = 0;  // 0恒频 1扫频 2阶频
  int timeMin = 20;
};

class AppConfig {
 public:
  QString serialPort;          // 空 = 模拟总线模式
  int baudRate = 57600;
  // 注意：不可命名为 slots（与 Qt 宏冲突，会被展开为空）
  QVector<SlotConfig> slotList;
  QVector<PresetConfig> presets;
  QString hospital;
  QString department;
  QString pinSalt;
  QString pinHash;             // 设置面板 PIN（M3.5 启用）

  static QString configPath();
  static AppConfig loadOrCreate();
  bool save() const;

 private:
  static AppConfig defaults();
};

}  // namespace massage::app
