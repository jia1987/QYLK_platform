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
#include "audit/audit_clock.h"
#include "audit/audit_log.h"
#include "audit/pin_guard.h"
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
  Q_PROPERTY(bool pinSet READ pinSet NOTIFY pinChanged)
  Q_PROPERTY(bool auditOk READ auditOk NOTIFY auditStateChanged)
  Q_PROPERTY(bool pinLocked READ pinLocked NOTIFY pinLockChanged)
  Q_PROPERTY(qlonglong currentOperatorId READ currentOperatorId NOTIFY currentOperatorChanged)
  Q_PROPERTY(QString currentOperatorName READ currentOperatorName NOTIFY currentOperatorChanged)
 public:
  AppCore(AppConfig* cfg, core::ITransport& transport, bool realBus,
          audit::AuditLog* audit = nullptr, QObject* parent = nullptr);

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
  Q_INVOKABLE void showAbout();

  // ---- M3.5 设置面板 / PIN 门禁（决策 D9）+ M4a 防暴破（D20）----
  bool pinSet() const;
  bool pinLocked() const { return pinGuard_.locked(); }
  Q_INVOKABLE bool verifyPin(const QString& pin,
                             const QString& source = QStringLiteral("settings"));
  Q_INVOKABLE bool setInitialPin(const QString& pin);       // 首启强制设置
  Q_INVOKABLE bool changePin(const QString& oldPin, const QString& newPin);

  // ---- M4a 审计（决策 D13–D22）----
  bool auditOk() const;
  qlonglong currentOperatorId() const { return currentOpId_; }
  QString currentOperatorName() const;
  Q_INVOKABLE QVariantList operatorList() const;            // 活跃操作员（D14）
  Q_INVOKABLE qlonglong addOperator(const QString& name, const QString& code);
  Q_INVOKABLE void removeOperator(qlonglong id);            // 软删（审计留痕）
  Q_INVOKABLE void setCurrentOperator(qlonglong id);        // 治疗前选人，记住上次
  Q_INVOKABLE void snapshotNow();                           // 立即写会话快照（30s 定时/测试用）
  Q_INVOKABLE void saveIdentity(const QString& hospital, const QString& department);
  Q_INVOKABLE void saveSerial(const QString& port, int baud);  // 重启生效
  Q_INVOKABLE void setSystemTime(const QString& isoDateTime);  // 仅 Linux（timedatectl）
  Q_INVOKABLE QString serialConfigText() const;
  Q_INVOKABLE QString hospitalText() const { return cfg_->hospital; }
  Q_INVOKABLE QString departmentText() const { return cfg_->department; }
  Q_INVOKABLE QString serialPortText() const { return cfg_->serialPort; }
  Q_INVOKABLE int baudRate() const { return cfg_->baudRate; }

  // ---- M3.5 绑定 / 维护模式（决策 D2：0xAA 单头烧录）----
  Q_INVOKABLE QVariantList bindingInfos() const;  // [{slot,addr,type,online}]
  Q_INVOKABLE void maintSetType(int slotIndex, const QString& type);
  Q_INVOKABLE bool maintSetAddr(int slotIndex, int newAddr);  // 改配置，重启生效
  Q_INVOKABLE void maintProbeZero();   // 探测地址 0（新头出厂默认）
  Q_INVOKABLE void maintBurn(int slotIndex);  // 带安全联锁的烧录

 signals:
  void toast(const QString& msg, const QString& type, int ms);
  void confirmRequest(const QVariantMap& info);
  void sysStateChanged();
  void clockChanged();
  void hospChanged();
  void selectionChanged();
  void presetsChanged();
  void pinChanged();
  void bindingsChanged();
  void maintZeroFound(bool found);
  void maintBurnResult(bool ok, const QString& msg);
  void auditStateChanged();       // M4a：审计可用/降级切换（D13 醒目告警）
  void pinLockChanged();          // M4a：PIN 锁定状态（D20）
  void currentOperatorChanged();  // M4a：当前操作员（D14）
  void operatorsChanged();

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
  // M4a 审计辅助
  void auditLog(const char* category, const char* type, const QString& slot, int addr,
                const std::string& payload, qlonglong operatorId = 0);
  void noteParamChange(HeadItem* h, const core::HeadConfig& before);  // 2s 防抖（D15）
  void flushParamChange();

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
  // ---- M4a 审计（D13–D22）----
  audit::AuditLog* audit_;               // 可空（旧测试/无审计场景）
  audit::SystemClock auditClock_;        // 单调+墙钟（D17）
  audit::PinGuard pinGuard_;             // 5 次失败锁 5 分钟（D20）
  QTimer snapTimer_;                     // 30s 会话快照（D15）
  QTimer paramDebounceTimer_;            // 改参 2s 防抖（D15）
  QString pendingSlot_;                  // 防抖中的槽位
  core::HeadConfig pendingBefore_;       // 防抖起点参数
  qlonglong currentOpId_ = 0;            // 当前操作员（D14）
};

}  // namespace massage::app
