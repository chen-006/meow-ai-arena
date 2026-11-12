# 拼两张 6 格表情母版的提示词：prompts\<角色>_A.txt / _B.txt
import re, os
# 在工作目录里运行：从 ./prompts/ 里的立绘提示词取角色设计要点，写出 ./expr/prompts/<角色>_A.txt、_B.txt
P = 'prompts'
OUT = os.path.join('expr', 'prompts')
os.makedirs(OUT, exist_ok=True)
SRC = {'GPT_大_Astra': ('gpt_astra', 'GPT (OpenAI), the adult elder-sister version named Astra'),
       'GPT_小_Sol': ('gpt_sol', 'GPT (OpenAI), the little-sister version named Sol (a young girl)'),
       'Claude_特大_Fable': ('v3_cur_fable', 'Claude (Anthropic), the eldest-sister version named Fable'),
       'Claude_大_Opus': ('v3_cur_opus', 'Claude (Anthropic), the adult elder-sister version named Opus'),
       'DeepSeek_大': ('v3_cur_ds', 'DeepSeek, the adult elder-sister version'),
       'Gemini_大': ('v3_cur_gem', 'Gemini (Google), the adult elder-sister version'),
       'Kimi_大': ('v3_cur_kimi', 'Kimi (Moonshot AI), the adult elder-sister version'),
       'GLM_大': ('v3_cur_glm', 'GLM (Zhipu / Z.ai), the adult elder-sister version'),
       'Grok_大': ('v3_cur_grok', 'Grok (xAI), the adult elder-sister version'),
       '千问_大': ('v3_cur_qwen', 'Qwen (Alibaba), the adult elder-sister version'),
       'MiMo_大': ('v3_cur_mimo', 'MiMo (Xiaomi), the adult elder-sister version'),
       '文心_大': ('v3_ernie_da_char', 'ERNIE (Baidu Wenxin Yiyan), the adult elder-sister version')}
EXPR = {
 'A': [('常态', 'NEUTRAL: her calm default expression, relaxed face, faint gentle smile, looking at the viewer.'),
       ('开心', 'HAPPY: a bright, open-mouthed joyful smile, eyes curved happily, head tilted slightly.'),
       ('得意', 'SMUG / PROUD: a confident smug grin, eyes half-closed, chin raised, one hand lightly on her hip or chest.'),
       ('生气', 'ANGRY: furrowed eyebrows, glaring, cheeks puffed or teeth clenched, clearly annoyed but still cute.'),
       ('伤心', 'SAD: teary eyes, a few tears on her cheeks, trembling down-turned mouth, inner eyebrows raised.'),
       ('震惊', 'SHOCKED: eyes wide open with small pupils, mouth wide open, leaning back slightly, both hands raised near her face.')],
 'B': [('无语', 'SPEECHLESS / UNIMPRESSED: flat deadpan half-lidded stare, small straight-line mouth, completely unimpressed.'),
       ('思考', 'THINKING: one hand on her chin, eyes looking up and to the side, slight frown of concentration.'),
       ('流口水', 'DROOLING: dreamy blissful face as if imagining something delicious, mouth slightly open, a small drop of drool at the corner of her mouth.'),
       ('头顶铁盆', 'HIT BY A BASIN: a shiny silver stainless-steel mixing bowl sits upside-down on top of her head like a helmet, slightly askew; she looks dazed and aggrieved, teary-eyed, as if it just fell on her head. The bowl is the only prop.'),
       ('尴尬地笑', 'AWKWARD LAUGH: a forced, embarrassed smile, eyebrows slanted, one hand scratching her cheek or the back of her head, a single sweat drop on her temple.'),
       ('犯困', 'SLEEPY: drowsy half-closed eyes, yawning with one hand covering her mouth, head tilted to one side.')]}


def details(fn):
    t = open(os.path.join(P, fn + '.txt'), encoding='utf-8').read()
    t = t[(t.index('Design details') if 'Design details' in t else t.index('Design:')):]
    lines = [l for l in t.splitlines()[1:] if l.startswith('- ')]
    keep = []
    for l in lines:
        if re.match(r'- (Tail|Pose|Shoes|Legs)', l):
            continue
        l = re.sub(r',? ?(exactly )?(like|as in|than in) images? \d( and \d)?', '', l)
        l = re.sub(r'\(\s*\)', '', l)
        l = re.sub(r'[Ii]mages? [0-9]( and [0-9])?', 'the reference illustration', l)
        keep.append(l)
    return '\n'.join(keep)


for c, (fn, who) in SRC.items():
    d = details(fn)
    for k, ex in EXPR.items():
        cells = '\n'.join(f'{i + 1}. ({"top" if i < 3 else "bottom"} row, {["left", "middle", "right"][i % 3]}) {e}' for i, (_, e) in enumerate(ex))
        p = f'''EXPRESSION SHEET of one existing character: {who}, from an anime-style AI character series.

REFERENCE IMAGES (both show this exact character, the official approved illustration):
- Image 1: her head and upper body, close-up. Image 2: her full figure.
Reproduce her identity exactly in every panel: the same face, eye color, hair color, hairstyle and bangs, the same large brand-logo hair ornament on the same side of the head with its satin bow (same shape, colors and size relative to the head), and the same outfit collar, ribbons and sleeves. Only the facial expression and a small amount of upper-body gesture (hands, head tilt) change.

Design details (for reference):
{d}

LAYOUT (strict):
A clean 3 columns x 2 rows grid of six separate bust portraits (head and shoulders down to mid-chest) of this same character on a fully transparent background. All six busts are the same size and the same scale, each centered in its own equal cell, the head at the same height in every cell, facing the viewer. Each bust ends at mid-chest with a clean straight horizontal cut. Leave clear empty transparent space between neighboring busts; nothing overlaps or crosses into another cell, hair and hands stay inside their own cell. No grid lines, no panel borders, no frames, no background color, no text, no labels, no speech bubbles, no floating symbols or effects (tears and sweat drops stay on the skin), no extra props.

THE SIX PANELS, left to right, top row then bottom row:
{cells}

[ART STYLE] Exactly the same rendering as the reference illustration: polished Japanese anime gacha-game official art, fine clean crisp lineart tinted darker than each local color (never heavy black outlines), soft cel shading with two to three tone steps, hair in clearly separated strands with long thin highlights, large expressive eyes with luminous lower iris and white catchlights, soft light from the upper left, bright airy clean palette with medium-high saturation. Expressions are clear and readable like anime reaction faces, but the face stays on-model (not chibi, not super-deformed). Not painterly, not realistic, not 3D.

[MODESTY] Wholesome and age-appropriate, modest high neckline as in the reference, hands anatomically correct with five fingers.
'''
        open(os.path.join(OUT, f'{c}_{k}.txt'), 'w', encoding='utf-8').write(p)
    print(c, '\n', d, '\n')
