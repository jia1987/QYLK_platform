#pragma once
// HeadItem —— TreatmentHead（纯 C++ 核心）的 QML 包装层。
// 只做类型转换与信号转发，不含业务逻辑（逻辑全在 core，保持可单测）。
#include <QObject>
#include <QString>

#include "app_config.h"
#include "core/head_state.h"

namespace massage::app {

class HeadItem : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString slotId READ slotId CONSTANT)
  Q_PROPERTY(QString side READ side CONSTANT)
  Q_PROPERTY(int addr READ addr CONSTANT)
  Q_PROPERTY(QString headType READ headType NOTIFY configChanged)
  Q_PROPERTY(QString stateClass READ stateClass NOTIFY stateChanged)
  Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
  Q_PROPERTY(QString faultText READ faultText NOTIFY stateChanged)
  Q_PROPERTY(bool online READ online NOTIFY stateChanged)
  Q_PROPERTY(bool selected READ selected WRITE setSelected NOTIFY selectedChanged)
  Q_PROPERTY(int freqHz READ freqHz NOTIFY configChanged)
  Q_PROPERTY(int mode READ modeInt NOTIFY configChanged)
  Q_PROPERTY(QString modeName READ modeName NOTIFY configChanged)
  Q_PROPERTY(int timeMin READ timeMin NOTIFY configChanged)
  Q_PROPERTY(int remaining READ remaining NOTIFY uiTicked)
  Q_PROPERTY(QString remainingText READ remainingText NOTIFY uiTicked)
  Q_PROPERTY(double progress READ progress NOTIFY uiTicked)
  Q_PROPERTY(int actualHz READ actualHz NOTIFY telemetryChanged)
  Q_PROPERTY(int tempC READ tempC NOTIFY telemetryChanged)
 public:
  explicit HeadItem(const SlotConfig& slot, QObject* parent = nullptr);

  core::TreatmentHead& coreHead() { return head_; }
  const core::TreatmentHead& coreHead() const { return head_; }

  // ---- QML 读取 ----
  QString slotId() const { return QString::fromStdString(head_.slotId()); }
  QString side() const { return slotId().left(1); }
  int addr() const { return head_.addr(); }
  QString headType() const { return type_; }
  QString stateClass() const;
  QString stateText() const;
  QString faultText() const;
  bool online() const { return head_.online(); }
  bool selected() const { return selected_; }
  void setSelected(bool s);
  int freqHz() const { return head_.config().freqHz; }
  int modeInt() const { return static_cast<int>(head_.config().mode); }
  QString modeName() const;
  int timeMin() const { return head_.config().timeMin; }
  int remaining() const { return head_.remainingSeconds(); }
  QString remainingText() const;
  double progress() const;
  int actualHz() const { return head_.reportedRpm() / 60; }
  int tempC() const { return head_.tempC(); }

  // ---- 设备事件入口（AppCore 从 BusScheduler 路由过来）----
  void deliverAck(core::AckFlag flag);
  void deliverInfo(quint8 faultBits, quint8 tempC, bool motorRunning);
  void deliverSpeed(quint32 rpm);
  void deliverPresence(bool online);
  void tickUi();  // 1s：刷新倒计时显示

  // ---- 配置 ----
  bool applyCore(const core::HeadConfig& c);
  void setHeadType(const QString& t);

 signals:
  void stateChanged();
  void configChanged();
  void telemetryChanged();
  void uiTicked();
  void selectedChanged();
  void headEvent(int ev);  // core::HeadEvent 转 int

 private:
  core::TreatmentHead head_;
  QString type_;
  bool selected_ = false;
};

}  // namespace massage::app
