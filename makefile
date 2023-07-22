CXX = g++
CXXFLAGS = -g -std=c++17 -Wall -Wextra -I/usr/local/include -I/usr/local/include/boost
LDFLAGS = -L/usr/local/lib -lgtest -lgtest_main -lpthread -lboost_system

# Set your source and header files here
SRCS = server.cpp connection.cpp http_request.cpp http_response.cpp routing_module.cpp request_handler.cpp
HDRS = server.h connection.h http_request.h http_response.h routing_module.h
OBJS = $(SRCS:.cpp=.o)

TEST_SRCS = server_test.cpp routing_module_test.cpp
TEST_OBJS = $(TEST_SRCS:.cpp=.o)
TESTS = server_test routing_module_test

all: $(TESTS) simple_server_test

test: all
	for test in $(TESTS); do ./$$test; done

define test_rule
$(1): $$(filter-out $$(addsuffix .o, $$(filter-out $(1), $$(TESTS))), $$(OBJS) $$(TEST_OBJS))
	$$(CXX) $$(CXXFLAGS) -o $$@ $$^ $$(LDFLAGS)
endef

$(foreach test, $(TESTS), $(eval $(call test_rule, $(test))))

simple_server_test: simple_server_test.cpp $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

.PHONY: clean test
clean:
	rm -f *.o simple_server_test $(TESTS)
