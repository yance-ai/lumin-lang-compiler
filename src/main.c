#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "yacc/yacc.tab.h"
#include "ast/ast.h"
#include "ir/ir_compile.h"
#include "ir/vm.h"
#include "ir/ir_cgen.h"

extern AstNode* root;
extern int yyparse(void);
extern FILE *yyin;
void yyrestart(FILE *f);

// 从路径中提取文件名，去掉扩展名，作为默认输出 basename
static void default_basename(const char* path, char* out, int size) {
    const char* name = strrchr(path, '/');
    name = name ? name + 1 : path;

    strncpy(out, name, size - 1);
    out[size - 1] = '\0';

    char* dot = strrchr(out, '.');
    if(dot) *dot = '\0';
}

int main(int argc, char** argv) {
    int codegen_mode = 0;
    int only_emit_c = 0;

    const char* src_file = NULL;
    const char* out_base = NULL;
    int i = 1;

    while(i < argc) {
        if(strcmp(argv[i], "-c") == 0) {
            codegen_mode = 1;
            i++;
        } else if(strcmp(argv[i], "-S") == 0) {
            codegen_mode = 1;
            only_emit_c = 1;
            i++;
        } else if(strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_base = argv[i + 1];
            i += 2;
        } else {
            src_file = argv[i];
            i++;
        }
    }

    if(!src_file) {
        fprintf(stderr, "Usage: %s [-c|-S] <source.lm> [-o output_name]\n", argv[0]);
        fprintf(stderr, "  默认:               解释执行源码\n");
        fprintf(stderr, "  -c:                 转译C源码 + gcc编译生成可执行文件\n");
        fprintf(stderr, "  -S:                 仅输出C源码，不编译\n");
        fprintf(stderr, "  -o:                 指定输出basename，默认取源文件名\n");
        return 1;
    }

    yyin = fopen(src_file, "r");
    if(!yyin) {
        perror("open file failed");
        return 1;
    }
    yyrestart(yyin);

    int ret = yyparse();
    if(ret == 0 && root != NULL) {
        if(ast_typecheck(root)) {
            fprintf(stderr, "语义检查未通过，编译中止\n");
            ast_free(root);
            root = NULL;
            ret = 1;
        } else if(codegen_mode) {
            if(only_emit_c) {
                // -S：输出字节码指令文本（反汇编）
                BytecodeFunc* main_fn = ir_compile_main(root);
                for(int k = 0; k < ir_func_table_count(); k++) {
                    bc_disasm(stdout, ir_func_table_get(k));
                }
                bc_disasm(stdout, main_fn);
                bytecode_func_free(main_fn);
                ret = 0;
            } else {
                char base[PATH_MAX];
                if(out_base) {
                    strncpy(base, out_base, sizeof(base) - 1);
                    base[sizeof(base) - 1] = '\0';
                } else {
                    default_basename(src_file, base, sizeof(base));
                }

                char c_path[PATH_MAX];
                char exe_path[PATH_MAX];
                snprintf(c_path, sizeof(c_path), "%s.c", base);
                snprintf(exe_path, sizeof(exe_path), "%s", base);

                BytecodeFunc* main_fn = ir_compile_main(root);
                ir_cgen_file(c_path, main_fn);
                bytecode_func_free(main_fn);
                printf("[CodeGen] 已生成 %s\n", c_path);

                if(!only_emit_c) {
                    char cmd[PATH_MAX * 2];
                    snprintf(cmd, sizeof(cmd), "gcc -std=gnu11 %s -o %s -lcurl", c_path, exe_path);
                    int sys_ret = system(cmd);

                    if(sys_ret == 0) {
                        printf("[CodeGen] 已编译为 ./%s\n", exe_path);
                    } else {
                        fprintf(stderr, "[CodeGen] gcc 编译失败\n");
                        return 1;
                    }
                }
            }
        } else {
            // 字节码 VM 执行（AST → IR → vm）
            BytecodeFunc* main_fn = ir_compile_main(root);
            vm_run_main(main_fn);
            bytecode_func_free(main_fn);
        }
        ast_free(root);
    }

    if(yyin && yyin != stdin) {
        fclose(yyin);
    }
    return ret;
}
