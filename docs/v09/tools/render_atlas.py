#!/usr/bin/env python3
"""Render exact spatial manifests; requires matplotlib. No imagery generation."""
from pathlib import Path
import csv, heapq, json, math
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, Rectangle
from matplotlib.lines import Line2D
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'atlas';OUT.mkdir(exist_ok=True)
def load(n):return json.loads((ROOT/'data'/f'{n}.json').read_text())
P=load('pois')['rows'];S=load('settlements')['rows'];B=load('buildings')['rows'];E=load('routes')['rows'];T=load('terrain'); props=load('props')['rows']
pd={p['id']:p for p in P}
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':10,'axes.titleweight':'bold'})
colors={'capital':'#b69045','city':'#c7a66f','town':'#c8bca2','camp':'#b46c50','ruin':'#956ca3','tower':'#c09738','heart':'#8a3855','boss':'#9f4e56','resource':'#6b8e75','service':'#6d737d','shrine':'#67879b','crossing':'#427d95','pass':'#8a806f','terminal':'#8a3855','landmark':'#888170','settlement':'#bc9b64'}
def draw_base(ax,detail=False):
    ax.set_facecolor('#ece9db')
    biome_colors=['#d6d6bc','#e1dfc4','#c9d5ba','#ccd7c7','#e0d4be','#e4dfca']
    for b,col in zip(T['biomes'],biome_colors):
        x0,y0,x1,y1=[v/100 for v in b['bounds_cm']];ax.add_patch(Rectangle((x0,y0),x1-x0,y1-y0,facecolor=col,edgecolor='none',alpha=.7,zorder=0))
    for f in T['forest_patches']:
        x,y,_=[v/100 for v in f['center_cm']];ax.add_patch(Circle((x,y),f['radius_cm']/100,color='#6d8d64',alpha=.22,lw=0,zorder=1))
    for s in S:
        x,y,z=[v/100 for v in s['position_cm']]
        ax.add_patch(Circle((x,y),s['footprint_radius_cm']/100+25,fill=False,edgecolor='#9d9a83',linestyle=':',lw=.8,zorder=2))
        ax.add_patch(Circle((x,y),s['footprint_radius_cm']/100,facecolor=colors[s['kind']],alpha=.12,lw=0,zorder=2))
    for r in T['rivers']:
        x=[p[0]/100 for p in r['points_cm']];y=[p[1]/100 for p in r['points_cm']]
        # Width in plot points is illustrative; data keeps exact cm width.
        ax.plot(x,y,color='#8caeb8',lw=9 if r['id']=='main_river' else 6,solid_capstyle='round',zorder=2)
        ax.plot(x,y,color='#c1d6d7',lw=3,alpha=.8,zorder=2)
    for e in E:
        x=[p[0]/100 for p in e['points_cm']];y=[p[1]/100 for p in e['points_cm']]
        road=e['kind']=='road'
        ax.plot(x,y,color='#ac8d59' if road else '#9b9580',lw=1.6 if road else .65,ls='-' if road else '--',alpha=.75,zorder=3)
    for b in B:
        x,y,_=[v/100 for v in b['position_cm']];w,h,_=[v/100 for v in b['size_cm']]
        ax.add_patch(Rectangle((x-w/2,y-h/2),w,h,facecolor='#7a786f' if b['enterable'] else '#b0aaa0',edgecolor='#5a5e59',linewidth=.45,zorder=5))
    inset=T['heart_water_inset'];cx,cy,_=[v/100 for v in inset['center_cm']];w,h=[v/100 for v in inset['size_cm']]
    ax.add_patch(Rectangle((cx-w/2,cy-h/2),w,h,color='#8caeb8',zorder=6))
    if detail:
        for p in props:
            x,y,_=[v/100 for v in p['position_cm']]
            if p['type_id']=='door':
                ax.plot(x,y,'s',color='#d6bb69',ms=3,zorder=8)
            else:ax.plot(x,y,'.',color='#69845c' if p['type_id'] in ['sapling','tall_grass'] else '#c78158',ms=3,zorder=6)
    ax.set_aspect('equal');ax.set_xlabel('East (m)');ax.set_ylabel('North (m)')
    ax.grid(color='#a6a99a',alpha=.15,lw=.6)
    for sp in ax.spines.values():sp.set_color('#a4a493')

fig,ax=plt.subplots(figsize=(16,16),dpi=190)
draw_base(ax)
for i,p in enumerate(P):
    x,y,_=[v/100 for v in p['position_cm']];kind=p['kind']
    if kind in ['service','terminal','pass'] and p['id'] not in ['heart_forge','heart_dry_flank']:continue
    ax.plot(x,y,marker='*' if kind=='heart' else '^' if kind=='tower' else 'D' if kind in ['boss','ruin'] else 'o',ms=10 if kind=='heart' else 5,color=colors.get(kind,'#766e5c'),mec='#f1efe2',mew=.7,zorder=9)
    if kind=='settlement':
        label=p['name'];shift=(-52,-26) if p['id']=='capital' else (8,8);sz=9
    elif kind in ['heart','tower','boss','ruin'] or p['id']=='heart_forge':label=p['name'];shift=(5,7);sz=7
    else:label=f'{i+1:02d}';shift=(4,3);sz=6
    ax.annotate(label,(x,y),xytext=shift,textcoords='offset points',fontsize=sz,color='#343e39',zorder=12,bbox={'facecolor':'#f5f1e4','alpha':.75,'edgecolor':'none','pad':1})
ax.set_xlim(-1200,1200);ax.set_ylim(-1200,1200)
ax.annotate('N',xy=(1110,1090),xytext=(1110,990),ha='center',fontsize=12,arrowprops={'arrowstyle':'->','color':'#4e6056'})
ax.plot([-1100,-900],[-1110,-1110],color='#49534b',lw=3);ax.text(-1000,-1080,'200 m',ha='center',fontsize=8)
ax.set_title('PROJECT LOCKDOWN  /  THE CROWNREACH BASIN\nv0.9 · seed 9001 · 2.4 × 2.4 km · full-map layout',fontsize=17,pad=18)
handles=[Line2D([0],[0],color='#ac8d59',lw=2,label='Road'),Line2D([0],[0],color='#9b9580',ls='--',label='Trail / local link'),Line2D([0],[0],color='#8caeb8',lw=6,label='Walkable river'),Line2D([0],[0],marker='D',ls='',color='#956ca3',label='Unique ruin / boss'),Line2D([0],[0],marker='^',ls='',color='#c09738',label='Grand Tower'),Line2D([0],[0],marker='*',ls='',color='#8a3855',label='Grand Heart')]
ax.legend(handles=handles,loc='lower center',bbox_to_anchor=(.5,-.09),ncol=3,frameon=False)
fig.text(.5,.025,'Exact POI IDs / coordinates: data/pois.csv  •  Macro elevation bands: 0 / 4 / 8 m  •  Route lines are strategic centerlines, not native navmesh evidence.',ha='center',fontsize=8,color='#566056')
fig.tight_layout(rect=(0,.065,1,1))
for suffix in ['png','svg']:fig.savefig(OUT/f'01_World_Atlas.{suffix}',facecolor='#f6f2e6')
plt.close(fig)

for stem,center,span,title in [('02_Crownreach_Detail',(40,40),270,'CROWNREACH  /  FINAL CONTEST'),('03_Riverport_Detail',(-800,-660),160,'RIVERPORT  /  INTERACTION REFERENCE BLOCK')]:
    fig,ax=plt.subplots(figsize=(13,12),dpi=180);draw_base(ax,True)
    cx,cy=center;ax.set_xlim(cx-span,cx+span);ax.set_ylim(cy-span,cy+span)
    for p in P:
        x,y,_=[v/100 for v in p['position_cm']]
        if abs(x-cx)>span or abs(y-cy)>span:continue
        ax.plot(x,y,marker='*' if p['kind']=='heart' else '^' if p['kind']=='tower' else 'o',color=colors.get(p['kind'],'#777'),ms=12 if p['kind']=='heart' else 6,zorder=10)
        ax.annotate(p['name'],(x,y),xytext=(6,9),textcoords='offset points',fontsize=8,zorder=12,bbox={'facecolor':'#f6f2e6','alpha':.9,'edgecolor':'none','pad':2})
    if stem.startswith('02'):
        ax.add_patch(Circle((40,40),70,fill=False,lw=1.2,ls='--',edgecolor='#8a3855',zorder=10));ax.annotate('70 m contested precinct\nNo civic guard immunity / recovery pads',xy=(94,85),xytext=(155,100),fontsize=8,color='#8a3855',arrowprops={'arrowstyle':'-','color':'#8a3855'},bbox={'facecolor':'#f6f2e6','alpha':.95,'edgecolor':'none','pad':3},zorder=13)
        # Claim circle is correctly 1.2 m, not exaggerated for map size.
        ax.add_patch(Circle((40,40),1.2,fill=False,lw=1.5,edgecolor='#8a3855',zorder=11))
    ax.set_title(title+'\nDark shells enterable · yellow door nodes · green foliage · orange props',fontsize=14,pad=16)
    fig.text(.5,.02,'Semantic placement atlas. B90/B91 must fit real terrain, doorway paths and proxy geometry; no engine reachability is claimed.',ha='center',fontsize=8,color='#566056')
    fig.tight_layout(rect=(0,.04,1,1))
    for suffix in ['png','svg']:fig.savefig(OUT/f'{stem}.{suffix}',facecolor='#f6f2e6')
    plt.close(fig)

with (ROOT/'data/pois.csv').open('w',newline='') as f:
    w=csv.writer(f);w.writerow(['atlas_index','id','name','kind','east_m','north_m','height_m','functions','stage_id'])
    for i,p in enumerate(P):w.writerow([i+1,p['id'],p['name'],p['kind'],*[v/100 for v in p['position_cm']],';'.join(p['functions']),p['stage_id']])
G={p['id']:[] for p in P}
for e in E:G[e['from']].append((e['to'],e['length_cm']));G[e['to']].append((e['from'],e['length_cm']))
def shortest(a,b):
    q=[(0,a)];seen={}
    while q:
        d,n=heapq.heappop(q)
        if n in seen:continue
        seen[n]=d
        if n==b:return d
        for nxt,l in G[n]:heapq.heappush(q,(d+l,nxt))
    raise ValueError('unreachable')
pairs=[(s['id'],'capital') for s in S if s['id']!='capital']+[(x,'antechamber') for x in ['ruin_a','ruin_b','ruin_c','heart_forge','capital_treasury']]+[('antechamber','heart'),('highfort','scholars'),('riverport','ironworks'),('highfort','riverport'),('scholars','ironworks')]
with (OUT/'TRAVEL_TABLE.csv').open('w',newline='') as f:
    w=csv.writer(f);w.writerow(['from','to','strategic_length_m','nominal_walk_seconds_420cm_s','nominal_minutes','native_path_length_m','native_walk_seconds','evidence'])
    for a,b in pairs:
        d=shortest(a,b);w.writerow([a,b,round(d/100,2),round(d/420,2),round(d/420/60,2),'','','Not run; graph estimate only'])
print('Rendered three PNG/SVG atlases; exported POI index and nominal travel table.')
