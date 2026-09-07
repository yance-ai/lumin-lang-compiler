# lumin‑lang‑compiler Makefile
# single‑arch build only, multi‑arch/universal handled by build.sh shell
CC ?= gcc
CFLAGS ?= -Wall -Wextra -g -I./src -I./generated

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
BIN_LOCAL   := $(BIN_DIR)/$(BIN_NAME)

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

BREW_M4 := /usr/local/opt/m4/bin/m4

# sed 兼容 Mac / Linux
ifeq ($(shell uname -s),Darwin)
SED_I := sed -i ''
SED_E := sed -E
else
SED_I := sed -i
SED_E := sed -r
endif

# 重点：prune src/runtime、src/runtime-full 与 src/embed；后两者 .c 绝不编译进编译器本体，仅用于 xxd 打包
# （src/embed 的嵌入数组 .c 在下方 C_SRCS += 里显式追加，find 必须排除以免重复链接）
C_SRCS := $(shell find $(SRC_DIR) \( -path $(SRC_DIR)/runtime -o -path $(BUILD_DIR) -o -path $(SRC_EMBED) \) -prune -o -name '*.c' ! -name 'lex.yy.c' ! -name 'yacc.tab.c' -print)
# 追加自动生成的二进制数据c文件
C_SRCS += $(RT_EMBED_C_SRC) $(RT_EMBED_C_RT)
C_SRCS += $(LEX_GEN) $(YACC_GEN_C)
# 编译器本体链接语言运行时运算层（字节码 VM 解释器直接调用；generate 侧仍以 src/runtime/ 为源）
# 按职责拆分：值核心 + 字符串/数组/数学/IO/字典（gc_runtime 与 lm_runtime 聚合不参与链接）
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
C_SRCS += $(SRC_DIR)/runtime/lm_qs.c

OBJS := $(C_SRCS:.c=.o)

.PHONY: all clean distclean check-env parser-gen

# | 顺序依赖：先生成嵌入文件，再生成parser，最后链接
all: check-env | $(RT_EMBED_GEN) parser-gen $(BIN_LOCAL)

check-env:
	@echo "=== Toolchain Check ==="
	@flex --version
	@bison --version
	@bison --version | grep -q " 3." || (echo "ERROR: bison >=3.x required"; exit 1)
	@test -x $(BREW_M4) || (echo "ERROR: brew m4 missing, brew install m4"; exit 1)

# ---------- 1. 拼接生成 runtime_full.h / runtime_full.c ----------
$(RT_FULL_H) $(RT_FULL_C): runtime_manifest.txt
	@echo "==> Build runtime single‑file via concat_manifest.sh"
	mkdir -p $(BUILD_DIR)
	./concat_manifest.sh

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
	M4=$(BREW_M4) bison -v --report-file=$(YACC_REPORT) -d $< -o $(YACC_GEN_C)

$(LEX_GEN): $(LEX_SRC) $(YACC_GEN_H)
	mkdir -p $(YACC_DIR)
	flex -o $@ $<

# 所有编译单元依赖生成出来的extern头文件
$(OBJS): $(RT_EMBED_GEN)

$(BIN_LOCAL): $(OBJS)
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(OBJS) -lcurl -liconv -o $@

# ---------- 单元测试：栈帧 CRUD ----------
TEST_STACKFRAME := $(TEST_DIR)/stackframe_test

$(TEST_STACKFRAME): src/ast/stackframe.c src/ast/lumin_value.c tests/stackframe_test.c
	$(CC) $(CFLAGS) src/ast/stackframe.c src/ast/lumin_value.c tests/stackframe_test.c -o $@

.PHONY: test
test: $(TEST_STACKFRAME)
	./$(TEST_STACKFRAME)

clean:
	rm -f $(OBJS)
	rm -f $(SRC_DIR)/runtime/*.o $(BUILD_DIR)/*.o
	rm -rf $(BIN_DIR)
	@echo "clean done: keep parser, runtime‑full, embed generated artifacts"

distclean: clean
	rm -f $(LEX_GEN) $(YACC_GEN_C) $(YACC_GEN_H) $(YACC_REPORT)
	rm -f $(RT_EMBED_GEN)
	rm -rf $(BUILD_DIR)
	rm -rf $(GEN_DIR)
	find . -name "*''" -delete
	@echo "distclean done: restore to source‑only state, keep src/yacc folder structure"
