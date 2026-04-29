CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -O3 -fPIC
LDFLAGS = -lm -pthread

# Directories
INCLUDE_DIR = include
SRC_DIR = src
TEST_DIR = tests
BENCH_DIR = benchmark
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj

# Source files
SOURCES = $(wildcard $(SRC_DIR)/*.cpp)
# Exclude executable entrypoints from library sources
LIB_SOURCES = $(filter-out $(SRC_DIR)/main.cpp $(SRC_DIR)/web_portal.cpp, $(SOURCES))
TEST_SOURCES = $(wildcard $(TEST_DIR)/test_*.cpp)
BENCH_SOURCES = $(wildcard $(BENCH_DIR)/benchmark_*.cpp)

# Object files
LIB_OBJS = $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(LIB_SOURCES))
DEMO_OBJ = $(OBJ_DIR)/main.o
WEB_OBJ = $(OBJ_DIR)/web_portal.o

# Targets
LIB_TARGET = $(BUILD_DIR)/libnewsscope.a
DEMO_TARGET = $(BUILD_DIR)/newsscope_demo
WEB_TARGET = $(BUILD_DIR)/newsscope_webserver
TEST_TARGETS = $(patsubst $(TEST_DIR)/test_%.cpp, $(BUILD_DIR)/test_%, $(TEST_SOURCES))
BENCH_TARGETS = $(patsubst $(BENCH_DIR)/benchmark_%.cpp, $(BUILD_DIR)/benchmark_%, $(BENCH_SOURCES))

# Default target
.PHONY: all clean build tests benchmarks help

all: $(DEMO_TARGET) $(WEB_TARGET) $(TEST_TARGETS) $(BENCH_TARGETS)
	@echo "✓ Build complete!"
	@echo "  Demo:       $(DEMO_TARGET)"
	@echo "  Web:        $(WEB_TARGET)"
	@echo "  Tests:      $(TEST_TARGETS)"
	@echo "  Benchmarks: $(BENCH_TARGETS)"

# Library
$(LIB_TARGET): $(LIB_OBJS) | $(BUILD_DIR)
	ar rcs $@ $^

# Demo executable
$(DEMO_TARGET): $(DEMO_OBJ) $(LIB_TARGET) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Web executable
$(WEB_TARGET): $(WEB_OBJ) $(LIB_TARGET) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Test executables
$(BUILD_DIR)/test_%: $(TEST_DIR)/test_%.cpp $(LIB_TARGET) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -o $@ $< $(LIB_TARGET) $(LDFLAGS)

# Benchmark executables
$(BUILD_DIR)/benchmark_%: $(BENCH_DIR)/benchmark_%.cpp $(LIB_TARGET) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -o $@ $< $(LIB_TARGET) $(LDFLAGS)

# Object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -c $< -o $@

# Create directories
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Run targets
.PHONY: run run-web run-tests run-benchmarks

run: $(DEMO_TARGET)
	@echo "\n=== Running NewsScope Demo ===\n"
	@NEWSSCOPE_ENABLE_ML=1 NEWSSCOPE_ML_BLEND_WEIGHT=0.25 $(DEMO_TARGET)

run-web: $(WEB_TARGET)
	@echo "\n=== Running NewsScope Web Server ===\n"
	@echo "Open http://localhost:$${PORT:-8080}"
	@PORT=$${PORT:-8080} NEWSSCOPE_ENABLE_ML=1 NEWSSCOPE_ML_BLEND_WEIGHT=0.25 $(WEB_TARGET)

run-tests: $(TEST_TARGETS)
	@echo "\n=== Running All Tests ===\n"
	@for test in $(TEST_TARGETS); do \
		echo "\nRunning $$test..."; \
		NEWSSCOPE_ENABLE_ML=1 NEWSSCOPE_ML_BLEND_WEIGHT=0.25 $$test || exit 1; \
	done
	@echo "\n✓ All tests passed!"

run-throughput: $(BUILD_DIR)/benchmark_throughput
	@echo "\n=== Running Throughput Benchmark ===\n"
	@$(BUILD_DIR)/benchmark_throughput

run-latency: $(BUILD_DIR)/benchmark_latency
	@echo "\n=== Running Latency Benchmark ===\n"
	@$(BUILD_DIR)/benchmark_latency

run-memory: $(BUILD_DIR)/benchmark_memory
	@echo "\n=== Running Memory Benchmark ===\n"
	@$(BUILD_DIR)/benchmark_memory

run-benchmarks: run-throughput run-latency run-memory

# Clean
clean:
	rm -rf $(BUILD_DIR)
	@echo "✓ Build directory cleaned"

# Help
help:
	@echo "NewsScope Build System"
	@echo "====================="
	@echo ""
	@echo "Targets:"
	@echo "  make all           - Build everything (default)"
	@echo "  make run           - Run demo application"
	@echo "  make run-web       - Run web portal server"
	@echo "  make run-tests     - Run all unit tests"
	@echo "  make run-benchmarks- Run all benchmarks"
	@echo "  make run-throughput- Run throughput benchmark"
	@echo "  make run-latency   - Run latency benchmark"
	@echo "  make run-memory    - Run memory benchmark"
	@echo "  make clean         - Clean build directory"
	@echo "  make help          - Show this message"
	@echo ""
	@echo "Compiler: $(CXX)"
	@echo "Flags: $(CXXFLAGS)"
