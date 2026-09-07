// 栈帧 CRUD 单元测试：stackframe_new / destroy / get / set / bind
// 编译：make test（见 Makefile），或手动：
//   gcc -Wall -Wextra -g -I./src src/ast/stackframe.c src/ast/lumin_value.c tests/stackframe_test.c -o tests/stackframe_test
#include "ast/stackframe.h"
#include "ast/lumin_value.h"
#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;

#define CHECK(cond, msg) do { \
    if(cond) { passed++; } \
    else { failed++; fprintf(stderr, "FAIL(line %d): %s\n", __LINE__, msg); } \
} while(0)

int main(void)
{
    // ---- 1. new + destroy 基础 ----
    StackFrame* top = stackframe_new(NULL);
    CHECK(top != NULL, "new(NULL) 非空");
    CHECK(top->cnt == 0, "新帧 cnt==0");
    CHECK(top->parent == NULL, "新帧 parent==NULL");
    stackframe_destroy(top);
    stackframe_destroy(NULL);          // 空指针安全

    // ---- 2. set/get 基础类型 ----
    StackFrame* f = stackframe_new(NULL);
    stackframe_set(f, "i", val_int(42));
    stackframe_set(f, "d", val_double(3.14));
    stackframe_set(f, "b", val_bool(1));
    stackframe_set(f, "c", val_char('x'));
    CHECK(f->cnt == 4, "4个变量入帧");

    _Bool fnd = 0;
    Value vi = stackframe_get(f, "i", &fnd);
    CHECK(fnd && vi.type == VAL_INT && vi.v.i == 42, "get int");
    Value vd = stackframe_get(f, "d", &fnd);
    CHECK(fnd && vd.type == VAL_DOUBLE && vd.v.d == 3.14, "get double");
    Value vb = stackframe_get(f, "b", &fnd);
    CHECK(fnd && vb.type == VAL_BOOL && vb.v.b == 1, "get bool");
    Value vc = stackframe_get(f, "c", &fnd);
    CHECK(fnd && vc.type == VAL_CHAR && vc.v.c == 'x', "get char");

    // ---- 3. get 不存在的变量 ----
    fnd = 1;
    stackframe_get(f, "nope", &fnd);
    CHECK(!fnd, "get missing -> found=0");

    // ---- 4. 字符串 set/get + 覆盖（旧字符串释放，槽位不增）----
    stackframe_set(f, "s", val_string("hello"));
    Value vs = stackframe_get(f, "s", &fnd);
    CHECK(fnd && vs.type == VAL_STRING && strcmp(lumin_str_cstr(&vs), "hello") == 0, "set/get string");
    stackframe_set(f, "s", val_string("world"));
    vs = stackframe_get(f, "s", &fnd);
    CHECK(fnd && strcmp(lumin_str_cstr(&vs), "world") == 0, "覆盖字符串生效");
    CHECK(f->cnt == 5, "覆盖不新增槽位");

    // ---- 5. 父链：子帧可读父帧变量 ----
    StackFrame* child = stackframe_new(f);
    Value vp = stackframe_get(child, "i", &fnd);
    CHECK(fnd && vp.v.i == 42, "子帧读父帧变量");

    // ---- 6. set 语义：子帧赋值同名变量 → 沿链更新父帧，不产生本地槽位 ----
    stackframe_set(child, "i", val_int(7));
    CHECK(f->cnt == 5 && child->cnt == 0, "子帧set更新父帧，无本地槽位");
    vp = stackframe_get(f, "i", &fnd);
    CHECK(fnd && vp.v.i == 7, "父帧值被更新");

    // ---- 7. bind 语义：子帧绑定同名变量 → 本地新建，遮蔽父帧 ----
    stackframe_bind(child, "i", val_int(99));
    CHECK(child->cnt == 1, "bind在子帧新建槽位");
    vp = stackframe_get(child, "i", &fnd);
    CHECK(fnd && vp.v.i == 99, "子帧读到本地绑定(遮蔽)");
    vp = stackframe_get(f, "i", &fnd);
    CHECK(fnd && vp.v.i == 7, "bind不影响父帧");
    // 同帧重复bind → 覆盖不增槽位
    stackframe_bind(child, "i", val_int(100));
    CHECK(child->cnt == 1, "同帧重复bind覆盖");
    vp = stackframe_get(child, "i", &fnd);
    CHECK(fnd && vp.v.i == 100, "重复bind值生效");

    // ---- 8. 销毁子帧不影响父帧 ----
    stackframe_destroy(child);
    vp = stackframe_get(f, "s", &fnd);
    CHECK(fnd && strcmp(lumin_str_cstr(&vp), "world") == 0, "销毁子帧后父帧完好");

    // ---- 9. 深链查找（孙帧读祖帧） ----
    StackFrame* g = stackframe_new(f);
    stackframe_set(g, "local_g", val_int(1));
    vp = stackframe_get(g, "s", &fnd);
    CHECK(fnd && strcmp(lumin_str_cstr(&vp), "world") == 0, "孙帧找到祖帧变量");
    vp = stackframe_get(g, "local_g", &fnd);
    CHECK(fnd && vp.v.i == 1, "孙帧本地变量");
    stackframe_destroy(g);
    stackframe_destroy(f);

    // ---- 10. 帧销毁释放字符串（不崩溃/不双释放） ----
    StackFrame* sf = stackframe_new(NULL);
    stackframe_set(sf, "a", val_string("alpha"));
    stackframe_set(sf, "b", val_string("beta"));
    stackframe_set(sf, "a", val_string("gamma"));   // 覆盖后销毁
    stackframe_destroy(sf);
    CHECK(1, "多字符串帧销毁无崩溃");

    // ---- 11. VAL_FUNC 槽位不销毁函数对象（引用语义） ----
    // 用一个假 RuntimeFunc 验证：销毁帧后对象仍在（由注册方管理）
    {
        RuntimeFunc fake = {0};
        StackFrame* ff = stackframe_new(NULL);
        Value fv;
        fv.type = VAL_FUNC;
        fv.v.func.func_obj = &fake;
        stackframe_set(ff, "fn", fv);
        stackframe_destroy(ff);
        CHECK(1, "帧销毁不销毁VAL_FUNC对象");
    }

    // ---- 12. 共享帧标记 + 加锁 get/set 不崩溃（模拟多线程访问路径） ----
    {
        StackFrame* sh = stackframe_new(NULL);
        stackframe_set_shared(sh);
        stackframe_set(sh, "x", val_int(5));
        Value vx = stackframe_get(sh, "x", &fnd);
        CHECK(fnd && vx.v.i == 5, "共享帧 set/get 正常");
        stackframe_destroy(sh);
    }

    printf("\nstackframe_test: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
