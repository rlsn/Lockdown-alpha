"""Small specification examples, deliberately not an Unreal gameplay implementation."""
from dataclasses import dataclass

@dataclass
class Victim:
    wet: bool = False
    ready_at: int = 0
    guard: bool = False
    stun_until: int = 0

def frost(v, now, stun_ms, lock_ms, *, direct=True, hostile=True, blocked=False, immune=False):
    if blocked or not direct or not hostile or not v.wet or now < v.ready_at:
        return 'no_reaction'
    v.wet = False
    v.ready_at = now + lock_ms
    if v.guard:
        v.guard = False
        return 'guarded'
    if immune:
        return 'immune'
    v.stun_until = max(v.stun_until, now + stun_ms)
    return 'stunned'

def loadout_valid(skills, accessories, equipped):
    if len(skills) > 6 or len(accessories) > 4:
        return False
    sources=[]
    for kind, identity in skills:
        if kind == 'gear' and identity not in equipped:
            return False
        if kind not in ('learned', 'gear'):
            return False
        sources.append((kind, identity))
    return len(sources)==len(set(sources)) and len(accessories)==len(set(accessories))

def applied_primary(items, order, attributes):
    result={a:0 for a in attributes};remaining=6
    for binding in order:
        row=items.get(binding,{})
        for attr in attributes:
            add=min(max(0,row.get(attr,0)),2,3-result[attr],remaining)
            result[attr]+=add;remaining-=add
    return result

def mitigate(raw, ordinary, conditional=0):
    reduction=min(.6,1-(1-ordinary)*(1-conditional))
    return raw*(1-reduction)

def barrier_hit(damage,pool,bonus):
    absorbed=min(pool,damage*(1+bonus))
    base_spent=min(damage,absorbed/(1+bonus))
    return damage-base_spent,pool-absorbed

def claim_survives(*,alive=True,stun=False,hexed=False,damage=0,root=False,slow=False,wet=False,silence=False,mute=False,forced_move=0,explicit_interrupt=False,authorized=True):
    return alive and authorized and not (stun or hexed or forced_move!=0 or explicit_interrupt)

def field_transition(states, impulse, material='stone'):
    """One nonrecursive cell transition; caller owns durations/source records."""
    s=set(states)
    wet='wet' in s or material=='water'
    if impulse=='water':return (s-{'burning'})|{'wet'}
    if impulse=='frost':
        return (s-{'burning','electrified','overgrown'})|{'frozen'} if wet else s
    if impulse=='fire':
        if 'frozen' in s:return (s-{'frozen','burning','electrified','overgrown','oil'})|{'wet','obscured'}
        if wet:return (s-{'burning','overgrown','oil'})|{'obscured'}
        if 'oil' in s or 'overgrown' in s or material=='flammable_socket':return (s-{'oil','overgrown'})|{'burning'}
    if impulse=='lightning':return s|{'electrified'} if wet and 'frozen' not in s else s
    if impulse=='wind':return s-{'obscured'}
    if impulse=='shatter':return (s-{'frozen'})|{'wet'} if 'frozen' in s else s
    if impulse=='nature':return s|{'overgrown'} if material=='soil' and not s.intersection({'frozen','burning'}) else s
    return s
