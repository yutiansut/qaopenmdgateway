/////////////////////////////////////////////////////////////////////////
///@file subscription_dispatcher.h
///@brief	全局订阅分发器
///@copyright	QuantAxis版权所有
/////////////////////////////////////////////////////////////////////////

#pragma once

#include "multi_ctp_config.h"
#include <memory>
#include <map>
#include <set>
#include <string>
#include <mutex>
#include <functional>
#include <vector>
#include <atomic>
#include <queue>
#include <thread>
#include <chrono>

class MarketDataServer;
class CTPConnection;
class CTPConnectionManager;

// 订阅状态
enum class SubscriptionStatus {
    PENDING = 0,     // 等待订阅
    SUBSCRIBING = 1, // 订阅中
    ACTIVE = 2,      // 已订阅
    FAILED = 3,      // 订阅失败
    CANCELLED = 4    // 已取消
};

// 订阅信息
struct SubscriptionInfo {
    std::string instrument_id;
    std::string assigned_connection_id;
    SubscriptionStatus status;
    std::set<std::string> requesting_sessions;  // 请求该订阅的session列表
    std::chrono::system_clock::time_point created_time;
    std::chrono::system_clock::time_point last_update_time;
    int retry_count;
    
    SubscriptionInfo(const std::string& inst_id) 
        : instrument_id(inst_id)
        , status(SubscriptionStatus::PENDING)
        , created_time(std::chrono::system_clock::now())
        , last_update_time(std::chrono::system_clock::now())
        , retry_count(0) {}
};


// 全局订阅分发器
class SubscriptionDispatcher
{
public:
    explicit SubscriptionDispatcher(MarketDataServer* server);
    ~SubscriptionDispatcher();
    
    // 初始化
    bool initialize(CTPConnectionManager* connection_manager);
    void shutdown();
    
    // 订阅管理
    bool add_subscription(const std::string& session_id, const std::string& instrument_id);
    bool remove_subscription(const std::string& session_id, const std::string& instrument_id);
    void remove_all_subscriptions_for_session(const std::string& session_id);
    
    // 订阅状态查询
    std::vector<std::string> get_subscriptions_for_session(const std::string& session_id);
    std::vector<std::string> get_sessions_for_instrument(const std::string& instrument_id);
    SubscriptionStatus get_subscription_status(const std::string& instrument_id);
    size_t get_total_subscriptions() const;
    
    // 负载均衡策略
    void set_load_balance_strategy(LoadBalanceStrategy strategy);
    LoadBalanceStrategy get_load_balance_strategy() const { return load_balance_strategy_; }
    
    // 故障转移
    void handle_connection_failure(const std::string& connection_id);
    void handle_connection_recovery(const std::string& connection_id);
    
    // 订阅状态回调（由CTPConnection调用）
    void on_subscription_success(const std::string& connection_id, const std::string& instrument_id);
    void on_subscription_failed(const std::string& connection_id, const std::string& instrument_id);
    void on_unsubscription_success(const std::string& connection_id, const std::string& instrument_id);
    
    // 行情数据分发（由CTPConnection调用）
    void on_market_data(const std::string& connection_id, 
                       const std::string& instrument_id, 
                       const std::string& json_data);
    
    // 统计信息
    struct Statistics {
        size_t total_instruments;
        size_t active_subscriptions;
        size_t pending_subscriptions;
        size_t failed_subscriptions;
        std::map<std::string, size_t> connection_distribution;
        size_t total_sessions;
    };
    Statistics get_statistics() const;
    
    // 监控和维护
    void start_maintenance_timer();
    void stop_maintenance_timer();
    
private:
    // 负载均衡算法实现
    std::shared_ptr<CTPConnection> select_connection_round_robin();
    std::shared_ptr<CTPConnection> select_connection_least_connections();
    std::shared_ptr<CTPConnection> select_connection_by_quality();
    std::shared_ptr<CTPConnection> select_connection_by_hash(const std::string& instrument_id);
    
    // 订阅处理
    bool execute_subscription(const std::string& instrument_id, const std::string& connection_id);
    bool execute_unsubscription(const std::string& instrument_id, const std::string& connection_id);
    void process_pending_subscriptions();
    void retry_failed_subscriptions();
    
    // 故障转移处理
    void migrate_subscriptions(const std::string& failed_connection_id, 
                             const std::string& target_connection_id);
    void migrate_subscription(const std::string& instrument_id, 
                            const std::string& from_connection_id, 
                            const std::string& to_connection_id);
    
    // 连接评分
    int calculate_connection_score(std::shared_ptr<CTPConnection> connection);
    
    // 维护任务
    void maintenance_task();
    void cleanup_expired_subscriptions();
    
    MarketDataServer* server_;
    CTPConnectionManager* connection_manager_;
    
    // 订阅数据结构
    std::map<std::string, std::shared_ptr<SubscriptionInfo>> global_subscriptions_;  // instrument_id -> SubscriptionInfo
    std::map<std::string, std::set<std::string>> session_subscriptions_;             // session_id -> instrument_ids
    std::map<std::string, std::set<std::string>> connection_subscriptions_;          // connection_id -> instrument_ids
    
    // 负载均衡
    LoadBalanceStrategy load_balance_strategy_;
    std::atomic<size_t> round_robin_counter_;
    
    // 线程安全
    mutable std::mutex subscriptions_mutex_;
    mutable std::mutex sessions_mutex_;
    mutable std::mutex connections_mutex_;
    
    // 维护定时器
    std::unique_ptr<std::thread> maintenance_thread_;
    std::atomic<bool> maintenance_running_;
    std::chrono::seconds maintenance_interval_;
    
    // 重试机制
    std::queue<std::string> retry_queue_;
    std::mutex retry_queue_mutex_;
    int max_retry_count_;
    
    // 统计数据
    std::atomic<size_t> total_subscriptions_processed_;
    std::atomic<size_t> successful_subscriptions_;
    std::atomic<size_t> failed_subscriptions_;
};