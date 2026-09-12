// ast_types.c —— type 声明类型表（编译期全局注册）
#include "ast_types.h"
#include "ast_node.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
        default: return VAL_NONE;
    }
}

/* CAST_xxx -> 类型名字符串（用于 FFI extern 函数返回类型存储） */
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
        case CAST_LONGLONG: return strdup("longlong");
        case CAST_FLOAT: return strdup("float");
        case CAST_ASCII: return strdup("ascii");
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
