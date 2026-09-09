#ifndef LM_HTTP_H
#define LM_HTTP_H

#include "lumin_value_type.h"
#include "lumin_value.h"

// 发 HTTP 请求（libcurl）
//   method : "GET"/"POST"/"PUT"/"DELETE"/"HEAD"/"PATCH" 等
//   url    : VAL_STRING，必填
//   params : VAL_MAP（键值对拼查询串）/ VAL_STRING（原样查询串）/ VAL_NONE
//   config : VAL_MAP，可选键：headers(VAL_MAP 请求头)、body(VAL_STRING 请求体)、
//            timeout(VAL_INT 秒，默认 30)
// 返回    : VAL_MAP {status:int, body:string, headers:map}
// 连接/协议错误 → runtime_error（可 catch）
Value lumin_http_request(const char* method, Value url, Value params, Value config);

#endif //LM_HTTP_H
