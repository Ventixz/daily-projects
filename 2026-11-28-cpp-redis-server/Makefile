CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra -Wpedantic -Wshadow -Werror
LDFLAGS  ?=
LDLIBS   += -pthread
BUILD    ?= build

LIB_SRCS  := src/hashtable.cpp src/avl.cpp src/zset.cpp src/heap.cpp \
             src/protocol.cpp src/thread_pool.cpp src/store.cpp
UNIT_SRCS := tests/test_main.cpp tests/test_hashtable.cpp tests/test_avl.cpp \
             tests/test_zset.cpp tests/test_heap.cpp tests/test_protocol.cpp \
             tests/test_store.cpp tests/test_thread_pool.cpp

LIB_OBJS  := $(LIB_SRCS:%.cpp=$(BUILD)/%.o)
UNIT_OBJS := $(UNIT_SRCS:%.cpp=$(BUILD)/%.o)

.PHONY: all test asan tsan clean

all: $(BUILD)/server $(BUILD)/client

$(BUILD)/server: $(BUILD)/src/server.o $(LIB_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/client: $(BUILD)/src/client.o $(BUILD)/src/protocol.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/unit_tests: $(UNIT_OBJS) $(LIB_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/server_tests: $(BUILD)/tests/test_main.o $(BUILD)/tests/test_server.o $(BUILD)/src/protocol.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Isrc -MMD -MP -c -o $@ $<

test: $(BUILD)/unit_tests $(BUILD)/server_tests $(BUILD)/server $(BUILD)/client
	$(BUILD)/unit_tests
	SERVER_BIN=$(BUILD)/server CLIENT_BIN=$(BUILD)/client $(BUILD)/server_tests

# Same suite under AddressSanitizer + UndefinedBehaviorSanitizer.
asan:
	$(MAKE) BUILD=build-asan \
	  CXXFLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all" \
	  LDFLAGS="-fsanitize=address,undefined" test

# Data-race check (the thread pool is the only shared state).
tsan:
	$(MAKE) BUILD=build-tsan \
	  CXXFLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=thread" \
	  LDFLAGS="-fsanitize=thread" test

clean:
	rm -rf build build-asan build-tsan

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
