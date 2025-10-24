/////////////////////////////////////////////////////////////////////////
///@file multi_ctp_config.cpp
///@brief	多CTP连接配置管理实现
///@copyright	QuantAxis版权所有
/////////////////////////////////////////////////////////////////////////

#include "multi_ctp_config.h"
#include "../include/open-trade-common/types.h"
#include <fstream>
#include <iostream>

bool ConfigLoader::load_from_file(const std::string& config_file, MultiCTPConfig& config)
{
    std::ifstream file(config_file);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << config_file << std::endl;
        return false;
    }
    
    std::string json_content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    file.close();
    
    return load_from_json(json_content, config);
}

bool ConfigLoader::load_from_json(const std::string& json_content, MultiCTPConfig& config)
{
    try {
        rapidjson::Document doc;
        doc.Parse(json_content.c_str());
        
        if (doc.HasParseError()) {
            std::cerr << "JSON parse error: " << doc.GetParseError() << std::endl;
            return false;
        }
        
        // 解析全局配置
        if (doc.HasMember("websocket_port") && doc["websocket_port"].IsInt()) {
            config.websocket_port = doc["websocket_port"].GetInt();
        }
        
        if (doc.HasMember("redis_host") && doc["redis_host"].IsString()) {
            config.redis_host = doc["redis_host"].GetString();
        }
        
        if (doc.HasMember("redis_port") && doc["redis_port"].IsInt()) {
            config.redis_port = doc["redis_port"].GetInt();
        }
        
        // 解析负载均衡策略
        if (doc.HasMember("load_balance_strategy") && doc["load_balance_strategy"].IsString()) {
            std::string strategy = doc["load_balance_strategy"].GetString();
            if (strategy == "round_robin") {
                config.load_balance_strategy = LoadBalanceStrategy::ROUND_ROBIN;
            } else if (strategy == "least_connections") {
                config.load_balance_strategy = LoadBalanceStrategy::LEAST_CONNECTIONS;
            } else if (strategy == "connection_quality") {
                config.load_balance_strategy = LoadBalanceStrategy::CONNECTION_QUALITY;
            } else if (strategy == "hash_based") {
                config.load_balance_strategy = LoadBalanceStrategy::HASH_BASED;
            }
        }
        
        // 解析高级配置
        if (doc.HasMember("health_check_interval") && doc["health_check_interval"].IsInt()) {
            config.health_check_interval = doc["health_check_interval"].GetInt();
        }
        
        if (doc.HasMember("maintenance_interval") && doc["maintenance_interval"].IsInt()) {
            config.maintenance_interval = doc["maintenance_interval"].GetInt();
        }
        
        if (doc.HasMember("max_retry_count") && doc["max_retry_count"].IsInt()) {
            config.max_retry_count = doc["max_retry_count"].GetInt();
        }
        
        if (doc.HasMember("auto_failover") && doc["auto_failover"].IsBool()) {
            config.auto_failover = doc["auto_failover"].GetBool();
        }
        
        // 解析连接配置
        if (doc.HasMember("connections") && doc["connections"].IsArray()) {
            const auto& connections_array = doc["connections"].GetArray();
            config.connections.clear();
            
            for (const auto& conn_json : connections_array) {
                if (!conn_json.IsObject()) continue;
                
                CTPConnectionConfig conn_config;
                
                if (conn_json.HasMember("connection_id") && conn_json["connection_id"].IsString()) {
                    conn_config.connection_id = conn_json["connection_id"].GetString();
                }
                
                if (conn_json.HasMember("front_addr") && conn_json["front_addr"].IsString()) {
                    conn_config.front_addr = conn_json["front_addr"].GetString();
                }
                
                if (conn_json.HasMember("broker_id") && conn_json["broker_id"].IsString()) {
                    conn_config.broker_id = conn_json["broker_id"].GetString();
                }
                
                if (conn_json.HasMember("max_subscriptions") && conn_json["max_subscriptions"].IsInt()) {
                    conn_config.max_subscriptions = conn_json["max_subscriptions"].GetInt();
                }
                
                if (conn_json.HasMember("priority") && conn_json["priority"].IsInt()) {
                    conn_config.priority = conn_json["priority"].GetInt();
                }
                
                if (conn_json.HasMember("enabled") && conn_json["enabled"].IsBool()) {
                    conn_config.enabled = conn_json["enabled"].GetBool();
                }
                
                config.connections.push_back(conn_config);
            }
        }
        
        return validate_config(config);
        
    } catch (const std::exception& e) {
        std::cerr << "Error parsing config JSON: " << e.what() << std::endl;
        return false;
    }
}

MultiCTPConfig ConfigLoader::create_default_config()
{
    MultiCTPConfig config;
    config.websocket_port = 7799;
    config.redis_host = "192.168.2.27";
    config.redis_port = 6379;
    config.load_balance_strategy = LoadBalanceStrategy::CONNECTION_QUALITY;
    config.health_check_interval = 30;
    config.maintenance_interval = 60;
    config.max_retry_count = 3;
    config.auto_failover = true;
    
    setup_default_connections(config);
    
    return config;
}

bool ConfigLoader::validate_config(const MultiCTPConfig& config)
{
    // 检查基本配置
    
    if (config.websocket_port <= 0 || config.websocket_port > 65535) {
        std::cerr << "Invalid WebSocket port: " << config.websocket_port << std::endl;
        return false;
    }
    
    if (config.connections.empty()) {
        std::cerr << "No CTP connections configured" << std::endl;
        return false;
    }
    
    // 检查连接配置
    std::set<std::string> connection_ids;
    for (const auto& conn : config.connections) {
        if (conn.connection_id.empty()) {
            std::cerr << "Connection ID cannot be empty" << std::endl;
            return false;
        }
        
        if (connection_ids.find(conn.connection_id) != connection_ids.end()) {
            std::cerr << "Duplicate connection ID: " << conn.connection_id << std::endl;
            return false;
        }
        connection_ids.insert(conn.connection_id);
        
        if (conn.front_addr.empty()) {
            std::cerr << "Front address cannot be empty for connection: " << conn.connection_id << std::endl;
            return false;
        }
        
        if (conn.broker_id.empty()) {
            std::cerr << "Broker ID cannot be empty for connection: " << conn.connection_id << std::endl;
            return false;
        }
        
        if (conn.max_subscriptions <= 0) {
            std::cerr << "Invalid max_subscriptions for connection: " << conn.connection_id << std::endl;
            return false;
        }
    }
    
    return true;
}

void ConfigLoader::setup_default_connections(MultiCTPConfig& config)
{
    // 默认使用SimNow的多个前置机
    CTPConnectionConfig conn1;
    conn1.connection_id = "simnow_telecom";
    conn1.front_addr = "tcp://180.168.146.187:10210";
    conn1.broker_id = "9999";  // SimNow默认broker_id
    conn1.max_subscriptions = 500;
    conn1.priority = 1;
    conn1.enabled = true;
    
    CTPConnectionConfig conn2;
    conn2.connection_id = "simnow_unicom";  
    conn2.front_addr = "tcp://180.168.146.187:10211";
    conn2.broker_id = "9999";  // SimNow默认broker_id
    conn2.max_subscriptions = 500;
    conn2.priority = 2;
    conn2.enabled = true;
    
    CTPConnectionConfig conn3;
    conn3.connection_id = "simnow_mobile";
    conn3.front_addr = "tcp://218.202.237.33:10212";
    conn3.broker_id = "9999";  // SimNow默认broker_id
    conn3.max_subscriptions = 500;  
    conn3.priority = 3;
    conn3.enabled = true;
    
    config.connections = {conn1, conn2, conn3};
}