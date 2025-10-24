/////////////////////////////////////////////////////////////////////////
///@file log.h
///@brief	日志操作
///@copyright	上海信易信息科技股份有限公司 版权所有 
/////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include <boost/asio.hpp>

typedef enum 
{
    LOG_FATAL,
    LOG_ERROR,
    LOG_WARNING,
    LOG_INFO,
    LOG_DEBUG
} LogLevel;

void Log(LogLevel level,const char* pack_str,const char* message_fmt, ...);

void LogMs(LogLevel level, const char* pack_str, const char* message_fmt, ...);