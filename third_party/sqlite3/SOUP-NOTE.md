# SOUP 条目：SQLite（amalgamation 嵌入）

| 项 | 值 |
|---|---|
| 名称 | SQLite |
| 版本 | **3.53.4** |
| 来源 | https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip（官方站点直接下载） |
| SOURCE_ID | `2026-07-24 19:02:57 bf7c7f30031888f4e796e429ab3978879485813aaca6f641c7b33e4e09459bcc` |
| 获取日期 | 2026-09-26 |
| 许可 | Public Domain（https://www.sqlite.org/copyright.html） |
| 使用方式 | `sqlite3.c` 单文件随本仓库编译（静态链接），不依赖系统/Qt 的 SQLite |
| 用途 | 审计追踪数据库（决策 D9/D13–D22，设计方案 §7） |
| 编译定义 | `SQLITE_THREADSAFE=1`、`SQLITE_DQS=0`、`SQLITE_DEFAULT_MEMSTATUS=0`、`SQLITE_OMIT_LOAD_EXTENSION`（禁用扩展加载，缩小攻击面） |

## 选择理由（决策 D29）

1. **零 Qt 依赖**：AuditLog 保持纯 C++，Windows/WSL 双平台全量单测（与 core 层同等待遇）。
2. **规避部署链风险**：不依赖 `libqt5sql5-sqlite` 驱动包（qtbase5-dev 不自带，遗漏会导致目标板 DB
   打不开 → 静默降级文件兜底，现场难以发现）。
3. **版本冻结可复现**：amalgamation 入仓，构建与目标板系统库版本解耦；SOUP 清单直接引用本文件。

## 已知异常评估（IEC 62304 §5.3.4 / 62304 8.1）

- SQLite 3.53.x 为官方稳定分支；本软件使用面窄（单进程、单写者、WAL、`synchronous=FULL`、
  本地文件、无扩展、无网络），不触及历史上多数缺陷所在区（FTS、扩展加载、并发写）。
- 缓解措施：`PRAGMA integrity_check` 自检（打开时 + 测试中验证）、审计触发器防删改、
  链式 SHA256 完整性校验（D21）、写失败自动降级 NDJSON 文件兜底（D13）。
- 升级策略：仅安全通告或注册检验要求时升级；升级即更新本文件与 SOUP 清单，跑全量审计单测。
