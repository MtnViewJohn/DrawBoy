TARGET_TEST ?= FakeLoom
TARGET_USER ?= DrawBoy
PREFIX ?= /usr/local
BINARY_DIR ?= $(PREFIX)/bin
INSTALL ?= install
INSTALL_PROGRAM ?= $(INSTALL) -s
INSTALL_DATA ?= $(INSTALL) -m 644
MKDIR_P ?= mkdir -p

BUILD_DIR ?= ./build

all: bin
bin: $(BUILD_DIR)/$(TARGET_TEST) $(BUILD_DIR)/$(TARGET_USER)

install:
	$(MKDIR_P) $(DESTDIR)$(BINARY_DIR)
	$(INSTALL_PROGRAM) $(BUILD_DIR)/$(TARGET_TEST) $(DESTDIR)$(BINARY_DIR)/
	$(INSTALL_PROGRAM) $(BUILD_DIR)/$(TARGET_USER) $(DESTDIR)$(BINARY_DIR)/

SRCS_COMMON := term.cpp ipc.cpp

SRCS_TEST := fakemain.cpp fakeargs.cpp fakedriver.cpp
SRCS_TEST += $(SRCS_COMMON)

SRCS_USER := main.cpp args.cpp driver.cpp
SRCS_USER += wif.cpp
SRCS_USER += $(SRCS_COMMON)


INCS := .
LIBS := stdc++

OBJS_TEST := $(SRCS_TEST:%=$(BUILD_DIR)/%.o)
OBJS_USER := $(SRCS_USER:%=$(BUILD_DIR)/%.o)

INC_FLAGS := $(addprefix -I,$(INCS))
CPPFLAGS += $(INC_FLAGS)
CPPFLAGS += -Wdate-time -D_FORTIFY_SOURCE=2
CPPFLAGS += -std=c++20
CPPFLAGS += -MMD -MP
CPPFLAGS += -O2
CPPFLAGS += -fstack-protector-strong -Wformat -Werror=format-security
CPPFLAGS += -Wall -Wextra -pedantic

LDFLAGS += $(addprefix -l,$(LIBS))


# c++ source

$(BUILD_DIR)/%.cpp.o: %.cpp
	@$(MKDIR_P) $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET_TEST): $(OBJS_TEST)
	$(CC) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/$(TARGET_USER): $(OBJS_USER)
	$(CC) $^ -o $@ $(LDFLAGS)


.PHONY: clean test deb deb-clean tars

clean:
	$(RM) -r $(BUILD_DIR)

# dependencies

DEPS := $(OBJS_TEST:.o=.d) $(OBJS_USER:.o=.d)

-include $(DEPS)

