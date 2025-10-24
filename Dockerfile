# QuantAxis CTP Market Data Gateway Dockerfile
# 基于Ubuntu 20.04构建独立的多CTP连接行情网关
FROM ubuntu:20.04

LABEL maintainer="QuantAxis Team"
LABEL description="Multi-CTP Market Data Gateway with 90+ futures brokers support"
LABEL version="2.0.0"

# 设置非交互式安装
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

# 创建工作用户
RUN groupadd -r quantaxis && useradd -r -g quantaxis -m -d /home/quantaxis -s /bin/bash quantaxis

# 安装系统依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    g++ \
    gcc \
    gdb \
    git \
    pkg-config \
    wget \
    curl \
    unzip \
    # Boost库
    libboost-all-dev \
    # 网络和加密库
    libssl-dev \
    libcurl4-openssl-dev \
    # Redis客户端
    libhiredis-dev \
    # JSON库
    rapidjson-dev \
    # 系统工具
    net-tools \
    htop \
    vim \
    tzdata \
    # 清理缓存
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# 设置时区
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

# 创建应用目录
RUN mkdir -p /opt/quantaxis/qactpmdgateway/{bin,config,logs,ctpflow,backup}
RUN chown -R quantaxis:quantaxis /opt/quantaxis

# 设置工作目录
WORKDIR /opt/quantaxis/qactpmdgateway

# 复制项目文件
COPY --chown=quantaxis:quantaxis . .

# 确保CTP库文件有正确的权限
RUN chmod +x libs/*.so

# 编译项目
RUN make clean && make all

# 创建配置目录结构
RUN mkdir -p config logs ctpflow backup \
    && chown -R quantaxis:quantaxis .

# 暴露WebSocket端口
EXPOSE 7799

# 切换到quantaxis用户
USER quantaxis

# 健康检查
HEALTHCHECK --interval=30s --timeout=10s --start-period=60s --retries=3 \
    CMD curl -f http://localhost:7799/ || exit 1

# 创建启动脚本
RUN echo '#!/bin/bash' > /opt/quantaxis/qactpmdgateway/start.sh && \
    echo 'set -e' >> /opt/quantaxis/qactpmdgateway/start.sh && \
    echo '' >> /opt/quantaxis/qactpmdgateway/start.sh && \
    echo 'echo "🚀 Starting QuantAxis Multi-CTP Market Data Gateway..."' >> /opt/quantaxis/qactpmdgateway/start.sh && \
    echo 'echo "======================================================"' >> /opt/quantaxis/qactpmdgateway/start.sh && \
    echo 'echo "Version: 2.0.0"' >> /opt/quantaxis/qactpmdgateway/start.sh && \
    echo 'echo "Supports: 90+ CTP connections, 25000+ subscriptions"' >> /opt/quantaxis/qactpmdgateway/start.sh && \
    echo 'echo "WebSocket: ws://0.0.0.0:7799"' >> /opt/quantaxis/qactpmdgateway/start.sh && \
    echo 'echo "======================================================"' >> /opt/quantaxis/qactpmdgateway/start.sh && \
    echo '' >> /opt/quantaxis/qactpmdgateway/start.sh && \
    echo '# 检查配置文件' >> /opt/quantaxis/qactpmdgateway/start.sh && \
    echo 'if [ ! -f "config/multi_ctp_config.json" ]; then' >> /opt/quantaxis/qactpmdgateway/start.sh && \
    echo '    echo "⚠️  配置文件不存在，使用默认多CTP配置"' >> /opt/quantaxis/qactpmdgateway/start.sh && \
    echo '    exec ./bin/market_data_server --multi-ctp "$@"' >> /opt/quantaxis/qactpmdgateway/start.sh && \
    echo 'else' >> /opt/quantaxis/qactpmdgateway/start.sh && \
    echo '    echo "✅ 使用配置文件: config/multi_ctp_config.json"' >> /opt/quantaxis/qactpmdgateway/start.sh && \
    echo '    exec ./bin/market_data_server --config config/multi_ctp_config.json "$@"' >> /opt/quantaxis/qactpmdgateway/start.sh && \
    echo 'fi' >> /opt/quantaxis/qactpmdgateway/start.sh && \
    chmod +x /opt/quantaxis/qactpmdgateway/start.sh

# 设置环境变量
ENV LD_LIBRARY_PATH=/opt/quantaxis/qactpmdgateway/libs:$LD_LIBRARY_PATH
ENV PATH=/opt/quantaxis/qactpmdgateway/bin:$PATH

# 默认启动命令
ENTRYPOINT ["/opt/quantaxis/qactpmdgateway/start.sh"]
CMD []