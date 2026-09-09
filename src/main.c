#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include "yacc/yacc.tab.h"
#include "ast/ast.h"
#include "ir/ir_compile.h"
#include "ir/vm.h"
#include "ir/ir_cgen.h"
#include "parse/import.h"

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

    /* 模块系统（第一阶段）：yyparse 之前做文本预处理，把 import 模块内联、
       把 export 符号收集成 map。无模块语法的源文件走原始 fopen 路径，行为不变。 */
    char pp_tmp[PATH_MAX] = {0};   /* 预处理临时文件路径（若使用） */
    int used_pp_tmp = 0;

    int had_mod = 0;
    char* merged = lm_preprocess_main(src_file, &had_mod);
    if(had_mod < 0) {
        fprintf(stderr, "模块预处理失败，编译中止\n");
        return 1;
    }

    if(had_mod == 0) {
        yyin = fopen(src_file, "r");
        if(!yyin) {
            perror("open file failed");
            return 1;
        }
        yyrestart(yyin);
    } else {
        /* 把合并后的源码写入临时文件，再交给 parser（对 VM / 编译通道完全一致） */
#ifdef _WIN32
        const char* win_tmp = getenv("TEMP");
        if (!win_tmp) win_tmp = getenv("TMP");
        if (!win_tmp) win_tmp = ".";
        snprintf(pp_tmp, sizeof(pp_tmp), "%s/lumin_pp_XXXXXX", win_tmp);
#else
        snprintf(pp_tmp, sizeof(pp_tmp), "%s/lumin_pp_XXXXXX", P_tmpdir ? P_tmpdir : "/tmp");
#endif
        int fd = mkstemp(pp_tmp);
        if(fd < 0) {
            perror("mkstemp");
            free(merged);
            return 1;
        }
        if(write(fd, merged, strlen(merged)) < 0) {
            perror("write pp tmp");
            close(fd);
            unlink(pp_tmp);
            free(merged);
            return 1;
        }
        close(fd);
        free(merged);

        yyin = fopen(pp_tmp, "r");
        if(!yyin) {
            perror("open pp tmp failed");
            unlink(pp_tmp);
            return 1;
        }
        used_pp_tmp = 1;
        yyrestart(yyin);
    }

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
                // 大项目架构：编译产物统一放到 build/ 目录
                snprintf(c_path, sizeof(c_path), "build/%s.c", base);
                snprintf(exe_path, sizeof(exe_path), "build/%s", base);

                BytecodeFunc* main_fn = ir_compile_main(root);
                // 确保 build/ 目录存在
#ifdef _WIN32
                _mkdir("build");
#else
                mkdir("build", 0755);
#endif
                ir_cgen_file(c_path, main_fn);
                bytecode_func_free(main_fn);
                printf("[CodeGen] 已生成 %s\n", c_path);

                if(!only_emit_c) {
                    char cmd[PATH_MAX * 2];
                    /* 允许通过环境变量覆盖生成代码的编译器/ flags（用于 ASan 等调试） */
                    const char* gen_cc = getenv("LM_GEN_CC");
                    const char* gen_cflags = getenv("LM_GEN_CFLAGS");
                    if(!gen_cc) gen_cc = "gcc";
                    if(!gen_cflags) gen_cflags = "-O2";
                    // 大项目架构：链接 runtime 静态库
                    // 根据平台添加第三方库路径和系统库
#ifdef _WIN32
                    snprintf(cmd, sizeof(cmd),
                             "%s -std=gnu11 %s -Ikit/runtime/include -Ithird_party/windows/include -DCURL_STATICLIB "
                             "-Llib -Lthird_party/windows/lib %s -o %s "
                             "-lruntime -lcurl -liconv -ltre "
                             "-lcrypt32 -lws2_32 -lwldap32 -lwinmm -lnormaliz -liphlpapi -lbcrypt -lsecur32",
                             gen_cc, gen_cflags, c_path, exe_path);
#else
                    snprintf(cmd, sizeof(cmd),
                             "%s -std=gnu11 %s -Ikit/runtime/include -Llib %s -o %s -lruntime -lcurl -liconv",
                             gen_cc, gen_cflags, c_path, exe_path);
#endif
                    int sys_ret = system(cmd);

                    if(sys_ret == 0) {
                        printf("[CodeGen] 已编译为 ./%s\n", exe_path);
                    } else {
                        fprintf(stderr, "[CodeGen] %s 编译失败\n", gen_cc);
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
    if(used_pp_tmp) unlink(pp_tmp);
    return ret;
}
