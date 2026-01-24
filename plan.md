# Plan 工具设计（讨论稿 v5）

> v5 收敛目标：
> - Plan 持久化使用 JSON，但 **JSON 不保存任何任务 id/编号**。
> - 任务定位完全依赖“渲染时按位置生成的标号（1/1.2/1.3.2）”。
> - JSON 用 `active: true` 表示当前任务；工具保证 active 规则成立。
> - LLM 不能整体替换 plan，只能 replan 某个任务的子任务列表。

---

## 0. 核心共识

1) Plan 持久化采用 **JSON**（工具内部权威状态）。
2) 每轮把“Plan 的全量 Markdown 视图”注入到 system prompt（只读视图）。
3) LLM **不能**用 Markdown 覆盖/整体替换 Plan，只能通过 plan 工具的结构化操作修改 Plan。
4) 任务没有任何 id；渲染 Markdown 时按树结构生成标号（`1 / 1.2 / 1.3.2`）。
5) 当前任务必须是叶子；展示上：当前任务及其父链均标记为 CURRENT。
6) complete = 物理删除节点：
   - 一级任务 complete 后立刻删除；
   - 非一级任务删除节点本身（兄弟/父任务保留）；
   - 父任务 complete 或 replan_children 时，其子树会被整体删除。
7) history 为纯文本列表：
   - 节点删除时其 history 一并删除。
8) 不需要全局 goal；每个任务都有自己的 goal。
9) LLM 可以暂时切换去做其他任务；原因用 history 记录即可（不引入 blocked/defer 状态机）。
10) active 节点如果因 complete/replan 被删除或落入被替换子树中：工具自动迁移 active 到“最近的可选叶子”（规则 B）。

---

## 1. JSON 数据模型（持久化权威状态）

文件：`<storage_dir>/plan.json`

```json
{
  "version": 1,
  "tasks": [
    {
      "goal": "...",
      "title": "...",
      "history": ["..."],
      "children": [
        {
          "goal": "...",
          "title": "...",
          "active": true,
          "history": [],
          "children": []
        }
      ]
    }
  ]
}
```

约束（由工具校验/维护）：
- 全树 `active:true` 的节点数量必须是 0 或 1（缺省视为 false）。
- 若存在 active 节点，则它必须是叶子（`children` 为空）。

> 注：JSON 不保存编号；编号是渲染视图的派生结果。

---

## 2. Markdown 渲染视图（每轮全量注入）

### 2.1 标号语义（非常重要）
- 标号是“地址”，不是“身份”。
- `1.2.1` 永远表示“当前树结构下：第 1 个根任务 → 第 2 个子任务 → 第 1 个子任务”。
- 一旦树发生修改（complete/replan），后续渲染的标号会重新计算，指向可能变化。
- LLM 只能以“本轮注入的最新标号”为准。

### 2.2 渲染规则
- 标号：按位置生成 `1/1.2/1.2.1`。
- active 叶子：标题加粗 `**...**`。
- active 父链：父节点标题同样加粗（父链均用粗体标记）。
- complete 展示删除线（`~~title~~`）。
  - 已完成任务在视图中仅展示一行标题（不展示 goal/history/children）。
  - 说明：节点在 JSON 中会被删除；删除线仅用于“渲染视图”展示最近完成的任务（见 2.3）。
- 未完成任务的 history 展示在任务下。

示例：

```markdown
## Tasks
1. **任务A**
   - goal: ...
   1.1 ~~子任务A1~~
   1.2 **子任务A2**
       - goal: ...
       1.2.1 **子任务A2-1**
             - goal: ...
2. 任务B
   - goal: ...
```

---

## 3. PlanTool API（LLM 只能用位置标号操作）

> 所有 API 都以 `no`（位置标号）定位任务。

### 3.1 一级任务管理（只能 add/complete）
- `plan.add({ parent_no?, goal, title, after_no? })`
  - 添加一个一级任务。
  - `after_no` 可选：将新任务插入到指定一级任务之后；缺省追加到末尾。

- `plan.complete({ no })`
  - 若 no 为一级任务：删除该一级任务。

### 3.2 非一级任务管理（只能 add/replan 间接改变）
- `plan.add({ parent_no, goal, title, after_no? })`
  - 在 parent_no 的 children 中添加一个子任务。
  - `after_no` 可选：插入到某个同级任务之后；缺省追加到末尾。

- `plan.replan({ no, new_children: [{ goal, title, children? }], history_line })`
  - 用 new_children 整体替换 no 节点的 children（硬删除旧子树）。

### 3.3 设置当前任务（必须是叶子）
- `plan.active({ no })`
  - 工具根据 no 定位节点：
    - 若不存在：返回错误
    - 若非叶子：返回错误
    - 否则清空全树 active，再设置该节点 active=true

### 3.4 完成任务（物理删除）
- `plan.complete({ no })`
  - 删除规则：
    - 若 no 为一级任务：从根 tasks 删除
    - 否则从父 children 删除
  - 若被删除节点是 active：按“active 迁移规则 B”自动迁移。

### 3.5 重规划：替换子任务列表
- `plan.replan({ no, new_children: [{ goal, title, children? }], history_line })`
  - 语义：整体替换 no 节点的 children。
  - 原 children 子树硬删除（含 history）。
  - 若提供 history_line：追加到父节点（no 节点）的 history。
  - 若 active 位于被替换的旧子树中：按“active 迁移规则 B”自动迁移。

> 注：LLM 不能整体替换 Plan，只能通过本操作替换某个任务的子任务列表。

---

## 4. active 自动迁移规则（B）

当 active 节点因操作失效（被删除，或落入被替换的旧子树）时，工具自动选择新的 active 叶子。

建议选择策略（可实现且可解释）：
1) 优先选择“原 active 的同父兄弟”中，**原位置之后**的第一个叶子（深度优先展开寻找叶子）。
2) 若没有，再选择同父兄弟中“原位置之前”的最近叶子。
3) 若同父没有叶子，则向上回溯到更高父节点，按上述规则寻找“下一个可选叶子”。
4) 若全树没有叶子（tasks 为空或只有非叶子但 children 为空不成立）：清空 active（全 false）。

> 注：active 必须是叶子；如果候选是非叶子，使用 DFS 找到其第一个叶子。

---

## 5. system prompt 行为约束（建议写入）

1) 每轮先阅读注入的 Tasks（全量）。
2) 若没有 active 任务：用 `plan.set_current` 选择一个叶子任务。
3) 每轮优先推进 active；如果暂时无法推进：
   - `plan.append_history({no:<active>, line:"原因..."})`
   - 再 `plan.set_current({no:<另一个叶子>})`
4) 完成任务后调用 `plan.complete`。
5) 需要重规划时调用 `plan.replan_children`。

---

## 6. 重要限制与风险提示

- 因为仅靠位置标号定位，任务结构变化会导致标号漂移。
  - 依赖：每轮全量注入保证 LLM 总能看到最新标号。
  - 同一轮内如果连续调用 plan 工具：LLM 必须在每次调用前以“最新注入/最新理解的树”为准；建议减少同轮多次 plan 操作。
