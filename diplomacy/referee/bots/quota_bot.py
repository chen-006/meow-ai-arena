"""测试用：模拟额度用完的报错。"""
import sys

sys.stderr.write('Error: 429 Too Many Requests: usage limit reached\n')
sys.exit(1)
