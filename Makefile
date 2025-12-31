CXX = g++
VCPKG_PKG_CONFIG_PATH = /opt/vcpkg/installed/x64-linux/lib/pkgconfig
PKG_CONFIG = PKG_CONFIG_PATH=$(VCPKG_PKG_CONFIG_PATH) pkg-config

CXXFLAGS = -std=c++17 -Wall \
           `pkg-config gtkmm-3.0 --cflags` \
           `$(PKG_CONFIG) google_cloud_cpp_iap --cflags`

LDFLAGS = `pkg-config gtkmm-3.0 --libs` \
          `$(PKG_CONFIG) google_cloud_cpp_iap --libs`

TARGET = iapRemote
SRCS = main.cpp Connections.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $(TARGET) $(OBJS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
