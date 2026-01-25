# HTTP 解耦重构：类设计文档（讨论稿 v1）

## 0. 目标与非目标
**目标**
- 降低耦合、分层清晰：core 不依赖 curl/OpenAI 协议细节。
- HTTP 作为可替换基础设施：未来可切换 curl/其他实现（甚至 mock）。
- 让 OpenAI/LLM 协议与 HTTP 传输解耦，便于测试 request/parse。

**非目标（本阶段不做/可后续）**
- 不改变整体功能与用户交互语义（除非后续确认）。
- 不引入大型第三方 JSON 库（除非项目已有且你同意）。
- 不一次性重写所有 interfaces DTO（先聚焦 HTTP）。

---

## 1. 分层与模块边界（建议）
### 1.1 core（业务流程）
- `core::Agent`：对话循环、调用 ILlmClient、工具执行、plan 驱动。
- **不允许**：core 直接触达 HTTP、curl、OpenAI payload。

### 1.2 interfaces（端口/抽象）
- `interfaces::ILlmClient`
- 新增：`interfaces::IHttpClient`（HTTP 传输端口）
- DTO：`HttpRequest/HttpResponse`（传输层数据结构）

### 1.3 infra（适配器/实现）
- `infra/http/CurlHttpClient`：curl 实现 IHttpClient
- `infra/llm/OpenAIChatCompletionsClient`：OpenAI 协议实现 ILlmClient（内部用 IHttpClient）

### 1.4 app（组装/依赖注入）
- `app::build_agent_or_throw`：构造具体实现并注入 Agent

---

## 2. 类清单与职责

### 2.1 interfaces::IHttpClient（新增）
**职责**
- 发送 HTTP 请求并返回响应
- 封装底层传输错误为统一异常/错误码（沿用现有 AgentError 也可）

**接口草案**
- `HttpResponse send(const HttpRequest& req)`

**HttpRequest 字段**
- `method`（先只需要 POST/GET）
- `url`
- `headers`（vector<string> 或 map<string,string>）
- `body`（string）
- `timeout_ms`（可选，未来扩展）
- （可选）`verify_tls`/`ca_path`（先不做）

**HttpResponse 字段**
- `status`（int）
- `headers`（可选）
- `body`

---

### 2.2 infra/http::CurlHttpClient（新增）
**职责**
- 仅负责 curl_easy_* 细节：headers、body、write callback、错误映射
- 不知道 OpenAI、不知道 tools_json、不知道 core::Message

**行为**
- 对网络/初始化失败抛 `AgentError(kNetwork/kInternal, ...)`
- 返回 `HttpResponse{status, body}`（headers 先可不收集）

---

### 2.3 infra/llm::OpenAIChatCompletionsClient（重命名/重组）
（可以由现 `infra/http/OpenAIClient` 演进而来）

**职责**
- 实现 `interfaces::ILlmClient`
- 构造 OpenAI `POST {base_url}/chat/completions` 请求
- 解析响应为 `LlmResponse`（assistant content + tool_calls）
- 不直接使用 curl（通过 IHttpClient）

**依赖**
- `interfaces::IHttpClient& http_`
- `base_url_ / api_key_ / tools_json_ / log_requests_`

**内部可拆分（可选）**
- `OpenAIRequestBuilder`：build HttpRequest
- `OpenAIResponseParser`：parse json -> LlmResponse

---

### 2.4 interfaces::ILlmClient（是否调整）
现状：`complete(vector<core::Message>, LlmOptions)`
这会让 interfaces 依赖 core（依赖方向不够纯）。

**两种方案**
- **A（最小改动）**：暂时保留 core::Message（先把 HTTP 解耦出来）
- **B（更纯）**：interfaces 定义 `ChatMessage/ToolCall` DTO，core 做转换

---

## 3. 依赖关系（必须满足）
- `core -> interfaces` ✅
- `infra/* -> interfaces` ✅
- `app -> core + infra + interfaces` ✅
- `core -X-> infra` ❌
- `interfaces -X-> infra` ❌
- `infra -X-> core` ❌（尽量；必要时通过 DTO/转换）

---

## 4. 数据流（一次 LLM 请求）
1) `core::Agent` 调用 `ILlmClient::complete(messages, options)`
2) `OpenAIChatCompletionsClient` 用 builder 构造 `HttpRequest`
3) 调 `IHttpClient::send(req)` 得 `HttpResponse`
4) parser 解析响应 json -> `LlmResponse`
5) 返回给 Agent，继续工具循环

---

## 5. 测试建议（跟随重构）
- 单测：OpenAIRequestBuilder（断言 URL/header/body）
- 单测：OpenAIResponseParser（覆盖 content/tool_calls）
- IHttpClient 可用 fake 实现注入到 OpenAI client，避免网络依赖

---

## 讨论点（逐点确认）
1) `ILlmClient` 的入参/出参是否要 DTO 化？（A 最小改动 / B 更纯）
2) `HttpRequest/HttpResponse` 字段是否要包含 response headers / timeout / proxy 等？
3) 错误模型：IHttpClient 层抛异常还是返回错误码结构？
4) 日志与可观测性：请求/响应日志放在哪层（infra 还是 app/policy）？
5) tools_json 的生成位置：OpenAI client 内部 set、wiring 注入、还是 tool registry 动态生成？
