# 操作

每次有新消息或新阶段，你会直接收到简报。处理完就结束回复，不需要等待或轮询。

所有命令都在本目录运行：

```text
python game.py send ENGLAND "提议：你进比利时，我不进勃艮第。"
python game.py send GLOBAL "公开声明内容"
python game.py orders "A PAR - BUR" "A MAR S A PAR - BUR" "F BRE - MAO"
python game.py draw        赞成协议和局（undraw 撤回）
```

- `orders` 一次提交本阶段全部命令；需要修改就重新提交整份。命令有误会列出该部队的合法选项。
- 冬季放弃增兵：`python game.py orders`（不带命令）。

需要时可以查询：

```text
python game.py brief                 完整当前局面
python game.py legal PAR BUR         某些部队的合法命令（不带参数则列出全部）
python game.py inbox --with FRANCE   与某国的全部消息（也可 --phase S1901M）
python game.py history               最近几次结算（也可 --phase F1901M）
python game.py simulate 假设.json    推演一组假设命令，不影响真实对局
```

`假设.json` 的格式：`{"FRANCE": ["A PAR - BUR"], "GERMANY": ["A MUN - BUR"]}`，没写的部队按驻守处理。

可以在本目录记笔记、写程序辅助计算。
