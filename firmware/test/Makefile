CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

test: test_flame_detector
	./test_flame_detector

test_flame_detector: test_flame_detector.cpp ../candle_sensor/FlameDetector.h
	$(CXX) $(CXXFLAGS) -o $@ test_flame_detector.cpp -lm

clean:
	rm -f test_flame_detector

.PHONY: test clean
