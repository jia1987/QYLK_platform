#include "app_core.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QRandomGenerator>
#include <QStorageInfo>
#include <QStringList>
#include <QVariantMap>

#include "audit/audit_export.h"
#include "audit/json_mini.h"
#include "core/frame_codec.h"

#ifndef APP_VERSION
#define APP_VERSION "0.3.0"
#endif

namespace massage::app {

using namespace core;

namespace {
QString modeNameOf(Mode m) {
  switch (m) {
    case Mode::Constant: return QStringLiteral("恒频");
    case Mode::Sweep:    return QStringLiteral("扫频");
    case Mode::Step:     return QStringLiteral("阶频");
  }
  return QString();
}
}  // namespace

AppCore::AppCore(AppConfig* cfg, ITransport& transport, bool realBus,
                 audit::AuditLog* audit, QObject* parent)
    : QObject(parent), cfg_(cfg), realBus_(realBus), audit_(audit),
      pinGuard_(auditClock_) {
  // ---- 治疗头 ----
  for (const SlotConfig& s : cfg_->slotList) {
    auto* h = new HeadItem(s, this);
    connect(h, &HeadItem::stateChanged, this, [this] {
      syncPolling();
      emit sysStateChanged();
    });
    connect(h, &HeadItem::headEvent, this,
            [this, h](int ev) { onHeadEvent(h, ev); });
    heads_.append(h);
  }

  // ---- 总线调度器 ----
  sched_ = std::make_unique<BusScheduler>(BusConfig{}, transport, clock_);
  sched_->onPresenceChange = [this](std::uint8_t addr, bool online) {
    if (HeadItem* h = findByAddr(addr)) h->deliverPresence(online);
    // SRS-022（ISS-011 修复）：头上线即写 0x54 限流/缓启动/换向延时——
    // 真实板卡出厂缓启动 0x01（起步太硬），D3「暂停后柔和恢复」依赖 kSoftStart=0x10。
    // 板载带记忆，重复写幂等无害；烧录后的新头经本路径自动完成参数初始化。
    if (online)
      sched_->enqueueControl(encodeConfigParams(addr, defaults::kCurrentLimit,
                                                defaults::kSoftStart,
                                                defaults::kDirDelay));
    // M4a（D24）：在位变化留痕（含 idle 插拔——「不该在位的头出现」必须有记录）
    if (audit_) {
      HeadItem* hh = findByAddr(addr);
      auditLog(audit::cat::Presence,
               online ? audit::ev::HeadPlugged : audit::ev::HeadUnplugged,
               hh ? hh->slotId() : QString(), addr,
               std::string("{") + audit::jbool("online", online) + "}");
    }
    if (online && !selected_) {
      // 首个上线的头默认选中（原型默认 L1；构造时头尚未上线，选择会落空）
      for (HeadItem* x : heads_) {
        if (x->online() && x->coreHead().state() != HeadState::Absent) {
          selectHead(x->slotId());
          break;
        }
      }
    }
    emit sysStateChanged();
  };
  sched_->onReplyRouted = [this](std::uint8_t addr, Reply header,
                                 const ReplyPayload& p) {
    HeadItem* h = findByAddr(addr);
    if (!h) return;
    if (header == Reply::Info) {
      const auto& info = std::get<InfoReply>(p);
      h->deliverInfo(info.faultBits, info.tempC, info.motorRunning);
    } else if (header == Reply::Speed) {
      h->deliverSpeed(std::get<SpeedReply>(p).rpm);
    }
    // Ack 由各控制事务的回调直达对应 head，不经此路由（避免双投递）
  };

  std::vector<std::uint8_t> addrs;
  for (const HeadItem* h : heads_) addrs.push_back(static_cast<std::uint8_t>(h->addr()));
  sched_->setDevices(addrs);

  // 决策 D5：真实总线上电清理——对全部绑定地址发滑行停止，
  // 防主机崩溃重启后电机按最后指令残转
  if (realBus_) {
    for (std::uint8_t a : addrs)
      sched_->enqueueControl(encodeCoastStop(a, MotorStatus::DirA));
    // M4a（D19）：上电清理留痕——风险分析「主机死机→电机失控」路径的审计佐证
    if (audit_) {
      std::string arr = "[";
      for (std::size_t i = 0; i < addrs.size(); ++i) {
        if (i) arr += ",";
        arr += std::to_string(addrs[i]);
      }
      arr += "]";
      auditLog(audit::cat::Session, audit::ev::StopAllOnBoot, QString(), 0,
               std::string("{") + audit::jraw("addrs", arr) + "," +
                   audit::jstr("note", "上电滑行停止清理（D5）") + "}");
    }
  }

  // ---- M4a 审计接线（D13：降级醒目告警）----
  if (audit_) {
    audit_->onDegradeChanged = [this](bool deg, const std::string& reason) {
      emit auditStateChanged();
      emit toast(deg ? QStringLiteral("⚠ 审计日志不可用，已降级文件记录：") +
                           QString::fromStdString(reason)
                     : QStringLiteral("审计日志已恢复，兜底记录已回填数据库"),
                 deg ? QStringLiteral("err") : QStringLiteral("ok"), deg ? 6000 : 2500);
    };
  }
  currentOpId_ = cfg_->currentOperatorId;

  // ---- 定时器 ----
  busTimer_.setInterval(10);
  connect(&busTimer_, &QTimer::timeout, this, [this] { sched_->tick(); });
  logicTimer_.setInterval(250);
  connect(&logicTimer_, &QTimer::timeout, this, &AppCore::onLogicTick);
  uiTimer_.setInterval(1000);
  connect(&uiTimer_, &QTimer::timeout, this, &AppCore::onUiTick);
  savePendTimer_.setSingleShot(true);
  savePendTimer_.setInterval(30000);  // 原型：30 秒无操作退出「存为预设」
  connect(&savePendTimer_, &QTimer::timeout, this, [this] {
    savePend_ = -1;
    emit presetsChanged();
  });
  snapTimer_.setInterval(30000);  // M4a（D15）：30s 会话快照
  connect(&snapTimer_, &QTimer::timeout, this, &AppCore::snapshotNow);
  paramDebounceTimer_.setSingleShot(true);
  paramDebounceTimer_.setInterval(2000);  // M4a（D15）：改参 2s 防抖
  connect(&paramDebounceTimer_, &QTimer::timeout, this, &AppCore::flushParamChange);

  busTimer_.start();
  logicTimer_.start();
  uiTimer_.start();
  snapTimer_.start();
  onUiTick();

  // 原型默认选中 L1
  if (!heads_.isEmpty()) selectHead(heads_.first()->slotId());
}

void AppCore::deliverReply(const Frame& f) { sched_->onReply(f); }

QVariantList AppCore::heads() const {
  QVariantList l;
  for (HeadItem* h : heads_) l.append(QVariant::fromValue(h));
  return l;
}

HeadItem* AppCore::findBySlot(const QString& slotId) const {
  for (HeadItem* h : heads_)
    if (h->slotId() == slotId) return h;
  return nullptr;
}

HeadItem* AppCore::findByAddr(quint8 addr) const {
  for (HeadItem* h : heads_)
    if (h->addr() == static_cast<int>(addr)) return h;
  return nullptr;
}

void AppCore::sendHeadFrame(HeadItem* h, const Frame& f) {
  QPointer<HeadItem> guard(h);
  sched_->enqueueControl(f, [guard](bool ok, Reply, const ReplyPayload& p) {
    if (!guard) return;
    if (ok) guard->deliverAck(std::get<AckReply>(p).flag);
    // ok=false（重试耗尽/离线）：不投递，head 的 ack 超时逻辑兜底，
    // 绝不因丢应答回退状态（防止「UI 已停、电机在转」）
  });
}

void AppCore::syncPolling() {
  for (HeadItem* h : heads_) {
    const auto s = h->coreHead().state();
    const bool needSpeed = s == HeadState::Starting ||
                           s == HeadState::Running ||
                           s == HeadState::Stopping;
    sched_->setSpeedPollEnabled(static_cast<std::uint8_t>(h->addr()), needSpeed);
  }
}

void AppCore::onLogicTick() {
  for (HeadItem* h : heads_) {
    if (auto f = h->coreHead().tick(0.25)) sendHeadFrame(h, *f);
  }
}

void AppCore::onUiTick() {
  clockText_ =
      QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
  emit clockChanged();
  for (HeadItem* h : heads_) h->tickUi();
  emit sysStateChanged();
}

void AppCore::onHeadEvent(HeadItem* h, int ev) {
  // M4a（D15/D24）：语义事件全部入审计；toast 维持原型行为
  const auto& head = h->coreHead();
  switch (static_cast<HeadEvent>(ev)) {
    case HeadEvent::Started: {
      const auto c = head.config();
      pendingSlot_.clear();          // 新会话开始：丢弃上一轮改参防抖
      paramDebounceTimer_.stop();
      auditLog(audit::cat::Therapy, audit::ev::TherapyStart, h->slotId(), h->addr(),
               std::string("{") + audit::jnum("freq", c.freqHz) + "," +
                   audit::jnum("mode", static_cast<int>(c.mode)) + "," +
                   audit::jnum("timeMin", c.timeMin) + "," +
                   audit::jnum("dir", static_cast<int>(c.dir)) + "," +
                   audit::jstr("type", h->headType().toStdString()) + "}",
               currentOpId_);
      emit toast(QStringLiteral("%1 治疗已启动 · %2Hz %3 %4min")
                     .arg(h->slotId())
                     .arg(c.freqHz)
                     .arg(modeNameOf(c.mode))
                     .arg(c.timeMin),
                 QStringLiteral("ok"), 2600);
      break;
    }
    case HeadEvent::Paused:
      auditLog(audit::cat::Therapy, audit::ev::Pause, h->slotId(), h->addr(),
               std::string("{") + audit::jnum("remainingS", h->remaining()) + "}");
      emit toast(h->slotId() + QStringLiteral(" 已暂停 · 计时暂停"),
                 QStringLiteral("warn"), 1400);
      break;
    case HeadEvent::Resumed:
      auditLog(audit::cat::Therapy, audit::ev::Resume, h->slotId(), h->addr(),
               std::string("{") + audit::jnum("remainingS", h->remaining()) + "}");
      emit toast(h->slotId() + QStringLiteral(" 已继续"),
                 QStringLiteral("ok"), 1400);
      break;
    case HeadEvent::StopRequested:
      auditLog(audit::cat::Therapy, audit::ev::Stop, h->slotId(), h->addr(),
               std::string("{") + audit::jstr("reason", "user") + "," +
                   audit::jnum("totalS", head.totalSeconds()) + "}");
      emit toast(h->slotId() + QStringLiteral(" 已完全停止 · 计时已归零"),
                 QStringLiteral("warn"), 2000);
      break;
    case HeadEvent::TimerExpired:
      break;  // Completed 紧随其后，避免双 toast
    case HeadEvent::Completed:
      auditLog(audit::cat::Therapy, audit::ev::Complete, h->slotId(), h->addr(),
               std::string("{") + audit::jnum("runS", head.totalSeconds()) + "," +
                   audit::jnum("freq", head.config().freqHz) + "," +
                   audit::jnum("mode", static_cast<int>(head.config().mode)) + "}");
      emit toast(h->slotId() + QStringLiteral(" 治疗完成 ✔"),
                 QStringLiteral("ok"), 2600);
      break;
    case HeadEvent::FaultEntered:
      auditLog(audit::cat::Fault, audit::ev::FaultDetected, h->slotId(), h->addr(),
               std::string("{") + audit::jnum("faultBits", head.faultBits()) + "," +
                   audit::jstr("faultText", h->faultText().toStdString()) + "," +
                   audit::jnum("tempC", head.tempC()) + "}");
      emit toast(h->slotId() + QStringLiteral(" 故障：") + h->faultText(),
                 QStringLiteral("err"), 4000);
      break;
    case HeadEvent::FaultCleared:
      auditLog(audit::cat::Fault, audit::ev::FaultReset, h->slotId(), h->addr(),
               std::string("{") + audit::jstr("result", "ok") + "}");
      emit toast(h->slotId() + QStringLiteral(" 故障已复位"),
                 QStringLiteral("ok"), 2000);
      break;
    case HeadEvent::WentOfflineDuringRun:
      auditLog(audit::cat::Fault, audit::ev::HeadOffline, h->slotId(), h->addr(),
               std::string("{") +
                   audit::jstr("note", "运行中总线失联，电机可能仍在转（D5）") + "}");
      emit toast(h->slotId() +
                     QStringLiteral(" 连接丢失！电机可能仍在运转，必要时使用硬件急停"),
                 QStringLiteral("err"), 6000);
      break;
    case HeadEvent::AckRejected:
      auditLog(audit::cat::Fault, audit::ev::AckRejected, h->slotId(), h->addr(),
               std::string("{") + audit::jstr("note", "板载拒绝指令（0xEE/0xBB）") + "}");
      emit toast(h->slotId() + QStringLiteral(" 指令被设备拒绝"),
                 QStringLiteral("warn"), 2000);
      break;
  }
}

// ---- 系统状态汇总（原型 header 语义） ----

bool AppCore::anyRunning() const {
  for (const HeadItem* h : heads_)
    if (h->coreHead().state() == HeadState::Running ||
        h->coreHead().state() == HeadState::Starting)
      return true;
  return false;
}

bool AppCore::allPaused() const {
  bool any = false;
  for (const HeadItem* h : heads_) {
    const auto s = h->coreHead().state();
    if (s == HeadState::Paused) any = true;
    else if (s == HeadState::Running || s == HeadState::Starting) return false;
  }
  return any;
}

bool AppCore::anyOutput() const {
  for (const HeadItem* h : heads_) {
    const auto s = h->coreHead().state();
    if (s != HeadState::Idle && s != HeadState::Absent && s != HeadState::Fault)
      return true;
  }
  return false;
}

QString AppCore::sysStateClass() const {
  if (anyRunning()) return QStringLiteral("running");
  if (allPaused()) return QStringLiteral("paused");
  return QStringLiteral("idle");
}

QString AppCore::sysStateText() const {
  if (anyRunning()) return QStringLiteral("运行中");
  if (allPaused()) return QStringLiteral("已暂停");
  // 待机：按类型汇总在位治疗头（原型：「待机 已连接2局部·1手法治疗头」）
  QMap<QString, int> cnt;
  for (const HeadItem* h : heads_)
    if (h->online() && h->coreHead().state() != HeadState::Absent)
      cnt[h->headType()]++;
  if (cnt.isEmpty()) return QStringLiteral("待机 未检测到治疗头");
  QStringList parts;
  for (auto it = cnt.cbegin(); it != cnt.cend(); ++it) {
    QString t = it.key();
    t.remove(QStringLiteral("治疗头"));
    parts << QString::number(it.value()) + t;
  }
  return QStringLiteral("待机 已连接") + parts.join(QStringLiteral("·")) +
         QStringLiteral("治疗头");
}

QString AppCore::hospInfo() const {
  if (cfg_->hospital.isEmpty() && cfg_->department.isEmpty())
    return QStringLiteral("未设置医院 / 科室");
  if (cfg_->department.isEmpty()) return cfg_->hospital;
  if (cfg_->hospital.isEmpty()) return cfg_->department;
  return cfg_->hospital + QStringLiteral(" · ") + cfg_->department;
}

QString AppCore::targetName() const {
  return selected_ ? selected_->slotId() : QStringLiteral("—");
}

QString AppCore::modeDesc() const {
  if (!selected_) return QString();
  switch (selected_->coreHead().config().mode) {
    case Mode::Constant: return QStringLiteral("输出频率恒定不变");
    case Mode::Sweep:    return QStringLiteral("0→工作频率 周期往复");
    case Mode::Step:     return QStringLiteral("爬升·保持·下降 循环");
  }
  return QString();
}

QString AppCore::versionText() const {
  return QStringLiteral("v" APP_VERSION "-m4a · ") +
         (realBus_ ? QStringLiteral("串口 ") + cfg_->serialPort
                   : QStringLiteral("模拟总线"));
}

// ---- 选择与参数调节 ----

void AppCore::selectHead(const QString& slotId) {
  HeadItem* h = findBySlot(slotId);
  if (!h) return;
  if (!h->online() && h->coreHead().state() == HeadState::Absent) {
    emit toast(h->slotId() + QStringLiteral(" 未连接"), QStringLiteral("warn"), 1600);
    return;
  }
  HeadItem* newSel = (selected_ == h) ? nullptr : h;
  // 不变式：selected 标记与 selected_ 指针严格一致（先清其余再置新）
  for (HeadItem* x : heads_)
    if (x != newSel) x->setSelected(false);
  selected_ = newSel;
  if (selected_) selected_->setSelected(true);
  qDebug("selectHead %s -> selected=%s", qPrintable(slotId),
         selected_ ? qPrintable(selected_->slotId()) : "(none)");
  emit selectionChanged();
}

void AppCore::adjustFreq(int deltaSteps) {
  if (!selected_) return;
  const HeadConfig before = selected_->coreHead().config();
  HeadConfig c = before;
  const int v = qBound(kMinFreqHz, c.freqHz + deltaSteps * 5, kMaxFreqHz);
  if (v == c.freqHz) return;
  c.freqHz = v;
  if (!selected_->applyCore(c)) return;
  noteParamChange(selected_, before);  // M4a（D15）：运行中改参 2s 防抖入审计
  const auto s = selected_->coreHead().state();
  if (s == HeadState::Running || s == HeadState::Starting || s == HeadState::Paused)
    emit toast(selected_->slotId() + QStringLiteral(" 频率实时调整为 ") +
                   QString::number(v) + QStringLiteral(" Hz"),
               QStringLiteral("ok"), 1400);
}

void AppCore::adjustTime(int deltaSteps) {
  if (!selected_) return;
  const HeadConfig before = selected_->coreHead().config();
  HeadConfig c = before;
  const int v = qBound(5, c.timeMin + deltaSteps * 5, 60);
  if (v == c.timeMin) return;
  c.timeMin = v;
  if (!selected_->applyCore(c)) return;  // 核心层实现差值语义
  noteParamChange(selected_, before);
}

void AppCore::setMode(int mode) {
  if (!selected_ || mode < 0 || mode > 2) return;
  const HeadConfig before = selected_->coreHead().config();
  HeadConfig c = before;
  const auto m = static_cast<Mode>(mode);
  if (c.mode == m) return;
  c.mode = m;
  if (!selected_->applyCore(c)) return;
  noteParamChange(selected_, before);
  emit selectionChanged();  // modeDesc 更新
  const auto s = selected_->coreHead().state();
  if (s == HeadState::Running || s == HeadState::Starting || s == HeadState::Paused)
    emit toast(selected_->slotId() + QStringLiteral(" 模式切换为「") +
                   modeNameOf(m) + QStringLiteral("」"),
               QStringLiteral("ok"), 1400);
}

// ---- 预设（原型：点一次应用，30s 内再点一次存为预设） ----

QString AppCore::presetText(int index) const {
  if (index < 0 || index >= cfg_->presets.size()) return QString();
  const PresetConfig& p = cfg_->presets[index];
  return QStringLiteral("%1Hz %2 %3min")
      .arg(p.freqHz)
      .arg(modeNameOf(static_cast<Mode>(p.mode)))
      .arg(p.timeMin);
}

QStringList AppCore::presetLabels() const {
  QStringList l;
  for (int i = 0; i < cfg_->presets.size(); ++i) l << presetText(i);
  return l;
}

void AppCore::applyPreset(int index) {
  if (index < 0 || index >= cfg_->presets.size()) return;
  if (!selected_) {
    emit toast(QStringLiteral("请先点选治疗头"), QStringLiteral("warn"), 1600);
    return;
  }
  if (savePend_ == index) {  // 第二次点击：存入预设
    const HeadConfig c = selected_->coreHead().config();
    const QString oldText = presetText(index);
    cfg_->presets[index] = PresetConfig{c.freqHz, static_cast<int>(c.mode),
                                        c.timeMin};
    cfg_->save();
    auditLog(audit::cat::Config, audit::ev::SettingChanged, QString(), 0,
             std::string("{") +
                 audit::jstr("key", "preset_" + std::to_string(index + 1)) + "," +
                 audit::jstr("old", oldText.toStdString()) + "," +
                 audit::jstr("new", presetText(index).toStdString()) + "}");
    savePend_ = -1;
    emit presetsChanged();
    emit toast(QStringLiteral("已存为 预设%1：%2")
                   .arg(index + 1)
                   .arg(presetText(index)),
               QStringLiteral("ok"), 2400);
    return;
  }
  // 第一次点击：应用预设
  const PresetConfig& p = cfg_->presets[index];
  const HeadConfig before = selected_->coreHead().config();
  HeadConfig c = before;
  c.freqHz = p.freqHz;
  c.mode = static_cast<Mode>(p.mode);
  c.timeMin = p.timeMin;
  if (!selected_->applyCore(c)) {
    emit toast(QStringLiteral("预设参数无效"), QStringLiteral("err"), 2000);
    return;
  }
  noteParamChange(selected_, before);
  savePend_ = index;
  savePendTimer_.start();
  emit presetsChanged();
  emit selectionChanged();
  emit toast(selected_->slotId() + QStringLiteral(" 已应用 预设") +
                 QString::number(index + 1) + QStringLiteral(" · 再点一次存为预设"),
             QStringLiteral("ok"), 2400);
}

// ---- 启停（确认弹窗流程） ----

void AppCore::runConfirmed(const QVariantMap& info, std::function<void()> action) {
  pendingConfirm_ = std::move(action);
  emit confirmRequest(info);
}

void AppCore::confirmResponse(bool accepted) {
  auto action = pendingConfirm_;
  pendingConfirm_ = nullptr;
  if (accepted && action) action();
}

void AppCore::requestStart(const QString& slotId) {
  HeadItem* h = findBySlot(slotId);
  if (!h || h->coreHead().state() != HeadState::Idle || !h->online()) return;
  const HeadConfig c = h->coreHead().config();
  QVariantMap info;
  info[QStringLiteral("icon")] = QStringLiteral("🩺");
  info[QStringLiteral("title")] =
      QStringLiteral("启动前请再次检查确认治疗头已就位！");
  const QString opName = currentOperatorName();
  info[QStringLiteral("msg")] =
      QStringLiteral("即将启动：%1\n参数：%2 Hz · %3 · %4 min\n计时将从 %4:00 "
                     "全程重新开始\n操作员：%5")
          .arg(h->slotId())
          .arg(c.freqHz)
          .arg(modeNameOf(c.mode))
          .arg(c.timeMin)
          .arg(opName.isEmpty() ? QStringLiteral("未指定（设置面板可选定）") : opName);
  info[QStringLiteral("okText")] = QStringLiteral("✓ 继续启动");
  info[QStringLiteral("cancelText")] = QStringLiteral("返回检查");
  info[QStringLiteral("danger")] = false;
  runConfirmed(info, [this, slotId] { startHeads({slotId}); });
}

void AppCore::startHeads(const QStringList& slotIds) {
  for (const QString& id : slotIds) {
    HeadItem* h = findBySlot(id);
    if (!h) continue;
    if (auto f = h->coreHead().start()) sendHeadFrame(h, *f);
    // start() 无帧（扫频起点滑行段）时：Started 事件已发，首个目标帧由 tick 下发
  }
}

void AppCore::requestStop(const QString& slotId) {
  HeadItem* h = findBySlot(slotId);
  if (!h) return;
  const auto s = h->coreHead().state();
  if (s == HeadState::Idle || s == HeadState::Absent || s == HeadState::Fault)
    return;
  QVariantMap info;
  info[QStringLiteral("icon")] = QStringLiteral("⏹");
  info[QStringLiteral("title")] = QStringLiteral("停止 %1？").arg(h->slotId());
  info[QStringLiteral("msg")] =
      QStringLiteral("完全停止治疗输出，计时归零。\n再次启动将从 %1 min 重新开始计时。")
          .arg(h->coreHead().config().timeMin);
  info[QStringLiteral("okText")] = QStringLiteral("确认停止");
  info[QStringLiteral("cancelText")] = QStringLiteral("取消");
  info[QStringLiteral("danger")] = true;
  runConfirmed(info, [this, slotId] { stopHeads({slotId}); });
}

void AppCore::requestStopAll() {
  QStringList ids;
  for (HeadItem* h : heads_) {
    const auto s = h->coreHead().state();
    if (s == HeadState::Running || s == HeadState::Starting ||
        s == HeadState::Paused || s == HeadState::Stopping)
      ids << h->slotId();
  }
  if (ids.isEmpty()) return;
  QVariantMap info;
  info[QStringLiteral("icon")] = QStringLiteral("⛔");
  info[QStringLiteral("title")] = QStringLiteral("全部停止？");
  info[QStringLiteral("msg")] =
      QStringLiteral("将完全停止 %1 的治疗输出，计时归零，再次启动将重新计时。")
          .arg(ids.join(QStringLiteral(" ")));
  info[QStringLiteral("okText")] = QStringLiteral("确认停止");
  info[QStringLiteral("cancelText")] = QStringLiteral("取消");
  info[QStringLiteral("danger")] = true;
  runConfirmed(info, [this, ids] { stopHeads(ids); });
}

void AppCore::stopHeads(const QStringList& slotIds) {
  for (const QString& id : slotIds) {
    HeadItem* h = findBySlot(id);
    if (!h) continue;
    if (auto f = h->coreHead().stop()) sendHeadFrame(h, *f);
  }
}

void AppCore::requestPauseAll() {
  if (anyRunning()) {
    for (HeadItem* h : heads_)
      if (auto f = h->coreHead().pause()) sendHeadFrame(h, *f);
  } else if (allPaused()) {
    for (HeadItem* h : heads_)
      if (auto f = h->coreHead().resume()) sendHeadFrame(h, *f);
  }
}

void AppCore::showAbout() {
  emit toast(versionText(), QStringLiteral("ok"), 3000);
}

// ================= M3.5：PIN 门禁（决策 D9） =================

namespace {
QString hashPin(const QString& salt, const QString& pin) {
  return QString::fromLatin1(
      QCryptographicHash::hash((salt + pin).toUtf8(), QCryptographicHash::Sha256)
          .toHex());
}
}  // namespace

bool AppCore::pinSet() const { return !cfg_->pinHash.isEmpty(); }

bool AppCore::verifyPin(const QString& pin, const QString& source) {
  // M4a（D20）：锁定期间拒绝且不累计；成功清零计数；第 5 次失败触发锁定
  // 注意用 std::int64_t 而非 qint64：Linux LP64 下两者是不同类型（long vs long long）
  std::int64_t remainMs = 0;
  if (pinGuard_.locked(&remainMs)) {
    auditLog(audit::cat::Access, audit::ev::PinFail, QString(), 0,
             std::string("{") + audit::jstr("source", source.toStdString()) + "," +
                 audit::jbool("locked", true) + "," + audit::jnum("remainMs", remainMs) +
                 "}");
    emit toast(QStringLiteral("PIN 连续失败次数过多，已锁定（剩余 %1 秒）")
                   .arg(remainMs / 1000 + 1),
               QStringLiteral("err"), 3000);
    return false;
  }
  const bool ok = pinSet() && hashPin(cfg_->pinSalt, pin) == cfg_->pinHash;
  if (ok) {
    pinGuard_.onSuccess();
    auditLog(audit::cat::Access, audit::ev::PinSuccess, QString(), 0,
             std::string("{") + audit::jstr("source", source.toStdString()) + "}");
    return true;
  }
  const bool justLocked = pinGuard_.onFailure();
  auditLog(audit::cat::Access, audit::ev::PinFail, QString(), 0,
           std::string("{") + audit::jstr("source", source.toStdString()) + "," +
               audit::jnum("failCount", pinGuard_.failCount()) + "," +
               audit::jbool("justLocked", justLocked) + "}");
  if (justLocked) {
    auditLog(audit::cat::Access, audit::ev::PinLocked, QString(), 0,
             std::string("{") + audit::jnum("lockSeconds", 300) + "," +
                 audit::jstr("note", "连续 5 次失败（D20 防暴破）") + "}");
    emit pinLockChanged();
    emit toast(QStringLiteral("PIN 连续失败 5 次，已锁定 5 分钟（已记入审计）"),
               QStringLiteral("err"), 4000);
  }
  return false;
}

bool AppCore::setInitialPin(const QString& pin) {
  if (pinSet() || pin.length() < 4 || pin.length() > 6) return false;
  cfg_->pinSalt =
      QString::number(QRandomGenerator::system()->generate64(), 16);
  cfg_->pinHash = hashPin(cfg_->pinSalt, pin);
  cfg_->save();
  auditLog(audit::cat::Access, audit::ev::PinInitialSet, QString(), 0,
           std::string("{") +
               audit::jstr("fp8", cfg_->pinHash.left(8).toStdString()) + "," +
               audit::jnum("length", pin.length()) + "}");  // 指纹前缀，不记明文
  emit pinChanged();
  emit toast(QStringLiteral("PIN 已设置（设置/维护入口已启用门禁）"),
             QStringLiteral("ok"), 2500);
  return true;
}

bool AppCore::changePin(const QString& oldPin, const QString& newPin) {
  if (!verifyPin(oldPin, QStringLiteral("changePin"))) return false;
  if (newPin.length() < 4 || newPin.length() > 6) return false;
  cfg_->pinSalt =
      QString::number(QRandomGenerator::system()->generate64(), 16);
  cfg_->pinHash = hashPin(cfg_->pinSalt, newPin);
  cfg_->save();
  auditLog(audit::cat::Access, audit::ev::PinChanged, QString(), 0,
           std::string("{") +
               audit::jstr("fp8", cfg_->pinHash.left(8).toStdString()) + "," +
               audit::jnum("length", newPin.length()) + "}");
  emit pinChanged();
  emit toast(QStringLiteral("PIN 已修改"), QStringLiteral("ok"), 2000);
  return true;
}

// ================= M3.5：设置持久化 =================

void AppCore::saveIdentity(const QString& hospital, const QString& department) {
  const QString oldHosp = cfg_->hospital, oldDept = cfg_->department;
  cfg_->hospital = hospital.trimmed();
  cfg_->department = department.trimmed();
  cfg_->save();
  if (cfg_->hospital != oldHosp)
    auditLog(audit::cat::Config, audit::ev::SettingChanged, QString(), 0,
             std::string("{") + audit::jstr("key", "hospital") + "," +
                 audit::jstr("old", oldHosp.toStdString()) + "," +
                 audit::jstr("new", cfg_->hospital.toStdString()) + "}");
  if (cfg_->department != oldDept)
    auditLog(audit::cat::Config, audit::ev::SettingChanged, QString(), 0,
             std::string("{") + audit::jstr("key", "department") + "," +
                 audit::jstr("old", oldDept.toStdString()) + "," +
                 audit::jstr("new", cfg_->department.toStdString()) + "}");
  emit hospChanged();
  emit toast(QStringLiteral("医院 / 科室信息已保存"), QStringLiteral("ok"), 2000);
}

void AppCore::saveSerial(const QString& port, int baud) {
  const QString oldPort = cfg_->serialPort;
  const int oldBaud = cfg_->baudRate;
  cfg_->serialPort = port.trimmed();
  if (baud > 0) cfg_->baudRate = baud;
  cfg_->save();
  if (cfg_->serialPort != oldPort || cfg_->baudRate != oldBaud)
    auditLog(audit::cat::Config, audit::ev::SettingChanged, QString(), 0,
             std::string("{") + audit::jstr("key", "serial") + ",\"old\":{" +
                 audit::jstr("port", oldPort.toStdString()) + "," +
                 audit::jnum("baud", oldBaud) + "},\"new\":{" +
                 audit::jstr("port", cfg_->serialPort.toStdString()) + "," +
                 audit::jnum("baud", cfg_->baudRate) + "}}");
  emit toast(QStringLiteral("串口配置已保存，重启应用后生效"),
             QStringLiteral("warn"), 3000);
}

QString AppCore::serialConfigText() const {
  return cfg_->serialPort.isEmpty()
             ? QStringLiteral("（模拟总线）")
             : cfg_->serialPort + QStringLiteral(" @ ") +
                   QString::number(cfg_->baudRate);
}

void AppCore::setSystemTime(const QString& isoDateTime) {
  const QDateTime t = QDateTime::fromString(isoDateTime, Qt::ISODate);
  if (!t.isValid()) {
    emit toast(QStringLiteral("时间格式无效（应为 YYYY-MM-DDTHH:MM:SS）"),
               QStringLiteral("err"), 2500);
    return;
  }
#ifdef Q_OS_LINUX
  // 目标板：timedatectl（polkit 授权运行用户）；审计时间戳另用单调时钟防污染
  const qint64 oldWall = QDateTime::currentMSecsSinceEpoch();
  QProcess p;
  p.start(QStringLiteral("timedatectl"),
          {QStringLiteral("set-time"),
           t.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))});
  p.waitForFinished(3000);
  if (p.exitCode() == 0) {
    // M4a（D17）：改钟入审计并重锚定（内部记 CLOCK_CHANGE，防 CLOCK_JUMP 双记）
    if (audit_)
      audit_->noteClockChange(oldWall, QDateTime::currentMSecsSinceEpoch(), "manual");
    emit toast(QStringLiteral("系统时间已设置"), QStringLiteral("ok"), 2000);
  } else {
    emit toast(QStringLiteral("设置系统时间失败（需要授权）：") +
                   QString::fromUtf8(p.readAllStandardError()),
               QStringLiteral("err"), 4000);
  }
#else
  Q_UNUSED(t)
  emit toast(QStringLiteral("开发平台不修改系统时间；目标板（Linux）可用"),
             QStringLiteral("warn"), 2500);
#endif
}

// ================= M3.5：绑定 / 维护模式（决策 D2） =================

QVariantList AppCore::bindingInfos() const {
  QVariantList l;
  for (const HeadItem* h : heads_) {
    QVariantMap m;
    m[QStringLiteral("slot")] = h->slotId();
    m[QStringLiteral("addr")] = h->addr();
    m[QStringLiteral("type")] = h->headType();
    m[QStringLiteral("online")] =
        sched_->deviceOnline(static_cast<std::uint8_t>(h->addr()));
    l.append(m);
  }
  return l;
}

void AppCore::maintSetType(int slotIndex, const QString& type) {
  if (slotIndex < 0 || slotIndex >= cfg_->slotList.size()) return;
  const QString oldType = cfg_->slotList[slotIndex].type;
  cfg_->slotList[slotIndex].type = type;
  cfg_->save();
  if (oldType != type)
    auditLog(audit::cat::Config, audit::ev::BindingChanged,
             cfg_->slotList[slotIndex].slotId, cfg_->slotList[slotIndex].addr,
             std::string("{") + audit::jstr("field", "type") + "," +
                 audit::jstr("old", oldType.toStdString()) + "," +
                 audit::jstr("new", type.toStdString()) + "}");
  if (HeadItem* h = heads_.value(slotIndex, nullptr)) h->setHeadType(type);
  emit bindingsChanged();
  emit sysStateChanged();  // header 类型汇总刷新
  emit toast(cfg_->slotList[slotIndex].slotId + QStringLiteral(" 类型已设为 ") + type,
             QStringLiteral("ok"), 2000);
}

bool AppCore::maintSetAddr(int slotIndex, int newAddr) {
  if (slotIndex < 0 || slotIndex >= cfg_->slotList.size()) return false;
  if (newAddr < 1 || newAddr > 255) return false;
  for (int i = 0; i < cfg_->slotList.size(); ++i)
    if (i != slotIndex && cfg_->slotList[i].addr == newAddr) return false;
  const int oldAddr = cfg_->slotList[slotIndex].addr;
  cfg_->slotList[slotIndex].addr = static_cast<quint8>(newAddr);
  cfg_->save();
  auditLog(audit::cat::Config, audit::ev::BindingChanged,
           cfg_->slotList[slotIndex].slotId, newAddr,
           std::string("{") + audit::jstr("field", "addr") + "," +
               audit::jnum("old", oldAddr) + "," + audit::jnum("new", newAddr) + "," +
               audit::jstr("note", "配置已改，重启生效；设备侧需烧录同步") + "}");
  emit bindingsChanged();
  emit toast(QStringLiteral("槽位地址配置已改，重启应用后生效（设备侧地址需用烧录流程同步）"),
             QStringLiteral("warn"), 3500);
  return true;
}

void AppCore::maintProbeZero() {
  // 地址 0 = 新头出厂默认；正常运行的总线上不应存在地址 0 响应
  sched_->enqueueControl(
      encodeRead(Cmd::ReadInfo, 0).value(),
      [this](bool ok, Reply, const ReplyPayload&) {
        auditLog(audit::cat::Config, audit::ev::ProbeZero, QString(), 0,
                 std::string("{") + audit::jbool("found", ok) + "}");
        emit maintZeroFound(ok);
      });
}

void AppCore::maintBurn(int slotIndex) {
  if (slotIndex < 0 || slotIndex >= cfg_->slotList.size()) return;
  const SlotConfig target = cfg_->slotList[slotIndex];

  // 安全联锁①：其余绑定头必须全部离线。
  // 0xAA 是广播性生效的（谁收到谁改地址）——若有其他头在线会被一起改写！
  for (const HeadItem* h : heads_) {
    if (h->slotId() == target.slotId) continue;
    if (sched_->deviceOnline(static_cast<std::uint8_t>(h->addr()))) {
      auditLog(audit::cat::Config, audit::ev::BurnResult, target.slotId, target.addr,
               std::string("{") + audit::jbool("ok", false) + "," +
                   audit::jstr("stage", "interlock") + "," +
                   audit::jstr("msg", "联锁拒绝：其他治疗头在线") + "}");
      emit maintBurnResult(
          false, QStringLiteral("联锁拒绝：其他治疗头仍在线。0xAA 会把在线头一起改写，"
                                "请仅保留新头（拔除其余治疗头）后重试。"));
      return;
    }
  }
  // 安全联锁②：必须先探到地址 0 的新头
  sched_->enqueueControl(
      encodeRead(Cmd::ReadInfo, 0).value(),
      [this, target](bool zeroOk, Reply, const ReplyPayload&) {
        if (!zeroOk) {
          auditLog(audit::cat::Config, audit::ev::BurnResult, target.slotId, target.addr,
                   std::string("{") + audit::jbool("ok", false) + "," +
                       audit::jstr("stage", "probe0") + "," +
                       audit::jstr("msg", "未检测到地址 0 设备") + "}");
          emit maintZeroFound(false);
          emit maintBurnResult(
              false, QStringLiteral("未检测到地址 0 的设备：请确认新头已接入且为出厂状态。"));
          return;
        }
        emit maintZeroFound(true);
        auditLog(audit::cat::Config, audit::ev::BurnStarted, target.slotId, target.addr,
                 std::string("{") + audit::jstr("interlock", "passed") + "," +
                     audit::jnum("closedLoop", defaults::kClosedLoop ? 1 : 0) + "," +
                     audit::jnum("stallTime", defaults::kStallTime) + "," +
                     audit::jnum("polePairs", defaults::kPolePairs) + "}");
        const Frame burn = encodeConfigAddress(
            target.addr, defaults::kClosedLoop, defaults::kStallTime,
            defaults::kPolePairs);
        sched_->enqueueControl(
            burn, [this, target](bool ok, Reply, const ReplyPayload& p) {
              const bool ackOk =
                  ok && std::get<AckReply>(p).flag == AckFlag::Ok;
              auditLog(audit::cat::Config, audit::ev::BurnResult, target.slotId,
                       target.addr,
                       std::string("{") + audit::jbool("ok", ackOk) + "," +
                           audit::jstr("stage", "burn") + "," +
                           audit::jnum("newAddr", target.addr) + "}");
              if (ackOk) {
                emit toast(target.slotId +
                               QStringLiteral(" 烧录地址 %1 成功，已可绑定使用")
                                   .arg(target.addr),
                           QStringLiteral("ok"), 3000);
                emit bindingsChanged();
              }
              emit maintBurnResult(
                  ackOk,
                  ackOk ? QString()
                        : QStringLiteral("烧录失败（设备拒绝或无应答）"));
            });
      });
}

void AppCore::headAction(const QString& slotId, const QString& action) {
  HeadItem* h = findBySlot(slotId);
  if (!h) return;
  std::optional<Frame> f;
  if (action == QLatin1String("pause"))
    f = h->coreHead().pause();
  else if (action == QLatin1String("resume"))
    f = h->coreHead().resume();
  else if (action == QLatin1String("reset"))
    f = h->coreHead().resetFault();
  else
    return;
  if (f) sendHeadFrame(h, *f);
}

// ================= M4a：审计追踪（决策 D13–D22） =================

void AppCore::auditLog(const char* category, const char* type, const QString& slot,
                       int addr, const std::string& payload, qlonglong operatorId) {
  if (!audit_) return;
  const std::string slotS = slot.toStdString();  // 生命周期须覆盖 log 调用
  audit_->log(category, type, slotS, addr, operatorId, payload);
}

bool AppCore::auditOk() const {
  return !audit_ || (!audit_->degraded() && !audit_->unavailable());
}

void AppCore::noteParamChange(HeadItem* h, const HeadConfig& before) {
  if (!audit_ || !h) return;
  const auto s = h->coreHead().state();
  // idle 改参不记（会进下一次 THERAPY_START 的参数快照，D15）
  if (s != HeadState::Starting && s != HeadState::Running && s != HeadState::Paused)
    return;
  if (!pendingSlot_.isEmpty() && pendingSlot_ != h->slotId() &&
      paramDebounceTimer_.isActive())
    flushParamChange();  // 换头：先结算上一头的防抖窗口
  if (pendingSlot_.isEmpty()) pendingBefore_ = before;  // 窗口起点参数
  pendingSlot_ = h->slotId();
  paramDebounceTimer_.start();
}

void AppCore::flushParamChange() {
  paramDebounceTimer_.stop();
  if (pendingSlot_.isEmpty()) return;
  const QString slot = pendingSlot_;
  pendingSlot_.clear();
  HeadItem* h = findBySlot(slot);
  if (!h || !audit_) return;
  const auto s = h->coreHead().state();
  if (s != HeadState::Starting && s != HeadState::Running && s != HeadState::Paused)
    return;  // 窗口内已停止：改动进下一次 START 快照
  const HeadConfig after = h->coreHead().config();
  if (after.freqHz == pendingBefore_.freqHz && after.mode == pendingBefore_.mode &&
      after.timeMin == pendingBefore_.timeMin)
    return;  // 防抖窗口内改回了原值：无净变化
  auto cfgJson = [](const HeadConfig& c) {
    return std::string("{") + audit::jnum("freq", c.freqHz) + "," +
           audit::jnum("mode", static_cast<int>(c.mode)) + "," +
           audit::jnum("timeMin", c.timeMin) + "}";
  };
  auditLog(audit::cat::Therapy, audit::ev::ParamChange, slot, h->addr(),
           std::string("{\"before\":") + cfgJson(pendingBefore_) + ",\"after\":" +
               cfgJson(after) + "," + audit::jstr("note", "2s 防抖生效值（D15）") + "}");
}

void AppCore::snapshotNow() {
  if (!audit_ || !anyOutput()) return;  // 快照仅在治疗会话期间（D15）
  std::string arr = "[";
  bool first = true;
  for (const HeadItem* h : heads_) {
    if (!first) arr += ",";
    first = false;
    arr += std::string("{") + audit::jstr("slot", h->slotId().toStdString()) + "," +
           audit::jnum("addr", h->addr()) + "," +
           audit::jnum("state", static_cast<int>(h->coreHead().state())) + "," +
           audit::jnum("targetRpm", h->coreHead().currentTargetRpm()) + "," +
           audit::jnum("actualRpm", h->coreHead().reportedRpm()) + "," +
           audit::jnum("tempC", h->coreHead().tempC()) + "}";
  }
  arr += "]";
  audit_->logSnapshot(arr);
}

QString AppCore::currentOperatorName() const {
  if (!audit_ || currentOpId_ <= 0) return QString();
  for (const auto& o : audit_->listOperators(true))
    if (o.id == currentOpId_) return QString::fromStdString(o.name);
  return QString();
}

QVariantList AppCore::operatorList() const {
  QVariantList l;
  if (!audit_) return l;
  for (const auto& o : audit_->listOperators(true)) {
    QVariantMap m;
    m[QStringLiteral("id")] = static_cast<qlonglong>(o.id);
    m[QStringLiteral("name")] = QString::fromStdString(o.name);
    m[QStringLiteral("code")] = QString::fromStdString(o.code);
    m[QStringLiteral("current")] = (o.id == currentOpId_);
    l.append(m);
  }
  return l;
}

qlonglong AppCore::addOperator(const QString& name, const QString& code) {
  if (!audit_) return 0;
  const qlonglong id =
      audit_->addOperator(name.trimmed().toStdString(), code.trimmed().toStdString());
  if (id > 0) emit operatorsChanged();
  return id;
}

void AppCore::removeOperator(qlonglong id) {
  if (!audit_) return;
  if (audit_->setOperatorActive(id, false)) {
    if (currentOpId_ == id) {
      currentOpId_ = 0;
      cfg_->currentOperatorId = 0;
      cfg_->save();
      emit currentOperatorChanged();
    }
    emit operatorsChanged();
  }
}

void AppCore::setCurrentOperator(qlonglong id) {
  currentOpId_ = id;
  cfg_->currentOperatorId = id;
  cfg_->save();
  emit currentOperatorChanged();
}

// ================= M4b：审计查询与导出（D22/D23） =================

QVariantList AppCore::auditPage(int offset, int limit, const QString& category) {
  QVariantList l;
  if (!audit_) return l;
  audit::EventQuery q;
  q.category = category.toStdString();
  q.limit = limit > 0 ? limit : 30;
  q.offset = offset > 0 ? offset : 0;
  q.desc = true;  // 最新在前
  const auto rows = audit_->queryEvents(q);
  QMap<qlonglong, QString> opNames;
  for (const auto& o : audit_->listOperators(false))
    opNames[static_cast<qlonglong>(o.id)] = QString::fromStdString(o.name);
  for (const auto& r : rows) {
    QVariantMap m;
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(r.wallUtc);
    QString payload = QString::fromStdString(r.payload);
    if (payload.size() > 90) payload = payload.left(87) + QStringLiteral("…");
    QString line = t.toString(QStringLiteral("MM-dd HH:mm:ss")) +
                   QStringLiteral(" [") + QString::fromStdString(r.category) +
                   QStringLiteral("] ") + QString::fromStdString(r.type);
    if (!r.slot.empty()) line += QStringLiteral(" ") + QString::fromStdString(r.slot);
    if (r.addr > 0) line += QStringLiteral(" addr") + QString::number(r.addr);
    if (r.operatorId > 0)
      line += QStringLiteral(" op:") +
              opNames.value(static_cast<qlonglong>(r.operatorId),
                            QStringLiteral("#") + QString::number(r.operatorId));
    m[QStringLiteral("line")] = line;
    m[QStringLiteral("payload")] = payload;
    m[QStringLiteral("id")] = static_cast<qlonglong>(r.id);
    l.append(m);
  }
  return l;
}

int AppCore::auditCount(const QString& category) {
  if (!audit_) return 0;
  audit::EventQuery q;
  q.category = category.toStdString();
  return static_cast<int>(audit_->countEvents(q));
}

QVariantList AppCore::exportTargets() const {
  QVariantList l;
  const auto vols = QStorageInfo::mountedVolumes();
  for (const QStorageInfo& v : vols) {
    if (!v.isValid() || !v.isReady() || v.isReadOnly()) continue;
    QVariantMap m;
    m[QStringLiteral("name")] = v.displayName();
    m[QStringLiteral("path")] = v.rootPath();
    m[QStringLiteral("freeMB")] =
        static_cast<qlonglong>(v.bytesAvailable() / (1024 * 1024));
    l.append(m);
  }
  return l;
}

QVariantMap AppCore::exportAuditTo(const QString& destDir, int rangeMode,
                                   qlonglong sinceMs) {
  QVariantMap out;
  if (!audit_) {
    out[QStringLiteral("ok")] = false;
    out[QStringLiteral("error")] = QStringLiteral("审计未启用");
    return out;
  }
  const qlonglong now = QDateTime::currentMSecsSinceEpoch();
  qlonglong since = -1;
  if (rangeMode == 1) since = now - 30LL * 24 * 3600 * 1000;
  else if (rangeMode == 2) since = now - 90LL * 24 * 3600 * 1000;
  else if (rangeMode == 3 && sinceMs > 0) since = sinceMs;

  audit::ExportOptions o;
  o.destDir = destDir.toStdString();
  o.sinceWall = since;
  o.appVersion = APP_VERSION;
  // deviceId：T3 板卡冻结后接设备 SN（当前 DEV-UNSET，写入清单可追溯）
  const audit::ExportResult r = audit::exportAudit(*audit_, o);

  out[QStringLiteral("ok")] = r.ok;
  out[QStringLiteral("error")] = QString::fromStdString(r.error);
  out[QStringLiteral("events")] = static_cast<qlonglong>(r.eventCount);
  out[QStringLiteral("snapshots")] = static_cast<qlonglong>(r.snapshotCount);
  out[QStringLiteral("csvName")] = QFileInfo(QString::fromStdString(r.csvPath)).fileName();
  out[QStringLiteral("jsonName")] =
      QFileInfo(QString::fromStdString(r.jsonPath)).fileName();
  out[QStringLiteral("manifest")] =
      QFileInfo(QString::fromStdString(r.manifestPath)).fileName();
  out[QStringLiteral("manifestSha")] =
      QString::fromStdString(r.manifestSha256).left(16);
  out[QStringLiteral("csvSha")] = QString::fromStdString(r.csvSha256).left(16);
  out[QStringLiteral("jsonSha")] = QString::fromStdString(r.jsonSha256).left(16);
  out[QStringLiteral("chainVerified")] = r.chainVerified;

  if (r.ok)
    emit toast(QStringLiteral("✔ 审计已导出：%1 条事件 + %2 条快照（清单含 SHA256，"
                              "EXPORT 事件已留痕）")
                   .arg(r.eventCount)
                   .arg(r.snapshotCount),
               QStringLiteral("ok"), 3500);
  else
    emit toast(QStringLiteral("导出失败：") + QString::fromStdString(r.error),
               QStringLiteral("err"), 4000);
  return out;
}

}  // namespace massage::app
