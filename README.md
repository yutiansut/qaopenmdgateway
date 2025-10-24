# QuantAxis CTP Market Data Gateway (qactpmdgateway)

**作者**: @yutiansut @quantaxis
**版本**: v2.0.0
**最后更新**: 2025-10-24

## 📖 项目概述

基于CTP API的高性能期货行情数据WebSocket服务器，提供实时行情数据分发服务。该项目是QuantAxis交易网关系统的独立行情模块，专门负责期货市场数据的接入、处理和分发。

**核心定位**: 作为 QuantAxis QIFI 体系的市场数据层组件，与 MongoDB QIFI 数据结构无缝集成，为策略层提供实时、高性能的行情数据流。

### 🎯 核心特性

#### 数据接入层
- **🔥 多CTP连接管理**: 支持同时连接90+期货公司，覆盖全国主要期货交易商
- **⚡ 智能负载均衡**: 4种负载均衡策略，支持25000+合约并发订阅
- **🛡️ 故障自动转移**: 连接断开时自动迁移订阅到其他可用连接
- **📊 海量订阅支持**: 单一系统支持数万个合约的实时行情订阅
- **🚀 高性能架构**: 异步I/O + 连接池 + 智能分发，毫秒级延迟

#### 数据分发层
- **📡 WebSocket服务**: 基于Boost.Beast的高性能WebSocket服务器
- **🧠 智能订阅管理**: 增量订阅机制，避免重复CTP订阅，提高系统效率
- **👥 多客户端支持**: 支持多个WebSocket客户端同时连接，精准推送
- **💾 Redis数据缓存**: 集成Redis存储，提供行情数据持久化
- **🔧 灵活配置管理**: JSON配置文件，支持动态添加期货公司连接

#### QIFI体系集成
- **💽 独立共享内存**: 使用专用共享内存段`qamddata`，与主项目解耦
- **🔗 数据结构兼容**: 与QuantAxis QIFI数据结构完全兼容
- **📝 MongoDB同步**: 支持与QIFI MongoDB数据库同步，保持数据一致性
- **🎯 策略层支持**: 为Python/C++策略层提供实时市场数据接口

## 🏗️ 系统架构

### 多CTP连接架构图
```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          多CTP连接管理系统                                  │
│                         (90+期货公司连接池)                                 │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
        ┌─────────────────────────────┼─────────────────────────────┐
        │                             │                             │
┌───────▼─────┐              ┌────────▼─────┐              ┌────────▼─────┐
│  广发期货   │              │   国泰君安   │              │   中信期货   │
│ (9000)      │              │  (7090)      │              │  (66666)     │
│ 600订阅/连接│              │ 600订阅/连接 │              │ 600订阅/连接 │
└─────┬───────┘              └──────┬───────┘              └──────┬───────┘
      │                             │                             │
┌─────▼─────┐              ┌────────▼─────┐              ┌────────▼─────┐
│  海通期货 │              │   华泰期货   │              │   银河期货   │
│ (8000)    │              │  (8080)      │              │  (4040)      │
│ 400订阅/连接│            │ 600订阅/连接 │              │ 350订阅/连接 │
└─────┬───────┘            └──────┬───────┘              └──────┬───────┘
      │                           │                             │
      └─────────────┬─────────────────────────────┬─────────────┘
                    │                             │
            ┌───────▼──────┐              ┌───────▼──────┐
            │ 订阅分发器   │              │ 负载均衡器   │
            │ 25000+合约   │              │ 4种策略      │
            └───────┬──────┘              └──────┬───────┘
                    │                            │
                    └─────────┬──────────────────┘
                              │
                    ┌─────────▼─────────┐
                    │   MarketDataServer│
                    │   核心协调层       │
                    └─────────┬─────────┘
                              │
                ┌─────────────┼─────────────┐
                │             │             │
        ┌───────▼───────┐ ┌──▼───────┐ ┌──▼──────────┐
        │  WebSocket    │ │  Redis   │ │ 共享内存    │
        │  服务器       │ │  缓存层  │ │ (qamddata)  │
        │  (7799)       │ │          │ │             │
        └───────┬───────┘ └──┬───────┘ └──┬──────────┘
                │            │            │
        ┌───────▼────────────▼────────────▼───────┐
        │          数据分发与存储层                │
        │                                          │
        │  • WebSocket实时推送 (JSON格式)          │
        │  • Redis最新行情 (String)                │
        │  • Redis历史Tick (ZSet时间序列)          │
        │  • 共享内存快速访问                      │
        └──────────────────────────────────────────┘
                              │
                ┌─────────────┼─────────────┐
                │             │             │
        ┌───────▼───────┐ ┌──▼───────┐ ┌──▼──────────┐
        │  WebSocket    │ │ Python   │ │   C++       │
        │  客户端       │ │ 策略层   │ │  策略层     │
        └───────────────┘ └──────────┘ └─────────────┘
```

### 核心组件

#### 1. CTPConnectionManager (多连接管理器) 🆕
- **职责**: 管理90+期货公司的CTP连接池
- **关键功能**:
  - 连接池生命周期管理
  - 连接质量监控和评估
  - 故障检测和自动重连
  - 连接负载均衡调度

#### 2. SubscriptionDispatcher (订阅分发器) 🆕
- **职责**: 智能分发订阅请求到最优CTP连接
- **核心算法**:
  - 轮询分发 (Round Robin)
  - 最少连接优先 (Least Connections)  
  - 连接质量优先 (Connection Quality)
  - 哈希分发 (Hash Based)

#### 3. MarketDataServer (主服务器)
- **职责**: 协调多CTP连接、WebSocket服务、订阅管理
- **关键功能**:
  - 多CTP连接协调管理
  - WebSocket会话管理
  - 智能订阅策略实现
  - Redis缓存管理
  - 共享内存管理

#### 4. CTPConnection (单连接处理器) 🆕
- **职责**: 管理单个期货公司的CTP连接
- **关键回调**:
  - `OnFrontConnected()`: CTP前置机连接成功
  - `OnRspUserLogin()`: CTP登录响应
  - `OnRtnDepthMarketData()`: 实时行情数据推送
  - `OnRspSubMarketData()`: 行情订阅响应

#### 5. WebSocketSession (WebSocket连接管理)
- **职责**: 管理单个WebSocket连接的生命周期
- **功能**:
  - 消息解析和路由
  - 连接状态管理
  - 订阅列表维护
  - 异步消息发送

#### 6. RedisClient (Redis缓存客户端) ✅
- **职责**: 行情数据的持久化存储和时间序列管理
- **核心功能**:
  - **实时数据存储**: 每个合约最新行情数据实时更新到Redis (key格式: `instrument_id`)
  - **历史数据存储**: 使用ZSet存储历史tick数据 (key格式: `history:instrument_id`)
  - **时间序列索引**: 使用timestamp_ms作为ZSet的score，支持时间范围查询
  - **自动过期清理**: 历史数据超过10万条时，自动清理2天前的旧数据
  - **连接池管理**: 线程安全的Redis连接管理，支持自动重连
- **数据格式**: JSON格式存储，与WebSocket推送格式完全一致
- **支持操作**:
  - String: `SET/GET/EXISTS/DEL`
  - Hash: `HSET/HGET/HGETALL`
  - ZSet: `ZADD/ZCARD/ZREMRANGEBYSCORE` (用于时间序列)

### 技术栈
- **C++17**: 现代C++特性，智能指针、移动语义
- **Boost.Asio**: 异步网络I/O框架
- **Boost.Beast**: HTTP/WebSocket协议实现
- **Boost.Interprocess**: 跨进程共享内存
- **RapidJSON**: 高性能JSON解析库
- **CTP API**: 上期技术期货交易API
- **Hiredis**: Redis C客户端库，支持连接池和线程安全

## 📋 技术路线

### Phase 1: 基础架构设计 ✅
- [x] 项目结构规划
- [x] 依赖管理和编译系统(Makefile)
- [x] 核心类设计(MarketDataServer, WebSocketSession, MarketDataSpi)
- [x] CTP API集成框架

### Phase 2: CTP连接实现 ✅
- [x] CTP前置机连接管理
- [x] 登录认证流程(无需用户名密码)
- [x] 连接状态监控和重连机制
- [x] 行情订阅/取消订阅API封装

### Phase 3: WebSocket服务实现 ✅
- [x] WebSocket服务器搭建
- [x] 连接会话管理
- [x] JSON消息协议设计
- [x] 消息路由和处理机制

### Phase 4: 智能订阅管理 ✅
- [x] 增量订阅算法实现
- [x] 会话-合约订阅映射管理
- [x] 会话断开自动清理
- [x] CTP订阅优化(避免重复订阅)

### Phase 5: 数据处理和分发 ✅
- [x] 行情数据结构转换
- [x] JSON格式化输出
- [x] 精准推送(只推送给订阅客户端)
- [x] 异步消息队列管理

### Phase 6: 共享内存集成 ✅
- [x] 独立共享内存段创建(`qamddata`)
- [x] 与主项目数据结构兼容性
- [x] 内存管理和清理机制

### Phase 7: 系统优化和稳定性 ✅
- [x] 线程安全保障
- [x] 错误处理和恢复机制
- [x] 日志系统完善
- [x] 内存泄漏防护

### Phase 8: 部署和运维支持 ✅
- [x] 命令行参数解析
- [x] 配置管理
- [x] 系统集成脚本
- [x] 故障诊断工具

## 🚀 快速开始

### 系统要求
- **操作系统**: Linux (推荐Ubuntu 18.04+)
- **编译器**: GCC 7.0+ (支持C++17)
- **内存**: 至少512MB可用内存
- **网络**: 能访问CTP前置机的网络环境

### 依赖安装
```bash
# Ubuntu/Debian系统
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    libboost-all-dev \
    libssl-dev \
    libcurl4-openssl-dev \
    rapidjson-dev \
    libhiredis-dev \
    redis-server

# 启动Redis服务
sudo systemctl start redis-server
sudo systemctl enable redis-server

# 验证Redis安装
redis-cli ping  # 应返回 PONG
```

### 编译部署
```bash
# 1. 进入项目目录
cd qactpmdgateway

# 2. 检查系统依赖
make check-deps

# 3. 编译项目
make all

# 4. 验证编译结果
ls -la bin/market_data_server

# 5. 安装到系统(可选)
sudo make install
```

### 启动服务

#### 🔥 多CTP连接模式 (推荐)
```bash
# 使用默认多CTP配置 (90个期货公司)
./bin/market_data_server --multi-ctp

# 使用自定义配置文件
./bin/market_data_server --config config/multi_ctp_config.json

# 指定负载均衡策略
./bin/market_data_server --multi-ctp --strategy round_robin

# 检查连接状态
./bin/market_data_server --multi-ctp --status

# 后台运行多CTP模式
nohup ./bin/market_data_server --multi-ctp > logs/server.log 2>&1 &
```

#### 传统单CTP模式 (兼容性)
```bash
# 使用SimNow模拟环境(默认配置)
./bin/market_data_server

# 使用自定义单CTP配置
./bin/market_data_server \
  --front-addr tcp://182.254.243.31:30011 \
  --broker-id 9999 \
  --port 7799
```

## 🔧 多CTP配置管理

### 配置文件结构
```json
{
  "websocket_port": 7799,
  "redis_host": "192.168.2.27",   // Redis服务器地址
  "redis_port": 6379,              // Redis端口
  "load_balance_strategy": "connection_quality",
  "health_check_interval": 30,
  "maintenance_interval": 60,
  "max_retry_count": 3,
  "auto_failover": true,
  "connections": [
    {
      "connection_id": "guangfa_telecom",
      "front_addr": "tcp://101.230.102.231:51213",
      "broker_id": "9000",
      "max_subscriptions": 600,
      "priority": 1,
      "enabled": true
    }
  ]
}
```

### 负载均衡策略

#### 1. Connection Quality (连接质量优先) 🌟
- **策略**: 根据连接质量评分选择最优连接
- **评分因素**: 延迟、错误率、连接稳定性
- **适用场景**: 追求最优行情质量的生产环境

#### 2. Round Robin (轮询分发)
- **策略**: 按顺序轮流分配订阅到各个连接
- **特点**: 负载分布均匀，简单可靠
- **适用场景**: 连接质量相近的环境

#### 3. Least Connections (最少连接优先)
- **策略**: 优先选择当前订阅数量最少的连接
- **特点**: 动态负载均衡，避免单点过载
- **适用场景**: 订阅数量动态变化的场景

#### 4. Hash Based (哈希分发)
- **策略**: 根据合约ID哈希值分配到固定连接
- **特点**: 相同合约总是路由到相同连接
- **适用场景**: 需要数据一致性的场景

### 期货公司配置覆盖 (90家)

| 期货公司 | Broker ID | 订阅容量 | 网络 | 状态 |
|---------|-----------|---------|------|------|
| 上期技术 SimNow | 9999 | 500 | 电信 | ✅ |
| 广发期货 | 9000 | 600 | 电信/联通 | ✅ |
| 国泰君安期货 | 7090 | 600 | 电信/联通 | ✅ |
| 华泰期货 | 8080 | 600 | 电信/网通 | ✅ |
| 海通期货 | 8000 | 400 | 电信/联通 | ✅ |
| 中信期货 | 66666 | 600 | 电信/联通 | ✅ |
| 银河期货 | 4040 | 350 | 电信/联通 | ✅ |
| 一德期货 | 5060 | 800 | 电信/联通 | ✅ |
| 方正中期期货 | 0034 | 400 | 电信/联通 | ✅ |
| 光大期货 | 6000 | 400 | 电信/联通 | ✅ |
| ... | ... | ... | ... | ... |
| **总计** | **90家** | **约25000** | **全网覆盖** | **✅** |

### 动态配置管理
```bash
# 验证配置文件
./bin/market_data_server --config config/multi_ctp_config.json --status

# 测试特定期货公司连接
./bin/market_data_server --config config/test_single_broker.json

# 使用broker解析工具生成配置
python3 broker_parser.py  # 解析broker.xml生成broker_data.json
```

### 🛠️ 配置生成工具

#### Broker XML解析器
项目提供了`broker_parser.py`工具，用于从标准的broker.xml文件自动生成配置：

```bash
# 解析broker.xml文件
python3 broker_parser.py

# 输出格式化的broker信息
# 自动生成broker_data.json文件
# 包含90+期货公司的完整连接信息
```

**broker_parser.py功能特点：**
- 📋 解析标准broker.xml格式文件
- 🔄 自动提取BrokerID和MarketData地址
- 📊 生成结构化JSON配置数据
- 📈 统计各期货公司的连接数量
- 🌏 支持中文broker名称显示

**使用示例：**
```bash
正在解析broker.xml文件...
找到 90 个broker

统计信息:
BrokerID: 9999, 名称: 上期技术, MarketData地址数量: 3
BrokerID: 9000, 名称: 广发期货, MarketData地址数量: 2
BrokerID: 7090, 名称: 国泰君安期货, MarketData地址数量: 2
...

结果已保存到: broker_data.json
```

## 📡 WebSocket API详细说明

### 连接端点
```
ws://hostname:7799/
```

### 消息协议

系统支持两种WebSocket协议格式，完全兼容：

---

## 🆕 协议一：aid格式（推荐，兼容QuantAxis QIFI体系）

### 1. 订阅行情数据
**请求格式:**
```json
{
  "aid": "subscribe_quote",
  "ins_list": "rb2501,i2501,au2512"
}
```

**响应格式:**
```json
{
  "aid": "subscribe_quote",
  "status": "ok"
}
```

**说明:**
- `ins_list`: 逗号分隔的合约列表
- 支持交易所前缀（如 `SHFE.rb2501`），系统会自动去除前缀
- 兼容QuantAxis mdservice协议

### 2. 获取行情数据（长轮询）
**请求格式:**
```json
{
  "aid": "peek_message"
}
```

**响应格式（完整数据）:**
```json
{
  "aid": "rtn_data",
  "data": [
    {
      "quotes": {
        "rb2501": {
          "instrument_id": "rb2501",
          "datetime": "2025-10-24 14:30:15.500",
          "last_price": 4180.0,
          "volume": 12580,
          "ask_price1": 4180.0,
          "ask_volume1": 8,
          "bid_price1": 4179.0,
          "bid_volume1": 10,
          "open_interest": 156890.0,
          "...": "..."
        },
        "i2501": {
          "...": "..."
        }
      }
    },
    {
      "account_id": "",
      "ins_list": "",
      "mdhis_more_data": false
    }
  ]
}
```

**响应格式（增量数据）:**
```json
{
  "aid": "rtn_data",
  "data": [
    {
      "quotes": {
        "rb2501": {
          "last_price": 4181.0,
          "volume": 12590
        }
      }
    },
    {
      "account_id": "",
      "ins_list": "",
      "mdhis_more_data": false
    }
  ]
}
```

**说明:**
- `peek_message` 实现长轮询机制
- 首次请求返回所有订阅合约的完整数据
- 后续请求只返回发生变化的字段（diff机制）
- 如果没有数据变化，服务器会挂起请求，直到有新数据才返回
- 实现了高效的增量推送机制

---

## 协议二：action格式（兼容性协议）

### 1. 订阅行情数据
**请求格式:**
```json
{
  "action": "subscribe",
  "instruments": ["rb2501", "i2501", "au2412"]
}
```

**响应格式:**
```json
{
  "type": "subscribe_response",
  "status": "success",
  "subscribed_count": 3
}
```

### 2. 取消订阅
**请求格式:**
```json
{
  "action": "unsubscribe",
  "instruments": ["rb2501"]
}
```

**响应格式:**
```json
{
  "type": "unsubscribe_response",
  "status": "success",
  "subscribed_count": 2
}
```

### 3. 查询合约列表
**请求格式:**
```json
{
  "action": "list_instruments"
}
```

**响应格式:**
```json
{
  "type": "instrument_list",
  "instruments": ["rb2501", "rb2502", "i2501", "..."],
  "total_count": 1250
}
```

### 4. 搜索合约
**请求格式:**
```json
{
  "action": "search",
  "pattern": "rb"
}
```

**响应格式:**
```json
{
  "type": "search_result",
  "pattern": "rb",
  "instruments": ["rb2501", "rb2502", "rb2503"],
  "match_count": 3
}
```

### 5. 实时行情数据（自动推送）
**推送格式:**
```json
{
  "type": "market_data",
  "instrument_id": "rb2501",
  "trading_day": "20231201",
  "update_time": "09:30:15",
  "update_millisec": 500,
  "last_price": 4180.0,
  "pre_settlement_price": 4170.0,
  "pre_close_price": 4175.0,
  "pre_open_interest": 155000.0,
  "open_price": 4175.0,
  "highest_price": 4185.0,
  "lowest_price": 4172.0,
  "volume": 12580,
  "turnover": 525490000.0,
  "open_interest": 156890.0,
  "close_price": 4178.0,
  "settlement_price": 4179.0,
  "upper_limit_price": 4587.0,
  "lower_limit_price": 3753.0,
  "pre_delta": 0.0,
  "curr_delta": 0.0,
  "bid_price1": 4179.0,
  "bid_volume1": 10,
  "ask_price1": 4180.0,
  "ask_volume1": 8,
  "bid_price2": 4178.0,
  "bid_volume2": 15,
  "ask_price2": 4181.0,
  "ask_volume2": 12,
  "average_price": 4177.5,
  "action_day": "20231201",
  "timestamp": 1701398415500
}
```

### 协议使用建议

| 协议类型 | 推荐场景 | 优势 |
|---------|---------|------|
| **aid协议** | QuantAxis策略、QIFI体系集成 | 增量推送、长轮询、兼容mdservice |
| **action协议** | 第三方客户端、实时推送场景 | 简单直观、主动推送 |

### Python客户端示例

#### aid协议示例（长轮询模式）
```python
import asyncio
import websockets
import json

async def aid_protocol_client():
    """使用aid协议的长轮询客户端"""
    uri = "ws://localhost:7799"

    async with websockets.connect(uri) as websocket:
        # 1. 订阅合约
        subscribe_msg = {
            "aid": "subscribe_quote",
            "ins_list": "rb2501,i2501,au2512"
        }
        await websocket.send(json.dumps(subscribe_msg))
        response = await websocket.recv()
        print(f"订阅响应: {response}")

        # 2. 长轮询获取行情数据
        while True:
            peek_msg = {"aid": "peek_message"}
            await websocket.send(json.dumps(peek_msg))

            # 服务器会在有数据变化时才返回（长轮询）
            response = await websocket.recv()
            data = json.loads(response)

            if data.get("aid") == "rtn_data":
                quotes = data["data"][0]["quotes"]
                for instrument_id, quote in quotes.items():
                    print(f"{instrument_id}: {quote.get('last_price', 'N/A')}")

# 运行
asyncio.run(aid_protocol_client())
```

#### action协议示例（主动推送模式）
```python
import asyncio
import websockets
import json

async def action_protocol_client():
    """使用action协议的主动推送客户端"""
    uri = "ws://localhost:7799"

    async with websockets.connect(uri) as websocket:
        # 1. 订阅合约
        subscribe_msg = {
            "action": "subscribe",
            "instruments": ["rb2501", "i2501", "au2512"]
        }
        await websocket.send(json.dumps(subscribe_msg))

        # 2. 接收服务器主动推送的行情数据
        async for message in websocket:
            data = json.loads(message)

            if data.get("type") == "market_data":
                print(f"{data['instrument_id']}: {data['last_price']}")
            elif data.get("type") == "subscribe_response":
                print(f"订阅成功: {data['subscribed_count']} 个合约")

# 运行
asyncio.run(action_protocol_client())
```

## 💾 Redis数据存储与查询

### Redis数据架构

系统将行情数据实时存储到Redis，提供两种数据访问模式：

#### 1. 最新行情数据 (String类型)
```bash
# 数据结构
Key: {instrument_id}           # 例如: "rb2601"
Value: {JSON格式的完整行情数据}
TTL: 永久存储 (每次更新覆盖)

# Redis查询示例
redis-cli get rb2601
redis-cli get i2501
redis-cli exists au2512
```

#### 2. 历史Tick数据 (ZSet类型)
```bash
# 数据结构
Key: history:{instrument_id}   # 例如: "history:rb2601"
Score: timestamp_ms            # 毫秒级时间戳作为排序依据
Member: {JSON格式的完整行情数据}
自动清理: 超过10万条记录时，删除2天前的数据

# Redis查询示例 - 按时间范围查询
# 查询最近100条tick
redis-cli ZREVRANGE history:rb2601 0 99 WITHSCORES

# 按时间范围查询 (timestamp_ms)
redis-cli ZRANGEBYSCORE history:rb2601 1701398400000 1701484800000

# 统计历史数据量
redis-cli ZCARD history:rb2601
```

### Python查询示例

```python
import redis
import json
from datetime import datetime, timedelta

# 连接Redis
r = redis.Redis(host='192.168.2.27', port=6379, decode_responses=True)

# 1. 获取最新行情
def get_latest_quote(instrument_id):
    data = r.get(instrument_id)
    if data:
        return json.loads(data)
    return None

# 2. 获取历史Tick数据
def get_historical_ticks(instrument_id, start_time=None, end_time=None, limit=100):
    """
    获取历史tick数据
    start_time/end_time: datetime对象或None
    limit: 最多返回多少条记录
    """
    key = f"history:{instrument_id}"

    if start_time and end_time:
        # 转换为毫秒时间戳
        start_ms = int(start_time.timestamp() * 1000)
        end_ms = int(end_time.timestamp() * 1000)
        # 按时间范围查询
        results = r.zrangebyscore(key, start_ms, end_ms, withscores=True)
    else:
        # 获取最新N条
        results = r.zrevrange(key, 0, limit-1, withscores=True)

    ticks = []
    for data, timestamp_ms in results:
        tick = json.loads(data)
        tick['redis_timestamp'] = timestamp_ms
        ticks.append(tick)

    return ticks

# 3. 批量获取多个合约的最新行情
def get_multiple_quotes(instrument_ids):
    """批量获取最新行情"""
    pipe = r.pipeline()
    for instrument_id in instrument_ids:
        pipe.get(instrument_id)

    results = pipe.execute()
    quotes = {}
    for i, data in enumerate(results):
        if data:
            quotes[instrument_ids[i]] = json.loads(data)

    return quotes

# 4. 实时监控行情更新 (使用Redis PubSub - 需要额外配置)
def subscribe_market_data(instrument_ids):
    """订阅行情更新通知"""
    pubsub = r.pubsub()
    channels = [f"quote:{inst}" for inst in instrument_ids]
    pubsub.subscribe(*channels)

    for message in pubsub.listen():
        if message['type'] == 'message':
            print(f"收到行情: {message['channel']}")
            data = json.loads(message['data'])
            print(f"  价格: {data['last_price']}")

# 使用示例
if __name__ == "__main__":
    # 获取rb2601最新行情
    quote = get_latest_quote("rb2601")
    if quote:
        print(f"合约: {quote['instrument_id']}")
        print(f"最新价: {quote['last_price']}")
        print(f"时间: {quote['update_time']}")

    # 获取最近1小时的历史数据
    end_time = datetime.now()
    start_time = end_time - timedelta(hours=1)
    ticks = get_historical_ticks("rb2601", start_time, end_time)
    print(f"获取到 {len(ticks)} 条历史tick数据")

    # 批量获取多个合约
    quotes = get_multiple_quotes(["rb2601", "i2501", "au2512"])
    for inst_id, quote in quotes.items():
        print(f"{inst_id}: {quote['last_price']}")
```

### Redis数据监控

```bash
# 监控Redis连接状态
redis-cli INFO clients
redis-cli INFO stats

# 查看所有合约keys
redis-cli KEYS "*" | grep -v "history:"

# 查看历史数据keys
redis-cli KEYS "history:*"

# 监控写入性能
redis-cli MONITOR | grep -E "(SET|ZADD)"

# 检查内存使用
redis-cli INFO memory

# 清理测试数据
redis-cli FLUSHDB  # 谨慎使用！
```

### Redis优化配置建议

```bash
# /etc/redis/redis.conf 优化建议

# 1. 内存配置
maxmemory 4gb
maxmemory-policy allkeys-lru  # 内存不足时使用LRU策略

# 2. 持久化配置 (根据需求选择)
# RDB方式 - 快照持久化
save 900 1
save 300 10
save 60 10000

# AOF方式 - 更安全但性能略低
appendonly yes
appendfsync everysec

# 3. 网络优化
tcp-backlog 511
timeout 300
tcp-keepalive 300

# 4. 性能优化
# 禁用慢查询
slowlog-log-slower-than 10000
slowlog-max-len 128

# 5. 安全配置
bind 192.168.2.27
requirepass your_password  # 建议生产环境设置密码
```

### Redis数据分析工具

项目提供了多个Redis调试工具：

```bash
# 检查Redis连接和数据
python3 check_redis.py

# 调试Redis数据写入
python3 debug_redis.py

# 简单Redis测试
python3 simple_redis_test.py
```

## 🧩 智能订阅管理机制

### 订阅策略
```cpp
// 伪代码示例
void MarketDataServer::subscribe_instrument(session_id, instrument_id) {
    subscribers[instrument_id].insert(session_id);
    
    // 智能CTP订阅: 只有第一个客户端时才向CTP订阅
    if (subscribers[instrument_id].size() == 1) {
        ctp_api->SubscribeMarketData({instrument_id});
        log_info("New CTP subscription: " + instrument_id);
    }
}

void MarketDataServer::unsubscribe_instrument(session_id, instrument_id) {
    subscribers[instrument_id].erase(session_id);
    
    // 智能CTP取消订阅: 没有客户端时才从CTP取消订阅
    if (subscribers[instrument_id].empty()) {
        subscribers.erase(instrument_id);
        ctp_api->UnSubscribeMarketData({instrument_id});
        log_info("CTP subscription removed: " + instrument_id);
    }
}
```

### 优势分析
1. **网络优化**: 减少不必要的CTP订阅请求
2. **资源节约**: 避免CTP端重复处理相同合约
3. **系统稳定**: 降低CTP API调用频率，提高稳定性
4. **成本控制**: 部分CTP接口可能按订阅量计费

## 🔧 开发者指南

### 编译目标
```bash
# 开发调试版本
make debug

# 生产发布版本  
make release

# 清理编译文件
make clean

# 生成文档
make docs

# 运行基础测试
make test
```

### 目录结构
```
qactpmdgateway/
├── src/                    # 源代码目录
│   ├── main.cpp           # 程序入口点
│   ├── market_data_server.h   # 服务器类声明
│   └── market_data_server.cpp # 服务器类实现
├── libs/                   # CTP库文件
│   ├── thostmduserapi_se.so
│   └── thosttraderapi_se.so
├── obj/                    # 编译中间文件
├── bin/                    # 可执行文件输出
├── ctp_flow/              # CTP日志文件
├── Makefile               # 构建配置
└── README.md              # 项目文档
```

### 日志系统
服务器提供三级日志输出：
- **INFO**: 一般信息，如连接建立、订阅状态等
- **WARNING**: 警告信息，如重连尝试、数据异常等  
- **ERROR**: 错误信息，如连接失败、解析错误等

## 🗂️ 目录结构与管理

### CTP Flow目录结构
```
ctpflow/                          # CTP Flow文件统一目录
├── guangfa_telecom/              # 广发期货连接目录
│   ├── DialogRsp.con            # 对话响应文件
│   ├── QueryRsp.con             # 查询响应文件
│   └── TradingDay.con           # 交易日文件
├── guotai_telecom/               # 国泰君安连接目录
├── zhongxin_telecom/             # 中信期货连接目录
├── yide_telecom/                 # 一德期货连接目录
├── ... (90个期货公司目录)
└── single/                       # 单CTP模式专用目录
    ├── DialogRsp.con
    ├── QueryRsp.con
    └── TradingDay.con
```

### 目录管理命令
```bash
# 查看所有CTP连接目录
ls -la ctpflow/

# 查看特定期货公司的日志文件
ls -la ctpflow/guangfa_telecom/

# 监控CTP连接状态
find ctpflow/ -name "*.con" -mmin -5  # 最近5分钟更新的文件

# 清理所有CTP Flow文件
rm -rf ctpflow/

# 备份CTP Flow文件
tar -czf ctpflow_backup_$(date +%Y%m%d_%H%M).tar.gz ctpflow/

# 统计连接数
ls -d ctpflow/*/ | wc -l
```

### 日志监控
```bash
# 实时监控特定连接的活动
watch -n 1 "ls -la ctpflow/guangfa_telecom/ | tail -5"

# 检查连接健康状态
for dir in ctpflow/*/; do
    echo "$(basename "$dir"): $(ls "$dir" 2>/dev/null | wc -l) files"
done

# 查找最活跃的连接
find ctpflow/ -name "*.con" -mmin -1 -exec dirname {} \; | sort | uniq -c | sort -nr
```

## 🔍 故障排除

### 常见问题及解决方案

#### 1. 编译问题
**问题**: `fatal error: boost/asio.hpp: No such file or directory`
```bash
# 解决方案
sudo apt-get install libboost-all-dev
# 或指定版本
sudo apt-get install libboost1.71-all-dev
```

**问题**: CTP库链接错误
```bash
# 解决方案
# 确保CTP库文件存在
ls -la libs/thostmduserapi_se.so libs/thosttraderapi_se.so

# 设置库搜索路径
export LD_LIBRARY_PATH=./libs:$LD_LIBRARY_PATH
```

#### 2. 运行时问题
**问题**: CTP连接失败
```bash
# 检查网络连通性
telnet 182.254.243.31 30011

# 检查防火墙设置
sudo ufw status

# 验证CTP前置机地址
ping 182.254.243.31
```

**问题**: 共享内存权限错误
```bash
# 查看现有共享内存
ls -la /dev/shm/

# 清理旧的共享内存
sudo rm -f /dev/shm/qamddata

# 检查用户权限
id
```

#### 4. 多CTP连接问题 🆕
**问题**: 某些期货公司连接失败
```bash
# 检查特定连接的CTP流文件
ls -la ctpflow/guangfa_telecom/

# 查看连接状态
./bin/market_data_server --config config/multi_ctp_config.json --status

# 测试单个连接
grep -A 20 "guangfa_telecom" config/multi_ctp_config.json
```

**问题**: 订阅分发不均匀
```bash
# 检查各连接订阅数量
tail -f server.log | grep "subscription count"

# 验证负载均衡策略
./bin/market_data_server --multi-ctp --strategy least_connections

# 监控连接质量
watch -n 5 'find ctpflow/ -name "*.con" -mmin -1 | wc -l'
```

**问题**: 连接数过多导致系统资源不足
```bash
# 监控进程资源使用
top -p `pgrep market_data_server`

# 检查打开的文件句柄数
lsof -p `pgrep market_data_server` | wc -l

# 临时禁用部分连接
# 在配置文件中设置 "enabled": false
```

#### 3. WebSocket连接问题
**问题**: 客户端连接被拒绝
```bash
# 检查端口占用
netstat -tlnp | grep 7799

# 检查防火墙规则
sudo ufw allow 7799

# 测试本地连接
curl --include \
     --no-buffer \
     --header "Connection: Upgrade" \
     --header "Upgrade: websocket" \
     --header "Sec-WebSocket-Key: SGVsbG8sIHdvcmxkIQ==" \
     --header "Sec-WebSocket-Version: 13" \
     http://localhost:7799/
```

### 调试工具

#### 1. 系统监控
```bash
# 进程监控
top -p `pgrep market_data_server`

# 内存使用
pmap -x `pgrep market_data_server`

# 网络连接
ss -tlnp | grep market_data_server
```

#### 2. CTP日志
```bash
# CTP流文件位置
ls -la ctp_flow/

# 实时监控CTP日志
tail -f ctp_flow/*.log
```

#### 3. WebSocket测试客户端
```bash
# 使用wscat测试
npm install -g wscat
wscat -c ws://localhost:7799

# 发送订阅消息
> {"action":"subscribe","instruments":["rb2501"]}

# 发送查询消息  
> {"action":"list"}
```

## 📊 性能优化

### 系统参数调优
```bash
# 增加文件描述符限制
ulimit -n 65536

# 调整网络缓冲区
echo 'net.core.rmem_max = 134217728' >> /etc/sysctl.conf
echo 'net.core.wmem_max = 134217728' >> /etc/sysctl.conf
sysctl -p
```

### 编译优化
```bash
# 使用优化编译选项
make release

# 启用特定CPU优化
export CXXFLAGS="-O3 -march=native -mtune=native"
make clean && make all
```

### 🎯 多CTP系统监控指标 🆕
```bash
# 核心性能指标监控脚本
#!/bin/bash

echo "=== QuantAxis 多CTP系统监控报告 ==="
echo "时间: $(date)"
echo

# 1. CTP连接状态
echo "📊 CTP连接状态:"
active_connections=$(ls -d ctpflow/*/ 2>/dev/null | wc -l)
echo "  活跃连接数: $active_connections"

recent_activity=$(find ctpflow/ -name "*.con" -mmin -5 2>/dev/null | wc -l)
echo "  最近活跃: $recent_activity 个文件更新"

# 2. 系统资源使用
echo
echo "💻 系统资源使用:"
pid=$(pgrep market_data_server)
if [ ! -z "$pid" ]; then
    cpu_usage=$(top -bn1 -p $pid | tail -1 | awk '{print $9}')
    mem_usage=$(top -bn1 -p $pid | tail -1 | awk '{print $10}')
    echo "  CPU使用率: ${cpu_usage}%"
    echo "  内存使用率: ${mem_usage}%"
    
    # 文件句柄数
    fd_count=$(lsof -p $pid 2>/dev/null | wc -l)
    echo "  打开文件数: $fd_count"
fi

# 3. 网络连接
echo
echo "🌐 网络连接状态:"
ws_connections=$(ss -tn | grep :7799 | grep ESTABLISHED | wc -l)
echo "  WebSocket连接: $ws_connections"

ctp_connections=$(ss -tn | grep -E ':(10210|10211|51213|61213|41313)' | grep ESTABLISHED | wc -l)
echo "  CTP连接数: $ctp_connections"

# 4. 订阅统计
echo
echo "📈 订阅统计:"
if [ -f server.log ]; then
    total_subs=$(tail -100 server.log | grep -o 'Total subscriptions: [0-9]*' | tail -1 | awk '{print $3}')
    echo "  总订阅数: ${total_subs:-0}"
fi

echo
echo "=== 监控完成 ==="
```

### 监控指标
**基础指标：**
- WebSocket连接数量
- 行情订阅合约数量  
- 内存使用量
- CPU使用率
- 网络I/O吞吐量

**多CTP特有指标：**
- 🔗 活跃CTP连接数 (目标: 90个)
- ⚡ 连接质量评分 (延迟、错误率)
- 📊 订阅分发均匀度
- 🔄 故障转移响应时间
- 📁 CTP Flow文件更新频率
- 🎯 负载均衡效率

## 🤝 与主项目集成

### 数据结构兼容性
本项目严格遵循主项目的数据结构定义：

```cpp
// 引用主项目类型定义
#include "../../open-trade-common/types.h"

// 使用相同的Instrument结构
typedef boost::interprocess::allocator<
    std::pair<const InsIdType, Instrument>,
    boost::interprocess::managed_shared_memory::segment_manager> ShmemAllocator;
    
typedef boost::interprocess::map<
    InsIdType, Instrument, CharArrayComparer, ShmemAllocator> InsMapType;
```

### 共享内存策略
- **独立性**: 使用专用共享内存段`qamddata`，避免与主项目冲突
- **兼容性**: 数据结构与主项目保持一致，便于后续集成
- **扩展性**: 预留接口，支持将来与主项目共享内存合并

### 生产部署建议 🆕

#### 🏭 生产环境配置
```bash
# 1. 系统资源优化
# 增加文件描述符限制
echo "* soft nofile 65536" >> /etc/security/limits.conf
echo "* hard nofile 65536" >> /etc/security/limits.conf

# 优化网络参数
echo 'net.core.rmem_max = 268435456' >> /etc/sysctl.conf
echo 'net.core.wmem_max = 268435456' >> /etc/sysctl.conf
echo 'net.core.netdev_max_backlog = 5000' >> /etc/sysctl.conf
sysctl -p

# 2. 目录权限和结构
mkdir -p /opt/quantaxis/qactpmdgateway/{config,logs,ctpflow,backup}
chown -R quantaxis:quantaxis /opt/quantaxis/

# 3. 生产配置文件
cp config/multi_ctp_config.json /opt/quantaxis/qactpmdgateway/config/production.json
# 根据实际环境调整配置参数
```

#### 🔄 服务管理
```bash
# systemd服务配置文件 (/etc/systemd/system/qactpmd.service)
[Unit]
Description=QuantAxis Multi-CTP Market Data Gateway
After=network.target

[Service]
Type=simple
User=quantaxis
WorkingDirectory=/opt/quantaxis/qactpmdgateway
ExecStart=/opt/quantaxis/qactpmdgateway/bin/market_data_server --config /opt/quantaxis/qactpmdgateway/config/production.json
Restart=always
RestartSec=5
StandardOutput=syslog
StandardError=syslog
SyslogIdentifier=qactpmd

[Install]
WantedBy=multi-user.target

# 启用和管理服务
sudo systemctl enable qactpmd
sudo systemctl start qactpmd
sudo systemctl status qactpmd
```

#### 📊 监控和告警
```bash
# 监控脚本 (monitor_multi_ctp.sh)
#!/bin/bash
ALERT_EMAIL="admin@company.com"
LOG_FILE="/var/log/qactpmd_monitor.log"

# 检查服务状态
if ! systemctl is-active qactpmd >/dev/null; then
    echo "$(date): Service down, restarting..." >> $LOG_FILE
    systemctl restart qactpmd
    echo "Multi-CTP service restarted" | mail -s "Alert: Service Restart" $ALERT_EMAIL
fi

# 检查连接数
active_conns=$(ls -d /opt/quantaxis/qactpmdgateway/ctpflow/*/ 2>/dev/null | wc -l)
if [ $active_conns -lt 50 ]; then
    echo "$(date): Low connection count: $active_conns" >> $LOG_FILE
    echo "CTP connections below threshold: $active_conns" | mail -s "Alert: Low Connections" $ALERT_EMAIL
fi

# 添加到crontab
# */5 * * * * /opt/quantaxis/scripts/monitor_multi_ctp.sh
```

#### 💾 备份策略
```bash
# 每日备份脚本
#!/bin/bash
BACKUP_DIR="/backup/qactpmd/$(date +%Y%m%d)"
mkdir -p $BACKUP_DIR

# 备份配置文件
cp -r /opt/quantaxis/qactpmdgateway/config $BACKUP_DIR/

# 备份CTP Flow文件
tar -czf $BACKUP_DIR/ctpflow_$(date +%H%M).tar.gz -C /opt/quantaxis/qactpmdgateway ctpflow/

# 清理30天前的备份
find /backup/qactpmd/ -type d -mtime +30 -exec rm -rf {} \;
```

### 部署建议
```bash
# 建议的启动顺序
# 1. 启动Redis服务
sudo systemctl start redis-server

# 2. 启动多CTP网关服务  
sudo systemctl start qactpmd

# 3. 验证服务状态
systemctl status qactpmd
./bin/market_data_server --config config/production.json --status

# 4. 验证WebSocket连接
curl --include --no-buffer --header "Connection: Upgrade" \
     --header "Upgrade: websocket" \
     --header "Sec-WebSocket-Key: SGVsbG8sIHdvcmxkIQ==" \
     --header "Sec-WebSocket-Version: 13" \
     http://localhost:7799/
```

## 📈 扩展规划

### 短期规划
- [ ] 支持更多交易所(中金所、大商所、郑商所)
- [ ] 增加行情数据持久化功能
- [ ] 实现WebSocket客户端认证机制
- [ ] 添加行情数据压缩传输

### 中期规划  
- [ ] 支持Level-2深度行情
- [ ] 集成技术指标计算
- [ ] 实现行情数据回放功能
- [ ] 添加监控面板和管理界面

### 长期规划
- [ ] 与主项目完全集成
- [ ] 支持分布式部署
- [ ] 实现行情数据分析引擎
- [ ] 提供REST API接口

## 📝 开发日志

### v1.0.0 (2023-12-01) ✅
- 完成基础架构设计和实现
- CTP API集成完成
- WebSocket服务器实现
- 智能订阅管理机制
- 独立共享内存管理
- 完整的错误处理机制

### v2.0.0 (2025-10-24) ✅ 🔥
- **多CTP连接架构** - 支持90+期货公司同时连接
- **智能负载均衡** - 4种分发策略，25000+合约并发
- **故障自动转移** - 连接断开自动迁移订阅
- **Redis数据缓存** - 完整实现，支持String最新行情 + ZSet历史tick数据
  - 实时数据存储（String）
  - 历史时间序列（ZSet，10万条自动清理2天前数据）
  - 线程安全连接池
- **双协议支持** - 同时支持aid协议（QIFI体系）和action协议（兼容性）
  - aid协议：长轮询 + 增量diff推送机制
  - action协议：传统主动推送模式
- **配置化管理** - JSON配置文件，灵活部署
- **压力测试工具** - 多CTP系统专用测试客户端
- **工业级稳定性** - 生产环境高可用架构

### v2.1.0 (计划中)
- Level-2深度行情支持
- 更多交易所接入 (中金所、大商所、郑商所)
- WebSocket客户端认证
- 监控面板和管理界面

## 📄 许可证

Copyright (c) QuantAxis. All rights reserved.

本项目是QuantAxis交易网关系统的一部分，遵循项目整体许可证条款。

---

## 🆘 技术支持

如遇到技术问题，请通过以下方式获取支持：

1. **GitHub Issues**: 提交问题报告和功能请求
2. **技术文档**: 查阅项目Wiki和API文档  
3. **社区交流**: 加入QuantAxis技术交流群
4. **商业支持**: 联系QuantAxis技术支持团队

**联系方式**: support@quantaxis.io

---

## 🧪 Python测试客户端

项目提供了多个Python测试客户端，用于验证WebSocket服务器功能和演示API使用方法。

### 🔥 多CTP压力测试 (新增)
```bash
# 安装Python依赖
./install_python_deps.sh

# 多CTP系统压力测试 (测试71个合约)
python3 test_multi_ctp.py

# 指定服务器地址
python3 test_multi_ctp.py ws://localhost:7799

# 测试功能包括:
# - 大量订阅分发测试 (71个合约)
# - 多连接负载均衡验证
# - 故障转移测试
# - 连接质量监控
```

### 快速测试
```bash
# 快速测试（自动订阅rb2601）
python3 quick_test.py

# 指定服务器地址
python3 quick_test.py ws://localhost:7799
```

### 完整版异步客户端
```bash
# 使用默认配置
python3 test_client.py

# 指定服务器和合约
python3 test_client.py ws://localhost:7799 rb2601,i2501

# 交互模式命令
sub rb2601           # 订阅合约
unsub rb2601         # 取消订阅
list                 # 查询所有合约
search rb            # 搜索合约
status               # 查看状态
help                 # 显示帮助
quit                 # 退出
```

### 简化版同步客户端
```bash
# 适合不支持asyncio的环境
python3 simple_test_client.py

# 指定服务器
python3 simple_test_client.py ws://localhost:7799
```

### 客户端功能对比

| 功能 | quick_test.py | simple_test_client.py | test_client.py | test_multi_ctp.py |
|------|---------------|----------------------|----------------|-------------------|
| 快速测试 | ✅ | ❌ | ❌ | ❌ |
| 交互模式 | ❌ | ✅ | ✅ | ❌ |  
| 异步处理 | ❌ | ❌ | ✅ | ✅ |
| 多CTP压力测试 | ❌ | ❌ | ❌ | ✅ |
| 负载均衡验证 | ❌ | ❌ | ❌ | ✅ |
| 故障转移测试 | ❌ | ❌ | ❌ | ✅ |
| 连接监控 | ❌ | ❌ | ❌ | ✅ |
| 格式化显示 | 简单 | 详细 | 详细 | 统计 |
| 依赖库 | websocket-client | websocket-client | websockets | websockets |
| 适用场景 | 快速验证 | 一般测试 | 生产使用 | 多CTP系统测试 |

### 测试输出示例
```
🚀 【rb2601】实时行情 14:30:15
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
💰 最新价: 4180.00 📈 (+10.00, +0.24%)
📊 昨结算: 4170.00
📈 开盘价: 4175.00 | 最高: 4185.00 | 最低: 4172.00
📋 成交量: 12,580 | 成交额: 525,490,000
🏦 持仓量: 156,890
📏 涨停: 4587.00 | 跌停: 3753.00
📋 买一: 4179.00(10) | 卖一: 4180.00(8)
⏰ 更新时间: 20231201 09:30:15.500
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```