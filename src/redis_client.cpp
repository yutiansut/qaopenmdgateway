/////////////////////////////////////////////////////////////////////////
///@file redis_client.cpp
///@brief	Redis客户端封装实现
///@copyright	QuantAxis版权所有
/////////////////////////////////////////////////////////////////////////

#include "redis_client.h"
#include <cstdarg>
#include <cstring>
#include <iostream>
#include <map>

RedisClient::RedisClient(const std::string& host, int port)
    : host_(host), port_(port), context_(nullptr)
{
}

RedisClient::~RedisClient()
{
    disconnect();
}

bool RedisClient::connect()
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (context_) {
        redisFree(context_);
    }
    
    // 设置连接超时
    struct timeval timeout = { 3, 0 }; // 3秒超时
    context_ = redisConnectWithTimeout(host_.c_str(), port_, timeout);
    
    if (context_ == nullptr || context_->err) {
        if (context_) {
            std::cerr << "Redis connection error: " << context_->errstr << std::endl;
            redisFree(context_);
            context_ = nullptr;
        } else {
            std::cerr << "Redis connection error: can't allocate redis context" << std::endl;
        }
        return false;
    }
    
    std::cout << "Connected to Redis server " << host_ << ":" << port_ << std::endl;
    return true;
}

void RedisClient::disconnect()
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (context_) {
        redisFree(context_);
        context_ = nullptr;
        std::cout << "Disconnected from Redis server" << std::endl;
    }
}

std::string RedisClient::get_error() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (context_ && context_->err) {
        return std::string(context_->errstr);
    }
    return "";
}

redisReply* RedisClient::execute_command(const char* format, ...)
{
    if (!is_connected()) {
        return nullptr;
    }
    
    va_list args;
    va_start(args, format);
    redisReply* reply = static_cast<redisReply*>(redisvCommand(context_, format, args));
    va_end(args);
    
    if (reply == nullptr) {
        std::cerr << "Redis command failed: " << (context_->err ? context_->errstr : "unknown error") << std::endl;
    }
    
    return reply;
}

void RedisClient::free_reply(redisReply* reply)
{
    if (reply) {
        freeReplyObject(reply);
    }
}

bool RedisClient::set(const std::string& key, const std::string& value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    redisReply* reply = execute_command("SET %s %s", key.c_str(), value.c_str());
    if (reply == nullptr) {
        return false;
    }
    
    bool success = (reply->type == REDIS_REPLY_STATUS && strcmp(reply->str, "OK") == 0);
    free_reply(reply);
    return success;
}

bool RedisClient::setex(const std::string& key, int seconds, const std::string& value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    redisReply* reply = execute_command("SETEX %s %d %s", key.c_str(), seconds, value.c_str());
    if (reply == nullptr) {
        return false;
    }
    
    bool success = (reply->type == REDIS_REPLY_STATUS && strcmp(reply->str, "OK") == 0);
    free_reply(reply);
    return success;
}

std::string RedisClient::get(const std::string& key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    redisReply* reply = execute_command("GET %s", key.c_str());
    if (reply == nullptr) {
        return "";
    }
    
    std::string result;
    if (reply->type == REDIS_REPLY_STRING) {
        result = std::string(reply->str, reply->len);
    }
    
    free_reply(reply);
    return result;
}

bool RedisClient::del(const std::string& key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    redisReply* reply = execute_command("DEL %s", key.c_str());
    if (reply == nullptr) {
        return false;
    }
    
    bool success = (reply->type == REDIS_REPLY_INTEGER && reply->integer > 0);
    free_reply(reply);
    return success;
}

bool RedisClient::exists(const std::string& key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    redisReply* reply = execute_command("EXISTS %s", key.c_str());
    if (reply == nullptr) {
        return false;
    }
    
    bool exists = (reply->type == REDIS_REPLY_INTEGER && reply->integer > 0);
    free_reply(reply);
    return exists;
}

bool RedisClient::hset(const std::string& key, const std::string& field, const std::string& value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    redisReply* reply = execute_command("HSET %s %s %s", key.c_str(), field.c_str(), value.c_str());
    if (reply == nullptr) {
        return false;
    }
    
    bool success = (reply->type == REDIS_REPLY_INTEGER);
    free_reply(reply);
    return success;
}

std::string RedisClient::hget(const std::string& key, const std::string& field)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    redisReply* reply = execute_command("HGET %s %s", key.c_str(), field.c_str());
    if (reply == nullptr) {
        return "";
    }
    
    std::string result;
    if (reply->type == REDIS_REPLY_STRING) {
        result = std::string(reply->str, reply->len);
    }
    
    free_reply(reply);
    return result;
}

std::map<std::string, std::string> RedisClient::hgetall(const std::string& key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::map<std::string, std::string> result;
    
    redisReply* reply = execute_command("HGETALL %s", key.c_str());
    if (reply == nullptr) {
        return result;
    }
    
    if (reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; i += 2) {
            if (i + 1 < reply->elements && 
                reply->element[i]->type == REDIS_REPLY_STRING &&
                reply->element[i + 1]->type == REDIS_REPLY_STRING) {
                
                std::string field(reply->element[i]->str, reply->element[i]->len);
                std::string value(reply->element[i + 1]->str, reply->element[i + 1]->len);
                result[field] = value;
            }
        }
    }
    
    free_reply(reply);
    return result;
}

bool RedisClient::zadd(const std::string& key, long long score, const std::string& member)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    redisReply* reply = execute_command("ZADD %s %lld %s", key.c_str(), score, member.c_str());
    if (reply == nullptr) {
        return false;
    }
    
    bool success = (reply->type == REDIS_REPLY_INTEGER);
    free_reply(reply);
    return success;
}

bool RedisClient::zremrangebyscore(const std::string& key, long long start, long long stop)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    redisReply* reply = execute_command("ZREMRANGEBYSCORE %s %lld %lld", key.c_str(), start, stop);
    if (reply == nullptr) {
        return false;
    }
    
    bool success = (reply->type == REDIS_REPLY_INTEGER);
    free_reply(reply);
    return success;
}

long long RedisClient::zcard(const std::string& key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    redisReply* reply = execute_command("ZCARD %s", key.c_str());
    if (reply == nullptr) {
        return 0;
    }
    
    long long count = 0;
    if (reply->type == REDIS_REPLY_INTEGER) {
        count = reply->integer;
    }
    
    free_reply(reply);
    return count;
}