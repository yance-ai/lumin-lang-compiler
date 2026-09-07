#ifndef LM_CHARSET_H
#define LM_CHARSET_H

// 字符编码支持：UTF-8 / GBK / GB18030 / BIG5 / Latin-1 / ASCII / Shift_JIS
// 内部字符串统一 UTF-8；bytes()/str()/qs()/json()/stringify() 的 enc 参数
// （可选，默认 UTF-8）控制与外部字节流的编码互换。
#include "lm_value.h"

// enc 参数规范化：NULL/空 → "UTF-8"；大小写、-_ 不敏感
// （utf8→UTF-8、gbk/gb2312/cp936→GBK、big5→BIG5、latin1/iso-8859-1→ISO-8859-1、
//   ascii→ASCII、shift_jis/sjis→SHIFT_JIS、gb18030→GB18030）；未知 → NULL
const char* lumin_charset_norm(const char* enc);
// enc Value → 规范化名；空值 → UTF-8；非法 → runtime_error
const char* lumin_charset_from_value(Value enc);

// iconv 通用转换：from → to；malloc 输出（*outlen=数据长，末尾补 \0）；失败 NULL
char* lumin_charset_convert(const char* from, const char* to,
                            const char* in, size_t inlen, size_t* outlen);

// 字符串（内部 UTF-8）→ 字节数组（按 enc 编码，VAL_BYTE 数组）
Value lumin_to_bytes(Value s, Value enc);
// 字节数组 → 字符串（按 enc 解码为内部 UTF-8）
Value lumin_from_bytes(Value arr, Value enc);
// 外部文本（enc 编码）→ UTF-8（malloc；失败 NULL）——json/qs 解析输入
char* lumin_text_to_utf8(const char* s, size_t len, Value enc);
// UTF-8 文本 → enc 编码（malloc；失败 NULL）——qs/stringify 输出
char* lumin_utf8_to_text(const char* s, Value enc);

#endif //LM_CHARSET_H
