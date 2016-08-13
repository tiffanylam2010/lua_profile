# 编译，生成 cpu_profile.so & storage.so
sh makeme.sh

# 运行需要profile的测试用例
lua test.lua

# 查看分析结果
python parse.py
