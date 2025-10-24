/////////////////////////////////////////////////////////////////////////
///@file redis_client.h
///@brief	Redis客户端封装
///@copyright	QuantAxis版权所有
/////////////////////////////////////////////////////////////////////////

#pragma once

#include <hiredis/hiredis.h>
#include <string>
#include <memory>
#include <mutex>
#include <map>

class RedisClient {
public:
    RedisClient(const std::string& host = "127.0.0.1", int port = 6379);
    ~RedisClient();
    
    bool connect();
    void disconnect();
    bool is_connected() const { return context_ != nullptr && !context_->err; }
    
    // 设置键值对
    bool set(const std::string& key, const std::string& value);
    
    // 设置键值对并指定过期时间（秒）
    bool setex(const std::string& key, int seconds, const std::string& value);
    
    // 获取值
    std::string get(const std::string& key);
    
    // 删除键
    bool del(const std::string& key);
    
    // 检查键是否存在
    bool exists(const std::string& key);
    
    // 设置Hash字段
    bool hset(const std::string& key, const std::string& field, const std::string& value);
    
    // 获取Hash字段
    std::string hget(const std::string& key, const std::string& field);
    
    // 获取Hash所有字段
    std::map<std::string, std::string> hgetall(const std::string& key);
    
    // ZSet相关操作
    // 添加成员到有序集合
    bool zadd(const std::string& key, long long score, const std::string& member);
    
    // 根据排名范围移除有序集合成员 (0表示第一个，-1表示最后一个)
    bool zremrangebyscore(const std::string& key, long long start, long long stop);
    
    // 获取有序集合成员数量
    long long zcard(const std::string& key);
    
    // 获取连接错误信息
    std::string get_error() const;

private:
    std::string host_;
    int port_;
    redisContext* context_;
    mutable std::mutex mutex_;
    
    // 执行Redis命令的通用方法
    redisReply* execute_command(const char* format, ...);
    void free_reply(redisReply* reply);
};