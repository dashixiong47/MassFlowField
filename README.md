# FlowField 插件文档

UE5 流场寻路 + MassAI 攻击系统插件。

---

## 目录

1. [攻击系统概览](#攻击系统概览)
2. [发起攻击（蓝图）](#发起攻击蓝图)
3. [攻击类型详解](#攻击类型详解)
4. [效果参数（FFlowFieldEffectParams）](#效果参数)
5. [内置效果说明](#内置效果说明)
6. [自定义效果扩展](#自定义效果扩展)
7. [事件委托](#事件委托)
8. [死亡管理](#死亡管理)
9. [调试绘制](#调试绘制)
10. [网络复制扩展](#网络复制扩展)

---

## 攻击系统概览

攻击系统由三层构成：

```
蓝图 / C++ 调用者
      ↓  FireProjectile / FireLaser / FireChain / FireExplosion
UFlowFieldSubsystem  （转发给 FlowFieldActor 上的组件）
      ↓
UFlowFieldAttackComponent  （管理活跃攻击队列、DoT、死亡队列）
      ↓  每帧由 MassProcessor 驱动
UFlowFieldAttackProcessor  （命中检测、效果施加、空间哈希缓存）
```

**攻击方式**（形状）和**效果**（伤害/状态）完全解耦：

- 攻击方式配置：`FFlowFieldProjectileConfig` / `FFlowFieldLaserConfig` / `FFlowFieldChainConfig` / `FFlowFieldExplosionConfig`
- 效果配置：每个攻击配置内都包含一个 `FFlowFieldEffectParams Effects` 字段

---

## 发起攻击（蓝图）

所有 Fire 函数通过 **FlowFieldSubsystem** 或直接在 **FlowFieldActor.AttackComp** 上调用均可。

| 蓝图节点 | 说明 |
|---|---|
| 发射飞行体（子弹） | 沿直线飞向目标，支持散弹扇形 |
| 发射激光（直线/扇形） | 瞬发直线或扇形多射线 |
| 发射连锁激光 | 先命中最近目标，再跳跃到附近目标 |
| 触发爆炸 | 瞬发范围伤害 |
| 取消攻击 | 根据 AttackId 提前终止 |

所有 Fire 函数返回 `AttackId`（int32），可用于后续取消或在事件委托中匹配。

---

## 攻击类型详解

### 飞行体（Projectile）

```
FFlowFieldProjectileConfig
├── 发射起点 Origin
├── 目标位置 Target
├── 命中半径 HitRadius      = 60 cm
├── 飞行时间 TravelTime     = 0（0=由速度自动算）
├── 飞行速度 ProjectileSpeed = 1000 cm/s
├── 弹数 RayCount           = 1（>1 = 散弹扇形）
├── 散射角度 FanAngle        = 30°（RayCount>1 生效）
├── 穿透 bPiercing           = false
└── 效果 Effects
```

**散弹用法**：将 `RayCount` 设为 5~8，`FanAngle` 设为 30~60°，即可实现霰弹枪效果。每颗弹独立返回 AttackId，`FireProjectile` 返回第一颗的 ID。

飞行体逐帧在 `Origin → Target` 间插值移动，到达终点或命中后销毁（非穿透），穿透弹到达终点才销毁。

---

### 激光（Laser）

```
FFlowFieldLaserConfig
├── 发射起点 Origin
├── 方向目标点 Target       （决定射线方向，不是终点）
├── 最大射程 MaxRange        = 3000 cm
├── 命中半径 HitRadius       = 60 cm
├── 视觉持续时间 VisualDuration = 0.15 s
├── 射线数量 RayCount        = 1（>1 = 扇形）
├── 扇形角度 FanAngle        = 60°（RayCount>1 生效）
├── 穿透 bPiercing           = false
└── 效果 Effects
```

激光是**瞬发**的：调用 `FireLaser` 后当帧立即完成命中检测，再持续 `VisualDuration` 秒以供 Debug 绘制。

非穿透模式下每条射线只命中最近目标；穿透模式下命中射线路径上所有目标。

---

### 连锁激光（Chain）

```
FFlowFieldChainConfig
├── 发射起点 Origin
├── 第一跳目标点 Target
├── 第一跳最大射程 MaxRange  = 3000 cm
├── 命中半径 HitRadius       = 60 cm
├── 跳跃搜索半径 ChainRadius = 400 cm
├── 最大跳数 MaxChainCount   = 3（含第一跳）
├── 视觉持续时间 VisualDuration = 0.15 s
└── 效果 Effects
```

流程：先在 `Origin→Target` 方向上找最近目标（第一跳），然后在该目标 `ChainRadius` 范围内找最近未命中目标（第二跳）……直到达到 `MaxChainCount` 或找不到新目标。

每一跳都独立触发效果（伤害/DoT/减速/眩晕等）。

---

### 爆炸（Explosion）

```
FFlowFieldExplosionConfig
├── 爆炸中心 Center
├── 爆炸半径 Radius          = 300 cm
├── 视觉持续时间 VisualDuration = 0.15 s
└── 效果 Effects
```

瞬发，对半径内所有实体各触发一次效果。

---

## 效果参数

`FFlowFieldEffectParams` 是所有攻击共用的效果配置，包含以下分组：

### 伤害
| 字段 | 默认值 | 说明 |
|---|---|---|
| 直接伤害 DirectDamage | 10 | 命中时一次性伤害，触发 OnEntityHit 委托 |

### 持续伤害（DoT）
| 字段 | 默认值 | 说明 |
|---|---|---|
| DoT 每秒伤害 DotDamage | 0 | 0 = 不触发 DoT |
| DoT 持续时间 DotDuration | 0 | 0 = 不触发 DoT |
| DoT 触发间隔 DotInterval | 0.5 s | 每隔多少秒触发一次 OnDoTTick 委托 |

DoT 按实体聚合，同一实体可以叠加多个来源的 DoT。

### 击退
| 字段 | 默认值 | 说明 |
|---|---|---|
| 启用击退 bKnockback | false | |
| 击退力度 KnockbackStrength | 500 cm/s | |
| 击退范围 KnockbackRadius | 0 | 0 = 与命中半径相同 |

### 减速（内置）
| 字段 | 默认值 | 说明 |
|---|---|---|
| 启用减速 bSlow | false | |
| 速度乘数 SlowFactor | 0.5 | 0=全停，0.5=半速，最大 0.99 |
| 减速持续时间 SlowDuration | 2 s | |

同一实体被多次减速命中时，取**最强乘数**和**最长时间**叠加。

### 眩晕（内置）
| 字段 | 默认值 | 说明 |
|---|---|---|
| 启用眩晕 bStun | false | |
| 眩晕持续时间 StunDuration | 1 s | 期间实体完全停止移动 |

眩晕期间实体停止所有主动移动，但仍会进行地面贴合和位置校正。

### 自定义效果列表
见下一节。

---

## 内置效果说明

减速和眩晕由 AttackProcessor 写入 `FFlowFieldAgentFragment`，MovementProcessor 每帧读取：

```
FFlowFieldAgentFragment
├── SlowFactor        — 当前速度乘数（1=正常）
├── SlowTimeRemaining — 减速剩余时间（s）
└── StunTimeRemaining — 眩晕剩余时间（s）
```

AttackProcessor 每帧递减这两个计时器，时间到后自动恢复正常（SlowFactor 重置为 1）。

---

## 自定义效果扩展

### 概念

`FFlowFieldEffectParams.CustomEffects` 是一个 `TArray<FFlowFieldCustomEffectEntry>` 数组。每条记录包含：

| 字段 | 类型 | 说明 |
|---|---|---|
| 效果标识符 TypeId | FName | 字符串标识符，例如 `"Freeze"`、`"Poison"` |
| 效果强度 Value | float | 含义由项目自定义（例如冰冻减速倍率、毒伤量） |
| 持续时间 Duration | float | 0 = 瞬发；含义由项目自定义 |

当攻击命中目标时，插件对 `CustomEffects` 数组的每一条记录广播 **`OnCustomEffect` 委托**，由项目代码决定如何处理。

### 蓝图接入步骤

1. 在关卡蓝图或塔楼蓝图中，获取 `FlowFieldActor` → `AttackComp`
2. 绑定 `OnCustomEffectDelegate`：

```
AttackComp.OnCustomEffect → 绑定事件
    参数：AttackId, EntityId, EntityActor, HitPos, TypeId, Value, Duration

→ Switch on Name (TypeId)
    "Freeze" → 调用自己的冰冻逻辑（例如播放粒子、冻结动画蒙太奇）
    "Poison" → 在实体身上添加中毒 Fragment 或调用游戏系统
    默认     → 忽略
```

### C++ 接入步骤

```cpp
// 在合适的地方（BeginPlay 或初始化时）绑定
if (AFlowFieldActor* FlowActor = ...)
{
    FlowActor->AttackComp->OnCustomEffectDelegate.AddDynamic(
        this, &UMyTowerComponent::HandleCustomEffect);
}

void UMyTowerComponent::HandleCustomEffect(
    int32 AttackId, int32 EntityId, AActor* EntityActor,
    FVector HitPos, FName TypeId, float Value, float Duration)
{
    if (TypeId == "Freeze")
    {
        // 在实体身上添加冰冻 Fragment，或通知动画系统
        // Value 可以是冰冻期间的速度乘数，Duration 是持续时间
    }
    else if (TypeId == "Poison")
    {
        // Value 可以是每秒毒伤，Duration 是持续时间
    }
}
```

### 配置示例（在攻击塔的配置里）

```
FFlowFieldProjectileConfig
└── Effects
    └── CustomEffects[0]
        ├── TypeId   = "Freeze"
        ├── Value    = 0.2        // 冰冻期间速度降至 20%
        └── Duration = 3.0        // 冰冻 3 秒
```

### 与内置效果的区别

| | 内置减速/眩晕 | 自定义效果 |
|---|---|---|
| 实现方式 | 插件直接修改 AgentFragment，MovementProcessor 自动生效 | 仅广播委托，项目代码自行实现 |
| 配置位置 | `Effects.bSlow` / `Effects.bStun` | `Effects.CustomEffects[]` |
| 叠加行为 | 取最强乘数 + 最长时间 | 由项目代码控制 |
| 适用场景 | 标准减速/眩晕 | 冰冻、中毒、燃烧、标记等游戏特有逻辑 |

---

## 事件委托

所有委托在 `UFlowFieldAttackComponent` 上，可在蓝图中绑定。

| 委托 | 参数 | 触发时机 |
|---|---|---|
| `OnEntityHit` | AttackId, EntityId, EntityActor, Damage, HitPos | 实体被命中（直接伤害 > 0） |
| `OnDoTTick` | AttackId, EntityId, EntityActor, Damage | DoT 每次触发间隔 |
| `OnAttackEnd` | AttackId, FinalPos | 攻击结束（到达终点/取消/视觉时间到） |
| `OnEntityDied` | EntityId, EntityActor | 实体被 KillAgent 标记死亡后 |
| `OnEntityDestroyed` | EntityId | 实体被 Mass 销毁前 |
| `OnCustomEffect` | AttackId, EntityId, EntityActor, HitPos, TypeId, Value, Duration | 命中且 CustomEffects 不为空时，每条记录各触发一次 |

**典型用法**：

```
OnEntityHit → 扣血（调用 Actor 上的 TakeDamage 或自定义血量组件）
OnEntityDied → 播放死亡动画、掉落道具
OnEntityDestroyed → 回收对象池、更新击杀计数
OnCustomEffect → 处理冰冻/中毒等特殊效果
```

---

## 死亡管理

```
KillAgent(EntityId)    — 给实体添加 FFlowFieldDeadTag，触发 OnEntityDied
DestroyAgent(EntityId) — 销毁 Mass 实体，触发 OnEntityDestroyed
```

**标准流程**：
1. 在 `OnEntityHit` 里判断实体血量归零 → 调用 `KillAgent`
2. 在 `OnEntityDied` 里播放死亡动画（此时实体仍存在但停止移动）
3. 动画结束后调用 `DestroyAgent`（或设置 `DeathLingerTime` 自动延迟销毁）

`FFlowFieldAgentFragment` 中的死亡相关字段（由 Trait 配置）：

| 字段 | 说明 |
|---|---|
| `bAutoDestroy = true` | 死亡后自动销毁（false = 等待手动调用 DestroyAgent） |
| `DeathLingerTime = 0` | 自动销毁延迟（s），0 = 立即，>0 可播放死亡动画 |

---

## 调试绘制

在 `FlowFieldActor` 的 `AttackComp` 组件详情面板开启：

| 开关 | 效果 |
|---|---|
| 绘制飞行体轨迹 | 黄色线：Origin → Target |
| 绘制飞行体箭头 | 橙色箭头跟随弹丸移动 |
| 绘制激光 | 青色线（直线/扇形），连锁为洋红色 |
| 绘制爆炸范围 | 红色球形和圆圈 |
| 绘制命中点 | 绿色球体标记每个命中位置 |
| 调试绘制距离 | 距离摄像机超过此值的元素不绘制（默认 100m） |

---

## 网络复制扩展

插件提供抽象基类层，项目可扩展自定义复制字段。

### 添加新的复制字段（例如血量百分比）

1. **`FMonsterReplicationFragment`**（服务端权威）— 添加字段
2. **`FMonsterReplicatedAgent`**（网络数据包）— 添加匹配字段
3. **`UMonsterAgentReplicator::ProcessClientReplicationInternal`** — 从 Fragment 复制到 Agent
4. **`TMonsterBubbleHandler::ApplyCustomDataOnSpawn/Change`** — 从 Agent 应用到 Fragment

### 配置（DefaultGame.ini）

```ini
[/Script/FlowField.FlowFieldSettings]
AgentReplicatorClass=/Script/BaseDefenders.MonsterAgentReplicator
ClientBubbleInfoClass=/Script/BaseDefenders.MonsterClientBubbleInfo
```

详见 `CLAUDE.md` 中的网络复制架构说明。
