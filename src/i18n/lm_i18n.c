/**
 * lm_i18n.c - Lumyr Language Compiler Internationalization
 *
 * Translation table and language detection implementation.
 */

#include "lm_i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifdef _WIN32
#include <windows.h>
#define strcasecmp _stricmp
#else
#include <strings.h>
#include <locale.h>
#endif

/* Current language */
static lm_lang_t g_current_lang = LM_LANG_ZH_CN;
static int g_initialized = 0;

/* Format buffer for lm_tr_fmt */
static char g_fmt_buffer[4096];

/* ============================================================
 * Translation Table
 * Each row: [ZH_CN, ZH_TW, EN, JA, KO, HI]
 * ============================================================ */

static const char* g_translations[MSG_COUNT][LM_LANG_COUNT] = {
    /* ===== main.c - usage / help ===== */
    [MSG_USAGE] = {
        "用法: %s [-c|-S] <source.lm> [-o output_name]\n",
        "用法: %s [-c|-S] <source.lm> [-o output_name]\n",
        "Usage: %s [-c|-S] <source.lm> [-o output_name]\n",
        "使い方: %s [-c|-S] <source.lm> [-o output_name]\n",
        "사용법: %s [-c|-S] <source.lm> [-o output_name]\n",
        "उपयोग: %s [-c|-S] <source.lm> [-o output_name]\n",
    },
    [MSG_USAGE_DEFAULT] = {
        "  默认:               解释执行源码\n",
        "  預設:               直譯執行原始碼\n",
        "  default:             Interpret and execute source\n",
        "  デフォルト:         ソースを解釈実行\n",
        "  기본값:             소스 인터프리트 실행\n",
        "  डिफ़ॉल्ट:           स्रोत की व्याख्या और निष्पादन\n",
    },
    [MSG_USAGE_C] = {
        "  -c:                 转译C源码 + gcc编译生成可执行文件\n",
        "  -c:                 轉譯C原始碼 + gcc編譯生成可執行檔\n",
        "  -c:                 Transpile to C + gcc compile to executable\n",
        "  -c:                 Cソースに変換 + gccで実行ファイルをコンパイル\n",
        "  -c:                 C 소스로 변환 + gcc로 실행 파일 컴파일\n",
        "  -c:                 C स्रोत में रूपांतरण + gcc से निष्पादन योग्य\n",
    },
    [MSG_USAGE_S] = {
        "  -S:                 仅输出C源码，不编译\n",
        "  -S:                 僅輸出C原始碼，不編譯\n",
        "  -S:                 Output C source only, do not compile\n",
        "  -S:                 Cソースのみ出力、コンパイルしない\n",
        "  -S:                 C 소스만 출력, 컴파일 안 함\n",
        "  -S:                 केवल C स्रोत आउटपुट, संकलन नहीं\n",
    },
    [MSG_USAGE_O] = {
        "  -o:                 指定输出basename，默认取源文件名\n",
        "  -o:                 指定輸出basename，預設取原始檔名\n",
        "  -o:                 Specify output basename, default is source filename\n",
        "  -o:                 出力basenameを指定、デフォルトはソースファイル名\n",
        "  -o:                 출력 basename 지정, 기본값은 소스 파일명\n",
        "  -o:                 आउटपुट basename निर्दिष्ट करें, डिफ़ॉल्ट स्रोत फ़ाइल नाम\n",
    },
    [MSG_MODULE_PREPROCESS_FAIL] = {
        "模块预处理失败，编译中止\n",
        "模組預處理失敗，編譯中止\n",
        "Module preprocessing failed, compilation aborted\n",
        "モジュール前処理に失敗、コンパイル中止\n",
        "모듈 전처리 실패, 컴파일 중단\n",
        "मॉड्यूल प्रीप्रोसेसिंग विफल, संकलन रद्द\n",
    },
    [MSG_SEMANTIC_CHECK_FAIL] = {
        "语义检查未通过，编译中止\n",
        "語意檢查未通過，編譯中止\n",
        "Semantic check failed, compilation aborted\n",
        "意味解析に失敗、コンパイル中止\n",
        "의미 체크 실패, 컴파일 중단\n",
        "शाब्दिक जाँच विफल, संकलन रद्द\n",
    },
    [MSG_CODEGEN_GENERATED] = {
        "[CodeGen] 已生成 %s\n",
        "[CodeGen] 已生成 %s\n",
        "[CodeGen] Generated %s\n",
        "[CodeGen] 生成しました %s\n",
        "[CodeGen] 생성됨 %s\n",
        "[CodeGen] उत्पन्न हुआ %s\n",
    },
    [MSG_CODEGEN_COMPILED] = {
        "[CodeGen] 已编译为 ./%s\n",
        "[CodeGen] 已編譯為 ./%s\n",
        "[CodeGen] Compiled to ./%s\n",
        "[CodeGen] コンパイル完了 ./%s\n",
        "[CodeGen] 컴파일됨 ./%s\n",
        "[CodeGen] संकलित ./%s\n",
    },
    [MSG_CODEGEN_COMPILE_FAIL] = {
        "[CodeGen] %s 编译失败\n",
        "[CodeGen] %s 編譯失敗\n",
        "[CodeGen] %s compilation failed\n",
        "[CodeGen] %s コンパイル失敗\n",
        "[CodeGen] %s 컴파일 실패\n",
        "[CodeGen] %s संकलन विफल\n",
    },

    /* ===== import.c - module errors ===== */
    [MSG_IMPORT_SYNTAX_STRING] = {
        "[module] 语法错误：import 后应为字符串字面量路径\n",
        "[module] 語法錯誤：import 後應為字串字面量路徑\n",
        "[module] Syntax error: import must be followed by string literal path\n",
        "[module] 構文エラー: import の後は文字列リテラルパスが必要\n",
        "[module] 구문 오류: import 뒤에는 문자열 리터럴 경로가 와야 함\n",
        "[module] वाक्य त्रुटि: import के बाद स्ट्रिंग लिटरल पथ होना चाहिए\n",
    },
    [MSG_IMPORT_SYNTAX_UNCLOSED] = {
        "[module] 语法错误：import 路径字符串未闭合\n",
        "[module] 語法錯誤：import 路徑字串未閉合\n",
        "[module] Syntax error: import path string not closed\n",
        "[module] 構文エラー: import パス文字列が閉じていません\n",
        "[module] 구문 오류: import 경로 문자열이 닫히지 않음\n",
        "[module] वाक्य त्रुटि: import पथ स्ट्रिंग बंद नहीं है\n",
    },
    [MSG_IMPORT_SYNTAX_AS] = {
        "[module] 语法错误：import 路径后应为 `as 别名`\n",
        "[module] 語法錯誤：import 路徑後應為 `as 別名`\n",
        "[module] Syntax error: import path should be followed by `as alias`\n",
        "[module] 構文エラー: import パスの後は `as エイリアス` が必要\n",
        "[module] 구문 오류: import 경로 뒤에는 `as 별칭` 이 와야 함\n",
        "[module] वाक्य त्रुटि: import पथ के बाद `as उपनाम` होना चाहिए\n",
    },
    [MSG_IMPORT_SYNTAX_ALIAS] = {
        "[module] 语法错误：as 后应为别名标识符\n",
        "[module] 語法錯誤：as 後應為別名識別符\n",
        "[module] Syntax error: as must be followed by alias identifier\n",
        "[module] 構文エラー: as の後はエイリアス識別子が必要\n",
        "[module] 구문 오류: as 뒤에는 별칭 식별자가 와야 함\n",
        "[module] वाक्य त्रुटि: as के बाद उपनाम पहचानकर्ता होना चाहिए\n",
    },
    [MSG_IMPORT_CANNOT_OPEN] = {
        "[module] 无法打开模块文件: %s\n",
        "[module] 無法開啟模組檔案: %s\n",
        "[module] Cannot open module file: %s\n",
        "[module] モジュールファイルを開けません: %s\n",
        "[module] 모듈 파일을 열 수 없음: %s\n",
        "[module] मॉड्यूल फ़ाइल नहीं खोल सकता: %s\n",
    },
    [MSG_IMPORT_INTERNAL_OVERFLOW] = {
        "[module] 内部错误：占位符越界\n",
        "[module] 內部錯誤：佔位符越界\n",
        "[module] Internal error: placeholder overflow\n",
        "[module] 内部エラー: プレースホルダーオーバーフロー\n",
        "[module] 내부 오류: 플레이스홀더 오버플로우\n",
        "[module] आंतरिक त्रुटि: प्लेसहोल्डर ओवरफ़्लो\n",
    },

    /* ===== vm.c - runtime errors ===== */
    [MSG_VM_RUNTIME_ERROR] = {
        "Runtime Error: %s\n",
        "Runtime Error: %s\n",
        "Runtime Error: %s\n",
        "ランタイムエラー: %s\n",
        "런타임 오류: %s\n",
        "रनटाइम त्रुटि: %s\n",
    },
    [MSG_VM_ARRAY_INDEX_OUT_OF_BOUNDS] = {
        "数组下标越界: %d (长度 %d)\n",
        "陣列下標越界: %d (長度 %d)\n",
        "Array index out of bounds: %d (length %d)\n",
        "配列インデックス範囲外: %d (長さ %d)\n",
        "배열 인덱스 범위 초과: %d (길이 %d)\n",
        "सरणी अनुक्रम सीमा से बाहर: %d (लंबाई %d)\n",
    },
    [MSG_VM_DIVISION_BY_ZERO] = {
        "除零错误\n",
        "除零錯誤\n",
        "Division by zero\n",
        "ゼロ除算エラー\n",
        "0으로 나누기 오류\n",
        "शून्य से विभाजन\n",
    },
    [MSG_VM_NULL_POINTER] = {
        "空指针访问\n",
        "空指標存取\n",
        "Null pointer access\n",
        "ヌルポインタアクセス\n",
        "널 포인터 접근\n",
        "नल पॉइंटर एक्सेस\n",
    },
    [MSG_VM_TYPE_MISMATCH] = {
        "类型不匹配: 期望 %s, 实际 %s\n",
        "類型不匹配: 期望 %s, 實際 %s\n",
        "Type mismatch: expected %s, got %s\n",
        "型の不一致: 期待 %s, 実際 %s\n",
        "타입 불일치: 예상 %s, 실제 %s\n",
        "प्रकार बेमेल: अपेक्षित %s, वास्तविक %s\n",
    },
    [MSG_VM_STACK_OVERFLOW] = {
        "栈溢出\n",
        "堆疊溢位\n",
        "Stack overflow\n",
        "スタックオーバーフロー\n",
        "스택 오버플로우\n",
        "स्टैक ओवरफ़्लो\n",
    },

    /* ===== ast_typecheck.c - type errors ===== */
    [MSG_TYPE_UNDECLARED_VAR] = {
        "未声明的变量: %s\n",
        "未宣告的變數: %s\n",
        "Undeclared variable: %s\n",
        "未宣言の変数: %s\n",
        "선언되지 않은 변수: %s\n",
        "अघोषित चर: %s\n",
    },
    [MSG_TYPE_REDECLARED_VAR] = {
        "重复声明的变量: %s\n",
        "重複宣告的變數: %s\n",
        "Redeclared variable: %s\n",
        "再宣言された変数: %s\n",
        "중복 선언된 변수: %s\n",
        "पुनर्घोषित चर: %s\n",
    },
    [MSG_TYPE_INCOMPATIBLE] = {
        "类型不兼容: %s 与 %s\n",
        "類型不相容: %s 與 %s\n",
        "Incompatible types: %s and %s\n",
        "互換性のない型: %s と %s\n",
        "호환되지 않는 타입: %s 와 %s\n",
        "असंगत प्रकार: %s और %s\n",
    },
    [MSG_TYPE_FUNCTION_NOT_FOUND] = {
        "未找到函数: %s\n",
        "未找到函式: %s\n",
        "Function not found: %s\n",
        "関数が見つかりません: %s\n",
        "함수를 찾을 수 없음: %s\n",
        "फ़ंक्शन नहीं मिला: %s\n",
    },
    [MSG_TYPE_ARG_COUNT_MISMATCH] = {
        "参数数量不匹配: 函数 %s 期望 %d 个，实际 %d 个\n",
        "參數數量不匹配: 函式 %s 期望 %d 個，實際 %d 個\n",
        "Argument count mismatch: function %s expects %d, got %d\n",
        "引数の数が一致しません: 関数 %s は %d 個必要、実際 %d 個\n",
        "인수 개수 불일치: 함수 %s 는 %d 개 필요, 실제 %d 개\n",
        "तर्क संख्या बेमेल: फ़ंक्शन %s को %d चाहिए, प्राप्त %d\n",
    },
    [MSG_TYPE_RETURN_MISMATCH] = {
        "返回值类型不匹配: 函数 %s 期望 %s, 实际 %s\n",
        "傳回值類型不匹配: 函式 %s 期望 %s, 實際 %s\n",
        "Return type mismatch: function %s expects %s, got %s\n",
        "戻り値の型が一致しません: 関数 %s は %s 期待、実際 %s\n",
        "반환 타입 불일치: 함수 %s 는 %s 예상, 실제 %s\n",
        "रिटर्न प्रकार बेमेल: फ़ंक्शन %s को %s चाहिए, प्राप्त %s\n",
    },

    /* ===== gc_runtime.c ===== */
    [MSG_GC_TRIGGERED] = {
        "[GC] 触发垃圾回收\n",
        "[GC] 觸發垃圾回收\n",
        "[GC] Garbage collection triggered\n",
        "[GC] ガベージコレクション発動\n",
        "[GC] 가비지 컬렉션 트리거됨\n",
        "[GC] कचरा संग्रह ट्रिगर हुआ\n",
    },
    [MSG_GC_STATS] = {
        "[GC] 存活对象: %d, 回收: %d, 内存: %zu bytes\n",
        "[GC] 存活物件: %d, 回收: %d, 記憶體: %zu bytes\n",
        "[GC] Live objects: %d, collected: %d, memory: %zu bytes\n",
        "[GC] 生存オブジェクト: %d, 回収: %d, メモリ: %zu bytes\n",
        "[GC] 라이브 객체: %d, 수집: %d, 메모리: %zu bytes\n",
        "[GC] लाइव ऑब्जेक्ट: %d, एकत्रित: %d, मेमोरी: %zu bytes\n",
    },

    /* ===== generic ===== */
    [MSG_ERROR] = {
        "错误: %s\n",
        "錯誤: %s\n",
        "Error: %s\n",
        "エラー: %s\n",
        "오류: %s\n",
        "त्रुटि: %s\n",
    },
    [MSG_WARNING] = {
        "警告: %s\n",
        "警告: %s\n",
        "Warning: %s\n",
        "警告: %s\n",
        "경고: %s\n",
        "चेतावनी: %s\n",
    },
    [MSG_NOTE] = {
        "注意: %s\n",
        "注意: %s\n",
        "Note: %s\n",
        "注意: %s\n",
        "참고: %s\n",
        "नोट: %s\n",
    },
    [MSG_OK] = {
        "OK\n",
        "OK\n",
        "OK\n",
        "OK\n",
        "OK\n",
        "OK\n",
    },
    [MSG_FAIL] = {
        "FAIL\n",
        "FAIL\n",
        "FAIL\n",
        "FAIL\n",
        "FAIL\n",
        "FAIL\n",
    },
    [MSG_DONE] = {
        "完成\n",
        "完成\n",
        "Done\n",
        "完了\n",
        "완료\n",
        "पूरा हुआ\n",
    },
};

/* ============================================================
 * Language Detection
 * ============================================================ */

lm_lang_t lm_i18n_detect_lang(void)
{
#ifdef _WIN32
    /* Windows: use GetUserDefaultLocaleName */
    wchar_t locale_name[LOCALE_NAME_MAX_LENGTH];
    if (GetUserDefaultLocaleName(locale_name, LOCALE_NAME_MAX_LENGTH) > 0) {
        char locale_utf8[LOCALE_NAME_MAX_LENGTH * 4];
        WideCharToMultiByte(CP_UTF8, 0, locale_name, -1,
                            locale_utf8, sizeof(locale_utf8), NULL, NULL);

        if (strstr(locale_utf8, "zh-CN") || strstr(locale_utf8, "zh-Hans") ||
            strstr(locale_utf8, "zh-SG")) {
            return LM_LANG_ZH_CN;
        }
        if (strstr(locale_utf8, "zh-TW") || strstr(locale_utf8, "zh-HK") ||
            strstr(locale_utf8, "zh-MO") || strstr(locale_utf8, "zh-Hant")) {
            return LM_LANG_ZH_TW;
        }
        if (strstr(locale_utf8, "en")) {
            return LM_LANG_EN;
        }
        if (strstr(locale_utf8, "ja")) {
            return LM_LANG_JA;
        }
        if (strstr(locale_utf8, "ko")) {
            return LM_LANG_KO;
        }
        if (strstr(locale_utf8, "hi") || strstr(locale_utf8, "-IN")) {
            return LM_LANG_HI;
        }
    }
#else
    /* Unix: check environment variables */
    const char* lang = getenv("LC_ALL");
    if (!lang || !*lang) lang = getenv("LC_MESSAGES");
    if (!lang || !*lang) lang = getenv("LANG");

    if (lang && *lang) {
        if (strstr(lang, "zh_CN") || strstr(lang, "zh_Hans") ||
            strstr(lang, "zh_SG")) {
            return LM_LANG_ZH_CN;
        }
        if (strstr(lang, "zh_TW") || strstr(lang, "zh_HK") ||
            strstr(lang, "zh_MO") || strstr(lang, "zh_Hant")) {
            return LM_LANG_ZH_TW;
        }
        if (strstr(lang, "en")) {
            return LM_LANG_EN;
        }
        if (strstr(lang, "ja")) {
            return LM_LANG_JA;
        }
        if (strstr(lang, "ko")) {
            return LM_LANG_KO;
        }
        if (strstr(lang, "hi") || strstr(lang, "_IN")) {
            return LM_LANG_HI;
        }
    }
#endif

    /* Default to Simplified Chinese */
    return LM_LANG_ZH_CN;
}

/* ============================================================
 * Public API
 * ============================================================ */

void lm_i18n_init(void)
{
    if (g_initialized) return;

    /* Check LUMYR_LANG environment variable first for manual override */
    const char* env_lang = getenv("LUMYR_LANG");
    if (env_lang && *env_lang) {
        if (strcasecmp(env_lang, "zh-CN") == 0 || strcasecmp(env_lang, "zh_CN") == 0 ||
            strcasecmp(env_lang, "zh") == 0 || strcasecmp(env_lang, "zh-Hans") == 0) {
            g_current_lang = LM_LANG_ZH_CN;
        } else if (strcasecmp(env_lang, "zh-TW") == 0 || strcasecmp(env_lang, "zh_TW") == 0 ||
                   strcasecmp(env_lang, "zh-HK") == 0 || strcasecmp(env_lang, "zh-Hant") == 0) {
            g_current_lang = LM_LANG_ZH_TW;
        } else if (strcasecmp(env_lang, "en") == 0 || strcasecmp(env_lang, "en-US") == 0 ||
                   strcasecmp(env_lang, "en_US") == 0) {
            g_current_lang = LM_LANG_EN;
        } else if (strcasecmp(env_lang, "ja") == 0 || strcasecmp(env_lang, "ja-JP") == 0 ||
                   strcasecmp(env_lang, "ja_JP") == 0) {
            g_current_lang = LM_LANG_JA;
        } else if (strcasecmp(env_lang, "ko") == 0 || strcasecmp(env_lang, "ko-KR") == 0 ||
                   strcasecmp(env_lang, "ko_KR") == 0) {
            g_current_lang = LM_LANG_KO;
        } else if (strcasecmp(env_lang, "hi") == 0 || strcasecmp(env_lang, "hi-IN") == 0 ||
                   strcasecmp(env_lang, "hi_IN") == 0) {
            g_current_lang = LM_LANG_HI;
        } else {
            g_current_lang = lm_i18n_detect_lang();
        }
    } else {
        g_current_lang = lm_i18n_detect_lang();
    }
    g_initialized = 1;
}

void lm_i18n_set_lang(lm_lang_t lang)
{
    if (lang >= 0 && lang < LM_LANG_COUNT) {
        g_current_lang = lang;
    }
    g_initialized = 1;
}

lm_lang_t lm_i18n_get_lang(void)
{
    return g_current_lang;
}

const char* lm_i18n_lang_name(lm_lang_t lang)
{
    static const char* names[LM_LANG_COUNT] = {
        "zh-CN", "zh-TW", "en-US", "ja-JP", "ko-KR", "hi-IN"
    };
    if (lang >= 0 && lang < LM_LANG_COUNT) {
        return names[lang];
    }
    return "unknown";
}

const char* lm_tr(lm_string_id_t id)
{
    if (!g_initialized) {
        lm_i18n_init();
    }
    if (id >= 0 && id < MSG_COUNT) {
        const char* str = g_translations[id][g_current_lang];
        if (str && *str) {
            return str;
        }
        /* Fallback to Simplified Chinese */
        return g_translations[id][LM_LANG_ZH_CN];
    }
    return "(unknown string id)";
}

const char* lm_tr_fmt(lm_string_id_t id, ...)
{
    const char* fmt = lm_tr(id);
    va_list args;
    va_start(args, id);
    vsnprintf(g_fmt_buffer, sizeof(g_fmt_buffer), fmt, args);
    va_end(args);
    return g_fmt_buffer;
}
