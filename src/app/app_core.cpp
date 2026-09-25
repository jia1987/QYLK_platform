#include "app_core.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QMap>
#include <QProcess>
#include <QRandomGenerator>
#include <QStringList>
#include <QVariantMap>

#include "core/frame_codec.h"

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
                 QObject* parent)
    : QObject(parent), cfg_(cfg), realBus_(realBus) {
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
  }

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

  busTimer_.start();
  logicTimer_.start();
  uiTimer_.start();
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
  switch (static_cast<HeadEvent>(ev)) {
    case HeadEvent::Started: {
      const auto c = h->coreHead().config();
      emit toast(QStringLiteral("%1 治疗已启动 · %2Hz %3 %4min")
                     .arg(h->slotId())
                     .arg(c.freqHz)
                     .arg(modeNameOf(c.mode))
                     .arg(c.timeMin),
                 QStringLiteral("ok"), 2600);
      break;
    }
    case HeadEvent::Paused:
      emit toast(h->slotId() + QStringLiteral(" 已暂停 · 计时暂停"),
                 QStringLiteral("warn"), 1400);
      break;
    case HeadEvent::Resumed:
      emit toast(h->slotId() + QStringLiteral(" 已继续"),
                 QStringLiteral("ok"), 1400);
      break;
    case HeadEvent::StopRequested:
      emit toast(h->slotId() + QStringLiteral(" 已完全停止 · 计时已归零"),
                 QStringLiteral("warn"), 2000);
      break;
    case HeadEvent::TimerExpired:
      break;  // Completed 紧随其后，避免双 toast
    case HeadEvent::Completed:
      emit toast(h->slotId() + QStringLiteral(" 治疗完成 ✔"),
                 QStringLiteral("ok"), 2600);
      break;
    case HeadEvent::FaultEntered:
      emit toast(h->slotId() + QStringLiteral(" 故障：") + h->faultText(),
                 QStringLiteral("err"), 4000);
      break;
    case HeadEvent::FaultCleared:
      emit toast(h->slotId() + QStringLiteral(" 故障已复位"),
                 QStringLiteral("ok"), 2000);
      break;
    case HeadEvent::WentOfflineDuringRun:
      emit toast(h->slotId() +
                     QStringLiteral(" 连接丢失！电机可能仍在运转，必要时使用硬件急停"),
                 QStringLiteral("err"), 6000);
      break;
    case HeadEvent::AckRejected:
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
  return QStringLiteral("v0.2.0-m3 · ") +
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
  HeadConfig c = selected_->coreHead().config();
  const int v = qBound(kMinFreqHz, c.freqHz + deltaSteps * 5, kMaxFreqHz);
  if (v == c.freqHz) return;
  c.freqHz = v;
  if (!selected_->applyCore(c)) return;
  const auto s = selected_->coreHead().state();
  if (s == HeadState::Running || s == HeadState::Starting || s == HeadState::Paused)
    emit toast(selected_->slotId() + QStringLiteral(" 频率实时调整为 ") +
                   QString::number(v) + QStringLiteral(" Hz"),
               QStringLiteral("ok"), 1400);
}

void AppCore::adjustTime(int deltaSteps) {
  if (!selected_) return;
  HeadConfig c = selected_->coreHead().config();
  const int v = qBound(5, c.timeMin + deltaSteps * 5, 60);
  if (v == c.timeMin) return;
  c.timeMin = v;
  selected_->applyCore(c);  // 核心层实现差值语义
}

void AppCore::setMode(int mode) {
  if (!selected_ || mode < 0 || mode > 2) return;
  HeadConfig c = selected_->coreHead().config();
  const auto m = static_cast<Mode>(mode);
  if (c.mode == m) return;
  c.mode = m;
  if (!selected_->applyCore(c)) return;
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
    cfg_->presets[index] = PresetConfig{c.freqHz, static_cast<int>(c.mode),
                                        c.timeMin};
    cfg_->save();
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
  HeadConfig c = selected_->coreHead().config();
  c.freqHz = p.freqHz;
  c.mode = static_cast<Mode>(p.mode);
  c.timeMin = p.timeMin;
  if (!selected_->applyCore(c)) {
    emit toast(QStringLiteral("预设参数无效"), QStringLiteral("err"), 2000);
    return;
  }
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
  info[QStringLiteral("msg")] =
      QStringLiteral("即将启动：%1\n参数：%2 Hz · %3 · %4 min\n计时将从 %4:00 全程重新开始")
          .arg(h->slotId())
          .arg(c.freqHz)
          .arg(modeNameOf(c.mode))
          .arg(c.timeMin);
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

bool AppCore::verifyPin(const QString& pin) {
  return pinSet() && hashPin(cfg_->pinSalt, pin) == cfg_->pinHash;
}

bool AppCore::setInitialPin(const QString& pin) {
  if (pinSet() || pin.length() < 4 || pin.length() > 6) return false;
  cfg_->pinSalt =
      QString::number(QRandomGenerator::system()->generate64(), 16);
  cfg_->pinHash = hashPin(cfg_->pinSalt, pin);
  cfg_->save();
  emit pinChanged();
  emit toast(QStringLiteral("PIN 已设置（设置/维护入口已启用门禁）"),
             QStringLiteral("ok"), 2500);
  return true;
}

bool AppCore::changePin(const QString& oldPin, const QString& newPin) {
  if (!verifyPin(oldPin)) return false;
  if (newPin.length() < 4 || newPin.length() > 6) return false;
  cfg_->pinSalt =
      QString::number(QRandomGenerator::system()->generate64(), 16);
  cfg_->pinHash = hashPin(cfg_->pinSalt, newPin);
  cfg_->save();
  emit pinChanged();
  emit toast(QStringLiteral("PIN 已修改"), QStringLiteral("ok"), 2000);
  return true;
}

// ================= M3.5：设置持久化 =================

void AppCore::saveIdentity(const QString& hospital, const QString& department) {
  cfg_->hospital = hospital.trimmed();
  cfg_->department = department.trimmed();
  cfg_->save();
  emit hospChanged();
  emit toast(QStringLiteral("医院 / 科室信息已保存"), QStringLiteral("ok"), 2000);
}

void AppCore::saveSerial(const QString& port, int baud) {
  cfg_->serialPort = port.trimmed();
  if (baud > 0) cfg_->baudRate = baud;
  cfg_->save();
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
  QProcess p;
  p.start(QStringLiteral("timedatectl"),
          {QStringLiteral("set-time"),
           t.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))});
  p.waitForFinished(3000);
  if (p.exitCode() == 0)
    emit toast(QStringLiteral("系统时间已设置"), QStringLiteral("ok"), 2000);
  else
    emit toast(QStringLiteral("设置系统时间失败（需要授权）：") +
                   QString::fromUtf8(p.readAllStandardError()),
               QStringLiteral("err"), 4000);
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
  cfg_->slotList[slotIndex].type = type;
  cfg_->save();
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
  cfg_->slotList[slotIndex].addr = static_cast<quint8>(newAddr);
  cfg_->save();
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
          emit maintZeroFound(false);
          emit maintBurnResult(
              false, QStringLiteral("未检测到地址 0 的设备：请确认新头已接入且为出厂状态。"));
          return;
        }
        emit maintZeroFound(true);
        const Frame burn = encodeConfigAddress(
            target.addr, defaults::kClosedLoop, defaults::kStallTime,
            defaults::kPolePairs);
        sched_->enqueueControl(
            burn, [this, target](bool ok, Reply, const ReplyPayload& p) {
              const bool ackOk =
                  ok && std::get<AckReply>(p).flag == AckFlag::Ok;
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

}  // namespace massage::app
