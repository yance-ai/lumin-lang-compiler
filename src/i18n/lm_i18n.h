/**
 * lm_i18n.h - Lumin Language Compiler Internationalization
 *
 * Lightweight i18n framework for C.
 * Supports: Simplified Chinese (default), Traditional Chinese,
 *           English, Japanese, Korean, Hindi.
 *
 * Usage:
 *   lm_i18n_init();  // call once at startup, auto-detects system language
 *   printf("%s", LM_TR(MSG_USAGE));
 *   printf(LM_TR(MSG_ERROR_FILE), filename);  // format strings supported
 */

#ifndef LM_I18N_H
#define LM_I18N_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Supported languages */
typedef enum {
    LM_LANG_ZH_CN = 0,  /* Simplified Chinese - default */
    LM_LANG_ZH_TW,      /* Traditional Chinese */
    LM_LANG_EN,          /* English */
    LM_LANG_JA,          /* Japanese */
    LM_LANG_KO,          /* Korean */
    LM_LANG_HI,          /* Hindi */
    LM_LANG_COUNT
} lm_lang_t;

/* String IDs - add new strings here */
typedef enum {
    /* ===== main.c - usage / help ===== */
    MSG_USAGE = 0,
    MSG_USAGE_DEFAULT,
    MSG_USAGE_C,
    MSG_USAGE_S,
    MSG_USAGE_O,
    MSG_MODULE_PREPROCESS_FAIL,
    MSG_SEMANTIC_CHECK_FAIL,
    MSG_CODEGEN_GENERATED,
    MSG_CODEGEN_COMPILED,
    MSG_CODEGEN_COMPILE_FAIL,

    /* ===== import.c - module errors ===== */
    MSG_IMPORT_SYNTAX_STRING,
    MSG_IMPORT_SYNTAX_UNCLOSED,
    MSG_IMPORT_SYNTAX_AS,
    MSG_IMPORT_SYNTAX_ALIAS,
    MSG_IMPORT_CANNOT_OPEN,
    MSG_IMPORT_INTERNAL_OVERFLOW,

    /* ===== vm.c - runtime errors ===== */
    MSG_VM_RUNTIME_ERROR,
    MSG_VM_ARRAY_INDEX_OUT_OF_BOUNDS,
    MSG_VM_DIVISION_BY_ZERO,
    MSG_VM_NULL_POINTER,
    MSG_VM_TYPE_MISMATCH,
    MSG_VM_STACK_OVERFLOW,

    /* ===== ast_typecheck.c - type errors ===== */
    MSG_TYPE_UNDECLARED_VAR,
    MSG_TYPE_REDECLARED_VAR,
    MSG_TYPE_INCOMPATIBLE,
    MSG_TYPE_FUNCTION_NOT_FOUND,
    MSG_TYPE_ARG_COUNT_MISMATCH,
    MSG_TYPE_RETURN_MISMATCH,

    /* ===== gc_runtime.c ===== */
    MSG_GC_TRIGGERED,
    MSG_GC_STATS,

    /* ===== generic ===== */
    MSG_ERROR,
    MSG_WARNING,
    MSG_NOTE,
    MSG_OK,
    MSG_FAIL,
    MSG_DONE,

    /* Add new strings above this line */
    MSG_COUNT
} lm_string_id_t;

/**
 * Initialize i18n subsystem.
 * Auto-detects system language and loads corresponding translations.
 * Must be called once at program startup before any LM_TR() calls.
 */
void lm_i18n_init(void);

/**
 * Set language explicitly.
 * @param lang Target language
 */
void lm_i18n_set_lang(lm_lang_t lang);

/**
 * Get current language.
 * @return Current language
 */
lm_lang_t lm_i18n_get_lang(void);

/**
 * Get language name string.
 * @param lang Language
 * @return Language name (e.g., "zh-CN", "en-US")
 */
const char* lm_i18n_lang_name(lm_lang_t lang);

/**
 * Detect system language from environment.
 * Windows: GetUserDefaultLocaleName()
 * Unix: LANG / LC_ALL / LC_MESSAGES environment variables
 * @return Detected language, defaults to LM_LANG_ZH_CN
 */
lm_lang_t lm_i18n_detect_lang(void);

/**
 * Translate a string ID to current language.
 * @param id String ID
 * @return Translated string (falls back to Simplified Chinese)
 */
const char* lm_tr(lm_string_id_t id);

/**
 * Translate and format a string.
 * Wrapper around printf-style formatting with translated format string.
 * @param id String ID (must contain format specifiers)
 * @param ... Format arguments
 * @return Formatted string (static buffer, valid until next call)
 */
const char* lm_tr_fmt(lm_string_id_t id, ...);

/* Convenience macro */
#define LM_TR(id) lm_tr(id)
#define LM_TR_FMT(id, ...) lm_tr_fmt(id, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* LM_I18N_H */
