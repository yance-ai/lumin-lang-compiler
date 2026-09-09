# lumin-lang-compiler Makefile
# Cross-platform: macOS / Linux / Windows (MinGW-w64 + Git Bash)
CC ?= gcc
CFLAGS ?= -Wall -Wextra -g -I./src -I./generated

# ========== 操作系统检测 ==========
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    OS_NAME := macos
    EXE_EXT :=
else ifeq ($(findstring MINGW,$(UNAME_S)),MINGW)
    OS_NAME := windows
    EXE_EXT := .exe
else ifeq ($(findstring MSYS,$(UNAME_S)),MSYS)
    OS_NAME := windows
    EXE_EXT := .exe
else ifeq ($(findstring CYGWIN,$(UNAME_S)),CYGWIN)
    OS_NAME := windows
    EXE_EXT := .exe
else
    OS_NAME := linux
    EXE_EXT :=
endif

# Directories
SRC_DIR     := src
GEN_DIR     := generated
SRC_EMBED   := src/embed
BUILD_DIR   := src/runtime-full
YACC_DIR    := src/yacc
PARSE_SRC   := src/parse
TEST_DIR    := tests
BIN_DIR     := bin

BIN_NAME    := lumin
BIN_LOCAL   := $(BIN_DIR)/$(BIN_NAME)$(EXE_EXT)

LEX_SRC     := $(SRC_DIR)/lex/lex.l
YACC_SRC    := $(PARSE_SRC)/yacc.y

LEX_GEN     := $(YACC_DIR)/lex.yy.c
YACC_GEN_C  := $(YACC_DIR)/yacc.tab.c
YACC_GEN_H  := $(YACC_DIR)/yacc.tab.h
YACC_REPORT := $(GEN_DIR)/yacc.output

# ========== 预处理展开后的完整中间文件 ==========
RT_FULL_H := $(BUILD_DIR)/runtime_full.h
RT_FULL_C := $(BUILD_DIR)/runtime_full.c

# 【根治】xxd输出拆分：.c放数组定义，.h只放extern声明
RT_EMBED_H_SRC := $(SRC_EMBED)/lm_runtime_h_embed.h
RT_EMBED_C_SRC := $(SRC_EMBED)/lm_runtime_h_embed.c
RT_EMBED_H_RT  := $(SRC_EMBED)/lm_runtime_c_embed.h
RT_EMBED_C_RT  := $(SRC_EMBED)/lm_runtime_c_embed.c

RT_EMBED_GEN := $(RT_EMBED_H_SRC) $(RT_EMBED_C_SRC) $(RT_EMBED_H_RT) $(RT_EMBED_C_RT)

# ========== m4 路径（跨平台） ==========
ifeq ($(OS_NAME),macos)
    # macOS: Homebrew m4 (Intel / Apple Silicon)
    BREW_M4_INTEL := /usr/local/opt/m4/bin/m4
    BREW_M4_ARM   := /opt/homebrew/opt/m4/bin/m4
    ifeq ($(shell test -x $(BREW_M4_INTEL) && echo yes),yes)
        M4_PATH := $(BREW_M4_INTEL)
    else ifeq ($(shell test -x $(BREW_M4_ARM) && echo yes),yes)
        M4_PATH := $(BREW_M4_ARM)
    else
        M4_PATH := m4
    endif
    BISON_M4_ENV := M4=$(M4_PATH)
else ifeq ($(OS_NAME),windows)
    # Windows: WinFlexBison 内置 m4，无需外部 m4
    M4_PATH :=
    BISON_M4_ENV :=
else
    # Linux: 系统 m4
    M4_PATH := m4
    BISON_M4_ENV := M4=$(M4_PATH)
endif

# ========== sed 兼容（macOS / Linux / Windows） ==========
ifeq ($(OS_NAME),macos)
SED_I := sed -i ''
SED_E := sed -E
else
SED_I := sed -i
SED_E := sed -r
endif

# ========== 链接库（跨平台） ==========
# macOS/Linux: -lcurl -liconv
# Windows: MinGW 下同样用 -lcurl -liconv（需自行安装开发库）
LDLIBS := -lcurl -liconv

# 允许用户通过环境变量覆盖库搜索路径
ifneq ($(CURL_DIR),)
    CFLAGS += -I$(CURL_DIR)/include
    LDFLAGS += -L$(CURL_DIR)/lib
endif
ifneq ($(ICONV_DIR),)
    CFLAGS += -I$(ICONV_DIR)/include
    LDFLAGS += -L$(ICONV_DIR)/lib
endif

# 重点：排除 src/runtime、src/runtime-full 与 src/embed；后两者 .c 绝不编译进编译器本体，仅用于 xxd 打包
# （src/embed 的嵌入数组 .c 在下方 C_SRCS += 里显式追加）
# 使用 wildcard 而非 find，确保跨平台（Windows cmd 下没有 GNU find）
C_SRCS := $(wildcard $(SRC_DIR)/ast/*.c)
C_SRCS += $(wildcard $(SRC_DIR)/ir/*.c)
C_SRCS += $(wildcard $(SRC_DIR)/parse/*.c)
C_SRCS += $(SRC_DIR)/main.c
# yacc 目录下排除 lex.yy.c 和 yacc.tab.c（自动生成，下方显式追加）
C_SRCS += $(filter-out $(SRC_DIR)/yacc/lex.yy.c $(SRC_DIR)/yacc/yacc.tab.c, $(wildcard $(SRC_DIR)/yacc/*.c))
# 追加自动生成的二进制数据c文件
C_SRCS += $(RT_EMBED_C_SRC) $(RT_EMBED_C_RT)
C_SRCS += $(LEX_GEN) $(YACC_GEN_C)
# 编译器本体链接语言运行时运算层（字节码 VM 解释器直接调用；generate 侧仍以 src/runtime/ 为源）
# 按职责拆分：值核心 + 字符串/数组/数学/IO/字典 + GC（gc_runtime 参与链接，提供标记-清除回收）
C_SRCS += $(SRC_DIR)/runtime/gc_runtime.c
C_SRCS += $(SRC_DIR)/runtime/lm_value.c
C_SRCS += $(SRC_DIR)/runtime/lm_string.c
C_SRCS += $(SRC_DIR)/runtime/lm_array.c
C_SRCS += $(SRC_DIR)/runtime/lm_math.c
C_SRCS += $(SRC_DIR)/runtime/lm_io.c
C_SRCS += $(SRC_DIR)/runtime/lm_map.c
C_SRCS += $(SRC_DIR)/runtime/lm_thread.c
C_SRCS += $(SRC_DIR)/runtime/lm_lock.c
C_SRCS += $(SRC_DIR)/runtime/lm_tls.c
C_SRCS += $(SRC_DIR)/runtime/lm_http.c
C_SRCS += $(SRC_DIR)/runtime/lm_json.c
C_SRCS += $(SRC_DIR)/runtime/lm_charset.c
C_SRCS += $(SRC_DIR)/runtime/lm_crypto.c
C_SRCS += $(SRC_DIR)/runtime/lm_regex.c
C_SRCS += $(SRC_DIR)/runtime/lm_time.c
C_SRCS += $(SRC_DIR)/runtime/lm_qs.c

OBJS := $(C_SRCS:.c=.o)

# ========== Windows 兼容层（POSIX regex 等） ==========
WIN_DEPS := third_party/windows
ifeq ($(OS_NAME),windows)
    # 优先使用项目内的 WinFlexBison（flex 2.6.4 / bison 3.8.2），无需额外安装
    export PATH := $(CURDIR)/$(WIN_DEPS)/tools/winflexbison;$(PATH)
    # 使用项目内 third_party/windows/ 下的预编译库（MinGW 静态库，仅 Windows 可用）
    CFLAGS += -I$(WIN_DEPS)/include -DCURL_STATICLIB
    LDFLAGS += -L$(WIN_DEPS)/lib
    LDLIBS += -ltre -lcrypt32 -lws2_32 -lwldap32 -lwinmm -lnormaliz -liphlpapi -lbcrypt -lsecur32
endif

.PHONY: all clean distclean check-env parser-gen env-info

# | 顺序依赖：先生成嵌入文件，再生成parser，最后链接
all: check-env | $(RT_EMBED_GEN) parser-gen $(BIN_LOCAL)

env-info:
	@echo "=== Build Environment ==="
	@echo "OS: $(OS_NAME) ($(UNAME_S))"
	@echo "CC: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
	@echo "LDLIBS: $(LDLIBS)"
	@echo "M4: $(M4_PATH)"
	@echo "EXE_EXT: $(EXE_EXT)"
	@echo "Target: $(BIN_LOCAL)"

check-env:
	@echo "=== Toolchain Check ($(OS_NAME)) ==="
	@flex --version
	@bison --version | head -1
	@bison --version | grep -q " 3." || (echo "ERROR: bison >=3.x required"; exit 1)
ifeq ($(OS_NAME),macos)
	@test -x $(M4_PATH) || (echo "ERROR: m4 missing, brew install m4"; exit 1)
endif
	@xxd -version >/dev/null 2>&1 || (echo "ERROR: xxd missing"; exit 1)

# ---------- 1. 拼接生成 runtime_full.h / runtime_full.c ----------
$(RT_FULL_H) $(RT_FULL_C): runtime_manifest.txt
	@echo "==> Build runtime single-file via concat_manifest.sh"
	mkdir -p $(BUILD_DIR)
	bash ./concat_manifest.sh

# ---------- 2. xxd + sed拆分：输出 .c(定义) + .h(extern声明) 【根治】 ----------
$(RT_EMBED_H_SRC) $(RT_EMBED_C_SRC): $(RT_FULL_H)
	@echo "==> Generate split embed for runtime_full.h"
	mkdir -p $(SRC_EMBED)
	xxd -i $< > $(SRC_EMBED)/_tmp_embed1.h
	# 全部定义(数组+len)输出到 .c，并修改为 const unsigned char
	$(SED_E) 's/^unsigned char/const unsigned char/' $(SRC_EMBED)/_tmp_embed1.h > $(RT_EMBED_C_SRC)
	# .h 只输出 extern 声明
	$(SED_E) -n 's/^const unsigned char ([^[]+)\[\].*/extern const unsigned char \1[];/p; s/^unsigned int ([^ ]+)_len.*/extern unsigned int \1_len;/p' $(SRC_EMBED)/_tmp_embed1.h > $(RT_EMBED_H_SRC)
	rm -f $(SRC_EMBED)/_tmp_embed1.h

$(RT_EMBED_H_RT) $(RT_EMBED_C_RT): $(RT_FULL_C)
	@echo "==> Generate split embed for runtime_full.c"
	mkdir -p $(SRC_EMBED)
	xxd -i $< > $(SRC_EMBED)/_tmp_embed2.h
	$(SED_E) 's/^unsigned char/const unsigned char/' $(SRC_EMBED)/_tmp_embed2.h > $(RT_EMBED_C_RT)
	$(SED_E) -n 's/^const unsigned char ([^[]+)\[\].*/extern const unsigned char \1[];/p; s/^unsigned int ([^ ]+)_len.*/extern unsigned int \1_len;/p' $(SRC_EMBED)/_tmp_embed2.h > $(RT_EMBED_H_RT)
	rm -f $(SRC_EMBED)/_tmp_embed2.h

# ---------- bison/flex 解析器生成 ----------
parser-gen: $(YACC_GEN_C) $(LEX_GEN)
	@mkdir -p $(YACC_DIR)

$(YACC_GEN_C) $(YACC_GEN_H): $(YACC_SRC)
	mkdir -p $(GEN_DIR) $(YACC_DIR)
	$(BISON_M4_ENV) bison -v --report-file=$(YACC_REPORT) -d $< -o $(YACC_GEN_C)

$(LEX_GEN): $(LEX_SRC) $(YACC_GEN_H)
	mkdir -p $(YACC_DIR)
	flex -o $@ $<

# 所有编译单元依赖生成出来的extern头文件
$(OBJS): $(RT_EMBED_GEN)

$(BIN_LOCAL): $(OBJS)
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@

# ---------- 单元测试：栈帧 CRUD ----------
TEST_STACKFRAME := $(TEST_DIR)/stackframe_test$(EXE_EXT)

$(TEST_STACKFRAME): $(OBJS) tests/stackframe_test.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(filter-out src/main.o,$(OBJS)) tests/stackframe_test.c $(LDLIBS) -o $@

.PHONY: test
test: $(TEST_STACKFRAME)
	./$(TEST_STACKFRAME)

clean:
	rm -f $(OBJS)
	rm -f $(SRC_DIR)/runtime/*.o $(BUILD_DIR)/*.o
	rm -rf $(BIN_DIR)
	@echo "clean done: keep parser, runtime-full, embed generated artifacts"

distclean: clean
	rm -f $(LEX_GEN) $(YACC_GEN_C) $(YACC_GEN_H) $(YACC_REPORT)
	rm -f $(RT_EMBED_GEN)
	rm -rf $(BUILD_DIR)
	rm -rf $(GEN_DIR)
	find . -name "*''" -delete
	@echo "distclean done: restore to source-only state, keep src/yacc folder structure"
