/////////////////////////////////////////////////////////////////////////
///@file market_data_server.h
///@brief	行情数据WebSocket服务器
///@copyright	QuantAxis版权所有
/////////////////////////////////////////////////////////////////////////

#pragma once

#include "../libs/ThostFtdcMdApi.h"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/thread.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <memory>
#include <set>
#include <map>
#include <string>
#include <atomic>
#include <mutex>
#include <queue>
// 使用项目中的类型定义，其中包含了rapidjson的正确配置
#include "../include/open-trade-common/types.h"
#include "redis_client.h"
#include "ctp_connection_manager.h"
#include "subscription_dispatcher.h"
#include "multi_ctp_config.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class MarketDataServer;

// WebSocket连接会话
class WebSocketSession : public std::enable_shared_from_this<WebSocketSession>
{
public:
    explicit WebSocketSession(tcp::socket&& socket, MarketDataServer* server);
    ~WebSocketSession();
    
    void run();
    void send_message(const std::string& message);
    void close();
    
    std::string get_session_id() const { return session_id_; }
    const std::set<std::string>& get_subscriptions() const { return subscriptions_; }
    
private:
    void on_accept(beast::error_code ec);
    void do_read();
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    void start_write();
    void on_write(beast::error_code ec, std::size_t bytes_transferred);
    
    void handle_message(const std::string& message);
    void send_error(const std::string& error_msg);
    void send_response(const std::string& type, const rapidjson::Document& data);
    
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
    std::queue<std::string> message_queue_;
    std::string current_write_message_;
    std::string session_id_;
    std::set<std::string> subscriptions_;
    MarketDataServer* server_;
    std::mutex write_mutex_;
    bool is_writing_;
};

// CTP行情SPI回调实现
class MarketDataSpi : public CThostFtdcMdSpi
{
public:
    explicit MarketDataSpi(MarketDataServer* server);
    virtual ~MarketDataSpi();
    
    // CTP回调函数
    virtual void OnFrontConnected() override;
    virtual void OnFrontDisconnected(int nReason) override;
    virtual void OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin, 
                               CThostFtdcRspInfoField *pRspInfo, 
                               int nRequestID, bool bIsLast) override;
    virtual void OnRspSubMarketData(CThostFtdcSpecificInstrumentField *pSpecificInstrument, 
                                   CThostFtdcRspInfoField *pRspInfo, 
                                   int nRequestID, bool bIsLast) override;
    virtual void OnRtnDepthMarketData(CThostFtdcDepthMarketDataField *pDepthMarketData) override;
    virtual void OnRspError(CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) override;
    
private:
    MarketDataServer* server_;
};

// 主服务器类
class MarketDataServer
{
public:
    // 原有构造函数（兼容性）
    explicit MarketDataServer(const std::string& ctp_front_addr,
                             const std::string& broker_id,
                             int websocket_port = 7799);
    
    // 新的多连接构造函数
    explicit MarketDataServer(const MultiCTPConfig& config);
    
    ~MarketDataServer();
    
    bool start();
    void stop();
    bool is_running() const { return is_running_; }
    
    // WebSocket会话管理
    void add_session(std::shared_ptr<WebSocketSession> session);
    void remove_session(const std::string& session_id);
    void subscribe_instrument(const std::string& session_id, const std::string& instrument_id);
    void unsubscribe_instrument(const std::string& session_id, const std::string& instrument_id);
    
    // 行情数据推送
    void broadcast_market_data(const std::string& instrument_id, const std::string& json_data);
    void send_to_session(const std::string& session_id, const std::string& message);
    void handle_peek_message(const std::string& session_id);
    void cache_market_data(const std::string& instrument_id, const std::string& json_data);
    
    // 构建标准格式的单个合约行情数据
    static rapidjson::Value build_quote_data(CThostFtdcDepthMarketDataField *pDepthMarketData, 
                                             const std::string& display_instrument,
                                             rapidjson::Document::AllocatorType& allocator,
                                             long long& timestamp_ms);
    
    // 合约管理
    std::vector<std::string> get_all_instruments();
    std::vector<std::string> search_instruments(const std::string& pattern);
    
    // CTP连接状态（多连接版本）
    bool is_ctp_connected() const;
    bool is_ctp_logged_in() const;
    size_t get_active_connections_count() const;
    std::vector<std::string> get_connection_status() const;
    
    // 多连接管理接口
    CTPConnectionManager* get_connection_manager() { return connection_manager_.get(); }
    SubscriptionDispatcher* get_subscription_dispatcher() { return subscription_dispatcher_.get(); }
    
    void send_empty_rtn_data(const std::string& session_id);
    void notify_pending_sessions(const std::string& instrument_id);
    
    // Redis存储相关
    void store_market_data_to_redis(const std::string& instrument_id, 
                                   const std::string& json_data, 
                                   long long& timestamp_ms);

    // 日志函数
    void log_info(const std::string& message);
    void log_error(const std::string& message);
    void log_warning(const std::string& message);
    
    // Redis客户端访问（供MarketDataSpi使用）
    std::unique_ptr<RedisClient>& get_redis_client() { return redis_client_; }
    
private:
    void init_shared_memory();
    void cleanup_shared_memory();
    void start_websocket_server();
    void handle_accept(beast::error_code ec, tcp::socket socket);
public:
    void ctp_login();
    std::string create_session_id();
    std::map<std::string, std::string> noheadtohead_instruments_map_; // ctp_instrument -> display_instrument
private:
    bool init_multi_ctp_system();
    void cleanup_multi_ctp_system();
    
    // 兼容性：单连接模式
    std::string ctp_front_addr_;
    std::string broker_id_;
    CThostFtdcMdApi* ctp_api_;
    std::unique_ptr<MarketDataSpi> md_spi_;
    std::atomic<bool> ctp_connected_;
    std::atomic<bool> ctp_logged_in_;
    
    // 多连接系统
    MultiCTPConfig multi_ctp_config_;
    std::unique_ptr<CTPConnectionManager> connection_manager_;
    std::unique_ptr<SubscriptionDispatcher> subscription_dispatcher_;
    bool use_multi_ctp_mode_;
    
    // WebSocket服务器
    net::io_context ioc_;
    int websocket_port_;
    tcp::acceptor acceptor_;
    std::map<std::string, std::shared_ptr<WebSocketSession>> sessions_;
    std::map<std::string, std::set<std::string>> instrument_subscribers_; // instrument_id -> session_ids
    std::map<std::string, std::string> market_data_cache_; // instrument_id -> latest_market_data_json
    std::mutex market_data_cache_mutex_;
    
    // 客户端上次发送的完整消息json缓存: session_id -> last_sent_json
    std::map<std::string, std::string> session_last_sent_json_;
    std::mutex session_last_sent_mutex_;
    
    // 等待行情更新的session集合（挂起的peek_message）
    std::set<std::string> pending_peek_sessions_;
    std::mutex pending_peek_mutex_;
    
    // 共享内存相关
    boost::interprocess::managed_shared_memory* segment_;
    ShmemAllocator* alloc_inst_;
    InsMapType* ins_map_;
    
    // 线程同步
    std::mutex sessions_mutex_;
    std::mutex subscribers_mutex_;
    std::atomic<bool> is_running_;
    boost::thread server_thread_;
    
    // 请求ID管理
    std::atomic<int> request_id_;
    
    // Redis客户端
    std::unique_ptr<RedisClient> redis_client_;
};