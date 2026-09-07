// ast_types.c —— type 声明类型表（编译期全局注册）
#include "ast_types.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static TypeDef* g_types = NULL;
static int g_types_n = 0;
static int g_types_cap = 0;

int type_register(const char* name, char** props, ValueType* ptypes, int nprops)
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
    }
    // 释放旧属性（重声明覆盖）
    if(g_types[i].props) {
        for(int k = 0; k < g_types[i].nprops; k++) free(g_types[i].props[k]);
        free(g_types[i].props);
        free(g_types[i].ptypes);
    }
    g_types[i].props = (char**)malloc((size_t)(nprops > 0 ? nprops : 1) * sizeof(char*));
    g_types[i].ptypes = (ValueType*)malloc((size_t)(nprops > 0 ? nprops : 1) * sizeof(ValueType));
    for(int k = 0; k < nprops; k++) {
        g_types[i].props[k] = strdup(props[k]);
        g_types[i].ptypes[k] = ptypes[k];
    }
    g_types[i].nprops = nprops;
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
            return VAL_INT;
        default: return VAL_NONE;
    }
}
