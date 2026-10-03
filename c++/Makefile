CXX ?= clang++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -Iinclude -Itests
LDFLAGS ?= -lcurl -lsqlite3

SRC_DIR = src
TEST_DIR = tests
INC_DIR = include
BUILD_DIR = build
TARGET = $(BUILD_DIR)/search_engine
TEST_TARGET = $(BUILD_DIR)/run_tests

CORE_SRCS = $(SRC_DIR)/Datalake.cpp $(SRC_DIR)/MetadataExtractor.cpp $(SRC_DIR)/JsonIndex.cpp \
            $(SRC_DIR)/HierarchicalIndex.cpp $(SRC_DIR)/SqliteIndex.cpp $(SRC_DIR)/QueryEngine.cpp \
            $(SRC_DIR)/ControlLayer.cpp $(SRC_DIR)/Tokenizer.cpp $(SRC_DIR)/BenchmarkRunner.cpp
CORE_OBJS = $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(CORE_SRCS))

MAIN_OBJ = $(BUILD_DIR)/main.o
TEST_SRCS = $(wildcard $(TEST_DIR)/*.cpp)
TEST_OBJS = $(patsubst $(TEST_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(TEST_SRCS))

.PHONY: all clean run test

all: $(TARGET) $(TEST_TARGET)

$(TARGET): $(CORE_OBJS) $(MAIN_OBJ) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_TARGET): $(CORE_OBJS) $(TEST_OBJS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(TEST_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

run: $(TARGET)
	./$(TARGET)

test: $(TEST_TARGET)
	./$(TEST_TARGET)
