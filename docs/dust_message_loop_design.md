# dust::MessageLoop 设计文档（讨论稿 v1）

> 背景：MessageLoop 将放入独立 repo **dust**，作为与业务无关的基础库模块。cpp-agent 侧的 curl-multi NetworkLoop 将依赖 MessageLoop 做主线程投递与（可选）I/O 注册。

## 0. 目标与约束

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

## 1. 核心 API 草案

### 1.1 基本类型
- `using Task = std::function<void()>;`
- `using Duration = std::chrono::milliseconds;`

### 1.2 MessageLoop
- `void post(Task);`  // 线程安全
- `TimerId post_delayed(Duration, Task);`  // 线程安全
- `void cancel_timer(TimerId);`  // 线程安全（建议）
- `void run();`  // 仅 loop 线程调用，阻塞直到 stop
- `bool run_once(std::optional<Duration> max_wait);`  // 仅 loop 线程
- `void stop();`  // 线程安全，唤醒 run

### 1.3 I/O Watch（仅 loop 线程调用）
- `enum class IoEvent { Readable, Writable, Error, Hangup };`
- `using IoEventMask = uint32_t;`
- `WatchId watch_io(IoHandle, IoEventMask, IoCallback);`
- `void unwatch_io(WatchId);`

`IoHandle` 为跨平台变体：
- Linux/macOS：`int fd`
- Windows：`SOCKET`（或 HANDLE，v1 以 socket 为重点）

`IoCallback`：`void(IoEventMask ready)`

> 注意：v1 约束 watch/unwatch 只允许在 loop 线程调用；跨线程注册需要调用方通过 `post()` 把注册动作投递到 loop。

---

## 2. 线程模型与回调顺序

### 2.1 线程模型
- loop 绑定到一个线程（通常主线程），负责执行：tasks/timers/io callbacks。
- 任意线程可调用 `post/post_delayed/stop`。

### 2.2 执行顺序与公平性
每轮循环建议：
1) drain tasks（可设每轮上限 N，避免饿死 I/O）
2) 执行到期 timers
3) wait（I/O 或 wakeup 或 timer 超时）
4) 执行就绪 I/O callbacks

---

## 3. 内部结构（跨平台一致抽象）

### 3.1 Wakeup 机制
用于跨线程 post/stop/timer 变更时唤醒 loop。
- Linux：eventfd 或 self-pipe
- macOS：self-pipe 或 kqueue user event
- Windows：通过 IOCP 自定义 completion（见 5.3）

### 3.2 队列
- `pending_tasks`：MPSC 队列（多生产者，loop 单消费）
- `timers`：最小堆（到期时间 + TimerId + Task）
- `watches`：WatchId -> (IoHandle, mask, callback)

---

## 4. Linux 后端（epoll）
- 使用 epoll 管理：wakeup fd + watched fds
- `epoll_wait` 的 timeout 来自最近 timer
- wakeup readable 时 drain tasks/timers

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

