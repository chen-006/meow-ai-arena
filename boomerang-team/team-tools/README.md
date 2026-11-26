# 队长派活与亲手改工具（选手端）

本目录收录第 12 集 5v5 团队赛中**队长和队员直接使用的协同工具**——也就是让两位队长（Astra / Fable）把四名异构模型队员当成“子代理”调用的整套机制，以及“亲手改”和“编译即共享”的实现。

> 按 [`../../docs/open-source-scope.md`](../../docs/open-source-scope.md) 范围，本目录只收录选手侧直接调用的工具和依赖，**不收录**纯主办方运营脚本（如开赛/冻结/回合调度 `组队.py`、`封存.py`、`队长启动.py`、看门狗、赛前压缩与路径核查脚本）。

## 工作原理：队长如何把队员当子代理用

```text
队长命令行（Codex / Claude Code）
  │
  ├─ 1. 派活：ask.cmd 队员名 --file 任务书.txt [--background] / ask.cmd wait / ask.cmd status
  │      └─ ask_entry.py（固定指向本队 team.json，禁止队长篡改 --config）
  │           └─ team_cli.py
  │                ├─ team12.before_ask()
  │                │    ├─ 阶段与截止时间窗口校验（dev / round / postgame）
  │                │    ├─ code_check()：拦截 ``` 代码块或连续 ≥3 行 C++，拒发不计次
  │                │    ├─ 自动前置队员前言（前言_开发.txt / 前言_商量.txt）
  │                │    └─ 附加“队长亲手改过你第 X 行”的待告知提醒
  │                ├─ member_env()：清除队长自身的 CLAUDE*/CODEX_*/ANTHROPIC_* 变量，避免串号
  │                ├─ 按工具类型拉起队员 CLI（codex / claude / agy / grok / kimi / opencode / dsh / fake）
  │                │    └─ 续接队员各自的 session_id（每位队员只看得到任务书和自己的历史）
  │                ├─ scrub_output()：日志仅留文本与工具元数据，剥离思考/推理字段
  │                └─ 仅将队员的**最终回复**返回给队长；单队员忙锁（退出码 75）与截止超时截断（124）
  │
  ├─ 2. 亲手改：亲手改.cmd 队员名 改好的文件.cpp  /  亲手改.cmd 剩余
  │      └─ edit_entry.py
  │           └─ team12.hand_edit_cli()
  │                ├─ 全场次数校验（默认 3 次）与窗口校验
  │                ├─ count_changes()：忽略空行与行尾空白，SequenceMatcher 计算改动行数（≤30 行）
  │                ├─ 确认队员当前不忙 → 用正式编译参数试编（编不过不写入、不扣次数）
  │                ├─ 备份原文件、保存 unified diff、原子替换 player.cpp 并分发新 exe 给队友
  │                └─ 登记待告知提醒：下次给该队员派活时自动告知改动行号（不贴代码）
  │
  └─ 3. 编译即共享（team12.BUILD_WRAPPER）
         └─ 每名选手目录的 build.py 包装 build_core.py：开发期编译自己的 player.cpp 成功后，
            自动将新 exe 原子复制到其余四名队友的 `队友程序/<名字>.exe`，随时可做 5 人整队联调。
```

## 目录内容

| 文件 | 作用 |
|---|---|
| [`team_cli.py`](team_cli.py) | `ask.cmd` 的核心后端：多工具 CLI 适配、会话续接、同步/后台排队（`--background` / `wait` / `status`）、进程环境隔离、单队员文件锁与审计日志脱敏 |
| [`team12.py`](team12.py) | 第 12 集规则钩子：任务书禁代码检查（`code_check`）、开发/回合前言注入（`before_ask`）、亲手改计数/30 行比对/试编/原子写入（`hand_edit_cli`）、编译即共享包装模板（`BUILD_WRAPPER`） |
| [`common.py`](common.py) | 跨平台文件锁（`Lock`）、无 shell 拼接子进程执行器（`run_process`，超时按 PID 进程树清理）与原子 JSON 读写 |
| [`session_lookup.py`](session_lookup.py) | 子进程输出截断时的会话编号元数据后备查询（只读元数据字段，不读对话或思考正文） |
| [`grok_cli.py`](grok_cli.py) | Grok Build CLI 包装：从 stdin 读任务书写入临时 `--prompt-file` 并剥离输出中的思考字段 |
| [`opencode_runtime.py`](opencode_runtime.py) | OpenCode 私有本地服务包装：启动一次性随机口令 `opencode serve` 供队员隔离调用 |
| [`fake_member.py`](fake_member.py) | 零依赖假队员（不调用模型、不联网），用于本地验证派活、并发排队、代码拦截与亲手改流程 |
| [`前言_开发.txt`](前言_开发.txt)、[`前言_商量.txt`](前言_商量.txt) | 开发期与回合决策期自动附在队长任务书前的规则前言模板 |
| [`队长须知模板.md`](队长须知模板.md) | 生成到队长工作目录的 `队长须知.md` 模板 |
| [`templates/`](templates/) | 放进队长工作目录的 4 个入口模板：`ask.cmd`、`ask_entry.py`、`亲手改.cmd`、`edit_entry.py` |
| [`examples/`](examples/) | 脱敏后的配置样例：`match.example.json`（赛事配置结构）与 `team.example.json`（单队派活配置结构） |

## 本地零成本复现（使用 `fake_member.py`）

不需要任何 API 密钥或第三方模型命令行，只需 **Python 3.9+**（纯标准库），即可在本地完整跑通“队长派活 + 续接会话 + 代码禁发拦截 + 后台并发 `wait`”：

1. 准备一个临时演示目录（例如 `/tmp/team-demo` 或 `D:\tmp\team-demo`），按 [`examples/team.example.json`](examples/team.example.json) 写一份 `team.json`：
   - 将 `captain_dir`、`state_dir`、`log_dir` 和队员的 `cwd` 改为该临时目录中的绝对路径；
   - 将队员的 `tool` 设为 `"fake"`，`exe` 设为本目录 `fake_member.py` 的绝对路径；
   - 如不测试赛事阶段锁和亲手改，可先省略 `match_config`。
2. 命令行直接体验派活、会话续接与并发等待：

```bash
cd boomerang-team/team-tools

# 预检命令（不执行子进程）
python team_cli.py ask --config /path/to/team.json member1 "请检查占点 B 桥走位" --dry-run

# 同步派活（首次自动生成 session_id，第二次自动续接同一会话）
python team_cli.py ask --config /path/to/team.json member1 "第一轮任务：分析火山口开局"
python team_cli.py ask --config /path/to/team.json member1 "第二轮追问：结合刚才的结论补充站位"

# 后台并发派活 + 查看状态 + 统一收取回复
python team_cli.py ask --config /path/to/team.json member1 "【假】慢活 2" --background
python team_cli.py status --config /path/to/team.json
python team_cli.py wait --config /path/to/team.json member1 --timeout 30
```

3. 若要同时体验 **代码禁发检查** 与 **30 行亲手改**：
   - 参考 [`examples/match.example.json`](examples/match.example.json) 配置 `match.json`，在 `<root>/_主办方/阶段.json` 写入 `{"phase": "dev", "deadline": 4102444800}`，并在 `<root>/_主办方/kit/` 放一份 [`../workspace/`](../workspace/) 的选手包；
   - 在 `team.json` 中加上 `"match_config": "/path/to/match.json"` 和 `"team": "Demo队"`；
   - 试发包含 ```` ``` ```` 或连续 3 行 C++ 代码的任务书，会收到退出码 `67` 的拒发提示；
   - 修改一份 `改_member1.cpp` 后运行 `python -c "import team12, sys; raise SystemExit(team12.hand_edit_cli('/path/to/match.json', 'Demo队', sys.argv[1:]))" member1 改_member1.cpp`（或通过 `templates/亲手改.cmd`），即可体验行数统计、编译检查、diff 留档与下次派活自动提醒。

## 接入真实模型 CLI

将 `team.json` 中队员的 `tool` 改为 `codex`、`claude`、`agy`、`grok`、`kimi`、`opencode` 或 `dsh`，`exe` 指向对应 CLI 的绝对路径，`model` / `effort` 填所需型号与档位；如需注入 API 密钥，通过 `env.<VAR>.value_file` 指向本机私钥文件（密钥仅在真实调用子进程时读取，日志会自动脱敏，切勿写入配置文件或仓库）。
