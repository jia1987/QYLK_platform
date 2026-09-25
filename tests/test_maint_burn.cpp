// 换头烧录流程集成测试（安全关键路径的可执行证据）：
//   场景1：仅地址 0 新头在线 → 联锁通过 → 0xAA 烧录成功 → 新地址上线
//   场景2：另有绑定头在线 → 联锁拒绝（0xAA 会把在线头一起改写）
// MockBus 复现真实 0xAA 语义（谁收到谁改），故联锁逻辑可被真正触发。
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>
#include <QVariantList>

#include "app_core.h"
#include "mock_bus.h"
#include "test_harness.h"

using namespace massage;

static void spin(int ms) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

static bool slotOnline(app::AppCore& core, const QString& slot) {
  const QVariantList infos = core.bindingInfos();
  for (const QVariant& v : infos) {
    const QVariantMap m = v.toMap();
    if (m.value(QStringLiteral("slot")).toString() == slot)
      return m.value(QStringLiteral("online")).toBool();
  }
  return false;
}

int main(int argc, char** argv) {
  QCoreApplication qapp(argc, argv);

  // ---- 场景1：只有新头（地址0）在位 → 烧录到 L2(addr2) 应成功 ----
  {
    app::AppConfig cfg = app::AppConfig::loadOrCreate();
    app::MockBus mock({0});
    app::AppCore core(&cfg, mock, /*realBus=*/false);
    mock.replySink = [&core](const core::Frame& f) { core.deliverReply(f); };

    bool got = false, ok = false;
    QString msg;
    QObject::connect(&core, &app::AppCore::maintBurnResult,
                     [&](bool o, const QString& m) {
                       got = true;
                       ok = o;
                       msg = m;
                     });

    spin(500);                       // 让轮询建立「绑定头全离线」的事实
    CHECK(!slotOnline(core, QStringLiteral("L2")));
    core.maintBurn(1);               // slotList[1] = L2, addr 2
    spin(8000);                      // 探测0 → 烧录 → 轮询上线（ absent 头轮询较慢）
    CHECK(got);
    if (!ok) std::printf("  burn msg: %s\n", qPrintable(msg));
    CHECK(ok);
    spin(3000);
    CHECK(slotOnline(core, QStringLiteral("L2")));  // 新地址 2 已被轮询发现
  }

  // ---- 场景2：地址 3 的绑定头仍在线 → 联锁必须拒绝 ----
  {
    app::AppConfig cfg = app::AppConfig::loadOrCreate();
    app::MockBus mock({0, 3});
    app::AppCore core(&cfg, mock, false);
    mock.replySink = [&core](const core::Frame& f) { core.deliverReply(f); };

    bool got = false, ok = true;
    QString msg;
    QObject::connect(&core, &app::AppCore::maintBurnResult,
                     [&](bool o, const QString& m) {
                       got = true;
                       ok = o;
                       msg = m;
                     });
    spin(1500);  // 等 addr3 轮询上线
    core.maintBurn(1);
    spin(500);   // 联锁是同步判定，无需久等
    CHECK(got);
    CHECK(!ok);
    CHECK(msg.contains(QStringLiteral("联锁")));
  }

  return th::summary();
}
