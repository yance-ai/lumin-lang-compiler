// ast_types.c —— type 声明类型表（编译期全局注册）
#include "ast_types.h"
#include "ast_node.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "func_compile.h"

static TypeDef* g_types = NULL;
static int g_types_n = 0;
static int g_types_cap = 0;

int type_register(const char* name, char** props, ValueType* ptypes, int nprops, char** generic_params, int generic_param_count, char** interfaces, int ninterfaces)
{
    // 重名：覆盖（后声明优先，与变量赋值一致）
    int i = type_lookup(name);
    if(i < 0) {
        if(g_types_n >= g_types_cap) {
            int nc = g_types_cap > 0 ? g_types_cap * 2 : 8;
            TypeDef* nt = (TypeDef*)realloc(g_types, (size_t)nc * sizeof(TypeDef));
            if(!nt) { fprintf(stderr, "type 表扩容内存不足\n"); exit(EXIT_FAILURE); }
            g_types = nt;
            g_types_cap = nc;
        }
        i = g_types_n++;
        g_types[i].name = strdup(name);
        g_types[i].props = NULL;
        g_types[i].ptypes = NULL;
        g_types[i].nprops = 0;
        g_types[i].generic_params = NULL;
        g_types[i].generic_param_count = 0;
        g_types[i].interfaces = NULL;
        g_types[i].ninterfaces = 0;
        g_types[i].is_struct = 0;
        g_types[i].is_class = 0;
        g_types[i].parent = NULL;
        g_types[i].field_cast_kinds = NULL;
        g_types[i].field_struct_names = NULL;
        g_types[i].field_offsets = NULL;
        g_types[i].method_names = NULL;
        g_types[i].method_nodes = NULL;
        g_types[i].nmethods = 0;
    }
    // 释放旧属性（重声明覆盖）
    if(g_types[i].props) {
        for(int k = 0; k < g_types[i].nprops; k++) free(g_types[i].props[k]);
        free(g_types[i].props);
        free(g_types[i].ptypes);
    }
    if(g_types[i].generic_params) {
        for(int k = 0; k < g_types[i].generic_param_count; k++) free(g_types[i].generic_params[k]);
        free(g_types[i].generic_params);
    }
    if(g_types[i].interfaces) {
        for(int k = 0; k < g_types[i].ninterfaces; k++) free(g_types[i].interfaces[k]);
        free(g_types[i].interfaces);
    }
    if(g_types[i].field_cast_kinds) free(g_types[i].field_cast_kinds);
    if(g_types[i].field_offsets) free(g_types[i].field_offsets);
    g_types[i].props = (char**)malloc((size_t)(nprops > 0 ? nprops : 1) * sizeof(char*));
    g_types[i].ptypes = (ValueType*)malloc((size_t)(nprops > 0 ? nprops : 1) * sizeof(ValueType));
    for(int k = 0; k < nprops; k++) {
        g_types[i].props[k] = strdup(props[k]);
        g_types[i].ptypes[k] = ptypes[k];
    }
    g_types[i].nprops = nprops;
    // 泛型参数
    if(generic_params && generic_param_count > 0) {
        g_types[i].generic_params = (char**)malloc((size_t)generic_param_count * sizeof(char*));
        for(int k = 0; k < generic_param_count; k++) {
            g_types[i].generic_params[k] = strdup(generic_params[k]);
        }
        g_types[i].generic_param_count = generic_param_count;
    } else {
        g_types[i].generic_params = NULL;
        g_types[i].generic_param_count = 0;
        g_types[i].interfaces = NULL;
        g_types[i].ninterfaces = 0;
    }
    return i;
}

int type_lookup(const char* name)
{
    for(int i = 0; i < g_types_n; i++) {
        if(strcmp(g_types[i].name, name) == 0) return i;
    }
    return -1;
}

TypeDef* type_get(int idx)
{
    if(idx < 0 || idx >= g_types_n) return NULL;
    return &g_types[idx];
}

int type_count(void)
{
    return g_types_n;
}

ValueType type_name_to_valtype(const char* tname)
{
    if(!tname) return VAL_NONE;
    if(strcmp(tname, "string") == 0)  return VAL_STRING;
    if(strcmp(tname, "int") == 0)     return VAL_INT;
    if(strcmp(tname, "double") == 0)  return VAL_DOUBLE;
    if(strcmp(tname, "bool") == 0)    return VAL_BOOL;
    if(strcmp(tname, "char") == 0)    return VAL_CHAR;
    if(strcmp(tname, "ascii") == 0)   return VAL_INT;  /* ASCII 码值按 int 处理 */
    if(strcmp(tname, "byte") == 0)    return VAL_BYTE;
    return VAL_NONE;
}

ValueType castkind_to_valtype(int ck)
{
    switch(ck) {
        case CAST_STRING: return VAL_STRING;
        case CAST_INT: case CAST_ASCII: return VAL_INT;
        case CAST_DOUBLE: return VAL_DOUBLE;
        case CAST_BOOL: return VAL_BOOL;
        case CAST_CHAR: return VAL_CHAR;
        case CAST_BYTE: return VAL_BYTE;
        case CAST_INT8: case CAST_INT16: case CAST_INT32: case CAST_INT64:
        case CAST_UINT8: case CAST_UINT16: case CAST_UINT32: case CAST_UINT64:
        case CAST_LONG: case CAST_LONGLONG:
            return VAL_INT;
        case CAST_FLOAT:
            return VAL_DOUBLE;
        case CAST_ULONG: case CAST_UCHAR: case CAST_SHORT: case CAST_USHORT:
        case CAST_SIZE_T: case CAST_SSIZE_T: case CAST_PTR:
            return VAL_INT;
        case CAST_LONG_DOUBLE:
            return VAL_DOUBLE;
        case CAST_VOID:
            return VAL_NONE;
        default: return VAL_NONE;
    }
}

/* CAST_xxx -> 类型名字符串（用于 FFI extern 函数返回类型存储） */
int valuetype_to_castkind(int vt) {
    switch(vt) {
        case VAL_INT: return CAST_LONGLONG;
        case VAL_DOUBLE: return CAST_DOUBLE;
        case VAL_BOOL: return CAST_BOOL;
        case VAL_CHAR: return CAST_CHAR;
        case VAL_STRING: return CAST_STRING;
        case VAL_BYTE: return CAST_BYTE;
        default: return CAST_LONGLONG;  /* 默认整数类型 */
    }
}

char* castkind_to_name(int ck) {
    switch(ck) {
        case CAST_STRING: return strdup("string");
        case CAST_INT: return strdup("int");
        case CAST_DOUBLE: return strdup("double");
        case CAST_BOOL: return strdup("bool");
        case CAST_CHAR: return strdup("char");
        case CAST_BYTE: return strdup("byte");
        case CAST_INT8: return strdup("int8");
        case CAST_INT16: return strdup("int16");
        case CAST_INT32: return strdup("int32");
        case CAST_INT64: return strdup("int64");
        case CAST_UINT8: return strdup("uint8");
        case CAST_UINT16: return strdup("uint16");
        case CAST_UINT32: return strdup("uint32");
        case CAST_UINT64: return strdup("uint64");
        case CAST_LONG: return strdup("long");
        case CAST_LONGLONG: return strdup("long long");
        case CAST_FLOAT: return strdup("float");
        case CAST_ASCII: return strdup("ascii");
        case CAST_ULONG: return strdup("ulong");
        case CAST_UCHAR: return strdup("uchar");
        case CAST_SHORT: return strdup("short");
        case CAST_USHORT: return strdup("ushort");
        case CAST_SIZE_T: return strdup("size_t");
        case CAST_SSIZE_T: return strdup("ssize_t");
        case CAST_VOID: return strdup("void");
        case CAST_LONG_DOUBLE: return strdup("long double");
        case CAST_PTR: return strdup("ptr");
        default: return strdup("int");
    }
}

/* ValueType -> 类型名字符串（用于接口方法返回类型存储） */
char* valtype_to_name(ValueType vt) {
    switch(vt) {
        case VAL_INT: return strdup("int");
        case VAL_STRING: return strdup("string");
        case VAL_DOUBLE: return strdup("double");
        case VAL_BOOL: return strdup("bool");
        case VAL_CHAR: return strdup("char");
        case VAL_NONE: return strdup("void");
        default: return strdup("any");
    }
}


/* ===== 接口/trait 系统实现 ===== */

static InterfaceDef* g_interfaces = NULL;
static int g_ninterfaces = 0;
static int g_interfaces_cap = 0;

int interface_register(const char* name, void* methods, const char* parent) {
    /* 检查是否已存在 */
    for(int i = 0; i < g_ninterfaces; i++) {
        if(strcmp(g_interfaces[i].name, name) == 0) {
            return i; /* 已存在，返回原下标 */
        }
    }

    /* 扩容 */
    if(g_ninterfaces >= g_interfaces_cap) {
        g_interfaces_cap = g_interfaces_cap ? g_interfaces_cap * 2 : 16;
        g_interfaces = (InterfaceDef*)realloc(g_interfaces, (size_t)g_interfaces_cap * sizeof(InterfaceDef));
    }

    InterfaceDef* idef = &g_interfaces[g_ninterfaces];
    idef->name = strdup(name);
    idef->methods = NULL;
    idef->nmethods = 0;
    idef->parent = parent ? strdup(parent) : NULL;

    /* 先收集父接口的方法（如果有父接口且已注册） */
    int parent_methods_count = 0;
    InterfaceMethod* parent_methods = NULL;
    if(parent) {
        int pidx = interface_lookup(parent);
        if(pidx >= 0) {
            InterfaceDef* pdef = interface_get(pidx);
            if(pdef && pdef->nmethods > 0) {
                parent_methods_count = pdef->nmethods;
                parent_methods = pdef->methods;
            }
        }
    }

    /* 遍历当前接口的方法列表（AstNode* param 链表） */
    AstNode* m = (AstNode*)methods;
    int own_count = 0;
    AstNode* cur = m;
    while(cur) { own_count++; cur = cur->u.param.next; }

    int total_count = parent_methods_count + own_count;
    if(total_count > 0) {
        idef->methods = (InterfaceMethod*)malloc((size_t)total_count * sizeof(InterfaceMethod));
        int idx = 0;
        /* 先复制父接口的方法 */
        for(int i = 0; i < parent_methods_count; i++) {
            idef->methods[idx].name = strdup(parent_methods[i].name);
            idef->methods[idx].return_type = parent_methods[i].return_type ? strdup(parent_methods[i].return_type) : NULL;
            idx++;
        }
        /* 再复制当前接口的方法 */
        cur = m;
        for(int i = 0; i < own_count; i++) {
            idef->methods[idx].name = strdup(cur->u.param.name);
            idef->methods[idx].return_type = cur->u.param.constraint ? strdup(cur->u.param.constraint) : NULL;
            cur = cur->u.param.next;
            idx++;
        }
        idef->nmethods = total_count;
    }

    return g_ninterfaces++;
}

int interface_lookup(const char* name) {
    for(int i = 0; i < g_ninterfaces; i++) {
        if(strcmp(g_interfaces[i].name, name) == 0) return i;
    }
    return -1;
}

InterfaceDef* interface_get(int idx) {
    if(idx < 0 || idx >= g_ninterfaces) return NULL;
    return &g_interfaces[idx];
}

/* ===== struct 注册 ===== */
int struct_register(const char* name, char** props, int* cast_kinds, char** struct_names, int nprops)
{
    // 先注册为普通 type（用 ValueType，从 CastKind 转换）
    ValueType* vtypes = (ValueType*)malloc((size_t)(nprops > 0 ? nprops : 1) * sizeof(ValueType));
    for(int k = 0; k < nprops; k++) {
        vtypes[k] = castkind_to_valtype(cast_kinds[k]);
    }
    int idx = type_register(name, props, vtypes, nprops, NULL, 0, NULL, 0);
    free(vtypes);

    // 标记为 struct 并保存精确 CastKind 类型
    g_types[idx].is_struct = 1;
    g_types[idx].field_cast_kinds = (int*)malloc((size_t)(nprops > 0 ? nprops : 1) * sizeof(int));
    g_types[idx].field_struct_names = (char**)calloc((size_t)(nprops > 0 ? nprops : 1), sizeof(char*));
    for(int k = 0; k < nprops; k++) {
        g_types[idx].field_cast_kinds[k] = cast_kinds[k];
        if(struct_names && struct_names[k]) {
            g_types[idx].field_struct_names[k] = strdup(struct_names[k]);
        }
    }
    g_types[idx].field_offsets = NULL; // 编译通道计算偏移时填充
    g_types[idx].method_names = NULL;
    g_types[idx].method_nodes = NULL;
    g_types[idx].method_funcs = NULL;
    g_types[idx].nmethods = 0;
    g_types[idx].constructor = NULL;
    g_types[idx].constructor_func = NULL;
    return idx;
}

// 添加 struct 方法
void struct_add_method(const char* struct_name, const char* method_name, struct AstNode* method_node)
{
    TypeDef* td = struct_lookup(struct_name);
    if(!td) return;
    int n = td->nmethods + 1;
    td->method_names = (char**)realloc(td->method_names, (size_t)n * sizeof(char*));
    td->method_nodes = (struct AstNode**)realloc(td->method_nodes, (size_t)n * sizeof(struct AstNode*));
    td->method_names[td->nmethods] = strdup(method_name);
    td->method_nodes[td->nmethods] = method_node;
    td->nmethods = n;
}

// 查找 struct 方法
struct AstNode* struct_find_method(const char* struct_name, const char* method_name)
{
    TypeDef* td = struct_lookup(struct_name);
    if(!td) return NULL;
    for(int i = 0; i < td->nmethods; i++) {
        if(strcmp(td->method_names[i], method_name) == 0) {
            return td->method_nodes[i];
        }
    }
    return NULL;
}

// 查找是否是 struct（返回 TypeDef* 或 NULL）
TypeDef* struct_lookup(const char* name)
{
    int idx = type_lookup(name);
    if(idx < 0) return NULL;
    if(!g_types[idx].is_struct) return NULL;
    return &g_types[idx];
}

/* ===== class 注册 ===== */
int class_register(const char* name, char** props, ValueType* ptypes, int nprops, const char* parent, char** interfaces)
{
    // 合并父类和子类的属性（父类属性在前，子类属性在后）
    char** merged_props = props;
    ValueType* merged_ptypes = ptypes;
    int merged_nprops = nprops;

    if(parent) {
        int parent_idx = type_lookup(parent);
        if(parent_idx >= 0) {
            TypeDef* parent_td = &g_types[parent_idx];
            int parent_nprops = parent_td->nprops;
            if(parent_nprops > 0) {
                // 分配合并后的数组
                merged_nprops = parent_nprops + nprops;
                merged_props = (char**)malloc((size_t)merged_nprops * sizeof(char*));
                merged_ptypes = (ValueType*)malloc((size_t)merged_nprops * sizeof(ValueType));
                // 父类属性在前
                for(int i = 0; i < parent_nprops; i++) {
                    merged_props[i] = strdup(parent_td->props[i]);
                    merged_ptypes[i] = parent_td->ptypes[i];
                }
                // 子类属性在后
                for(int i = 0; i < nprops; i++) {
                    merged_props[parent_nprops + i] = strdup(props[i]);
                    merged_ptypes[parent_nprops + i] = ptypes[i];
                }
            }
        }
    }

    // 先注册为普通 type（使用合并后的属性）
    int nifaces = 0;
    if(interfaces) {
        while(interfaces[nifaces]) nifaces++;
    }
    int idx = type_register(name, merged_props, merged_ptypes, merged_nprops, NULL, 0, interfaces, nifaces);

    // 标记为 class 并保存父类
    g_types[idx].is_class = 1;
    g_types[idx].parent = parent ? strdup(parent) : NULL;
    g_types[idx].method_names = NULL;
    g_types[idx].method_nodes = NULL;
    g_types[idx].method_funcs = NULL;
    g_types[idx].nmethods = 0;
    g_types[idx].constructor = NULL;
    g_types[idx].constructor_func = NULL;
    return idx;
}

// 查找是否是 class（返回 TypeDef* 或 NULL）
TypeDef* class_lookup(const char* name)
{
    int idx = type_lookup(name);
    if(idx < 0) return NULL;
    if(!g_types[idx].is_class) return NULL;
    return &g_types[idx];
}

// 添加 class 方法（同时编译为 RuntimeFunc 存储）
void class_add_method(const char* class_name, const char* method_name, struct AstNode* method_node)
{
    TypeDef* td = class_lookup(class_name);
    if(!td) return;
    // 编译方法为 RuntimeFunc
    RuntimeFunc* rf = compile_func_from_ast(method_node);
    // 设置 class_name 字段（用于 CC 模式方法命名，避免命名冲突）
    if(rf && rf->capture_count == -1) {
        InterpFuncPayload* pl = (InterpFuncPayload*)rf->captures;
        if(pl && pl->bytecode) {
            pl->bytecode->class_name = strdup(class_name);
        }
    }
    // 检查是否已有同名方法（方法重写）
    for(int i = 0; i < td->nmethods; i++) {
        if(strcmp(td->method_names[i], method_name) == 0) {
            // 方法重写：替换旧方法
            td->method_nodes[i] = method_node;
            td->method_funcs[i] = rf;
            return;
        }
    }
    // 新方法：添加到方法表
    int n = td->nmethods + 1;
    td->method_names = (char**)realloc(td->method_names, (size_t)n * sizeof(char*));
    td->method_nodes = (struct AstNode**)realloc(td->method_nodes, (size_t)n * sizeof(struct AstNode*));
    td->method_funcs = (void**)realloc(td->method_funcs, (size_t)n * sizeof(void*));
    td->method_names[td->nmethods] = strdup(method_name);
    td->method_nodes[td->nmethods] = method_node;
    td->method_funcs[td->nmethods] = rf;
    td->nmethods = n;
}

// 查找 class 方法的 RuntimeFunc（支持继承链查找）
void* class_find_method_func(const char* class_name, const char* method_name)
{
    TypeDef* td = class_lookup(class_name);
    if(!td) return NULL;
    // 先查当前类
    for(int i = 0; i < td->nmethods; i++) {
        if(strcmp(td->method_names[i], method_name) == 0) {
            return td->method_funcs[i];
        }
    }
    // 再查父类（递归）
    if(td->parent) {
        return class_find_method_func(td->parent, method_name);
    }
    return NULL;
}

// 设置 class 构造函数（__init__ 方法）
void class_set_constructor(const char* class_name, struct AstNode* constructor_node, void* constructor_func)
{
    TypeDef* td = class_lookup(class_name);
    if(!td) return;
    td->constructor = constructor_node;
    td->constructor_func = constructor_func;
}

// 获取 class 构造函数的 RuntimeFunc（支持继承链查找）
void* class_get_constructor_func(const char* class_name)
{
    TypeDef* td = class_lookup(class_name);
    if(!td) return NULL;
    // 先查当前类
    if(td->constructor_func) return td->constructor_func;
    // 再查父类（递归）
    if(td->parent) {
        return class_get_constructor_func(td->parent);
    }
    return NULL;
}

// 查找 class 方法（返回 AST 节点或 NULL，包含继承的方法）
struct AstNode* class_find_method(const char* class_name, const char* method_name)
{
    TypeDef* td = class_lookup(class_name);
    if(!td) return NULL;
    // 先在当前类中查找
    for(int i = 0; i < td->nmethods; i++) {
        if(strcmp(td->method_names[i], method_name) == 0) {
            return td->method_nodes[i];
        }
    }
    // 如果有父类，递归查找父类的方法
    if(td->parent) {
        return class_find_method(td->parent, method_name);
    }
    return NULL;
}

int type_implements_interface(const char* type_name, const char* interface_name) {
    /* 鸭子类型检查：类型是否有接口要求的所有方法 */
    int iidx = interface_lookup(interface_name);
    if(iidx < 0) return 0; /* 接口不存在 */

    InterfaceDef* idef = interface_get(iidx);
    if(!idef) return 0;

    /* 查找类型定义 */
    int tidx = type_lookup(type_name);
    if(tidx < 0) return 0; /* 类型不存在 */

    TypeDef* tdef = type_get(tidx);
    if(!tdef) return 0;

    /* 检查类型是否有接口要求的所有方法（属性） */
    for(int i = 0; i < idef->nmethods; i++) {
        int found = 0;
        for(int j = 0; j < tdef->nprops; j++) {
            if(strcmp(tdef->props[j], idef->methods[i].name) == 0) {
                found = 1;
                break;
            }
        }
        if(!found) return 0; /* 缺少方法 */
    }

    return 1; /* 实现了所有方法 */
}

/* 检查 class 是否实现了接口中定义的所有方法（包括继承的方法）
   返回 1=实现了所有方法，0=缺少方法，-1=接口不存在或class不存在 */
int class_check_interface_implementation(const char* class_name, const char* interface_name) {
    int iidx = interface_lookup(interface_name);
    if(iidx < 0) return -1; /* 接口不存在 */

    InterfaceDef* idef = interface_get(iidx);
    if(!idef) return -1;

    TypeDef* td = class_lookup(class_name);
    if(!td) return -1; /* class不存在 */

    /* 检查 class 是否有接口要求的所有方法（包括继承的方法） */
    for(int i = 0; i < idef->nmethods; i++) {
        struct AstNode* method = class_find_method(class_name, idef->methods[i].name);
        if(!method) {
            /* 缺少方法，打印警告 */
            fprintf(stderr, "警告：class \"%s\" 未实现接口 \"%s\" 要求的方法 \"%s\"\n",
                    class_name, interface_name, idef->methods[i].name);
            return 0;
        }
    }

    return 1; /* 实现了所有方法 */
}
