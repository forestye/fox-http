CXX = ccache g++
CXXFLAGS = -g -std=c++17 -Wall -Wextra -I/usr/local/include -I/usr/local/include/boost
LDFLAGS = -L/usr/local/lib -lgtest -lgtest_main -lpthread -lboost_system

SRCS = server.cpp connection.cpp http_request.cpp http_response.cpp routing_module.cpp request_handler.cpp server_status.cpp timer_manager.cpp
HDRS = server.h connection.h http_request.h http_response.h routing_module.h request_handler.h server_status.h timer_manager.h
OBJS = $(SRCS:.cpp=.o)

STATIC_LIB = liblight_httpd.a

TEST_SRCS = server_test.cpp routing_module_test.cpp
TEST_OBJS = $(TEST_SRCS:.cpp=.o)
TESTS = server_test routing_module_test

all: simple_server_test 

test: all $(TESTS) 
	for test in $(TESTS); do ./$$test; done

define test_rule
$(1): $$(filter-out $$(addsuffix .o, $$(filter-out $(1), $$(TESTS))), $$(OBJS) $$(TEST_OBJS))
	$$(CXX) $$(CXXFLAGS) -o $$@ $$^ $$(LDFLAGS)
endef

$(foreach test, $(TESTS), $(eval $(call test_rule, $(test))))

simple_server_test: simple_server_test.cpp $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(STATIC_LIB): $(OBJS)
	ar rcs $@ $^

%.o: %.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

.PHONY: clean test install
clean:
	rm -f *.o simple_server_test $(TESTS)

install: $(STATIC_LIB)
	cp $(STATIC_LIB) /usr/local/lib
	mkdir -p /usr/local/include/light_httpd
	cp $(HDRS) /usr/local/include/light_httpd