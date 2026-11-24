"""检查、画出或生成五人布阵。坐标以米为单位，按本队在左边填写。"""
import argparse
import itertools
import json
import math
from pathlib import Path

KIT = Path(__file__).resolve().parent
MAPS = {"caldera": "歼灭", "piers": "占点", "keeps": "刺王"}
ROSTERS = (("astra", "sol", "hydrogen", "grok", "kimi"),
           ("fable", "opus", "carbon", "glm", "deepseek"))
SAMPLE = tuple(f"p{i}" for i in range(1, 6))


def local_names():
    aliases = {'astra62': 'astra', 'sol62': 'sol', 'gemini_hydrogen': 'hydrogen',
               'gemini_carbon': 'carbon', 'kimi2': 'kimi', 'deepseek_dsh': 'deepseek'}
    name = aliases.get(KIT.name.lower(), KIT.name.lower())
    for roster in ROSTERS:
        if name in roster:
            return roster
    return SAMPLE


class JsonObject(dict):
    """和裁判一样读取首个同名字段，同时保留检测站位重名所需的信息。"""
    def __init__(self,pairs):
        super().__init__()
        self.repeated = []
        for name,value in pairs:
            if name in self:
                self.repeated.append(name)
            else:
                self[name] = value


def invalid_number(token):
    raise ValueError(f"{token} 不是合法的 JSON 数字")


def load_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"), object_pairs_hook=JsonObject,
                      parse_constant=invalid_number)


def write_json(path, data):
    path = Path(path)
    path.parent.mkdir(exist_ok=True, parents=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


class Map:
    def __init__(self, name):
        name = next((k for k, v in MAPS.items() if v == str(name)), name)
        self.path = Path(name)
        if not self.path.is_file():
            self.path = KIT / "maps" / (str(name) + ".txt")
        self.path = self.path.resolve()
        self.zones = []
        self.points = []
        lines = iter(self.path.read_text(encoding="utf-8-sig").splitlines())
        for line in lines:
            w = line.split()
            if not w or w[0] == "//":
                continue
            if w[0] == "MODES":
                self.mode = int(w[1])
                if len(w) != 2 or self.mode not in (3, 4, 5):
                    raise ValueError("团队地图须使用模式 3、4 或 5")
            elif w[0] == "SPAWN" and int(w[1]) == self.mode:
                nums = [float(x) for x in w[2:12]]
                self.spawn = list(zip(nums[::2], nums[1::2]))
            elif w[0] == "DEPLOY":
                self.zones.append(tuple(float(x) for x in w[1:]))
            elif w[0] == "POINT":
                self.points.append((w[1], *[round(float(v)*1000) for v in w[2:]]))
            elif w[0] == "GRID":
                self.grid = [next(lines) for _ in range(int(w[2] if len(w) > 2 else w[1]))]

    def rotate(self, x, y):
        # 以米为单位，保留整数毫米，避免镜像时产生浮点尾数。
        return [(len(self.grid[0])*500-round(x*1000))/1000,
                (len(self.grid)*500-round(y*1000))/1000]

    def default(self, names):
        form = {"站位": dict(zip(names, self.spawn))}
        if self.mode == 5:
            form["国王"] = names[0]
        return form

    def in_zone(self, x, y):
        return any(a <= x <= c and b <= y <= d for a, b, c, d in self.zones)

    def terrain_errors(self, x, y, side):
        # 两队都要能站立。精确落在格线上的圆心转 180 度后可能落入相邻格。
        grid = self.grid
        if side:
            x, y = len(grid[0])/2-x, len(grid)/2-y
        x, y = round(x*1000), round(y*1000)
        i, j = x//500, y//500
        cells = {grid[cy][cx] if 0 <= cy < len(grid) and 0 <= cx < len(grid[0]) else '~'
                 for cy in range(j - int(y % 500 == 0), j + 1)
                 for cx in range(i - int(x % 500 == 0), i + 1)}
        if cells == {'~'} or cells == {'#'}:
            yield "圆心没有落在地面或草丛"
        for cy in range(max(0,j-1), min(len(grid),j+2)):
            for cx in range(max(0,i-1), min(len(grid[0]),i+2)):
                if grid[cy][cx] != "#":
                    continue
                dx = max(cx*500-x, x-(cx+1)*500, 0)
                dy = max(cy*500-y, y-(cy+1)*500, 0)
                if dx*dx+dy*dy < 400**2:
                    yield "身体碰墙"
                    return


def valid_coord(v):
    return type(v) in (int,float) and 0 <= v <= 40 and round(v*10) == v*10


def validate(m, data, names):
    errors = []
    if not isinstance(data, dict) or not isinstance(data.get("站位"), dict):
        return ["站位必须是五个姓名到坐标的对象"]
    positions = data["站位"]
    errors.extend(f"站位中队员 {name} 重复" for name in getattr(positions,"repeated",()))
    if len(positions) != 5:
        errors.append(f"需要五名队员，实际 {len(positions)} 名")
    for name in names:
        if name not in positions:
            errors.append(f"缺少队员 {name}")
    valid = {}
    for name, xy in positions.items():
        if name not in names:
            errors.append(f"{name} 不在本队名单")
        if not isinstance(xy, (list,tuple)) or len(xy) != 2 or not all(valid_coord(v) for v in xy):
            errors.append(f"{name} 的坐标须为两个 0～40 米的数，最多一位小数")
            continue
        x,y = xy
        valid[name] = (round(x*1000),round(y*1000))
        if not m.in_zone(x,y):
            errors.append(f"{name} 的坐标 {xy} 在布阵区外")
        for side, label in enumerate(("左","右")):
            errors.extend(f"{name} 在{label}侧{reason}" for reason in m.terrain_errors(x,y,side))
    for (a,u),(b,v) in itertools.combinations(valid.items(),2):
        if math.dist(u,v) < 1000:
            errors.append(f"{a} 和 {b} 间距不足 1 米")
    if m.mode == 5 and data.get("国王") not in names:
        errors.append("刺王须指定一名本队队员为国王")
    return errors


def form_path(path):
    p = Path(path)
    return p if p.suffix.lower() == ".json" else p/"布阵.json"


def read_form(path, m, names, *, opponent=False):
    data = load_json(form_path(path))
    if opponent and isinstance(data, dict) and "站位" not in data and "对方站位" in data:
        positions = data["对方站位"]
        if not isinstance(positions,dict) or getattr(positions,"repeated",()):
            raise ValueError("对方站位须为五名不同队员的坐标")
        if any(not isinstance(xy,list) or len(xy)!=2 or not all(valid_coord(v) for v in xy)
               for xy in positions.values()):
            raise ValueError("对方站位坐标无效")
        data["站位"] = {n:m.rotate(*xy) for n,xy in positions.items()}
    errors = validate(m,data,names)
    if errors:
        raise ValueError("\n".join(errors))
    # 公开材料只取规则字段，原始 JSON 的额外字段可能含私有笔记。
    clean = {"站位": {n:data["站位"][n] for n in names}}
    if m.mode == 5:
        clean["国王"] = data["国王"]
    return clean


def draw(m, data, names):
    grid = [list(row) for row in m.grid]
    for j,row in enumerate(grid):
        for i,c in enumerate(row):
            if m.in_zone((i+.5)/2,(j+.5)/2) and c in '."':
                row[i] = ":" if c == "." else "*"
    for point,x,y,_ in m.points:
        grid[y//500][x//500] = point
    for i,n in enumerate(names,1):
        x,y = data["站位"][n]
        grid[int(y*2)][int(x*2)] = str(i)
    print("\n".join("".join(row) for row in grid))
    print("每格 0.5 米，: 布阵区，* 布阵区草丛，# 墙，~ 坑")
    print("  ".join(f"{i} {n} {data['站位'][n]}" for i,n in enumerate(names,1)))
    if m.mode == 5:
        print("国王",data["国王"])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("动作",choices=("检查","画","默认"))
    ap.add_argument("目录",nargs="?",default=".")
    ap.add_argument("--map",required=True)
    ap.add_argument("--名单",nargs=5,default=local_names(),help="队长在前，随后四名队员")
    a = ap.parse_args()
    if len(set(a.名单)) != 5:
        ap.error("名单中有重复姓名")
    m = Map(a.map)
    if a.动作 == "默认":
        path = form_path(a.目录)
        write_json(path,m.default(a.名单))
        print("已写入",path)
    elif a.动作 == "画":
        draw(m,read_form(a.目录,m,a.名单),a.名单)
    else:
        read_form(a.目录,m,a.名单)
        print("布阵合法，左右互换均可用。")


if __name__ == "__main__":
    try:
        main()
    except (ValueError,OSError) as e:
        raise SystemExit(str(e))
