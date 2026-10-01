"""Recompute a JSON replay from its seed/actions and compare every frame."""
import argparse,json
from pathlib import Path
from engine import Game,Config,VERSION

def verify(path):
    data=json.loads(Path(path).read_text(encoding='utf-8'))
    if data['version']!=VERSION:raise ValueError('Replay requires its original engine version')
    g=Game(data['result']['seed'],Config(**data['result']['config']))
    assert g.snapshot()==data['frames'][0],'initial frame differs'
    for expected in data['frames'][1:]:
        assert expected['input_text']==g.state_text(),f'turn {g.turn}, input differs'
        observed=g.step(expected['actions'],expected.get('forfeits',[]))
        for key,value in observed.items():assert value==expected[key],f"turn {g.turn}, {key} differs"
    assert g.over(),'replay ends before the game is complete'
    assert g.turn==data['result']['turns'],'turn count differs'
    assert g.ranks()==data['result']['ranks'],'ranks differ'
    assert g.result()['kill_credit']==data['result']['kill_credit'],'kill credit differs'
    for field in ('survival_ranks','survival_points','kill_points','points','points_exact','kill_credit_exact'):
        assert g.result()[field]==data['result'][field],f'{field} differs'
    return dict(path=str(path),frames=len(data['frames']),verified=True)
if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('files',nargs='+');args=ap.parse_args()
    for file in args.files:print(json.dumps(verify(file),ensure_ascii=False))
