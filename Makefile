# Makefile for QuantAxis Market Data Gateway
# =========================================

# 编译器设置
CXX = g++
CXXFLAGS = -std=c++17 -pthread -g -O2 -Wall -fPIC -Wno-unused-local-typedefs -Wno-reorder -Wno-class-memaccess

# 包含路径
INCLUDES = -I./libs \
          -I./include \
          -I/usr/include/rapidjson \
          -I/usr/local/include

# 库路径
LDFLAGS = -L./libs \
         -L/usr/local/lib

# 链接库
LIBS = ./libs/thostmduserapi_se.so ./libs/thosttraderapi_se.so \
       -lboost_system -lboost_thread -lboost_chrono -lboost_filesystem -lboost_regex \
       -lssl -lcrypto -lcurl -lhiredis \
       -lpthread -lrt -lstdc++fs

# 源文件目录
SRCDIR = src
OBJDIR = obj
BINDIR = bin

# 源文件
SOURCES = $(wildcard $(SRCDIR)/*.cpp)
OBJECTS = $(SOURCES:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)

# 目标文件
TARGET = $(BINDIR)/market_data_server

# 默认目标
all: directories $(TARGET)

# 创建目录
directories:
	@mkdir -p $(OBJDIR)
	@mkdir -p $(BINDIR)
	@mkdir -p ctp_flow
	@mkdir -p config

# 编译目标
$(TARGET): $(OBJECTS)
	@echo "Linking $@..."
	$(CXX) $(OBJECTS) $(LDFLAGS) $(LIBS) -o $@
	@echo "Build completed: $@"

# 编译对象文件
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# 安装目标
install: $(TARGET)
	@echo "Installing $(TARGET) to /usr/local/bin/"
	@sudo cp $(TARGET) /usr/local/bin/
	@sudo chmod 755 /usr/local/bin/market_data_server
	@echo "Installation completed"

# 清理
clean:
	@echo "Cleaning..."
	@rm -rf $(OBJDIR) $(BINDIR)
	@rm -rf ctp_flow
	@echo "Clean completed"

# 运行测试
test: $(TARGET)
	@echo "Starting market data server test..."
	@echo "Single-CTP mode: $(TARGET) --front-addr <addr> --broker-id <id>"  
	@echo "Multi-CTP mode:  $(TARGET) --multi-ctp"
	@echo "Config file mode: $(TARGET) --config config/multi_ctp_config.json"

# 检查依赖
check-deps:
	@echo "Checking dependencies..."
	@echo "Boost libraries:"
	@ldconfig -p | grep boost || echo "  Warning: Boost libraries may not be installed"
	@echo "OpenSSL libraries:"
	@ldconfig -p | grep ssl || echo "  Warning: OpenSSL libraries may not be installed"
	@echo "CTP libraries:"
	@ls -la libs/*.so || echo "  Warning: CTP libraries not found in libs/"

# 帮助
help:
	@echo "Available targets:"
	@echo "  all          - Build the market data server"
	@echo "  install      - Install to /usr/local/bin/"
	@echo "  clean        - Remove build files"
	@echo "  test         - Run basic test"
	@echo "  check-deps   - Check system dependencies"
	@echo "  help         - Show this help"

# 调试目标
debug: CXXFLAGS += -DDEBUG -g3
debug: clean $(TARGET)

# 发布目标
release: CXXFLAGS += -O3 -DNDEBUG
release: clean $(TARGET)

# 文档生成
docs:
	@echo "Generating documentation with Doxygen..."
	@doxygen Doxyfile || echo "Doxygen not found or Doxyfile missing"

.PHONY: all directories clean install test check-deps help debug release docs