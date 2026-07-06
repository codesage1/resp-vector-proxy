CC = clang
CFLAGS = -Wall -Wextra -g -MMD -MP

# Directories
SRC_DIR = src
TEST_DIR = tests
OBJ_DIR = obj
BIN_DIR = bin

# Individual Object Files
INSPECT_OBJ = $(OBJ_DIR)/inspect.o
RESP_OBJ = $(OBJ_DIR)/resp.o
PROXY_OBJ = $(OBJ_DIR)/proxy.o
MAIN_OBJ = $(OBJ_DIR)/main.o
TEST_OBJ = $(OBJ_DIR)/test_resp.o

# Final Binaries
PROXY_BIN = $(BIN_DIR)/rvp
TEST_BIN = $(BIN_DIR)/test_resp

all: dirs $(PROXY_BIN) $(TEST_BIN)

# Link the Proxy
$(PROXY_BIN): $(MAIN_OBJ) $(RESP_OBJ) $(PROXY_OBJ) $(INSPECT_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Link the Test Suite
$(TEST_BIN): $(TEST_OBJ) $(RESP_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Compile src/ files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Compile tests/ files
$(OBJ_DIR)/%.o: $(TEST_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

dirs:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

# Secretly pull in the header dependencies
-include $(OBJ_DIR)/*.d

.PHONY: all dirs clean

asan: CFLAGS += -fsanitize=address
asan: clean all