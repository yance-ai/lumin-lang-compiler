import re

# 读取文件
with open('regress_dual.py', 'r', encoding='utf-8') as f:
    content = f.read()

# 移除 ref_param_test.lm
old_skip = '''    "type_comprehensive_test.lm",  # CC模式struct参数还是引用传递，VM已修复为值传递，待CC修复后移除
    "ref_param_test.lm",  # CC模式还不支持ref参数，VM已实现，待CC修复后移除
}'''

new_skip = '''    "type_comprehensive_test.lm",  # CC模式struct参数还是引用传递，VM已修复为值传递，待CC修复后移除
}'''

if old_skip in content:
    content = content.replace(old_skip, new_skip)
    print("移除 ref_param_test.lm 成功")
else:
    print("未找到 ref_param_test.lm")

# 写入文件
with open('regress_dual.py', 'w', encoding='utf-8') as f:
    f.write(content)
