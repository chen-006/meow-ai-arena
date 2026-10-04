# 第二局：我是 ENGLAND
## 上一局经验（我当俄国，4.09分）
- 结果：S1906M 全体协议和局，按中心²分。英9城得36.8。玩家们约1905-06就愿意和局。
- 早期大家多守DMZ约定；背刺发生在1902后。上局英国两次背刺俄国（STP、MOS）且外交上一直“诚恳”，最终得分最高。
- 德国上局两次骗俄（PRU克制、不进WAR）。土耳其对俄一直守信，但对意扩张。意大利守信、爱请求援助。
- 和局阶段：中心多时尽早推动和局锁定份额；GLOBAL 公开施压有效。
- defense.py 可枚举敌方组合评估防守（改UNITS/PHASE/ENEMY/MYAREA）。

## 本局
协议：法-ENG DMZ, 互不犯BRE/PIC/BUR/GAS，法取SPA+POR，我取NWY+BEL。德-DEN/HOL归德，NWY+BEL(1901)归我，NTH/HEL互不进；德RUH可能支援我进BEL。对德1902承诺：我舰不进HEL/BAL/KIE/DEN/HOL，BEL陆军不向RUH/HOL推进。俄-NWY我SWE他，BAR DMZ，不碰STP。
秋季计划：F NWG-NWY, F NTH C A YOR-BEL。
F1901：全部按约。英5城（+NWY BEL）。俄6城领先。W1901建 F EDI, F LON。
1902思路：俄最大(6)，北方STP是目标（NWY+BAR+?）；守住与法/德约定，防法海峡。
S1902：法 PAR-PIC（威胁BEL），法称本季不攻BEL。俄承诺STP/SWE不攻NWY。德提议攻法BUR；我把法春令透露给德（请保密），计划秋季英德攻法：我 LON-ENG→BRE，BEL配合PIC/PAR。
德拒攻法（与法续和）；改为英德北方：SWE归德、STP归英。俄STP军调FIN。**承诺德：F1902 SKA支援德进SWE；我F NWY-STP/NC取空城STP。** 对俄说过“秋季不支援攻瑞典”（将违背）。
F1902：德取SWE；俄FIN-STP被我NWY弹回，STP空。俄背约感但提议英俄攻德。
**S1903 背刺德国**：NWY-SWE（请俄FIN/BOT支援），SKA-DEN（空），BAR-NWY回填，LON-NTH。对德谎称SKA S SWE。向法透露德MUN-SIL，提议法取MUN，英取DEN/SWE/HOL/KIE。英俄新界：SWE/DEN/NWY英，STP/FIN俄。
S1903结果：取DEN；俄A FIN-NWY偷袭被弹（俄称“保险”）。
F1903：法 PIC S BEL、BUR攻MUN；三方分德（HOL/KIE英，MUN/RUH法，BER/PRU俄）。俄称 FIN S NWY-SWE。令：DEN S NWY-SWE, NTH S DEN, NWY-SWE, NWG-NWY, BEL H。
F1903结果：取SWE（俄FIN守信支援），德F SWE退BOT。英7城。德RUH-KIE, PRU-BER（4城：BER HOL KIE MUN）。意取TRI，俄取BUD。W1903建 A LON, F EDI。
1904注意：德F BOT+F BAL威胁SWE，A KIE+BAL威胁DEN。
W1903：德解散F BOT/F BAL（无舰队）。S1904令：NTH C LON-HOL, BEL S, DEN-KIE切支援, SWE/NWY/EDI H。
S1904结果：取HOL（德A HOL退RUH）。法BUR-MUN弹回。俄WAR-SIL，取VIE；土攻俄RUM。德拒停战提议（我未回）。法索补偿：许诺MUN+RUH归法、西线舰队不进ENG/MAO/IRI。
F1904令(P5)：BAL S SIL-BER(俄), DEN-KIE S HOL, BEL S HOL, NTH S HOL, NWY H, EDI-NWG。fall04.py=简易枚举脚本（组合多时很慢）。
**F1904结果：法背刺！PIC-BEL S BUR + 德RUH支援，A BEL被逐(消灭)。** 我取KIE(+HOL) → 8城；丢BEL。俄SIL-BER弹回，俄丢RUM/SEV给土(土8)。俄FIN撤STP、F LVN（北方空）。W1904建 F LON, F LVP（对法海战）。
1905计划：联意攻法(MAR)；联德(2城)反法或至少中立；与俄续约；夺回BEL、攻BRE。冬季不能发消息。
S1905：法公开攻击我"独大"；我支援俄取BER（俄许MUN归我，待我陆军到RUH）。德BER-MUN/MUN-RUH让出BER。我占ENG/IRI/NAO。意取SER。
F1905令：NAO-MAO S IRI+ENG（打掉法MAO舰），HOL H S NTH，KIE H S BAL，NWY H。问题：只有1支陆军，需陆军登陆法/德。
F1905结果：法MAO-ENG S BRE逐我ENG(退LON)，我NAO入MAO。KIE守住(俄BER支援)。德剩MUN一城一军。
S1906令：LON-ENG S IRI+MAO（打掉法ENG舰）；HOL-RUH, KIE-HOL S NTH, BAL-KIE（陆军向MUN，秋请俄BER支援RUH-MUN）。
S1906结果：全成功，法ENG退PIC，我A RUH、F HOL、F KIE。拒德（求助夺BER）、拒土（邀攻俄STP）。
F1906令：RUH-MUN S 俄BER；HOL-BEL S NTH+ENG（法BUR只能援MUN或BEL之一=叉）；MAO-POR(空)；IRI-MAO；KIE/NWY H。
F1906结果：取BEL+POR → 英10城（法4，德1，意7，俄7，土5）。MUN弹回（法BUR援）。W1906建 A LON, A LVP（需陆军登陆法国PIC/BRE/PAR）。
S1907：意提和局（法德意赞成），我拒；俄不再助攻MUN但续互不侵犯（BER/STP vs KIE/NWY）。我取BRE（法退GAS），A RUH-BUR。意A VEN-PIE（向MAR？）。
F1907令：BUR-PAR(空城), BRE H S ENG, BEL-PIC切援, HOL-RUH, 其余H。
F1907：五国投和局，我拒绝；公开承诺"西线收尾后再议和局，期间对意俄土德互不侵犯"、对德确认本秋不攻MUN。取PAR+BRE → 英12城（法1 SPA，德1，意8含MAR，俄7，土5）。W1907建 A LON, A LVP。
此时和局份额≈144/284=50.7。缺6城：SPA, MUN, 然后意/俄（MAR、BER、STP…）。
F1908：取SPA（BUR-MAR切援）→ 英13城。法出局；俄取MUN(9城)，德0城。意俄土互有停战（俄土至1909底）→合围成型。对土承诺：1909春就和局给明确答复。W1908建 A LON。
13城和局份额≈169/(169+64+81+16)=51.2（俄9后）。还缺5城：MAR, MUN, BER, STP, PIE/TUN…
S1909：法德出局，剩英13 意8 俄9 土4。决定继续争胜（估EV>和局51）。策略：挑拨土、意对俄（俄9城最大）；稳俄。令：NTH-HEL, BEL-NTH, PIC-BEL, HOL S KIE, ENG C LON-BRE, MAO-WES, POR S SPA, PAR S BUR。
**结局 S1909M：四国和局。英13城得51.21（俄24.55，意19.39，土4.85）。** 接受原因：已公开承诺"西线收尾后谈和局"、三国合围、单胜概率低、同一批玩家跨局有信誉影响。
目标：争取单独获胜（18城）。剩余目标：MUN, BRE, PAR, SPA, MAR, 之后STP/BER或意大利。（管理员提醒），不急于和局。
S1901：F LON-NTH, F EDI-NWG, A LVP-YOR。提议：法-ENG DMZ；德-我NWY他DEN/HOL，NTH我、HEL他；俄-我NWY他SWE，BAR DMZ。
