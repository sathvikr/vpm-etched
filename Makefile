CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17
TARGET = vpm
SRCDIR = src
SRCS = $(SRCDIR)/main.cpp
OBJS = $(SRCS:.cpp=.o)

# Default target
all: $(TARGET)

# Link the target executable
$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET)

# Compile source files to object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean build files
clean:
	rm -f $(OBJS) $(TARGET)

# Install the binary to system (typically /usr/local/bin)
install:
	install -m 755 $(TARGET) /usr/local/bin/$(TARGET)

# Uninstall the binary
uninstall:
	rm -f /usr/local/bin/$(TARGET)

.PHONY: all clean install uninstall 