CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17
SRCS = src/main.cpp src/BuildGenerator.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = vpm

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: clean 