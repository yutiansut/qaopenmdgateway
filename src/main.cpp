/////////////////////////////////////////////////////////////////////////
///@file main.cpp
///@brief	行情服务器主程序
///@copyright	QuantAxis版权所有
/////////////////////////////////////////////////////////////////////////

#include "market_data_server.h"
#include "multi_ctp_config.h"
#include <iostream>
#include <signal.h>
#include <thread>
#include <fstream>

std::unique_ptr<MarketDataServer> g_server;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
    exit(0);
}

void print_usage() {
    std::cout << "Usage: market_data_server [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  Single-CTP mode (legacy):" << std::endl;
    std::cout << "    --front-addr <address>    CTP market data front address (default: tcp://182.254.243.31:30011)" << std::endl;
    std::cout << "    --broker-id <id>          Broker ID (default: 9999)" << std::endl;
    std::cout << "    --port <port>             WebSocket port (default: 7799)" << std::endl;
    std::cout << std::endl;
    std::cout << "  Multi-CTP mode (recommended):" << std::endl;
    std::cout << "    --config <config_file>    Load multi-CTP configuration from JSON file" << std::endl;
    std::cout << "    --multi-ctp               Use default multi-CTP configuration (SimNow)" << std::endl;
    std::cout << "    --strategy <strategy>     Load balance strategy: round_robin, least_connections, connection_quality, hash_based" << std::endl;
    std::cout << std::endl;
    std::cout << "  Common options:" << std::endl;
    std::cout << "    --help                    Show this help message" << std::endl;
    std::cout << "    --status                  Show connection status and exit" << std::endl;
    std::cout << std::endl;
    std::cout << "Note: Market data API does not require user credentials." << std::endl;
    std::cout << "Multi-CTP mode provides better performance and fault tolerance." << std::endl;
}

LoadBalanceStrategy parse_strategy(const std::string& strategy_str) {
    if (strategy_str == "round_robin") return LoadBalanceStrategy::ROUND_ROBIN;
    if (strategy_str == "least_connections") return LoadBalanceStrategy::LEAST_CONNECTIONS;
    if (strategy_str == "connection_quality") return LoadBalanceStrategy::CONNECTION_QUALITY;
    if (strategy_str == "hash_based") return LoadBalanceStrategy::HASH_BASED;
    return LoadBalanceStrategy::CONNECTION_QUALITY; // 默认策略
}

int main(int argc, char* argv[]) {
    // 单CTP模式参数（兼容性）
    std::string front_addr = "tcp://182.254.243.31:30011";
    std::string broker_id = "9999";
    int port = 7799;
    
    // 多CTP模式参数
    bool use_multi_ctp = false;
    std::string config_file;
    LoadBalanceStrategy strategy = LoadBalanceStrategy::CONNECTION_QUALITY;
    bool show_status = false;

    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help") {
            print_usage();
            return 0;
        } else if (arg == "--status") {
            show_status = true;
        } else if (arg == "--multi-ctp") {
            use_multi_ctp = true;
        } else if (arg == "--config" && i + 1 < argc) {
            config_file = argv[++i];
            use_multi_ctp = true;
        } else if (arg == "--strategy" && i + 1 < argc) {
            strategy = parse_strategy(argv[++i]);
        } else if (arg == "--front-addr" && i + 1 < argc) {
            front_addr = argv[++i];
        } else if (arg == "--broker-id" && i + 1 < argc) {
            broker_id = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            print_usage();
            return 1;
        }
    }

    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::cout << "========================================" << std::endl;
    std::cout << "  QuantAxis Market Data Server" << std::endl;
    std::cout << "========================================" << std::endl;

    try {
        if (use_multi_ctp) {
            // 多CTP连接模式
            MultiCTPConfig config;
            
            if (!config_file.empty()) {
                // 从配置文件加载
                std::cout << "Loading config from file: " << config_file << std::endl;
                if (!ConfigLoader::load_from_file(config_file, config)) {
                    std::cerr << "Failed to load config file: " << config_file << std::endl;
                    return 1;
                }
            } else {
                // 使用默认多CTP配置
                std::cout << "Using default multi-CTP configuration (SimNow)" << std::endl;
                config = create_simnow_config();
            }
            
            // 应用命令行参数覆盖
            if (argc > 1) {
                config.load_balance_strategy = strategy;
                // 如果指定了端口，覆盖配置文件中的端口
                for (int i = 1; i < argc - 1; i++) {
                    if (std::string(argv[i]) == "--port") {
                        config.websocket_port = port;
                        break;
                    }
                }
            }
            
            // 验证配置
            if (!ConfigLoader::validate_config(config)) {
                std::cerr << "Invalid configuration" << std::endl;
                return 1;
            }
            
            std::cout << "Multi-CTP Mode Configuration:" << std::endl;
            std::cout << "  WebSocket:    ws://0.0.0.0:" << config.websocket_port << std::endl;
            std::cout << "  Redis:        " << config.redis_host << ":" << config.redis_port << std::endl;
            std::cout << "  Strategy:     ";
            switch (config.load_balance_strategy) {
                case LoadBalanceStrategy::ROUND_ROBIN: std::cout << "Round Robin"; break;
                case LoadBalanceStrategy::LEAST_CONNECTIONS: std::cout << "Least Connections"; break;
                case LoadBalanceStrategy::CONNECTION_QUALITY: std::cout << "Connection Quality"; break;
                case LoadBalanceStrategy::HASH_BASED: std::cout << "Hash Based"; break;
            }
            std::cout << std::endl;
            std::cout << "  Connections:  " << config.connections.size() << " configured" << std::endl;
            
            for (const auto& conn : config.connections) {
                if (conn.enabled) {
                    std::cout << "    [" << conn.connection_id << "] " << conn.front_addr 
                              << " (priority: " << conn.priority 
                              << ", max_subs: " << conn.max_subscriptions << ")" << std::endl;
                }
            }
            
            std::cout << "========================================" << std::endl;
            
            // 创建多CTP服务器
            g_server = std::make_unique<MarketDataServer>(config);
            
        } else {
            // 单CTP连接模式（兼容性）
            std::cout << "Single-CTP Mode Configuration:" << std::endl;
            std::cout << "  MD Front:     " << front_addr << std::endl;
            std::cout << "  Broker ID:    " << broker_id << std::endl;
            std::cout << "  WebSocket:    ws://0.0.0.0:" << port << std::endl;
            std::cout << "  Auth:         No credentials required for market data" << std::endl;
            std::cout << "========================================" << std::endl;
            
            // 创建单CTP服务器
            g_server = std::make_unique<MarketDataServer>(front_addr, broker_id, port);
        }
        
        // 如果只是查看状态，启动服务器后显示状态并退出
        if (show_status) {
            if (!g_server->start()) {
                std::cerr << "Failed to start server for status check" << std::endl;
                return 1;
            }
            
            // 等待连接建立
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            std::cout << "\nConnection Status:" << std::endl;
            std::cout << "  Active connections: " << g_server->get_active_connections_count() << std::endl;
            std::cout << "  CTP connected: " << (g_server->is_ctp_connected() ? "Yes" : "No") << std::endl;
            std::cout << "  CTP logged in: " << (g_server->is_ctp_logged_in() ? "Yes" : "No") << std::endl;
            
            auto status_list = g_server->get_connection_status();
            for (const auto& status : status_list) {
                std::cout << "  " << status << std::endl;
            }
            
            g_server->stop();
            return 0;
        }
        
        // 启动服务器
        if (!g_server->start()) {
            std::cerr << "Failed to start server" << std::endl;
            return 1;
        }

        std::cout << "Server started successfully." << std::endl;
        std::cout << "WebSocket endpoint: ws://localhost:" << port << std::endl;
        std::cout << "Press Ctrl+C to stop." << std::endl;
        
        // 主循环
        while (g_server->is_running()) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            
            // 定期显示状态信息
            if (use_multi_ctp && g_server->get_connection_manager()) {
                size_t active_conns = g_server->get_active_connections_count();
                size_t total_subs = 0;
                if (g_server->get_subscription_dispatcher()) {
                    total_subs = g_server->get_subscription_dispatcher()->get_total_subscriptions();
                }
                std::cout << "[Status] Active connections: " << active_conns 
                         << ", Total subscriptions: " << total_subs << std::endl;
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Server stopped gracefully." << std::endl;
    return 0;
}