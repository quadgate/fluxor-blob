CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=

SRC_DIR := src
BIN_DIR := bin
OBJ_DIR := build

LIB_SRC := $(SRC_DIR)/blob_storage.cpp
LIB_OBJ := $(OBJ_DIR)/blob_storage.o
CLI_SRC := $(SRC_DIR)/main.cpp
CLI_BIN := $(BIN_DIR)/blobstore
TEST_SRC := tests/tests.cpp
TEST_BIN := $(BIN_DIR)/tests

.PHONY: all clean test dirs

all: dirs $(CLI_BIN) $(TEST_BIN)

dirs:
	@mkdir -p $(BIN_DIR) $(OBJ_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -Iinclude -c $< -o $@

$(CLI_BIN): $(LIB_OBJ) $(CLI_SRC)
	$(CXX) $(CXXFLAGS) -Iinclude $(LIB_OBJ) $(CLI_SRC) -o $@ $(LDFLAGS)

$(TEST_BIN): $(LIB_OBJ) $(TEST_SRC)
	$(CXX) $(CXXFLAGS) -Iinclude $(LIB_OBJ) $(TEST_SRC) -o $@ $(LDFLAGS)

test: $(TEST_BIN)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
