/////////////////////////////////////////////////////////////////////////
///@file ctp_connection_manager.h
///@brief	多CTP连接管理器
///@copyright	QuantAxis版权所有
/////////////////////////////////////////////////////////////////////////

#pragma once

#include "../libs/ThostFtdcMdApi.h"
#include "../include/open-trade-common/types.h"
#include "multi_ctp_config.h"
#include <memory>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <atomic>
#include <mutex>
#include <functional>
#include <thread>
#include <chrono>

class MarketDataServer;
class SubscriptionDispatcher;

// CTP连接状态
enum class CTPConnectionStatus {
    DISCONNECTED = 0,
    CONNECTING = 1,
    CONNECTED = 2,
    LOGGED_IN = 3,
    ERROR = 4
};

// 单个CTP连接管理类
class CTPConnection : public CThostFtdcMdSpi
{
public:
    explicit CTPConnection(const CTPConnectionConfig& config, 
                          MarketDataServer* server,
                          SubscriptionDispatcher* dispatcher);
    virtual ~CTPConnection();
    
    // 连接管理
    bool start();
    void stop();
    bool restart();
    
    // 订阅管理
    bool subscribe_instrument(const std::string& instrument_id);
    bool unsubscribe_instrument(const std::string& instrument_id);
    
    // 状态查询
    CTPConnectionStatus get_status() const { return status_; }
    const std::string& get_connection_id() const { return config_.connection_id; }
    size_t get_subscription_count() const;
    bool can_accept_more_subscriptions() const;
    
    // 连接质量指标
    int get_connection_quality() const { return connection_quality_; }
    std::chrono::milliseconds get_last_heartbeat() const { return last_heartbeat_; }
    int get_error_count() const { return error_count_; }
    
    // CTP SPI回调实现
    virtual void OnFrontConnected() override;
    virtual void OnFrontDisconnected(int nReason) override;
    virtual void OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin, 
                               CThostFtdcRspInfoField *pRspInfo, 
                               int nRequestID, bool bIsLast) override;
    virtual void OnRspSubMarketData(CThostFtdcSpecificInstrumentField *pSpecificInstrument, 
                                   CThostFtdcRspInfoField *pRspInfo, 
                                   int nRequestID, bool bIsLast) override;
    virtual void OnRspUnSubMarketData(CThostFtdcSpecificInstrumentField *pSpecificInstrument,
                                     CThostFtdcRspInfoField *pRspInfo,
                                     int nRequestID, bool bIsLast) override;
    virtual void OnRtnDepthMarketData(CThostFtdcDepthMarketDataField *pDepthMarketData) override;
    virtual void OnRspError(CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) override;
    
private:
    void login();
    void update_connection_quality();
    void handle_connection_error();
    
    CTPConnectionConfig config_;
    MarketDataServer* server_;
    SubscriptionDispatcher* dispatcher_;
    
    CThostFtdcMdApi* ctp_api_;
    std::atomic<CTPConnectionStatus> status_;
    std::set<std::string> subscribed_instruments_;
    
    // 连接质量监控
    std::atomic<int> connection_quality_;  // 0-100的连接质量评分
    std::chrono::milliseconds last_heartbeat_;
    std::atomic<int> error_count_;
    std::atomic<int> request_id_;
    
    // 线程安全
    mutable std::mutex subscriptions_mutex_;
    std::mutex api_mutex_;
};

// 多CTP连接管理器
class CTPConnectionManager
{
public:
    explicit CTPConnectionManager(MarketDataServer* server, 
                                 SubscriptionDispatcher* dispatcher);
    ~CTPConnectionManager();
    
    // 连接管理
    bool add_connection(const CTPConnectionConfig& config);
    bool remove_connection(const std::string& connection_id);
    bool start_all_connections();
    void stop_all_connections();
    
    // 连接查询
    std::shared_ptr<CTPConnection> get_connection(const std::string& connection_id);
    std::vector<std::shared_ptr<CTPConnection>> get_all_connections();
    std::vector<std::shared_ptr<CTPConnection>> get_available_connections();
    std::shared_ptr<CTPConnection> get_best_connection_for_subscription();
    
    // 连接状态监控
    size_t get_total_connections() const;
    size_t get_active_connections() const;
    size_t get_total_subscriptions() const;
    
    // 健康检查
    void start_health_monitor();
    void stop_health_monitor();
    
private:
    void health_check_loop();
    void handle_connection_failure(const std::string& connection_id);
    
    MarketDataServer* server_;
    SubscriptionDispatcher* dispatcher_;
    
    std::map<std::string, std::shared_ptr<CTPConnection>> connections_;
    mutable std::mutex connections_mutex_;
    
    // 健康检查线程
    std::unique_ptr<std::thread> health_check_thread_;
    std::atomic<bool> health_check_running_;
    std::chrono::seconds health_check_interval_;
    
    // 重启去重与退避控制
    std::mutex restart_mutex_;
    std::map<std::string, std::chrono::steady_clock::time_point> next_restart_allowed_;
};