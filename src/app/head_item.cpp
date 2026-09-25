#include "head_item.h"

#include <QStringList>

namespace massage::app {

using namespace core;

HeadItem::HeadItem(const SlotConfig& slot, QObject* parent)
    : QObject(parent),
      head_(slot.slotId.toStdString(), slot.addr),
      type_(slot.type) {
  head_.setEventCallback([this](HeadEvent e) {
    emit headEvent(static_cast<int>(e));
    emit stateChanged();
    emit uiTicked();
  });
}

QString HeadItem::stateClass() const {
  switch (head_.state()) {
    case HeadState::Absent:   return QStringLiteral("off");
    case HeadState::Idle:     return QStringLiteral("idle");
    case HeadState::Starting: return QStringLiteral("starting");
    case HeadState::Running:  return QStringLiteral("run");
    case HeadState::Paused:   return QStringLiteral("pause");
    case HeadState::Stopping: return QStringLiteral("stopping");
    case HeadState::Fault:    return QStringLiteral("fault");
  }
  return QStringLiteral("off");
}

QString HeadItem::stateText() const {
  switch (head_.state()) {
    case HeadState::Absent:   return QStringLiteral("未连接");
    case HeadState::Idle:     return QStringLiteral("待机 已连接") + type_;
    case HeadState::Starting: return QStringLiteral("启动中");
    case HeadState::Running:  return QStringLiteral("工作中");
    case HeadState::Paused:   return QStringLiteral("已暂停");
    case HeadState::Stopping: return QStringLiteral("停止中");
    case HeadState::Fault:    return QStringLiteral("故障");
  }
  return QString();
}

QString HeadItem::faultText() const {
  const quint8 fb = head_.faultBits();
  if (fb == 0) return QString();
  QStringList names;
  if (fb & FaultOverVolt)  names << QStringLiteral("过压保护");
  if (fb & FaultUnderVolt) names << QStringLiteral("欠压保护");
  if (fb & FaultOverTemp)  names << QStringLiteral("过温保护");
  if (fb & FaultHall)      names << QStringLiteral("霍尔异常");
  if (fb & FaultStall)     names << QStringLiteral("堵转保护");
  if (fb & FaultShort)     names << QStringLiteral("短路保护");
  return names.join(QStringLiteral("·"));
}

QString HeadItem::modeName() const {
  switch (head_.config().mode) {
    case Mode::Constant: return QStringLiteral("恒频");
    case Mode::Sweep:    return QStringLiteral("扫频");
    case Mode::Step:     return QStringLiteral("阶频");
  }
  return QString();
}

QString HeadItem::remainingText() const {
  const int s = head_.remainingSeconds();
  return QStringLiteral("%1:%2")
      .arg(s / 60, 2, 10, QLatin1Char('0'))
      .arg(s % 60, 2, 10, QLatin1Char('0'));
}

double HeadItem::progress() const {
  const int total = head_.totalSeconds();
  if (total <= 0) return 0.0;
  return static_cast<double>(total - head_.remainingSeconds()) / total;
}

void HeadItem::setSelected(bool s) {
  if (selected_ == s) return;
  selected_ = s;
  emit selectedChanged();
}

void HeadItem::deliverAck(AckFlag flag) {
  head_.onAck(flag);
  emit stateChanged();
  emit uiTicked();
}

void HeadItem::deliverInfo(quint8 faultBits, quint8 temp, bool motorRunning) {
  head_.onInfo(faultBits, temp, motorRunning);
  emit telemetryChanged();
  emit stateChanged();
}

void HeadItem::deliverSpeed(quint32 rpm) {
  head_.onSpeed(rpm);
  emit telemetryChanged();
}

void HeadItem::deliverPresence(bool online) {
  head_.onPresence(online);
  emit stateChanged();
  emit telemetryChanged();
}

void HeadItem::tickUi() { emit uiTicked(); }

bool HeadItem::applyCore(const HeadConfig& c) {
  const bool ok = head_.applyConfig(c);
  if (ok) {
    emit configChanged();
    emit uiTicked();
  }
  return ok;
}

void HeadItem::setHeadType(const QString& t) {
  if (type_ == t) return;
  type_ = t;
  emit configChanged();
  emit stateChanged();
}

}  // namespace massage::app
