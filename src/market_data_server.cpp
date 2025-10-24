/////////////////////////////////////////////////////////////////////////
///@file market_data_server.cpp
///@brief	行情数据WebSocket服务器实现
///@copyright	QuantAxis版权所有
/////////////////////////////////////////////////////////////////////////

#include "market_data_server.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <random>
#include <algorithm>
#include <cmath>
#include <thread>
#include <chrono>

namespace beast = boost::beast;
namespace http = beast::http;

// 增量json计算：比较两个json对象，返回差异部分
// 如果字段值相同则不包含在结果中，只返回变化的字段
static void ComputeJsonDiff(const rapidjson::Value& old_val, 
                            const rapidjson::Value& new_val,
                            rapidjson::Value& diff_val,
                            rapidjson::Document::AllocatorType& allocator)
{
    // 类型不同，返回新值
    if (old_val.GetType() != new_val.GetType()) {
        diff_val.CopyFrom(new_val, allocator);
        return;
    }
    
    // 处理对象类型
    if (new_val.IsObject()) {
        diff_val.SetObject();
        
        for (auto it = new_val.MemberBegin(); it != new_val.MemberEnd(); ++it) {
            const char* key = it->name.GetString();
            const rapidjson::Value& new_field = it->value;
            
            // 检查旧json中是否有这个字段
            auto old_it = old_val.FindMember(key);
            if (old_it == old_val.MemberEnd()) {
                // 旧json没有这个字段，加入差异
                rapidjson::Value key_copy(key, allocator);
                rapidjson::Value val_copy;
                val_copy.CopyFrom(new_field, allocator);
                diff_val.AddMember(key_copy, val_copy, allocator);
            } else {
                const rapidjson::Value& old_field = old_it->value;

                // 核心修复：在比较值之前，先比较类型
                if (new_field.GetType() != old_field.GetType()) {
                    // 类型不同，直接视为差异
                    rapidjson::Value key_copy(key, allocator);
                    rapidjson::Value val_copy;
                    val_copy.CopyFrom(new_field, allocator);
                    diff_val.AddMember(key_copy, val_copy, allocator);
                } else {
                    // 类型相同，才进行后续的值比较
                    if (new_field.IsObject()) { // IsObject() implies old_field is also an object
                        rapidjson::Value nested_diff(rapidjson::kObjectType);
                        ComputeJsonDiff(old_field, new_field, nested_diff, allocator);
                        
                        if (nested_diff.MemberCount() > 0) {
                            rapidjson::Value key_copy(key, allocator);
                            diff_val.AddMember(key_copy, nested_diff, allocator);
                        }
                    } 
                    else if (new_field.IsArray()) { // IsArray() implies old_field is also an array
                        rapidjson::StringBuffer old_buf, new_buf;
                        rapidjson::Writer<rapidjson::StringBuffer> old_writer(old_buf);
                        rapidjson::Writer<rapidjson::StringBuffer> new_writer(new_buf);
                        old_field.Accept(old_writer);
                        new_field.Accept(new_writer);
                        
                        if (std::string(old_buf.GetString()) != std::string(new_buf.GetString())) {
                            rapidjson::Value key_copy(key, allocator);
                            rapidjson::Value val_copy;
                            val_copy.CopyFrom(new_field, allocator);
                            diff_val.AddMember(key_copy, val_copy, allocator);
                        }
                    }
                    // 基本类型比较
                    else {
                        // 如果类型不同，顶层检查已处理，此处假定类型相同
                        if (new_field.IsNull()) {
                            // old_field也必然是null，无差异
                        } else if (new_field.IsString()) {
                            if (std::string(new_field.GetString()) != std::string(old_field.GetString())) {
                                rapidjson::Value key_copy(key, allocator);
                                rapidjson::Value val_copy;
                                val_copy.CopyFrom(new_field, allocator);
                                diff_val.AddMember(key_copy, val_copy, allocator);
                            }
                        } else if (new_field.IsNumber()) {
                            bool is_different = false;
                            if (new_field.IsDouble() || old_field.IsDouble()) {
                                is_different = (new_field.GetDouble() != old_field.GetDouble());
                            } else if (new_field.IsInt64() || old_field.IsInt64()) {
                                is_different = (new_field.GetInt64() != old_field.GetInt64());
                            } else {
                                is_different = (new_field.GetInt() != old_field.GetInt());
                            }
                            if (is_different) {
                                rapidjson::Value key_copy(key, allocator);
                                rapidjson::Value val_copy;
                                val_copy.CopyFrom(new_field, allocator);
                                diff_val.AddMember(key_copy, val_copy, allocator);
                            }
                        } else if (new_field.IsBool()) {
                            if (new_field.GetBool() != old_field.GetBool()) {
                                rapidjson::Value key_copy(key, allocator);
                                rapidjson::Value val_copy;
                                val_copy.CopyFrom(new_field, allocator);
                                diff_val.AddMember(key_copy, val_copy, allocator);
                            }
                        }
                    }
                }
            }
        }
    } 
    // 非对象类型直接返回新值
    else {
        diff_val.CopyFrom(new_val, allocator);
    }
}

// WebSocketSession实现
WebSocketSession::WebSocketSession(tcp::socket&& socket, MarketDataServer* server)
    : ws_(std::move(socket))
    , server_(server)
    , is_writing_(false)
{
    // 生成唯一的session ID
    session_id_ = server_->create_session_id();
}

WebSocketSession::~WebSocketSession()
{
    if (server_) {
        server_->remove_session(session_id_);
    }
}

void WebSocketSession::run()
{
    // 设置WebSocket选项
    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
    ws_.set_option(websocket::stream_base::decorator(
        [](websocket::response_type& res)
        {
            res.set(http::field::server, "QuantAxis-MarketData-Server");
        }));

    // 接受WebSocket握手
    ws_.async_accept(
        beast::bind_front_handler(&WebSocketSession::on_accept, shared_from_this()));
}

void WebSocketSession::on_accept(beast::error_code ec)
{
    if (ec) {
        server_->log_error("WebSocket accept error: " + ec.message());
        return;
    }

    server_->log_info("WebSocket session connected: " + session_id_);
    
    // 发送欢迎消息
    rapidjson::Document welcome;
    welcome.SetObject();
    rapidjson::Document::AllocatorType& allocator = welcome.GetAllocator();
    
    welcome.AddMember("type", "welcome", allocator);
    welcome.AddMember("message", "Connected to QuantAxis MarketData Server", allocator);
    welcome.AddMember("session_id", rapidjson::StringRef(session_id_.c_str()), allocator);
    welcome.AddMember("ctp_connected", server_->is_ctp_connected(), allocator);
    welcome.AddMember("timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count(), allocator);
    
    send_response("welcome", welcome);
    
    // 开始读取消息
    do_read();
}

void WebSocketSession::do_read()
{
    ws_.async_read(
        buffer_,
        beast::bind_front_handler(&WebSocketSession::on_read, shared_from_this()));
}

void WebSocketSession::on_read(beast::error_code ec, std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);

    if (ec == websocket::error::closed) {
        server_->log_info("WebSocket session closed: " + session_id_);
        return;
    }

    if (ec) {
        server_->log_error("WebSocket read error: " + ec.message());
        return;
    }

    // 处理接收到的消息
    std::string message = beast::buffers_to_string(buffer_.data());
    buffer_.clear();
    
    handle_message(message);
    
    // 继续读取下一条消息
    do_read();
}

void WebSocketSession::handle_message(const std::string& message)
{
    server_->log_info("Received message from session " + session_id_ + ": " + message);
    
    try {
        rapidjson::Document doc;
        if (doc.Parse(message.c_str()).HasParseError()) {
            send_error("Invalid JSON format");
            return;
        }
        
        // 兼容mdservice协议
        if (doc.HasMember("aid") && doc["aid"].IsString()) {
            std::string aid = doc["aid"].GetString();
            
            if (aid == "subscribe_quote") {
                // 处理mdservice订阅请求
                if (!doc.HasMember("ins_list") || !doc["ins_list"].IsString()) {
                    send_error("Missing or invalid 'ins_list' field");
                    return;
                }
                
                std::string ins_list = doc["ins_list"].GetString();
                std::vector<std::string> instruments;
                
                // 解析逗号分隔的合约列表
                std::istringstream iss(ins_list);
                std::string instrument;
                while (std::getline(iss, instrument, ',')) {
                    if (!instrument.empty()) {
                        // 去掉交易所前缀 (如 GFEX. -> 空)
                        std::string nohead_instrument = instrument;
                        size_t dot_pos = instrument.find('.');
                        if (dot_pos != std::string::npos) {
                            nohead_instrument = instrument.substr(dot_pos + 1);
                        }
                        
                        instruments.push_back(nohead_instrument);
                        subscriptions_.insert(nohead_instrument);
                        
                        // 更新映射表和订阅者
                        server_->noheadtohead_instruments_map_[nohead_instrument] = instrument;
                        server_->subscribe_instrument(session_id_, nohead_instrument);  // 使用CTP格式订阅
                    }
                }
                
                // 发送mdservice格式响应
                rapidjson::Document response;
                response.SetObject();
                auto& allocator = response.GetAllocator();
                response.AddMember("aid", "subscribe_quote", allocator);
                response.AddMember("status", "ok", allocator);
                
                send_response("subscribe_quote_response", response);
                return;
            }
            if (aid == "peek_message") {
                // 处理peek_message，发送缓存的行情数据
                server_->handle_peek_message(session_id_);
                return;
            }
        }
        
        if (!doc.HasMember("action") || !doc["action"].IsString()) {
            send_error("Missing or invalid 'action' field");
            return;
        }
        
        std::string action = doc["action"].GetString();
        
        if (action == "subscribe") {
            if (!doc.HasMember("instruments") || !doc["instruments"].IsArray()) {
                send_error("Missing or invalid 'instruments' field");
                return;
            }
            
            const auto& instruments = doc["instruments"].GetArray();
            for (const auto& inst : instruments) {
                if (inst.IsString()) {
                    std::string instrument_id = inst.GetString();
                    subscriptions_.insert(instrument_id);
                    server_->subscribe_instrument(session_id_, instrument_id);
                }
            }
            
            rapidjson::Document response;
            response.SetObject();
            auto& allocator = response.GetAllocator();
            response.AddMember("type", "subscribe_response", allocator);
            response.AddMember("status", "success", allocator);
            response.AddMember("subscribed_count", static_cast<int>(subscriptions_.size()), allocator);
            
            send_response("subscribe_response", response);
            
        } else if (action == "unsubscribe") {
            if (!doc.HasMember("instruments") || !doc["instruments"].IsArray()) {
                send_error("Missing or invalid 'instruments' field");
                return;
            }
            
            const auto& instruments = doc["instruments"].GetArray();
            for (const auto& inst : instruments) {
                if (inst.IsString()) {
                    std::string instrument_id = inst.GetString();
                    subscriptions_.erase(instrument_id);
                    server_->unsubscribe_instrument(session_id_, instrument_id);
                }
            }
            
            rapidjson::Document response;
            response.SetObject();
            auto& allocator = response.GetAllocator();
            response.AddMember("type", "unsubscribe_response", allocator);
            response.AddMember("status", "success", allocator);
            response.AddMember("subscribed_count", static_cast<int>(subscriptions_.size()), allocator);
            
            send_response("unsubscribe_response", response);
            
        } else if (action == "list_instruments") {
            auto instruments = server_->get_all_instruments();
            
            rapidjson::Document response;
            response.SetObject();
            auto& allocator = response.GetAllocator();
            response.AddMember("type", "instrument_list", allocator);
            
            rapidjson::Value inst_array(rapidjson::kArrayType);
            for (const auto& inst : instruments) {
                inst_array.PushBack(rapidjson::StringRef(inst.c_str()), allocator);
            }
            response.AddMember("instruments", inst_array, allocator);
            response.AddMember("count", static_cast<int>(instruments.size()), allocator);
            
            send_response("instrument_list", response);
            
        } else if (action == "search_instruments") {
            if (!doc.HasMember("pattern") || !doc["pattern"].IsString()) {
                send_error("Missing or invalid 'pattern' field");
                return;
            }
            
            std::string pattern = doc["pattern"].GetString();
            auto instruments = server_->search_instruments(pattern);
            
            rapidjson::Document response;
            response.SetObject();
            auto& allocator = response.GetAllocator();
            response.AddMember("type", "search_result", allocator);
            response.AddMember("pattern", rapidjson::StringRef(pattern.c_str()), allocator);
            
            rapidjson::Value inst_array(rapidjson::kArrayType);
            for (const auto& inst : instruments) {
                inst_array.PushBack(rapidjson::StringRef(inst.c_str()), allocator);
            }
            response.AddMember("instruments", inst_array, allocator);
            response.AddMember("count", static_cast<int>(instruments.size()), allocator);
            
            send_response("search_result", response);
            
        } else {
            send_error("Unknown action: " + action);
        }
        
    } catch (const std::exception& e) {
        send_error("Error processing message: " + std::string(e.what()));
    }
}

void WebSocketSession::send_error(const std::string& error_msg)
{
    rapidjson::Document error;
    error.SetObject();
    auto& allocator = error.GetAllocator();
    error.AddMember("type", "error", allocator);
    error.AddMember("message", rapidjson::StringRef(error_msg.c_str()), allocator);
    error.AddMember("timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count(), allocator);
    
    send_response("error", error);
}

void WebSocketSession::send_response(const std::string& type, const rapidjson::Document& data)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    data.Accept(writer);
    
    send_message(buffer.GetString());
}

void WebSocketSession::send_message(const std::string& message)
{
    std::lock_guard<std::mutex> lock(write_mutex_);
    
    message_queue_.push(message);
    
    if (!is_writing_) {
        is_writing_ = true;
        start_write();
    }
}

void WebSocketSession::start_write()
{
    if (message_queue_.empty()) {
        is_writing_ = false;
        return;
    }

    current_write_message_ = std::move(message_queue_.front());
    message_queue_.pop();

    ws_.async_write(
        net::buffer(current_write_message_),
        beast::bind_front_handler(&WebSocketSession::on_write, shared_from_this()));
}

void WebSocketSession::on_write(beast::error_code ec, std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);

    if (ec) {
        server_->log_error("WebSocket write error: " + ec.message());
        std::lock_guard<std::mutex> lock(write_mutex_);
        is_writing_ = false;
        return;
    }

    std::lock_guard<std::mutex> lock(write_mutex_);
    
    // 继续写入队列中的下一条消息
    start_write();
}

void WebSocketSession::close()
{
    beast::error_code ec;
    ws_.close(websocket::close_code::normal, ec);
    if (ec) {
        server_->log_error("Error closing WebSocket: " + ec.message());
    }
}

// MarketDataSpi实现
MarketDataSpi::MarketDataSpi(MarketDataServer* server) : server_(server)
{
}

MarketDataSpi::~MarketDataSpi()
{
}

void MarketDataSpi::OnFrontConnected()
{
    server_->log_info("CTP front connected");
    server_->ctp_login();
}

void MarketDataSpi::OnFrontDisconnected(int nReason)
{
    server_->log_warning("CTP front disconnected, reason: " + std::to_string(nReason));
}

void MarketDataSpi::OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin,
                                  CThostFtdcRspInfoField *pRspInfo,
                                  int nRequestID, bool bIsLast)
{
    if (pRspInfo && pRspInfo->ErrorID != 0) {
        server_->log_error("CTP login failed: " + std::string(pRspInfo->ErrorMsg));
        return;
    }
    
    server_->log_info("CTP login successful");
}

void MarketDataSpi::OnRspSubMarketData(CThostFtdcSpecificInstrumentField *pSpecificInstrument,
                                      CThostFtdcRspInfoField *pRspInfo,
                                      int nRequestID, bool bIsLast)
{
    if (pRspInfo && pRspInfo->ErrorID != 0) {
        server_->log_error("Subscribe market data failed: " + std::string(pRspInfo->ErrorMsg));
        return;
    }
    
    if (pSpecificInstrument) {
        server_->log_info("Subscribed to instrument: " + std::string(pSpecificInstrument->InstrumentID));
    }
}

// 构建标准格式的单个合约行情数据（严格按照字段顺序）
rapidjson::Value MarketDataServer::build_quote_data(CThostFtdcDepthMarketDataField *pDepthMarketData,
                                                     const std::string& display_instrument,
                                                     rapidjson::Document::AllocatorType& allocator,
                                                     long long& timestamp_ms)
{
    rapidjson::Value inst_data(rapidjson::kObjectType);
    
    // 1. instrument_id
    inst_data.AddMember("instrument_id", rapidjson::Value(display_instrument.c_str(), allocator), allocator);
    
    // 2. datetime - 合并trading_day和update_time
    std::string datetime_str;
    {
        std::string trading_day = pDepthMarketData->TradingDay;
        std::string update_time = pDepthMarketData->UpdateTime;
        int update_millisec = pDepthMarketData->UpdateMillisec;

        // 格式化为 YYYY-MM-DD HH:MM:SS.xxxxx （秒后5位），容错短字符串
        if (update_time.empty()) {
            update_time = "00:00:00";
        }

        std::string date_part;
        if (trading_day.size() >= 8) {
            date_part = trading_day.substr(0, 4) + "-" +
                        trading_day.substr(4, 2) + "-" +
                        trading_day.substr(6, 2);
        } else {
            date_part = trading_day; // 保留原始值，避免越界
        }

        {
            std::ostringstream oss;
            oss << date_part << " " << update_time << ".";
            int frac5 = update_millisec * 100; // ms -> 5位小数
            oss << std::setw(5) << std::setfill('0') << frac5;
            datetime_str = oss.str();
        }
        
        // 计算毫秒时间戳
        try {
            if (trading_day.size() >= 8 && update_time.size() >= 8) {
                // 解析年月日
                int year = std::stoi(trading_day.substr(0, 4));
                int month = std::stoi(trading_day.substr(4, 2));
                int day = std::stoi(trading_day.substr(6, 2));
                
                // 解析时分秒
                int hour = std::stoi(update_time.substr(0, 2));
                int minute = std::stoi(update_time.substr(3, 2));
                int second = std::stoi(update_time.substr(6, 2));
                
                // 转换为time_point
                std::tm tm_struct = {};
                tm_struct.tm_year = year - 1900;
                tm_struct.tm_mon = month - 1;
                tm_struct.tm_mday = day;
                tm_struct.tm_hour = hour;
                tm_struct.tm_min = minute;
                tm_struct.tm_sec = second;
                
                std::time_t time_t_val = std::mktime(&tm_struct);
                auto time_point = std::chrono::system_clock::from_time_t(time_t_val);
                
                // 转换为毫秒时间戳
                auto ms_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
                    time_point.time_since_epoch()).count();
                
                timestamp_ms = ms_since_epoch + update_millisec;
            } else {
                // 解析失败时使用当前时间戳
                timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
            }
        } catch (const std::exception& e) {
            // 异常时使用当前时间戳
            timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }
    }
    inst_data.AddMember("datetime", rapidjson::Value(datetime_str.c_str(), allocator), allocator);
    
    // 3. 卖价和卖量（ask_price10到ask_price1，从高到低）
    inst_data.AddMember("ask_price10", rapidjson::Value().SetNull(), allocator);
    inst_data.AddMember("ask_volume10", rapidjson::Value().SetNull(), allocator);
    inst_data.AddMember("ask_price9", rapidjson::Value().SetNull(), allocator);
    inst_data.AddMember("ask_volume9", rapidjson::Value().SetNull(), allocator);
    inst_data.AddMember("ask_price8", rapidjson::Value().SetNull(), allocator);
    inst_data.AddMember("ask_volume8", rapidjson::Value().SetNull(), allocator);
    inst_data.AddMember("ask_price7", rapidjson::Value().SetNull(), allocator);
    inst_data.AddMember("ask_volume7", rapidjson::Value().SetNull(), allocator);
    inst_data.AddMember("ask_price6", rapidjson::Value().SetNull(), allocator);
    inst_data.AddMember("ask_volume6", rapidjson::Value().SetNull(), allocator);
    
    // CTP提供的5档卖价（检查有效性：大于1e-6且小于1e300）
    double ask5 = pDepthMarketData->AskPrice5;
    if (ask5 > 1e-6 && ask5 < 1e300) {
        inst_data.AddMember("ask_price5", round(ask5 * 100.0) / 100.0, allocator);
        inst_data.AddMember("ask_volume5", pDepthMarketData->AskVolume5, allocator);
    } else {
        inst_data.AddMember("ask_price5", rapidjson::Value().SetNull(), allocator);
        inst_data.AddMember("ask_volume5", rapidjson::Value().SetNull(), allocator);
    }
    
    double ask4 = pDepthMarketData->AskPrice4;
    if (ask4 > 1e-6 && ask4 < 1e300) {
        inst_data.AddMember("ask_price4", round(ask4 * 100.0) / 100.0, allocator);
        inst_data.AddMember("ask_volume4", pDepthMarketData->AskVolume4, allocator);
    } else {
        inst_data.AddMember("ask_price4", rapidjson::Value().SetNull(), allocator);
        inst_data.AddMember("ask_volume4", rapidjson::Value().SetNull(), allocator);
    }
    
    double ask3 = pDepthMarketData->AskPrice3;
    if (ask3 > 1e-6 && ask3 < 1e300) {
        inst_data.AddMember("ask_price3", round(ask3 * 100.0) / 100.0, allocator);
        inst_data.AddMember("ask_volume3", pDepthMarketData->AskVolume3, allocator);
    } else {
        inst_data.AddMember("ask_price3", rapidjson::Value().SetNull(), allocator);
        inst_data.AddMember("ask_volume3", rapidjson::Value().SetNull(), allocator);
    }
    
    double ask2 = pDepthMarketData->AskPrice2;
    if (ask2 > 1e-6 && ask2 < 1e300) {
        inst_data.AddMember("ask_price2", round(ask2 * 100.0) / 100.0, allocator);
        inst_data.AddMember("ask_volume2", pDepthMarketData->AskVolume2, allocator);
    } else {
        inst_data.AddMember("ask_price2", rapidjson::Value().SetNull(), allocator);
        inst_data.AddMember("ask_volume2", rapidjson::Value().SetNull(), allocator);
    }
    
    double ask1 = pDepthMarketData->AskPrice1;
    if (ask1 > 1e-6 && ask1 < 1e300) {
        inst_data.AddMember("ask_price1", round(ask1 * 100.0) / 100.0, allocator);
        inst_data.AddMember("ask_volume1", pDepthMarketData->AskVolume1, allocator);
    } else {
        inst_data.AddMember("ask_price1", rapidjson::Value().SetNull(), allocator);
        inst_data.AddMember("ask_volume1", rapidjson::Value().SetNull(), allocator);
    }
    
    // 4. 买价和买量（bid_price1到bid_price10，从高到低）
    double bid1 = pDepthMarketData->BidPrice1;
    if (bid1 > 1e-6 && bid1 < 1e300) {
        inst_data.AddMember("bid_price1", round(bid1 * 100.0) / 100.0, allocator);
        inst_data.AddMember("bid_volume1", pDepthMarketData->BidVolume1, allocator);
    } else {
        inst_data.AddMember("bid_price1", rapidjson::Value().SetNull(), allocator);
        inst_data.AddMember("bid_volume1", rapidjson::Value().SetNull(), allocator);
    }
    
    double bid2 = pDepthMarketData->BidPrice2;
    if (bid2 > 1e-6 && bid2 < 1e300) {
        inst_data.AddMember("bid_price2", round(bid2 * 100.0) / 100.0, allocator);
        inst_data.AddMember("bid_volume2", pDepthMarketData->BidVolume2, allocator);
    } else {
        inst_data.AddMember("bid_price2", rapidjson::Value().SetNull(), allocator);
        inst_data.AddMember("bid_volume2", rapidjson::Value().SetNull(), allocator);
    }
    
    double bid3 = pDepthMarketData->BidPrice3;
    if (bid3 > 1e-6 && bid3 < 1e300) {
        inst_data.AddMember("bid_price3", round(bid3 * 100.0) / 100.0, allocator);
        inst_data.AddMember("bid_volume3", pDepthMarketData->BidVolume3, allocator);
    } else {
        inst_data.AddMember("bid_price3", rapidjson::Value().SetNull(), allocator);
        inst_data.AddMember("bid_volume3", rapidjson::Value().SetNull(), allocator);
    }
    
    double bid4 = pDepthMarketData->BidPrice4;
    if (bid4 > 1e-6 && bid4 < 1e300) {
        inst_data.AddMember("bid_price4", round(bid4 * 100.0) / 100.0, allocator);
        inst_data.AddMember("bid_volume4", pDepthMarketData->BidVolume4, allocator);
    } else {
        inst_data.AddMember("bid_price4", rapidjson::Value().SetNull(), allocator);
        inst_data.AddMember("bid_volume4", rapidjson::Value().SetNull(), allocator);
    }
    
    double bid5 = pDepthMarketData->BidPrice5;
    if (bid5 > 1e-6 && bid5 < 1e300) {
        inst_data.AddMember("bid_price5", round(bid5 * 100.0) / 100.0, allocator);
        inst_data.AddMember("bid_volume5", pDepthMarketData->BidVolume5, allocator);
    } else {
        inst_data.AddMember("bid_price5", rapidjson::Value().SetNull(), allocator);
        inst_data.AddMember("bid_volume5", rapidjson::Value().SetNull(), allocator);
    }
    
    inst_data.AddMember("bid_price6", rapidjson::Value().SetNull(), allocator);
    inst_data.AddMember("bid_volume6", rapidjson::Value().SetNull(), allocator);
    inst_data.AddMember("bid_price7", rapidjson::Value().SetNull(), allocator);
    inst_data.AddMember("bid_volume7", rapidjson::Value().SetNull(), allocator);
    inst_data.AddMember("bid_price8", rapidjson::Value().SetNull(), allocator);
    inst_data.AddMember("bid_volume8", rapidjson::Value().SetNull(), allocator);
    inst_data.AddMember("bid_price9", rapidjson::Value().SetNull(), allocator);
    inst_data.AddMember("bid_volume9", rapidjson::Value().SetNull(), allocator);
    inst_data.AddMember("bid_price10", rapidjson::Value().SetNull(), allocator);
    inst_data.AddMember("bid_volume10", rapidjson::Value().SetNull(), allocator);
    
    // 5. 其他字段（检查有效性：大于1e-6且小于1e300）
    double last_price = pDepthMarketData->LastPrice;
    if (last_price > 1e-6 && last_price < 1e300) {
        inst_data.AddMember("last_price", round(last_price * 100.0) / 100.0, allocator);
    } else {
        inst_data.AddMember("last_price", rapidjson::Value().SetNull(), allocator);
    }
    
    double highest = pDepthMarketData->HighestPrice;
    if (highest > 1e-6 && highest < 1e300) {
        inst_data.AddMember("highest", round(highest * 100.0) / 100.0, allocator);
    } else {
        inst_data.AddMember("highest", rapidjson::Value().SetNull(), allocator);
    }
    
    double lowest = pDepthMarketData->LowestPrice;
    if (lowest > 1e-6 && lowest < 1e300) {
        inst_data.AddMember("lowest", round(lowest * 100.0) / 100.0, allocator);
    } else {
        inst_data.AddMember("lowest", rapidjson::Value().SetNull(), allocator);
    }
    
    double open = pDepthMarketData->OpenPrice;
    if (open > 1e-6 && open < 1e300) {
        inst_data.AddMember("open", round(open * 100.0) / 100.0, allocator);
    } else {
        inst_data.AddMember("open", rapidjson::Value().SetNull(), allocator);
    }
    
    double close = pDepthMarketData->ClosePrice;
    if (close > 1e-6 && close < 1e300) {
        inst_data.AddMember("close", round(close * 100.0) / 100.0, allocator);
    } else {
        inst_data.AddMember("close", rapidjson::Value("-", allocator), allocator);
    }
    
    inst_data.AddMember("average", rapidjson::Value().SetNull(), allocator);

    inst_data.AddMember("volume", pDepthMarketData->Volume, allocator);
    inst_data.AddMember("amount", pDepthMarketData->Turnover, allocator);
    inst_data.AddMember("open_interest", static_cast<int64_t>(pDepthMarketData->OpenInterest), allocator);
    double settlement = pDepthMarketData->SettlementPrice;
    if (settlement > 1e-6 && settlement < 1e300) {
        inst_data.AddMember("settlement", round(settlement * 100.0) / 100.0, allocator);
    } else {
        inst_data.AddMember("settlement", rapidjson::Value("-", allocator), allocator);
    }
    
    double upper_limit = pDepthMarketData->UpperLimitPrice;
    if (upper_limit > 1e-6 && upper_limit < 1e300) {
        inst_data.AddMember("upper_limit", round(upper_limit * 100.0) / 100.0, allocator);
    } else {
        inst_data.AddMember("upper_limit", rapidjson::Value().SetNull(), allocator);
    }
    
    double lower_limit = pDepthMarketData->LowerLimitPrice;
    if (lower_limit > 1e-6 && lower_limit < 1e300) {
        inst_data.AddMember("lower_limit", round(lower_limit * 100.0) / 100.0, allocator);
    } else {
        inst_data.AddMember("lower_limit", rapidjson::Value().SetNull(), allocator);
    }
    
    inst_data.AddMember("pre_open_interest", static_cast<int64_t>(pDepthMarketData->PreOpenInterest), allocator);
    
    double pre_settlement = pDepthMarketData->PreSettlementPrice;
    if (pre_settlement > 1e-6 && pre_settlement < 1e300) {
        inst_data.AddMember("pre_settlement", round(pre_settlement * 100.0) / 100.0, allocator);
    } else {
        inst_data.AddMember("pre_settlement", rapidjson::Value().SetNull(), allocator);
    }
    
    double pre_close = pDepthMarketData->PreClosePrice;
    if (pre_close > 1e-6 && pre_close < 1e300) {
        inst_data.AddMember("pre_close", round(pre_close * 100.0) / 100.0, allocator);
    } else {
        inst_data.AddMember("pre_close", rapidjson::Value().SetNull(), allocator);
    }
    
    return inst_data;
}

void MarketDataSpi::OnRtnDepthMarketData(CThostFtdcDepthMarketDataField *pDepthMarketData)
{
    if (!pDepthMarketData) return;

    // Debug打印行情数据接收信息
    server_->log_info("DEBUG: Received market data for instrument: " + std::string(pDepthMarketData->InstrumentID) + 
                     ", price: " + std::to_string(pDepthMarketData->LastPrice) + 
                     ", volume: " + std::to_string(pDepthMarketData->Volume));
    
    std::string instrument_id = pDepthMarketData->InstrumentID;
    
    // 通过映射表查找带前缀的格式
    auto map_it = server_->noheadtohead_instruments_map_.find(instrument_id);
    std::string display_instrument = (map_it != server_->noheadtohead_instruments_map_.end()) 
        ? map_it->second : instrument_id;
    
    // 构建标准格式的行情数据
    rapidjson::Document doc;
    doc.SetObject();
    auto& allocator = doc.GetAllocator();
    
    long long timestamp_ms;
    rapidjson::Value inst_data = MarketDataServer::build_quote_data(pDepthMarketData, display_instrument, allocator, timestamp_ms);
    
    // 转换为JSON字符串用于Redis存储和内存缓存
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    inst_data.Accept(writer);
    std::string json_data = buffer.GetString();
    
    // 存储到Redis
    server_->store_market_data_to_redis(instrument_id, json_data, timestamp_ms);
    
    // 缓存行情数据（用于peek_message）
    server_->cache_market_data(instrument_id, json_data);
}

void MarketDataSpi::OnRspError(CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    if (pRspInfo && pRspInfo->ErrorID != 0) {
        server_->log_error("CTP error: " + std::string(pRspInfo->ErrorMsg));
    }
}

// MarketDataServer实现
MarketDataServer::MarketDataServer(const std::string& ctp_front_addr,
                                 const std::string& broker_id,
                                 int websocket_port)
    : ctp_front_addr_(ctp_front_addr)
    , broker_id_(broker_id)
    , ioc_()
    , websocket_port_(websocket_port)
    , ctp_api_(nullptr)
    , ctp_connected_(false)
    , ctp_logged_in_(false)
    , acceptor_(ioc_)
    , segment_(nullptr)
    , alloc_inst_(nullptr)
    , ins_map_(nullptr)
    , is_running_(false)
    , request_id_(0)
    , use_multi_ctp_mode_(false)
    , redis_client_(std::make_unique<RedisClient>("192.168.2.27", 6379))
{
}

MarketDataServer::MarketDataServer(const MultiCTPConfig& config)
    : broker_id_(config.connections.empty() ? "9999" : config.connections[0].broker_id)
    , ioc_()
    , websocket_port_(config.websocket_port)
    , ctp_api_(nullptr)
    , ctp_connected_(false)
    , ctp_logged_in_(false)
    , acceptor_(ioc_)
    , segment_(nullptr)
    , alloc_inst_(nullptr)
    , ins_map_(nullptr)
    , multi_ctp_config_(config)
    , use_multi_ctp_mode_(true)
    , is_running_(false)
    , request_id_(0)
    , redis_client_(std::make_unique<RedisClient>(config.redis_host, config.redis_port))
{
}

MarketDataServer::~MarketDataServer()
{
    stop();
    cleanup_shared_memory();
    if (use_multi_ctp_mode_) {
        cleanup_multi_ctp_system();
    }
}

bool MarketDataServer::start()
{
    if (is_running_) {
        return true;
    }
    
    std::string mode = use_multi_ctp_mode_ ? "multi-CTP" : "single-CTP";
    log_info("Starting MarketData Server in " + mode + " mode...");
    
    try {
        // 初始化共享内存
        init_shared_memory();
        
        // 初始化Redis连接
        std::string redis_info;
        if (use_multi_ctp_mode_) {
            redis_info = multi_ctp_config_.redis_host + ":" + std::to_string(multi_ctp_config_.redis_port);
        } else {
            redis_info = "192.168.2.27:6379";
        }
            
        if (!redis_client_->connect()) {
            log_error("Failed to connect to Redis server at " + redis_info);
            log_warning("Market data will not be stored in Redis");
        } else {
            log_info("Connected to Redis server at " + redis_info);
        }
        
        // 启动WebSocket服务器
        start_websocket_server();
        
        if (use_multi_ctp_mode_) {
            // 多CTP连接模式
            if (!init_multi_ctp_system()) {
                log_error("Failed to initialize multi-CTP system");
                return false;
            }
        } else {
            // 单CTP连接模式（兼容性）
            std::string flow_path = "./ctpflow/single/";
            
            // 确保flow目录存在
            std::string mkdir_cmd = "mkdir -p " + flow_path;
            if (system(mkdir_cmd.c_str()) != 0) {
                log_warning("Failed to create flow directory: " + flow_path);
            }
            
            ctp_api_ = CThostFtdcMdApi::CreateFtdcMdApi(flow_path.c_str());
            if (!ctp_api_) {
                log_error("Failed to create CTP API");
                return false;
            }
            
            md_spi_ = std::make_unique<MarketDataSpi>(this);
            ctp_api_->RegisterSpi(md_spi_.get());
            ctp_api_->RegisterFront(const_cast<char*>(ctp_front_addr_.c_str()));
            ctp_api_->Init(); 
        }
        
        is_running_ = true;
        
        // 启动服务器线程
        server_thread_ = boost::thread([this]() {
            ioc_.run();
        });
        
        log_info("MarketData Server started on port " + std::to_string(websocket_port_));
        return true;
        
    } catch (const std::exception& e) {
        log_error("Failed to start server: " + std::string(e.what()));
        return false;
    }
}

void MarketDataServer::stop()
{
    if (!is_running_) {
        return;
    }
    
    log_info("Stopping MarketData Server...");
    is_running_ = false;
    
    // 关闭所有WebSocket连接
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        for (auto& pair : sessions_) {
            pair.second->close();
        }
        sessions_.clear();
    }
    
    // 停止IO上下文
    ioc_.stop();
    
    // 等待服务器线程结束
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    
    // 清理CTP资源
    if (ctp_api_) {
        ctp_api_->Release();
        ctp_api_ = nullptr;
    }
    
    log_info("MarketData Server stopped");
}

void MarketDataServer::init_shared_memory()
{
    try {
        // 尝试连接到现有的共享内存段
        segment_ = new boost::interprocess::managed_shared_memory(
            boost::interprocess::open_only, "qamddata");
        
        alloc_inst_ = new ShmemAllocator(segment_->get_segment_manager());
        ins_map_ = segment_->find<InsMapType>("InsMap").first;
        
        if (ins_map_) {
            log_info("Connected to existing shared memory segment with " + 
                    std::to_string(ins_map_->size()) + " instruments");
        } else {
            log_warning("Shared memory segment found but InsMap not found");
        }
        
    } catch (const boost::interprocess::interprocess_exception& e) {
        log_warning("Failed to connect to existing shared memory: " + std::string(e.what()));
        log_info("Creating new shared memory segment");
        
        try {
            boost::interprocess::shared_memory_object::remove("qamddata");
            
            segment_ = new boost::interprocess::managed_shared_memory(
                boost::interprocess::create_only,
                "qamddata",
                32 * 1024 * 1024);  // 32MB
            
            alloc_inst_ = new ShmemAllocator(segment_->get_segment_manager());
            ins_map_ = segment_->construct<InsMapType>("InsMap")(
                CharArrayComparer(), *alloc_inst_);
            
            log_info("Created new shared memory segment");
            
        } catch (const std::exception& e) {
            log_error("Failed to create shared memory: " + std::string(e.what()));
            throw;
        }
    }
}

void MarketDataServer::cleanup_shared_memory()
{
    if (alloc_inst_) {
        delete alloc_inst_;
        alloc_inst_ = nullptr;
    }
    
    if (segment_) {
        delete segment_;
        segment_ = nullptr;
    }
    
    ins_map_ = nullptr;
}

void MarketDataServer::start_websocket_server()
{
    auto const address = net::ip::make_address("0.0.0.0");
    auto const port = static_cast<unsigned short>(websocket_port_);
    
    tcp::endpoint endpoint{address, port};
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(net::socket_base::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen(net::socket_base::max_listen_connections);
    
    // 开始接受连接
    acceptor_.async_accept(
        net::make_strand(ioc_),
        beast::bind_front_handler(&MarketDataServer::handle_accept, this));
}

void MarketDataServer::handle_accept(beast::error_code ec, tcp::socket socket)
{
    if (ec) {
        log_error("Accept error: " + ec.message());
    } else {
        // 创建新的会话
        auto session = std::make_shared<WebSocketSession>(std::move(socket), this);
        add_session(session);
        session->run();
    }
    
    // 继续接受连接
    acceptor_.async_accept(
        net::make_strand(ioc_),
        beast::bind_front_handler(&MarketDataServer::handle_accept, this));
}

void MarketDataServer::ctp_login()
{
    CThostFtdcReqUserLoginField req;
    memset(&req, 0, sizeof(req));
    
    // 行情API登录只需要BrokerID，不需要用户名和密码
    strcpy(req.BrokerID, broker_id_.c_str());
    // 行情登录可以使用空的用户ID和密码
    strcpy(req.UserID, "");
    strcpy(req.Password, "");
    
    int ret = ctp_api_->ReqUserLogin(&req, ++request_id_);
    if (ret != 0) {
        log_error("Failed to send market data login request, return code: " + std::to_string(ret));
    } else {
        ctp_connected_ = true;
        ctp_logged_in_ = true;
        log_info("Market data login request sent");
    }
}

std::string MarketDataServer::create_session_id()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(100000, 999999);
    
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << "session_" << time_t << "_" << ms.count() << "_" << dis(gen);
    return oss.str();
}

void MarketDataServer::add_session(std::shared_ptr<WebSocketSession> session)
{
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    sessions_[session->get_session_id()] = session;
}

void MarketDataServer::remove_session(const std::string& session_id)
{
    if (use_multi_ctp_mode_) {
        // 多CTP连接模式：使用订阅分发器
        if (subscription_dispatcher_) {
            subscription_dispatcher_->remove_all_subscriptions_for_session(session_id);
        }
    } else {
        // 单CTP连接模式（兼容性）
        std::lock_guard<std::mutex> lock2(subscribers_mutex_);
        
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            // 移除该会话的所有订阅
            const auto& subscriptions = it->second->get_subscriptions();
            for (const auto& instrument_id : subscriptions) {
                auto sub_it = instrument_subscribers_.find(instrument_id);
                if (sub_it != instrument_subscribers_.end()) {
                    sub_it->second.erase(session_id);
                    // 如果没有会话订阅该合约了，从CTP取消订阅
                    if (sub_it->second.empty()) {
                        instrument_subscribers_.erase(sub_it);
                        
                        if (ctp_api_ && ctp_logged_in_) {
                            char* instruments[] = {const_cast<char*>(instrument_id.c_str())};
                            int ret = ctp_api_->UnSubscribeMarketData(instruments, 1);
                            if (ret == 0) {
                                log_info("Auto-unsubscribed from CTP market data: " + instrument_id + 
                                       " (session disconnected)");
                            } else {
                                log_error("Failed to auto-unsubscribe from CTP market data: " + instrument_id +
                                         ", return code: " + std::to_string(ret));
                            }
                        }
                    }
                }
            }
        }
    }
    
    // 通用的session移除逻辑
    std::lock_guard<std::mutex> lock1(sessions_mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        sessions_.erase(it);
        log_info("Session removed: " + session_id);
    }
    
    // 清理上次发送的json缓存
    {
        std::lock_guard<std::mutex> lock_json(session_last_sent_mutex_);
        session_last_sent_json_.erase(session_id);
    }
    
    // 清理挂起队列
    {
        std::lock_guard<std::mutex> lock_pending(pending_peek_mutex_);
        pending_peek_sessions_.erase(session_id);
    }
}

void MarketDataServer::subscribe_instrument(const std::string& session_id, const std::string& instrument_id)
{
    if (use_multi_ctp_mode_) {
        // 多CTP连接模式：使用订阅分发器
        if (subscription_dispatcher_) {
            subscription_dispatcher_->add_subscription(session_id, instrument_id);
        }
        
        // 同时维护instrument_subscribers_映射以支持broadcast_market_data
        {
            std::lock_guard<std::mutex> lock(subscribers_mutex_);
            instrument_subscribers_[instrument_id].insert(session_id);
        }
    } else {
        // 单CTP连接模式（兼容性）
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        
        instrument_subscribers_[instrument_id].insert(session_id);
        
        // 如果这是第一个订阅该合约的会话，向CTP订阅行情
        if (instrument_subscribers_[instrument_id].size() == 1 && ctp_api_ && ctp_logged_in_) {
            char* instruments[] = {const_cast<char*>(instrument_id.c_str())};
            int ret = ctp_api_->SubscribeMarketData(instruments, 1);
            if (ret == 0) {
                log_info("Subscribed to CTP market data: " + instrument_id);
            } else {
                log_error("Failed to subscribe to CTP market data: " + instrument_id + 
                         ", return code: " + std::to_string(ret));
            }
        }
    }
}

void MarketDataServer::unsubscribe_instrument(const std::string& session_id, const std::string& instrument_id)
{
    if (use_multi_ctp_mode_) {
        // 多CTP连接模式：使用订阅分发器
        if (subscription_dispatcher_) {
            subscription_dispatcher_->remove_subscription(session_id, instrument_id);
        }
        
        // 同时维护instrument_subscribers_映射
        {
            std::lock_guard<std::mutex> lock(subscribers_mutex_);
            auto it = instrument_subscribers_.find(instrument_id);
            if (it != instrument_subscribers_.end()) {
                it->second.erase(session_id);
                if (it->second.empty()) {
                    instrument_subscribers_.erase(it);
                }
            }
        }
    } else {
        // 单CTP连接模式（兼容性）
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        
        auto it = instrument_subscribers_.find(instrument_id);
        if (it != instrument_subscribers_.end()) {
            it->second.erase(session_id);
            
            // 如果没有会话订阅该合约了，从CTP取消订阅
            if (it->second.empty()) {
                instrument_subscribers_.erase(it);
                
                if (ctp_api_ && ctp_logged_in_) {
                    char* instruments[] = {const_cast<char*>(instrument_id.c_str())};
                    int ret = ctp_api_->UnSubscribeMarketData(instruments, 1);
                    if (ret == 0) {
                        log_info("Unsubscribed from CTP market data: " + instrument_id);
                    } else {
                        log_error("Failed to unsubscribe from CTP market data: " + instrument_id +
                                 ", return code: " + std::to_string(ret));
                    }
                }
            }
        }
    }
}

void MarketDataServer::broadcast_market_data(const std::string& instrument_id, const std::string& json_data)
{
    // 不再立即广播，而是缓存行情数据
    cache_market_data(instrument_id, json_data);
}

void MarketDataServer::cache_market_data(const std::string& instrument_id, const std::string& json_data)
{
    {
        std::lock_guard<std::mutex> lock(market_data_cache_mutex_);
        market_data_cache_[instrument_id] = json_data;
    }
    
    // 检查是否有挂起的session需要被唤醒
    notify_pending_sessions(instrument_id);
}

void MarketDataServer::handle_peek_message(const std::string& session_id)
{
    std::lock_guard<std::mutex> lock1(sessions_mutex_);
    std::lock_guard<std::mutex> lock2(subscribers_mutex_);
    std::lock_guard<std::mutex> lock3(market_data_cache_mutex_);
    std::lock_guard<std::mutex> lock4(session_last_sent_mutex_);
    std::lock_guard<std::mutex> lock5(pending_peek_mutex_);
    
    auto session_it = sessions_.find(session_id);
    if (session_it == sessions_.end()) {
        return;
    }
    
    // 获取该session订阅的所有合约（仅保留已有缓存的条目）
    const auto& subscriptions = session_it->second->get_subscriptions();
    if (subscriptions.empty()) {
        return;
    }

    std::vector<std::string> cached_instruments;
    cached_instruments.reserve(subscriptions.size());
    for (const auto& instrument_id : subscriptions) {
        if (market_data_cache_.find(instrument_id) != market_data_cache_.end()) {
            cached_instruments.push_back(instrument_id);
        }
    }
    if (cached_instruments.empty()) {
        // 当沒有缓存数据时，也发送一个空的rtn_data回报
        send_empty_rtn_data(session_id);
        return;
    }
 
    // 构建完整的新消息（全量数据）
    rapidjson::Document full_response;
    full_response.SetObject();
    auto& allocator = full_response.GetAllocator();

    rapidjson::Value data_array(rapidjson::kArrayType);
    rapidjson::Value data_obj(rapidjson::kObjectType);
    rapidjson::Value quotes_obj(rapidjson::kObjectType);

    std::ostringstream ins_join_oss;
    bool first = true;

    for (const auto& instrument_id : cached_instruments) {
        auto cache_it = market_data_cache_.find(instrument_id);
        if (cache_it == market_data_cache_.end()) {
            continue;
        }

        rapidjson::Document cached_quote;
        cached_quote.Parse(cache_it->second.c_str());
        if (cached_quote.HasParseError() || !cached_quote.IsObject()) {
            continue;
        }

        auto map_it = noheadtohead_instruments_map_.find(instrument_id);
        std::string display_instrument = (map_it != noheadtohead_instruments_map_.end())
            ? map_it->second : instrument_id;

        if (!first) ins_join_oss << ",";
        ins_join_oss << display_instrument;
        first = false;

        rapidjson::Value inst_data_copy(cached_quote, allocator);
        quotes_obj.AddMember(rapidjson::Value(display_instrument.c_str(), allocator), inst_data_copy, allocator);
    }

    data_obj.AddMember("quotes", quotes_obj, allocator);
    data_array.PushBack(data_obj, allocator);

    rapidjson::Value meta_obj(rapidjson::kObjectType);
    meta_obj.AddMember("account_id", rapidjson::Value("", allocator), allocator);
    meta_obj.AddMember("ins_list", rapidjson::Value("", allocator), allocator);
    meta_obj.AddMember("mdhis_more_data", false, allocator);
    data_array.PushBack(meta_obj, allocator);

    full_response.AddMember("aid", "rtn_data", allocator);
    full_response.AddMember("data", data_array, allocator);

    // 将全量响应转为字符串，用于后续保存
    rapidjson::StringBuffer full_buffer;
    rapidjson::Writer<rapidjson::StringBuffer> full_writer(full_buffer);
    full_response.Accept(full_writer);
    std::string full_response_str = full_buffer.GetString();

    // 检查是否有上次发送的消息
    auto last_sent_it = session_last_sent_json_.find(session_id);
    
    if (last_sent_it == session_last_sent_json_.end()) {
        // 没有上次消息，发送全量
        session_it->second->send_message(full_response_str);
        session_last_sent_json_[session_id] = full_response_str;
    } else {
        // 有上次消息，计算增量
        rapidjson::Document old_doc;
        old_doc.Parse(last_sent_it->second.c_str());
        
        if (old_doc.HasParseError() || !old_doc.IsObject()) {
            // 上次消息解析失败，发送全量
            session_it->second->send_message(full_response_str);
            session_last_sent_json_[session_id] = full_response_str;
            return;
        }
        
        // 提取data[0].quotes对象进行比较
        if (!old_doc.HasMember("data") || !old_doc["data"].IsArray() || old_doc["data"].Size() == 0) {
            // 旧消息格式不正确，发送全量
            session_it->second->send_message(full_response_str);
            session_last_sent_json_[session_id] = full_response_str;
            return;
        }
        
        const rapidjson::Value& old_data_array = old_doc["data"];
        if (!old_data_array[0].IsObject() || !old_data_array[0].HasMember("quotes")) {
            // 旧消息格式不正确，发送全量
            session_it->second->send_message(full_response_str);
            session_last_sent_json_[session_id] = full_response_str;
            return;
        }
        
        const rapidjson::Value& old_quotes = old_data_array[0]["quotes"];
        const rapidjson::Value& new_quotes = full_response["data"][0]["quotes"];
        
        // 计算quotes的差异
        rapidjson::Document diff_response;
        diff_response.SetObject();
        auto& diff_allocator = diff_response.GetAllocator();
        
        rapidjson::Value diff_quotes(rapidjson::kObjectType);
        ComputeJsonDiff(old_quotes, new_quotes, diff_quotes, diff_allocator);
        
        // 如果没有差异，将session加入挂起队列，等待行情变化
        if (diff_quotes.MemberCount() == 0) {
            pending_peek_sessions_.insert(session_id);
            log_info("Pending peek_message for session: " + session_id + " (no market data change)");
            return;  // 不发送响应，挂起请求
        }
        
        // 构建增量响应
        rapidjson::Value diff_data_array(rapidjson::kArrayType);
        rapidjson::Value diff_data_obj(rapidjson::kObjectType);
        diff_data_obj.AddMember("quotes", diff_quotes, diff_allocator);
        diff_data_array.PushBack(diff_data_obj, diff_allocator);
        
        rapidjson::Value diff_meta_obj(rapidjson::kObjectType);
        diff_meta_obj.AddMember("account_id", rapidjson::Value("", diff_allocator), diff_allocator);
        diff_meta_obj.AddMember("ins_list", rapidjson::Value("", diff_allocator), diff_allocator);
        diff_meta_obj.AddMember("mdhis_more_data", false, diff_allocator);
        diff_data_array.PushBack(diff_meta_obj, diff_allocator);
        
        diff_response.AddMember("aid", "rtn_data", diff_allocator);
        diff_response.AddMember("data", diff_data_array, diff_allocator);
        
        // 发送增量消息
        rapidjson::StringBuffer diff_buffer;
        rapidjson::Writer<rapidjson::StringBuffer> diff_writer(diff_buffer);
        diff_response.Accept(diff_writer);
        std::string diff_response_str = diff_buffer.GetString();
        
        session_it->second->send_message(diff_response_str);
        
        // 更新缓存为全量消息
        session_last_sent_json_[session_id] = full_response_str;
    }
}

void MarketDataServer::notify_pending_sessions(const std::string& instrument_id)
{
    std::set<std::string> sessions_to_notify;
    
    // 获取订阅了该合约且处于挂起状态的session
    {
        std::lock_guard<std::mutex> lock1(subscribers_mutex_);
        std::lock_guard<std::mutex> lock2(pending_peek_mutex_);
        
        auto sub_it = instrument_subscribers_.find(instrument_id);
        if (sub_it == instrument_subscribers_.end()) {
            return;
        }
        
        // 找出既订阅了该合约，又在挂起队列中的session
        for (const auto& session_id : sub_it->second) {
            if (pending_peek_sessions_.find(session_id) != pending_peek_sessions_.end()) {
                sessions_to_notify.insert(session_id);
                pending_peek_sessions_.erase(session_id);  // 从挂起队列移除
            }
        }
    }
    
    // 唤醒这些session，触发数据推送
    for (const auto& session_id : sessions_to_notify) {
        log_info("Waking up pending session: " + session_id + " due to market data update: " + instrument_id);
        handle_peek_message(session_id);  // 重新处理peek_message
    }
}

void MarketDataServer::send_to_session(const std::string& session_id, const std::string& message)
{
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        it->second->send_message(message);
    }
}

void MarketDataServer::store_market_data_to_redis(const std::string& instrument_id,
                                                  const std::string& json_data,
                                                  long long& timestamp_ms)
{
    auto& redis_client = get_redis_client();
    if (!redis_client || !redis_client->is_connected()) {
        return;
    }

    // 最新
    if (!redis_client->set(instrument_id, json_data)) {
        log_warning("Failed to store latest market data to Redis for instrument: " + instrument_id);
    }

    // 历史
    if (timestamp_ms > 0) {
        std::string history_key = "history:" + instrument_id;
        if (!redis_client->zadd(history_key, timestamp_ms, json_data)) {
            log_warning("Failed to store historical market data to Redis for instrument: " + instrument_id);
        }
        const long long history_size = redis_client->zcard(history_key);
        if (history_size >= 100000) {
            const long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            const long long expire_before_ms = now_ms - static_cast<long long>(2) * 24 * 3600 * 1000; // 保留最近2天
            if (!redis_client->zremrangebyscore(history_key, 0, expire_before_ms)) {
                log_warning("Failed to remove historical market data from Redis for instrument: " + instrument_id);
            }
        }
    }
}

std::vector<std::string> MarketDataServer::get_all_instruments()
{
    std::vector<std::string> instruments;
    
    if (ins_map_) {
        for (auto it = ins_map_->begin(); it != ins_map_->end(); ++it) {
            std::string key(it->first.data());
            // 移除末尾的空字符
            key.erase(std::find(key.begin(), key.end(), '\0'), key.end());
            if (!key.empty()) {
                instruments.push_back(key);
            }
        }
    }
    
    return instruments;
}

std::vector<std::string> MarketDataServer::search_instruments(const std::string& pattern)
{
    std::vector<std::string> matching_instruments;
    
    if (ins_map_) {
        std::string lower_pattern = pattern;
        std::transform(lower_pattern.begin(), lower_pattern.end(), lower_pattern.begin(), ::tolower);
        
        for (auto it = ins_map_->begin(); it != ins_map_->end(); ++it) {
            std::string key(it->first.data());
            key.erase(std::find(key.begin(), key.end(), '\0'), key.end());
            
            if (!key.empty()) {
                std::string lower_key = key;
                std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
                
                if (lower_key.find(lower_pattern) != std::string::npos) {
                    matching_instruments.push_back(key);
                }
            }
        }
    }
    
    return matching_instruments;
}

void MarketDataServer::log_info(const std::string& message)
{
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    struct tm result_tm;
    
    std::cout << "[" << std::put_time(localtime_r(&time_t, &result_tm), "%Y-%m-%d %H:%M:%S") 
              << "] [INFO] " << message << '\n';
}

void MarketDataServer::log_error(const std::string& message)
{
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    struct tm result_tm;
    
    std::cerr << "[" << std::put_time(localtime_r(&time_t, &result_tm), "%Y-%m-%d %H:%M:%S") 
              << "] [ERROR] " << message << '\n';
}

void MarketDataServer::log_warning(const std::string& message)
{
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    struct tm result_tm;
    
    std::cout << "[" << std::put_time(localtime_r(&time_t, &result_tm), "%Y-%m-%d %H:%M:%S") 
              << "] [WARNING] " << message << '\n';
}

// 多CTP系统实现
bool MarketDataServer::init_multi_ctp_system()
{
    log_info("Initializing multi-CTP system...");
    
    try {
        // 创建订阅分发器
        subscription_dispatcher_ = std::make_unique<SubscriptionDispatcher>(this);
        
        // 创建连接管理器
        connection_manager_ = std::make_unique<CTPConnectionManager>(this, subscription_dispatcher_.get());
        
        // 初始化订阅分发器
        if (!subscription_dispatcher_->initialize(connection_manager_.get())) {
            log_error("Failed to initialize subscription dispatcher");
            return false;
        }
        
        // 设置负载均衡策略
        subscription_dispatcher_->set_load_balance_strategy(multi_ctp_config_.load_balance_strategy);
        
        // 添加所有连接配置
        for (const auto& conn_config : multi_ctp_config_.connections) {
            if (conn_config.enabled) {
                if (!connection_manager_->add_connection(conn_config)) {
                    log_error("Failed to add connection: " + conn_config.connection_id);
                    return false;
                }
                log_info("Added CTP connection: " + conn_config.connection_id + 
                        " -> " + conn_config.front_addr);
            } else {
                log_info("Skipped disabled connection: " + conn_config.connection_id);
            }
        }
        
        // 启动所有连接
        if (!connection_manager_->start_all_connections()) {
            log_warning("Some CTP connections failed to start");
        }
        
        log_info("Multi-CTP system initialized successfully with " + 
                std::to_string(connection_manager_->get_total_connections()) + " connections");
        return true;
        
    } catch (const std::exception& e) {
        log_error("Exception initializing multi-CTP system: " + std::string(e.what()));
        return false;
    }
}

void MarketDataServer::cleanup_multi_ctp_system()
{
    if (connection_manager_) {
        connection_manager_->stop_all_connections();
        connection_manager_.reset();
    }
    
    if (subscription_dispatcher_) {
        subscription_dispatcher_->shutdown();
        subscription_dispatcher_.reset();
    }
    
    log_info("Multi-CTP system cleaned up");
}

// 多连接版本的状态查询
bool MarketDataServer::is_ctp_connected() const
{
    if (use_multi_ctp_mode_) {
        return connection_manager_ && connection_manager_->get_active_connections() > 0;
    } else {
        return ctp_connected_;
    }
}

bool MarketDataServer::is_ctp_logged_in() const
{
    if (use_multi_ctp_mode_) {
        return connection_manager_ && connection_manager_->get_active_connections() > 0;
    } else {
        return ctp_logged_in_;
    }
}

size_t MarketDataServer::get_active_connections_count() const
{
    if (use_multi_ctp_mode_ && connection_manager_) {
        return connection_manager_->get_active_connections();
    }
    return ctp_logged_in_ ? 1 : 0;
}

std::vector<std::string> MarketDataServer::get_connection_status() const
{
    std::vector<std::string> status_list;
    
    if (use_multi_ctp_mode_ && connection_manager_) {
        auto connections = connection_manager_->get_all_connections();
        for (const auto& conn : connections) {
            std::string status = conn->get_connection_id() + ": ";
            switch (conn->get_status()) {
                case CTPConnectionStatus::DISCONNECTED:
                    status += "DISCONNECTED";
                    break;
                case CTPConnectionStatus::CONNECTING:
                    status += "CONNECTING";
                    break;
                case CTPConnectionStatus::CONNECTED:
                    status += "CONNECTED";
                    break;
                case CTPConnectionStatus::LOGGED_IN:
                    status += "LOGGED_IN (" + std::to_string(conn->get_subscription_count()) + " subs)";
                    break;
                case CTPConnectionStatus::ERROR:
                    status += "ERROR";
                    break;
            }
            status += " [Quality: " + std::to_string(conn->get_connection_quality()) + "%]";
            status_list.push_back(status);
        }
    } else {
        std::string status = "single_ctp: ";
        if (ctp_logged_in_) {
            status += "LOGGED_IN";
        } else if (ctp_connected_) {
            status += "CONNECTED";
        } else {
            status += "DISCONNECTED";
        }
        status_list.push_back(status);
    }
    
    return status_list;
}

void MarketDataServer::send_empty_rtn_data(const std::string& session_id)
{
    auto session_it = sessions_.find(session_id);
    if (session_it == sessions_.end()) {
        return;
    }

    rapidjson::Document response;
    response.SetObject();
    auto& allocator = response.GetAllocator();

    rapidjson::Value data_array(rapidjson::kArrayType);
    rapidjson::Value data_obj(rapidjson::kObjectType);
    rapidjson::Value quotes_obj(rapidjson::kObjectType);
    
    data_obj.AddMember("quotes", quotes_obj, allocator);
    data_array.PushBack(data_obj, allocator);

    rapidjson::Value meta_obj(rapidjson::kObjectType);
    meta_obj.AddMember("account_id", rapidjson::Value("", allocator), allocator);
    meta_obj.AddMember("ins_list", rapidjson::Value("", allocator), allocator);
    meta_obj.AddMember("mdhis_more_data", false, allocator);
    data_array.PushBack(meta_obj, allocator);

    response.AddMember("aid", "rtn_data", allocator);
    response.AddMember("data", data_array, allocator);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    response.Accept(writer);
    
    session_it->second->send_message(buffer.GetString());
}