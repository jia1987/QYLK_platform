#pragma once
// AppCore —— 应用装配层：BusScheduler + 6×TreatmentHead + 配置 + QML API。
// 线程模型：全部在主线程（QTimer 驱动），总线事务由 BusScheduler 状态机管理。
#include <functional>
#include <memory>

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>

#include "app_config.h"
#include "core/bus_scheduler.h"
#include "head_item.h"
#include "system_clock.h"

namespace massage::app {

class AppCore : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList heads READ heads CONSTANT)
  Q_PROPERTY(QString sysStateText READ sysStateText NOTIFY sysStateChanged)
  Q_PROPERTY(QString sysStateClass READ sysStateClass NOTIFY sysStateChanged)
  Q_PROPERTY(QString clockText READ clockText NOTIFY clockChanged)
  Q_PROPERTY(QString hospInfo READ hospInfo NOTIFY hospChanged)
  Q_PROPERTY(HeadItem* selectedHead READ selectedHead NOTIFY selectionChanged)
  Q_PROPERTY(QString targetName READ targetName NOTIFY selectionChanged)
  Q_PROPERTY(bool centerLocked READ centerLocked NOTIFY selectionChanged)
  Q_PROPERTY(QStringList presetLabels READ presetLabels NOTIFY presetsChanged)
  Q_PROPERTY(int savePendingIndex READ savePendingIndex NOTIFY presetsChanged)
  Q_PROPERTY(bool anyRunning READ anyRunning NOTIFY sysStateChanged)
  Q_PROPERTY(bool allPaused READ allPaused NOTIFY sysStateChanged)
  Q_PROPERTY(bool anyOutput READ anyOutput NOTIFY sysStateChanged)
  Q_PROPERTY(QString modeDesc READ modeDesc NOTIFY selectionChanged)
  Q_PROPERTY(QString versionText READ versionText CONSTANT)
 public:
  AppCore(AppConfig* cfg, core::ITransport& transport, bool realBus,
          QObject* parent = nullptr);

  // 传输入口：MockBus/串口适配层把校验过的应答帧交给这里
  void deliverReply(const core::Frame& f);

  QVariantList heads() const;
  QString sysStateText() const;
  QString sysStateClass() const;
  QString clockText() const { return clockText_; }
  QString hospInfo() const;
  HeadItem* selectedHead() const { return selected_; }
  QString targetName() const;
  bool centerLocked() const { return selected_ == nullptr; }
  QStringList presetLabels() const;
  int savePendingIndex() const { return savePend_; }
  bool anyRunning() const;
  bool allPaused() const;
  bool anyOutput() const;
  QString modeDesc() const;
  QString versionText() const;

  // ---- QML 操作 ----
  Q_INVOKABLE void selectHead(const QString& slotId);  // 再点一次取消选择
  Q_INVOKABLE void adjustFreq(int deltaSteps);         // ±1 步 = ±5Hz（10–50）
  Q_INVOKABLE void adjustTime(int deltaSteps);         // ±1 步 = ±5min（5–60）
  Q_INVOKABLE void setMode(int mode);
  Q_INVOKABLE void applyPreset(int index);
  Q_INVOKABLE void headAction(const QString& slotId, const QString& action);
  Q_INVOKABLE void requestStart(const QString& slotId);   // 弹确认
  Q_INVOKABLE void requestStop(const QString& slotId);    // 弹确认
  Q_INVOKABLE void requestStopAll();                      // 弹确认
  Q_INVOKABLE void requestPauseAll();                     // 全部暂停/继续切换
  Q_INVOKABLE void confirmResponse(bool accepted);
  Q_INVOKABLE QString presetText(int index) const;
  Q_INVOKABLE void showAbout();  // 齿轮按钮：M3.5 接入 PIN 设置面板前的占位

 signals:
  void toast(const QString& msg, const QString& type, int ms);
  void confirmRequest(const QVariantMap& info);
  void sysStateChanged();
  void clockChanged();
  void hospChanged();
  void selectionChanged();
  void presetsChanged();

 private:
  HeadItem* findBySlot(const QString& slotId) const;
  HeadItem* findByAddr(quint8 addr) const;
  void sendHeadFrame(HeadItem* h, const core::Frame& f);
  void syncPolling();
  void onLogicTick();
  void onUiTick();
  void onHeadEvent(HeadItem* h, int ev);
  void runConfirmed(const QVariantMap& info, std::function<void()> action);
  void startHeads(const QStringList& slotIds);
  void stopHeads(const QStringList& slotIds);

  AppConfig* cfg_;
  SystemClock clock_;
  std::unique_ptr<core::BusScheduler> sched_;
  QList<HeadItem*> heads_;
  QTimer busTimer_;    // 10ms：总线事务推进
  QTimer logicTimer_;  // 250ms：曲线/状态机 tick（D4）
  QTimer uiTimer_;     // 1s：时钟/倒计时显示
  QTimer savePendTimer_;
  HeadItem* selected_ = nullptr;
  int savePend_ = -1;
  std::function<void()> pendingConfirm_;
  QString clockText_;
  bool realBus_;
};

}  // namespace massage::app
