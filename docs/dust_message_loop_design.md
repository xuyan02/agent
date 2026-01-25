# dust MessageLoop / MessagePump / MessageQueue 设计文档（讨论稿 v1）

> 背景：MessageLoop 将放入独立 repo **dust**，作为与业务无关的基础库模块。cpp-agent 侧的 curl-multi NetworkLoop 将依赖 MessageLoop 做主线程投递与（可选）I/O 注册。

## 0. 目标与约束

> 讨论重点：优先落地 Linux（epoll + eventfd）实现；macOS/Windows 仅保留接口与设计方向，后续再补齐。

### 0.1 目标
- 提供通用的 **MessageLoop**（dispatcher + timer + 可选 I/O watch）。
- 支持主流平台：Linux / macOS / Windows。
- 支持跨线程 `post()` 投递，并由 loop 线程阻塞等待并执行任务。
- 支持 `post_delayed()` 定时任务。
- 支持 `watch_io()` 注册外部 I/O 事件回调（就绪语义）。

### 0.2 关键约束（来自需求）
- `watch_io/unwatch_io` **只能在 loop 线程调用**（非线程安全）。
- `post/post_delayed/stop` **线程安全**（可从任意线程调用）。
- 回调执行：I/O 回调与 task/timer 回调都 **直接在 loop 线程执行**。
- Windows 必须支持大量 socket watch：**不能依赖 WaitForMultipleObjects 的 64 限制**。
- I/O 语义：选择 **就绪语义（A）**：Readable/Writable readiness，不直接交付数据。

### 0.3 非目标（v1 不做）
- 不包含 HTTP/curl/TLS 等业务相关内容。
- 不提供跨平台文件描述符统一抽象的完整网络栈。
- 不保证稳定 ABI（先稳定 API）。

---

## 1. 核心对象与职责（Chromium 风格拆分）

### 1.1 关键对象
- **MessageLoop**：面向使用者的“线程绑定 loop”，负责：创建/拥有 MessagePump、管理 MessageQueue、提供 Current()。
  - 支持 re-run：`QuitWhenIdle()` 只退出本次 `Run()`，对象可再次 `Run()`。
- **MessagePump**：抽象不同平台的“阻塞等待与驱动逻辑”（Chromium 风格：`Run(Delegate*)`）。
  - Linux 实现：epoll + eventfd + timerfd。
  - Android：不实现，但保留抽象入口（2A）。
- **MessageQueue**：任务/延迟任务的存储与出队策略（线程安全）。
  - pending tasks：FIFO。
  - delayed tasks：按到期时间排序；同一到期时间按注册顺序稳定排序；到期后以该顺序进入 FIFO。
- **TaskRunner**：线程安全的投递入口（跨线程持有），内部通过 refcounted state + shutdown gate 避免 UAF。

### 1.2 基本类型
- Task 类型：建议使用 **OnceClosure** 语义（一次性执行）。
  - 伪代码：`using OnceClosure = base::OnceClosure;`（dust 内部若不依赖 base，则提供等价物）
- Watch 回调：使用可为空的 `base::RepeatingClosure`。

### 1.3 TaskRunner（跨线程投递）
- `bool PostTask(OnceClosure task);`
- `bool PostDelayedTask(Duration delay, OnceClosure task);`  // v1 不支持取消
  - 语义（v1）：`delay == 0` 也走延迟路径（最终由 timerfd 驱动），不等价于 PostTask。
- `bool Quit();`

说明：
- TaskRunner 可 copy，可跨线程长期持有。
- 当 MessageLoop 进入 shutdown 后，Post* 返回 false 丢弃任务。

### 1.4 MessageLoop（仅 loop 线程控制）
- `void Run();`  // 可重复调用（re-run）
- `bool RunOnce(std::optional<Duration> max_wait);`
- `void QuitWhenIdle();`  // 优雅退出：处理完已入队任务后退出当前 Run
- `scoped_refptr<TaskRunner> task_runner();`
- `void WatchFd(int fd, WatchCallbacks callbacks);`
- `void UnwatchFd(int fd);`

re-run 语义（v1）：
- `QuitWhenIdle()` 退出当前 Run 后，**保留** WatchFd 注册配置；下次 Run 继续监听。

线程约束：
- Run/RunOnce/WatchFd/UnwatchFd 只能在 loop 线程调用。
- 跨线程仅通过 TaskRunner 投递任务。

### 1.5 I/O Watch（仅 loop 线程；fd 作为 key）

分工（最终）：
- MessageLoop 持有 `watches_by_fd` 并负责触发回调。
- MessagePump 只负责等待与就绪通知（不直接触发 WatchCallbacks）。

API 草案：
- `enum class IoEvent { kReadable, kWritable, kError, kHangup };`
- `using IoEvents = uint32_t;`  // bitmask
- `struct WatchCallbacks {`
  - `base::RepeatingClosure on_readable;`  // 可为空：为空表示不监听该事件
  - `base::RepeatingClosure on_writable;`  // 可为空
  - `base::RepeatingClosure on_error;`  // 可为空；kError/kHangup 统一走这里（v1）
  - `};`
- `void WatchFd(int fd, WatchCallbacks callbacks);`  // 全量覆盖：替换该 fd 的回调集合/监听掩码
- `void UnwatchFd(int fd);`

语义约束（v1）：
- 回调为 Repeating：每次就绪都会触发对应回调。
- 不允许三个回调均为空：`WatchFd(fd, empty)` 等价于 `UnwatchFd(fd)`。
- 不自动处理 close：调用方必须在 close(fd) 之前 **在 loop 线程** 调用 `UnwatchFd(fd)`；回调内禁止直接 `close(fd)`。
- MessageLoop/MessagePump 不负责 close 被 watch 的 fd（fd 生命周期由调用方管理）。
- Linux epoll 策略：level-triggered（默认 EPOLLIN/EPOLLOUT，不启用 EPOLLET）。

---

## 2. 线程模型与回调顺序

### 2.1 线程模型
- loop 绑定到一个线程（通常主线程），负责执行：tasks/timers/io callbacks。
- 允许任意线程通过 **TaskRunner** 调用 `PostTask/PostDelayedTask/Quit`，包括在 loop 线程内重入调用。
  - 语义（v1）：Post* **从不 inline 直接执行**，总是先入队到 MessageQueue（避免递归/重入带来的不可预测性）。

### 2.2 执行顺序与公平性
每轮循环建议：
1) drain tasks（v1：不设上限，按需求执行全部；注意可能饿死 I/O）
2) 执行到期 delayed tasks（PostDelayedTask；Linux：timerfd 唤醒后批量执行）
3) wait（I/O 或 wakeup 或 timerfd）
4) 执行就绪 I/O callbacks

QuitWhenIdle 语义（v1）：
- `TaskRunner::Quit()` 触发 `MessageLoop::QuitWhenIdle()`：设置退出标志并唤醒 MessagePump。
- MessageLoop 会继续执行 **已入队** 的 tasks（MessageQueue 为空后退出 Run）。
- delayed tasks：
  - 若已经到期并被入队，则会执行。
  - 若未到期且仅存于 delayed 结构中，则不保证会在退出前触发。

---

## 3. MessagePump 抽象（跨平台驱动层）

目标：隔离不同平台的等待/唤醒机制；MessagePump 不直接拥有任务队列，只通过 Delegate 回调驱动工作（Chromium 风格）。

### 3.1 MessagePump 接口草案
- `class MessagePump {`
  - `class Delegate {`
    - `virtual bool DoWork() = 0;`  // 执行 pending tasks；返回是否还有更多 work
    - `virtual bool DoDelayedWork(TimeTicks* next_delayed_work_time) = 0;`
    - `virtual bool DoIdleWork() = 0;`
    - `virtual void OnFdEvents(int fd, IoEvents events) = 0;`  // I/O 就绪通知（跨平台抽象）
    - `};`
  - `virtual void Run(Delegate* delegate) = 0;`
  - `virtual void Quit() = 0;`
  - `virtual void ScheduleWork() = 0;`  // 跨线程唤醒（eventfd）
  - `virtual void ScheduleDelayedWork(TimeTicks next_time) = 0;`  // 驱动 timerfd
  - `};`

说明：
- `MessageLoop` 作为 `Delegate` 的实现者，内部用 `MessageQueue` 支撑 DoWork/DoDelayedWork。
- `ScheduleWork/ScheduleDelayedWork` 由 TaskRunner 在 post 时调用，用于唤醒阻塞的 Run。

---

## 4. 内部结构（跨平台一致抽象）

### 3.1 Wakeup 机制
用于跨线程 post/stop/timer 变更时唤醒 loop。
- Linux：eventfd 或 self-pipe
- macOS：self-pipe 或 kqueue user event
- Windows：通过 IOCP 自定义 completion（见 5.3）

### 3.2 队列
- `pending_tasks`：MPSC 队列（多生产者，loop 单消费）
- `timers`：最小堆（到期时间 + TimerId + Task）
- `watches_by_fd`：fd -> WatchCallbacks（每个 fd 一份配置；WatchFd 全量覆盖更新）

---

## 5. Linux MessagePump 实现（epoll + eventfd + timerfd）

> 约束：Linux 实现优先采用 Chromium 风格代码与命名。

职责边界：
- MessagePump 只做等待/唤醒与 I/O 就绪通知：
  - 将 epoll 事件映射为跨平台 `IoEvents`，并调用 `delegate->OnFdEvents(fd, events)`。
- WatchCallbacks 的触发由 MessageLoop 完成（按文档规则 error->read->write，on_error 短路）。

LinuxPump 机制：
- wakeup：**eventfd**（由 `ScheduleWork()` 写入，Run 中读出清空）。
- timer：**timerfd(CLOCK_MONOTONIC)**（由 `ScheduleDelayedWork(next_time)` 设置；Run 中读出清空并触发 `delegate->DoDelayedWork(...)`）。
- I/O：epoll 监听注册的 fds。

I/O 事件映射（level-triggered）：
- EPOLLIN  -> IoEvent::kReadable
- EPOLLOUT -> IoEvent::kWritable
- EPOLLERR -> IoEvent::kError
- EPOLLHUP -> IoEvent::kHangup

### 5.1 Pump::Run 伪代码（Linux）
1) loop:
   - `bool did_work = delegate->DoWork();`
   - `TimeTicks next_time; bool did_delayed = delegate->DoDelayedWork(&next_time);`
   - `if (quit_) break;`
   - `if (!did_work && !did_delayed) delegate->DoIdleWork();`
   - 计算 wait timeout：
     - v1：timerfd 由 ScheduleDelayedWork 负责，epoll_wait 直接用 -1
   - `epoll_wait(...)`
   - 处理 epoll events：
     - eventfd: ReadAndClear; continue
     - timerfd: ReadAndClear; delegate->DoDelayedWork(&next_time); continue
     - watched fd: `IoEvents events = MapEpollToIoEvents(...); delegate->OnFdEvents(fd, events);`

---

## 5. macOS 后端（kqueue）
- 使用 kqueue 管理：wakeup（EVFILT_USER 或 pipe）+ watched fds
- timer 可用 kevent timeout（或 EVFILT_TIMER，视实现选择）

---

## 6. Windows 后端（IOCP，桥接就绪语义）

### 6.1 为什么必须 IOCP
- WaitForMultipleObjects/WSAEventSelect 有 64 句柄上限，不满足“上限不可接受”的要求。
- IOCP 是 Windows 上可扩展的并发 I/O 等待原语。

### 6.2 关键挑战：IOCP 是“完成”模型而不是“就绪”模型
MessageLoop 对外提供的是 **Readable/Writable readiness**。
因此 Windows 后端需要一个桥接层，把完成事件转换成“就绪事件”。

### 6.3 v1 桥接策略（建议）
- WindowsBackend 以 IOCP 为核心 wait：`GetQueuedCompletionStatus(Ex)`。
- wakeup：使用 `PostQueuedCompletionStatus` 投递自定义 completion 作为唤醒。
- timer：使用 wait timeout（由最近 timer 计算）+ 在 timeout 分支执行 timers。
- socket watch：
  - 对每个被 watch 的 socket，维护内部状态，并在需要时发起 overlapped 操作以驱动 completion。
  - Readable：通过投递一个非阻塞的 `WSARecv`（可使用 0-byte recv 或小 buffer + MSG_PEEK 方案，最终实现需验证）来获得“可读/关闭/错误”的 completion。
  - Writable：通过 `WSASend` 0-byte 或 connect completion（更复杂，v1 可先定义为可选能力）。

> 说明：此桥接不会把数据交付给用户；内部缓冲若存在，仅用于判断 readiness 并可在下一轮触发 readable。

### 6.4 取消与 unwatch
- unwatch 在 loop 线程调用：
  - 标记 watch 已关闭
  - 取消相关 overlapped（CancelIoEx/关闭 socket 等策略需讨论）
  - 丢弃后续 completion（通过 generation/request_id 判定）

---

## 7. 尚待确认的问题（将逐点讨论）
1) Windows：Writable readiness 是否 v1 必须支持？
2) Windows：Readable 桥接到底用 0-byte WSARecv 还是 peek 小 buffer？（可行性/副作用）
3) 事件队列背压：post flood 或 streaming 事件过多时的策略（队列上限/合并/节流）
4) watch_io 的事件集合：是否需要 Error/Hangup 的细分语义一致化

---

## 8. 与 cpp-agent 的集成方式（约定）
- cpp-agent 内部 NetworkLoop（curl multi）在网络线程产出事件后：
  - `message_loop.post([=]{ /* 主线程处理 */ });`
- 若需要监听 stdin（CLI）或其他 fd：
  - 在主线程 run 前，通过 `watch_io(fd, Readable, cb)` 注册。

