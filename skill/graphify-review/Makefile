CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -pthread -Wall -Wextra

bin/review: review.cpp
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $<
	@cksum < review.cpp > bin/review.stamp

clean:
	rm -rf bin

.PHONY: clean
