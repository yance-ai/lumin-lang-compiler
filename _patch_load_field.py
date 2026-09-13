import re

# 读取文件
with open('src/ir/ir_cgen_emit.c', 'r', encoding='utf-8') as f:
    content = f.read()

# 查找 OPC_LOAD_FIELD 中的条件判断
old_cond = '''                if(sname && !(in.a < fn->param_cnt && !fn->is_method)) {
                    /* 只有 struct 局部变量 和 方法 self 参数 用高性能指针访问；
                       普通函数参数传递的是 Value(map)，回退到 lumyr_index_get */'''

new_cond = '''                if(sname && (!(in.a < fn->param_cnt && !fn->is_method) ||
                              (in.a < fn->param_cnt && fn->param_is_ref && fn->param_is_ref[in.a]))) {
                    /* struct 局部变量、方法 self 参数、ref 参数 用高性能指针访问；
                       普通函数参数传递的是 Value(map)，回退到 lumyr_index_get */'''

if old_cond in content:
    content = content.replace(old_cond, new_cond)
    print("修改 OPC_LOAD_FIELD 条件判断成功")
else:
    print("未找到 OPC_LOAD_FIELD 条件判断代码")

# 写入文件
with open('src/ir/ir_cgen_emit.c', 'w', encoding='utf-8') as f:
    f.write(content)
