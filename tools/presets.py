# Factory preset generator. Pure data: each preset = (bank, category, name, params{name:value}, packs[8][32])
import random, math
import spec as S
from spec import *


DN = {n:i for i,n in enumerate(DEST_NAMES)}
DIV = {n:i for i,n in enumerate(DIV_NAMES)}
FT  = {n:i for i,n in enumerate(FILTER_TYPES)}

class Pr:
    def __init__(s,cat,name):
        s.cat=cat; s.name=name
        s.p={e.name:e.p.df for e in ALL}
        s.packs=[[1.0]*32 for _ in range(NMOD)]
        s.used=set()
    def set(s,**kw):
        for k,v in kw.items():
            k=k.replace("__","_")
            assert k in s.p,k
            s.p[k]=v
        return s
    def fx(s,layer,**kw):
        for k,v in kw.items():
            n=f"{layer}_{k}"; assert n in s.p,n; s.p[n]=v
        return s
    def order(s,layer,o):
        for i,e in enumerate(o): s.p[f"{layer}_Slot{i+1}"]=e
    def route(s,slot,src,dest,depth):
        assert dest in DN,dest
        s.p[f"X{slot}_Src"]=src; s.p[f"X{slot}_Dst"]=DN[dest]; s.p[f"X{slot}_Depth"]=depth; s.p[f"X{slot}_Byp"]=0
    def mod(s,m,pat,div="1/16",sync=1,mode=0,gate=1,smooth=0,shuffle=0,tension=.5,invert=0,shift=0,trem=(0,0),flip=(0,0),speed=None):
        n=len(pat); b=f"M{m}_"
        s.used.add(m)
        s.p[b+"Steps"]=n; s.p[b+"Div"]=DIV[div]; s.p[b+"Sync"]=sync; s.p[b+"Mode"]=mode
        s.p[b+"Gate"]=gate; s.p[b+"Smooth"]=smooth; s.p[b+"Shuffle"]=shuffle; s.p[b+"Tension"]=tension
        s.p[b+"Invert"]=invert; s.p[b+"Shift"]=shift
        s.p[b+"TremDepth"]=trem[0]; s.p[b+"TremDest"]=trem[1]; s.p[b+"FlipDepth"]=flip[0]; s.p[b+"FlipDest"]=flip[1]
        if speed is not None: s.p[b+"Speed"]=speed; s.p[b+"Sync"]=0
        full=[float(pat[i%n]) for i in range(32)]
        s.packs[m-1]=full

# --- pattern helpers --------------------------------------------------------------------
def X(txt,hi=1.0,lo=0.0,acc=None):
    out=[]
    for ch in txt:
        if ch=="x": out.append(hi)
        elif ch=="o": out.append(acc if acc is not None else hi*0.55)
        elif ch=="-": out.append(lo)
        elif ch in "123456789": out.append(int(ch)/9.0)
    return out
def euclid(k,n,hi=1.0,lo=0.0,rot=0):
    pat=[(hi if (i*k)%n<k else lo) for i in range(n)]
    return pat[rot%n:]+pat[:rot%n]
def ramp(n,up=True): 
    v=[i/(n-1) for i in range(n)]; return v if up else v[::-1]
def tri(n): return [1-abs((i/(n-1))*2-1) for i in range(n)]
def rnd_pat(r,n,dens=0.65,lo=0.0):
    return [ (round(r.uniform(0.35,1.0),2) if r.random()<dens else lo) for _ in range(n)]
def stairs(r,n,levels=4): 
    return [round(r.randrange(levels)/(levels-1),2) for _ in range(n)]

FX_FILTER,FX_DIST,FX_DELAY,FX_LOFI,FX_FLAN,FX_GLITCH=range(6)
def clean(pr):  # all effect mixes sensible default / bypassed, caller enables what it needs
    for l in "AB":
        for pre in ("Fi","Di","De","Lo","Fl","Gl"): pr.p[f"{l}_{pre}Byp"]=1
    pr.p["B_Bypass"]=1
    pr.set(G_RvMix=0.15,G_RvDecay=0.45,G_RvSize=0.5)
    return pr

PATS_GATE=["x-x-x-x-x-x-x-x-","xx-xx-xx-xx-xx--","x--x--x-x--x--x-","xxx-xxx-xxx-xx-x","x-xxx-x-x-xxx-x-","xx--xx--xx-xxx--","x-x-x--xx-x-x-x-","xxxxxxxx-x-x-x--","x-xx-xx-x-xx-xx-","xx-x-xx-x-xx-x-x","x-x--x-x-x--x-x-","xxxx-x-xxx-xx-x-"]

# ============================ archetypes ===============================================
def a_gate(v,r):
    pr=clean(Pr("Gates",""))
    pat=PATS_GATE[v%len(PATS_GATE)]
    div=["1/16","1/8","1/16","1/16T","1/16","1/8","1/32","1/16","1/8T","1/16","1/16","1/8"][v%12]
    pr.mod(1,X(pat),div=div,gate=r.choice([0.7,0.85,1.0]),smooth=r.choice([0,0.05,0.15]),trem=(r.uniform(0.8,1.0),1),shuffle=r.choice([0,0,0.25,0.4]))
    pr.fx("A",DeByp=0,DeMix=r.uniform(0.2,0.45),DeFb=r.uniform(0.3,0.55),DeLS=DIV[r.choice(["1/8.","1/8","1/4"])],DeRS=DIV[r.choice(["1/8","1/4","1/16."])],DePing=r.choice([0,1]),DeLP=r.choice([4000,6000,9000]))
    pr.fx("A",FiByp=0,FiMix=1,FiType=FT[r.choice(["Clean LP 12","Dark LP 12","Clean HP 12"])],FiCut=r.choice([6000,9000,12000]) if False else 9000,FiRes=0.2,FiLow=r.choice([20,60,120]))
    pr.set(G_RvMix=r.uniform(0.1,0.3),G_RvDecay=r.uniform(0.35,0.65))
    pr.order("A",[0,1,2,3,4,5]); return pr

def a_filterseq(v,r):
    pr=clean(Pr("Filter Sequences",""))
    types=["Clean LP 12","Dark LP 12","Legacy LP 24","Clean BP 12","Dark BP 12","Phaser 8p","Vowel","Clean HP 12","Clean LP 42","Dark LP 42","Legacy BP 12","Phaser 4p"]
    t=types[v%len(types)]
    curve = v%3==1
    pat = stairs(r,16,r.choice([3,4,5])) if not curve else rnd_pat(r,8,0.9)
    pr.mod(1,pat,div=r.choice(["1/4","1/8","1 bar","1/2"]) if curve else r.choice(["1/16","1/8","1/16","1/16T"]),mode=1 if curve else 0,smooth=r.choice([0.1,0.3,0.6]) if not curve else 0,tension=r.choice([0.3,0.7,1.0]),shuffle=r.choice([0,0,0.3]))
    pr.fx("A",FiByp=0,FiMix=(0.8 if ("BP" in t or t=="Vowel") else 1),FiType=FT[t],FiCut=r.choice([300,450,650]) if t!="Vowel" else 150,FiRes=r.uniform(0.35,0.75),FiLow=20)
    pr.route(1,1,"A Filter Cutoff",r.uniform(0.45,0.8))
    if r.random()<0.5: pr.route(2,1,"A Filter Resonance",r.uniform(-0.3,0.3))
    pr.fx("A",DiByp=0,DiMix=0.6,DiDrive=r.uniform(0.15,0.4),DiType=0,DiOut=-2)
    pr.fx("A",DeByp=0,DeMix=r.uniform(0.15,0.3),DeFb=0.35,DeLS=DIV["1/8."])
    pr.order("A",[1,0,2,3,4,5]); return pr

def a_delay(v,r):
    pr=clean(Pr("Delays",""))
    pat=PATS_GATE[(v*5+1)%12]
    pr.mod(1,X(pat,lo=0.0),div=r.choice(["1/16","1/8","1/16"]),smooth=0.05)
    pr.mod(2,tri(8),div=r.choice(["1 bar","2 bars","4 bars"]),mode=1,tension=1.0)
    pr.fx("A",DeByp=0,DeMix=r.uniform(0.4,0.65),DeFb=r.uniform(0.5,0.75),DeLS=DIV[r.choice(["1/8.","1/4","1/8","1/4."])],DeRS=DIV[r.choice(["1/8","1/4.","1/16."])],DePing=r.choice([0,1,1]),DeLP=r.choice([2500,4000,6500]),DeBP=r.choice([0,0,0.25,0.5]))
    pr.route(1,1,"A Delay In" if "A Delay In" in DN else "A Delay IN",1.0) if ("A Delay IN" in DN or "A Delay In" in DN) else None
    pr.fx("A",FiByp=0,FiMix=1,FiType=FT["Dark LP 12"],FiCut=r.choice([5000,8000,12000]),FiRes=0.15)
    pr.route(2,2,"A Filter Cutoff",r.uniform(-0.35,-0.1))
    pr.set(G_RvMix=r.uniform(0.15,0.35),G_RvDecay=r.uniform(0.5,0.75))
    pr.order("A",[0,1,3,4,5,2]); return pr

def a_glitch(v,r):
    pr=clean(Pr("Glitch",""))
    pr.fx("A",GlByp=0,GlMix=1,GlMode=0,GlSize=r.choice([60,90,130,200,320]),GlScale=r.choice([0.5,1,1,1.5]),GlRev=r.choice([0,0,1]),GlSmooth=r.uniform(0.1,0.5),GlStretch=r.choice([1,1,0.5,2]))
    pat=PATS_GATE[(v*7+3)%12]
    pr.mod(1,X(pat),div=r.choice(["1/16","1/8","1/16"]),gate=r.choice([0.8,1.0]))
    pr.route(1,1,"A Glitch Mix",1.0); pr.p["A_GlMix"]=0.0
    pr.mod(2,stairs(r,8,4),div=r.choice(["1/4","1/8","1/2"]))
    pr.route(2,2,"A Glitch Buffer",r.uniform(0.15,0.45))
    pr.mod(3,rnd_pat(r,16,0.5),div="1/16")
    pr.route(3,3,"A Glitch Stretch",r.uniform(0.1,0.3))
    pr.fx("A",DeByp=0,DeMix=0.25,DeFb=0.35,DeLS=DIV["1/8."],DeRS=DIV["1/8"])
    pr.fx("A",LoByp=0 if v%2 else 1,LoMix=0.5,LoBit=r.choice([10,12,8]),LoRate=0.15)
    pr.order("A",[5,3,0,1,4,2]); return pr

def a_lofi(v,r):
    pr=clean(Pr("Lo-Fi",""))
    pr.fx("A",LoByp=0,LoMix=1,LoBit=r.choice([12,10,8,6]),LoRate=r.choice([0.15,0.3,0.45]),LoNoise=r.choice([0.1,0.2,0.35]),LoTone=r.choice([0.55,0.7,0.85]))
    pr.mod(1,stairs(r,16,r.choice([3,4])),div=r.choice(["1/8","1/16","1/4"]),smooth=r.choice([0,0.2]))
    pr.route(1,1,"A Lo-Fi Bit",r.uniform(0.2,0.5)); pr.route(2,1,"A Lo-Fi Rate",r.uniform(0.15,0.4))
    pr.fx("A",DiByp=0,DiMix=0.5,DiDrive=0.25,DiType=r.choice([0,3]),DiOut=-1)
    pr.fx("A",FiByp=0,FiMix=1,FiType=FT["Dark BP 12"] if v%3==0 else FT["Legacy LP 12"],FiCut=r.choice([1500,2500,4000]),FiRes=0.3)
    pr.route(3,1,"A Filter Cutoff",r.uniform(0.1,0.3))
    pr.set(G_RvMix=0.12)
    pr.order("A",[0,1,3,2,4,5]); return pr

def a_movement(v,r):
    pr=clean(Pr("Movement",""))
    kind=v%4
    if kind==0:
        pr.mod(1,tri(16),mode=1,div=r.choice(["2 bars","1 bar","4 bars"]),tension=1.0,flip=(r.uniform(0.5,1.0),1))
        pr.fx("A",FlByp=0,FlMix=0.55,FlMode=1,FlDelay=r.choice([1.5,2.5,4]),FlFb=r.uniform(0.3,0.7),FlSpeed=0.25,FlDepth=0.8,FlWidth=0.8)
        pr.route(1,1,"A Flanger Depth",0.2)
    elif kind==1:
        pr.mod(1,ramp(16,True),mode=1,div=r.choice(["1 bar","2 bars"]),tension=0.4,flip=(0.8,1))
        pr.fx("A",FlByp=0,FlMix=0.5,FlMode=0,FlDelay=r.choice([1,2,3]),FlFb=r.uniform(0.5,0.85),FlSpeed=0.12,FlDepth=0.5,FlWidth=1.0)
        pr.route(1,1,"A Flanger Delay",r.uniform(0.3,0.6))
    elif kind==2:
        pr.mod(1,X("x-x-x-x-"),div="1/8",smooth=0.4,flip=(1.0,1),shuffle=0.2)
        pr.fx("A",FlByp=0,FlMix=0.4,FlMode=1,FlDelay=3,FlFb=0.5,FlSync=1,FlDiv=DIV["1 bar"],FlDepth=0.9)
        pr.fx("A",DeByp=0,DeMix=0.3,DeFb=0.4,DePing=1,DeLS=DIV["1/8."],DeRS=DIV["1/8"])
    else:
        pr.mod(1,tri(16),mode=1,div="1 bar",tension=1.0,flip=(0.9,2))
        pr.mod(2,tri(16),mode=1,div="1 bar",tension=1.0,flip=(0.9,3),invert=1)
        pr.p["B_Bypass"]=0; pr.p["G_Routing"]=1
        pr.fx("A",FlByp=0,FlMix=0.6,FlMode=0,FlDelay=2,FlFb=0.6,FlSpeed=0.2,FlDepth=0.7)
        pr.fx("B",FlByp=0,FlMix=0.6,FlMode=1,FlDelay=3,FlFb=0.5,FlSpeed=0.17,FlDepth=0.8)
    pr.set(G_RvMix=r.uniform(0.15,0.3)); pr.order("A",[4,0,1,2,3,5]); return pr

def a_destroy(v,r):
    pr=clean(Pr("Destruction",""))
    pr.fx("A",DiByp=0,DiMix=1,DiDrive=r.uniform(0.45,0.8),DiFuzz=r.choice([0,0.2,0.5]),DiType=v%4,DiTone=r.choice([0.35,0.5,0.65]),DiOut=-2)
    pr.mod(1,stairs(r,16,4),div=r.choice(["1/16","1/8","1/8T"]),smooth=0.1)
    pr.route(1,1,"A Distortion Drive",r.uniform(0.2,0.45))
    pr.fx("A",FiByp=0,FiMix=0.7,FiType=FT[r.choice(["Dark LP 12","Clean BP 12","Legacy LP 24","Dark BP 12"])],FiCut=r.choice([1200,2500,5000]),FiRes=r.uniform(0.2,0.6))
    pr.route(2,1,"A Filter Cutoff",r.uniform(0.2,0.5))
    pr.fx("A",LoByp=0,LoMix=r.uniform(0.3,0.7),LoBit=r.choice([8,10,12]),LoRate=r.choice([0.1,0.25]))
    pr.p["G_OutClip"]=1; pr.order("A",[1,0,3,2,4,5]); return pr

def a_space(v,r):
    pr=clean(Pr("Space",""))
    pat=PATS_GATE[(v*3)%12]
    pr.mod(1,X(pat,lo=0.0),div=r.choice(["1/8","1/16","1/4"]),smooth=0.3)
    pr.set(G_RvOn=1,G_RvSize=r.uniform(0.6,0.95),G_RvDecay=r.uniform(0.7,0.95),G_RvMix=0.0,G_RvBass=0.4,G_RvTreble=r.choice([0.3,0.5]))
    pr.route(1,1,"Reverb Mix",r.uniform(0.4,0.6))
    pr.fx("A",DeByp=0,DeMix=r.uniform(0.3,0.6),DeFb=r.uniform(0.5,0.7),DePing=1,DeLS=DIV["1/4."],DeRS=DIV["1/4"],DeLP=3500)
    pr.fx("A",FiByp=0,FiMix=1,FiType=FT["Dark LP 12"],FiCut=r.choice([3500,6000]),FiRes=0.1)
    pr.fx("A",FlByp=0,FlMix=0.3,FlMode=1,FlSpeed=0.1,FlDepth=0.6,FlDelay=3)
    pr.p["G_OutMix"]=1; pr.order("A",[0,4,2,1,3,5]); return pr

def a_layers(v,r):
    pr=clean(Pr("Layers",""))
    pr.p["B_Bypass"]=0; pr.p["G_Routing"]=1
    pr.fx("A",FiByp=0,FiMix=1,FiType=FT[r.choice(["Clean BP 12","Dark LP 12","Phaser 8p"])],FiCut=r.choice([700,1200,2500]),FiRes=r.uniform(0.3,0.65))
    pr.fx("A",DeByp=0,DeMix=0.5,DeFb=0.45,DeLS=DIV["1/8."],DeRS=DIV["1/8"],DePing=1)
    pr.fx("B",LoByp=0,LoMix=1,LoBit=r.choice([8,10]),LoRate=r.choice([0.2,0.35]),LoNoise=0.1)
    pr.fx("B",GlByp=0,GlMix=1,GlSize=r.choice([80,140,220]),GlSmooth=0.3,GlRev=r.choice([0,1]))
    pr.fx("B",DiByp=0,DiMix=0.6,DiDrive=0.35,DiType=0,DiOut=-2)
    pr.mod(1,X(PATS_GATE[(v*5)%12]),div="1/16",smooth=0.05)
    pr.mod(2,X(PATS_GATE[(v*5+2)%12]),div="1/16",invert=1,smooth=0.05)
    pr.route(1,1,"A Filter Cutoff",r.uniform(0.3,0.6))
    pr.mod(3,stairs(r,8,4),div="1/8")
    pr.route(2,2,"B Level",-0.6)
    pr.p["A_Level"]=0.75; pr.p["B_Level"]=0.7
    pr.set(G_RvMix=0.18); pr.order("A",[0,1,2,3,4,5]); pr.order("B",[3,1,5,0,2,4]); return pr

def a_pump(v,r):
    pr=clean(Pr("Gates",""))
    pr.mod(1,[0.0,0.35,0.7,1.0]+[1.0]*4,div=r.choice(["1/4","1/8"]),mode=1,tension=r.choice([0.5,1.0]),trem=(r.uniform(0.6,0.9),1))
    pr.fx("A",FiByp=0,FiMix=1,FiType=FT["Dark LP 12"],FiCut=r.choice([6000,10000]),FiRes=0.1)
    pr.route(1,1,"A Filter Cutoff",-0.0)
    pr.fx("A",DeByp=0,DeMix=0.3,DeFb=0.4,DeLS=DIV["1/8"],DeRS=DIV["1/8."])
    pr.set(G_RvMix=0.2); pr.order("A",[0,1,2,3,4,5]); return pr

def a_ring(v,r):
    pr=clean(Pr("Destruction",""))
    t=["RingMod","OctaveBand","PitchShift","Vowel"][v%4]
    pr.fx("A",FiByp=0,FiMix=r.uniform(0.6,1.0),FiType=FT[t],FiCut=r.choice([150,330,600,900]),FiRes=r.uniform(0.2,0.6))
    pr.mod(1,stairs(r,16,r.choice([4,5])),div=r.choice(["1/8","1/16","1/4"]),smooth=r.choice([0,0.2]))
    pr.route(1,1,"A Filter Cutoff",r.uniform(0.2,0.55))
    pr.fx("A",DeByp=0,DeMix=0.3,DeFb=0.45,DeLS=DIV["1/8."],DeRS=DIV["1/8"],DePing=1)
    pr.fx("A",DiByp=0,DiMix=0.4,DiDrive=0.3,DiOut=-1)
    pr.set(G_RvMix=0.18); pr.order("A",[0,1,2,3,4,5]); return pr

ARCHES=[ # (func, count, names)
 (a_gate,12,["Stutter Gate","Trance Chop","Half Time Gate","Pulse Machine","Machine Gun","Broken Gate","Four Four Chop","Skipping Stone","Gate Bounce","Hiccup","Clipped Wings","Heartbeat"]),
 (a_pump,6,["Soft Pump","Sidechain Breath","Hard Duck","Pump Delay","Kick Pump","Slow Swell"]),
 (a_filterseq,12,["Acid Steps","Dark Staircase","Legacy Sweep","Band Hop","Murky Bands","Phase Walker","Vowel Talk","Rising Highs","Steep Climb","Fat Step","Rubber Bands","Quad Phase"]),
 (a_delay,10,["Echo Throw","Dub Fire","Ping Cascade","Tape Hop","Spiral Echo","Gated Echo","Skank Delay","Dotted Ghost","Slap Back Throw","Delay Burst"]),
 (a_glitch,12,["Stutter Repeater","Buffer Tear","Reverse Skip","Rapid Freeze","Granular Rain","Shatter","Scratch Loop","Tape Stop Run","Skip Machine","Half Speed Slip","Double Time Slip","Glitch Garden"]),
 (a_lofi,8,["Dusty Crush","Bit Staircase","Radio Static","Cassette Lunch","Crunch Box","Old Sampler","Telephone Wire","Muffled Pulse"]),
 (a_movement,12,["Auto Swirl","Ramp Jet","Phase Bounce","Wide Orbit","Slow Phaser","Jet Rise","Stereo Chop","Ping Pong Pan","Spin Cycle","Long Flange","Wobble Wide","Tilt Shift"]),
 (a_destroy,10,["Fuzz Walk","Fold Machine","Overload Steps","Smash Filter","Tube Burn","Hard Rinse","Crunch Seq","Drive Hop","Wreck It","Clipper Fire"]),
 (a_space,8,["Reverb Gate","Cathedral Chop","Ghost Room","Smoke Ring","Dub Space","Wash Throw","Hall Pulse","Echo Chamber"]),
 (a_layers,8,["Two Faces","Split Rhythm","Parallel Chaos","Twin Engines","Layer Cake","Left Right Brain","Double Trouble","Mirror Steps"]),
 (a_ring,8,["Ring Pulse","Octave Choir","Pitch Hop","Robot Vowel","Metal Steps","Shifter Seq","Bell Mod","Formant Chop"]),
]

# ============================ more modulation ==========================================
# Every factory preset carries several modulators and a dozen routings. The step patterns come from the DS Tantra 2 factory presets
# and modulator presets (tools/t2_patterns.json, read by tools/t2_extract.py); the effects, ranges and routings are MANTRA's own.
import json, os
_T2 = None
def t2():
    global _T2
    if _T2 is None:
        _T2 = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "t2_patterns.json")))
        _T2["all_gates"] = [g for p in _T2["presets"] for g in p["gates"]] + _T2["mod_gates"]
        _T2["all_seqs"] = [q for p in _T2["presets"] for q in p["seqs"]] + _T2["mod_seqs"]
    return _T2

# (destination without layer, lowest depth, highest depth, kinds: s = steps / g = gate-like / p = smooth)
FXT = {
 "Fi": [("Filter Cutoff",.3,.8,"s"),("Filter Resonance",.15,.35,"s"),("Filter Mix",.2,.5,"g"),("Filter Low Cut",.2,.5,"sg"),("Filter Hi Cut",.2,.5,"sp")],
 "Di": [("Distortion Drive",.2,.5,"sg"),("Distortion Fuzz",.15,.4,"s"),("Distortion Tone",.2,.4,"sp"),("Distortion Mix",.2,.5,"g")],
 "De": [("Delay Mix",.2,.5,"g"),("Delay Feedback",.1,.28,"s"),("Delay LP",.2,.5,"sp"),("Delay BP",.2,.5,"s"),("Delay IN",.3,.8,"g")],
 "Lo": [("Lo-Fi Bit",.2,.5,"s"),("Lo-Fi Rate",.15,.4,"s"),("Lo-Fi Noise",.15,.4,"g"),("Lo-Fi Tone",.2,.4,"sp"),("Lo-Fi Mix",.2,.5,"g")],
 "Fl": [("Flanger Mix",.2,.4,"g"),("Flanger Delay",.2,.5,"s"),("Flanger Feedback",.15,.35,"sp"),("Flanger Width",.2,.5,"p"),("Flanger Depth",.2,.5,"p"),("Flanger Tone",.2,.4,"sp")],
 "Gl": [("Glitch Buffer",.15,.45,"s"),("Glitch Scale",.15,.4,"s"),("Glitch Smooth",.2,.5,"p"),("Glitch Stretch",.15,.4,"s"),("Glitch Mix",.3,.7,"g")],
}
GLT = [("Reverb Mix",.2,.5,"g","RvOn"),("Reverb Size",.1,.3,"p","RvOn"),("Reverb Decay",.1,.3,"p","RvOn"),("Reverb Treble",.2,.4,"sp","RvOn"),
       ("EQ Mid",.15,.35,"s","EqOn"),("EQ Freq",.2,.5,"sp","EqOn"),("EQ Bass",.1,.25,"g","EqOn"),("EQ Treble",.1,.25,"s","EqOn"),("Output Pan",.3,.7,"p",None)]
DIVS_S = ["1/16","1/8","1/16","1/16T","1/8T","1/4","1/16.","1/32","1/8."]
DIVS_G = ["1/16","1/16","1/8","1/32","1/16T","1/8"]
DIVS_P = ["1 bar","2 bars","4 bars","1.5 bars","8 bars"]

def _targets(pr):
    out = []
    for l in "AB":
        if l == "B" and pr.p["B_Bypass"]: continue
        for pre in ("Fi","Di","De","Lo","Fl","Gl"):
            if pr.p[f"{l}_{pre}Byp"]: continue
            for d,lo,hi,k in FXT[pre]: out.append((f"{l} {d}",lo,hi,k))
        out.append((f"{l} Pan",.3,.7,"p"))
    for d,lo,hi,k,flag in GLT:
        if flag is None or pr.p[f"G_{flag}"]: out.append((d,lo,hi,k))
    return out

def _free(pr):
    for n in range(1,NMATRIX+1):
        if pr.p[f"X{n}_Dst"]==0: return n
    return 0

def _used_dests(pr):
    return {int(pr.p[f"X{n}_Dst"]) for n in range(1,NMATRIX+1) if pr.p[f"X{n}_Dst"]}

def _depth(pr,dest,lo,hi,r):
    e=S.DEST[DN[dest]]; base=pr.p[e.name]; nb=(base-e.p.lo)/(e.p.hi-e.p.lo) if e.p.hi!=e.p.lo else 0
    d=r.uniform(lo,hi)
    if dest.endswith(("Mix","IN")) and nb>0.85: return -d                    # a full mix can only dip
    if dest.endswith(("Resonance","Feedback")) and nb>0.55: return -d      # a high resonance / feedback stays out of the red
    return d if r.random()<0.65 else -d

def _add_routes(pr,m,kind,n,r):
    cands=[t for t in _targets(pr) if any(c in t[3] for c in kind) and S.DEST_NAMES.index(t[0]) not in _used_dests(pr)]
    r.shuffle(cands)
    for dest,lo,hi,_ in cands[:n]:
        slot=_free(pr)
        if not slot: return
        pr.route(slot,m,dest,round(_depth(pr,dest,lo,hi,r),3))

def enrich(pr,r,mods=5,routes=10):
    """Bring a preset to at least `mods` modulators and `routes` routings (its own are kept)."""
    T=t2()
    kinds=["s","g","p","s","g","p","s","g"]
    # every modulator that exists but drives nothing yet gets a routing
    routed={int(pr.p[f"X{n}_Src"]) for n in range(1,NMATRIX+1) if pr.p[f"X{n}_Dst"]}
    for m in sorted(pr.used):
        if m not in routed: _add_routes(pr,m,"sg",1,r)
    ki=0
    while len(pr.used)<mods and len(pr.used)<NMOD:
        m=next(i for i in range(1,NMOD+1) if i not in pr.used); k=kinds[ki%len(kinds)]; ki+=1
        if k=="g":
            pr.mod(m,r.choice(T["all_gates"]),div=r.choice(DIVS_G),gate=r.choice([0.6,0.8,1.0]),smooth=r.choice([0,0.05]),shuffle=r.choice([0,0,0.2,0.35]))
        elif k=="s":
            q=r.choice(T["all_seqs"]); pr.mod(m,q["v"],div=r.choice(DIVS_S),smooth=r.choice([0,0.1,0.25]),shuffle=r.choice([0,0,0.25]),tension=r.choice([0.3,0.5,0.8]),invert=1 if r.random()<0.2 else 0)
        else:
            base=r.choice([tri(16),ramp(16,True),ramp(16,False),[0.5+0.5*math.sin(2*math.pi*i/16) for i in range(16)]])
            pr.mod(m,base,div=r.choice(DIVS_P),mode=1,tension=r.choice([0.6,1.0]),flip=(round(r.uniform(0.3,0.8),2),1) if r.random()<0.4 else (0,0))
        _add_routes(pr,m,k,2 if k!="p" else 2,r)
    # more routings: round robin over the modulators until the target is reached
    ms=sorted(pr.used); guard=0
    while sum(1 for n in range(1,NMATRIX+1) if pr.p[f"X{n}_Dst"])<routes and guard<40 and ms:
        m=ms[guard%len(ms)]; guard+=1
        k="g" if (pr.p[f"M{m}_TremDepth"]>0 or (min(pr.packs[m-1])==0 and sorted(set(pr.packs[m-1]))[:3]==sorted(set(pr.packs[m-1]))[:3] and len(set(pr.packs[m-1]))<=3)) else ("p" if pr.p[f"M{m}_Mode"]==1 else "s")
        _add_routes(pr,m,k,1,r)
    return pr

NAME_A=["Amber","Brass","Cobalt","Dusk","Ember","Frost","Gilded","Hollow","Iron","Jade","Kinetic","Lunar","Molten","Neon","Opal","Plasma","Quartz","Rusted","Silver","Tidal","Umbra","Velvet","Wired","Xenon","Yonder","Zinc","Broken","Cracked","Drifting","Echoing","Fading","Glass","Humming","Inverted","Jagged","Kilo","Liquid","Muted","Nested","Orbiting","Pale","Quiet","Rolling","Static","Twisted","Upper","Vapor","Wandering","Hidden","Folded"]
NAME_N={"FLT":["Sweep","Filter","Bandpass","Formant","Ridge","Wah","Comb","Notch","Slope","Vowel","Tilt","Peak"],
 "GL":["Shards","Buffer","Stutter","Fragments","Splice","Scatter","Cutter","Slicer","Grain","Skip","Crumble","Tear"],
 "PAD":["Cloud","Drift","Bloom","Haze","Tide","Canopy","Aurora","Mirage","Horizon","Veil","Glow","Bay"],
 "DRM":["Kit","Break","Snap","Rim","Shuffle","Roll","Kick","Hat","Crash","Tick","Clap","Thump"],
 "DIST":["Burn","Grit","Crush","Scorch","Clip","Rust","Saw","Edge","Furnace","Smear","Bite","Mangle"],
 "LOFI":["Tape","Dust","Crunch","Cassette","Vinyl","Pixel","Static","Fuzz","Muffle","Haze","Noise","Crackle"],
 "SEQ":["Pulse","Walker","Runner","Loop","Chain","Ladder","Spiral","Engine","Circuit","Pattern","Cycle","Steps","Stride","Clock","Relay","Orbit","Switch","Weave"],
 "SFX":["Rise","Fall","Siren","Burst","Wave","Pulse","Signal","Swarm","Alarm","Echo","Sweeper","Impact"]}
CAT_ARCH={ # category -> archetype functions the Tantra-pattern presets are built on (rotated)
 "FLT":[a_filterseq,a_filterseq,a_ring],"GL":[a_glitch,a_glitch,a_lofi],"PAD":[a_space,a_movement,a_layers,a_delay],
 "DRM":[a_gate,a_pump,a_gate,a_layers],"DIST":[a_destroy,a_lofi,a_ring,a_destroy,a_glitch],"LOFI":[a_lofi,a_destroy],
 "SEQ":[a_filterseq,a_delay,a_gate,a_layers,a_glitch,a_movement,a_lofi,a_space,a_destroy,a_pump],"SFX":[a_ring,a_movement,a_glitch,a_space]}

def tantra_bank(taken):
    """One MANTRA preset per DS Tantra 2 factory preset: the same modulator patterns (gates and step sequences), MANTRA's own effects and names."""
    T=t2(); out=[]; cnt={}
    for j,src in enumerate(T["presets"]):
        cat=src["cat"] if src["cat"] in CAT_ARCH else "DRM"
        i=cnt.get(cat,0); cnt[cat]=i+1
        r=random.Random(77000+j*13)
        fn=CAT_ARCH[cat][i%len(CAT_ARCH[cat])]
        pr=fn(j+3*i,r)
        pats=[("g",g) for g in src["gates"]]+[("s",q) for q in src["seqs"]]
        r.shuffle(pats); pats=pats[:8]
        for m,(k,q) in enumerate(pats,1):
            if k=="g": pr.mod(m,q,div=r.choice(DIVS_G),gate=r.choice([0.6,0.8,1.0]),smooth=r.choice([0,0.05]),shuffle=r.choice([0,0,0.2,0.35]),trem=(round(r.uniform(0.5,1.0),2),r.choice([1,2])) if m==1 and r.random()<0.4 else (0,0))
            else:      pr.mod(m,q["v"],div=r.choice(DIVS_S),smooth=r.choice([0,0.1,0.25]),shuffle=r.choice([0,0,0.25]),tension=r.choice([0.3,0.5,0.8]),mode=1 if (q["n"]<=8 and r.random()<0.35) else 0,invert=1 if r.random()<0.15 else 0)
        for k in range(len(pats)+1,NMOD+1): pass
        enrich(pr,r,mods=max(5,len(pats)),routes=min(22,max(11,3*len(pats))))
        a,n=r.choice(NAME_A),r.choice(NAME_N[cat])
        nm=f"{a} {n}"; t=0
        while nm in taken: t+=1; a=r.choice(NAME_A); n=r.choice(NAME_N[cat]); nm=f"{a} {n}"+("" if t<12 else f" {t-10}")
        taken.add(nm); pr.name=nm[:24]
        out.append(pr)
    return out

def init_preset():
    p=Pr("Basic","Init")
    clean(p); p.p["A_FiByp"]=0; p.p["A_FiMix"]=1; p.p["G_RvMix"]=0.0
    return p

def all_presets():
    out=[init_preset()]
    for fi,(fn,cnt,names) in enumerate(ARCHES):
        for v in range(cnt):
            r=random.Random(1000*fi+v*17+5)
            pr=fn(v,r)
            pr.name=names[v] if v<len(names) else f"{names[0]} {v+1}"
            r2=random.Random(5000*fi+v*31+9)
            enrich(pr,r2,mods=5+(v%3),routes=11+(v%4))
            out.append(pr)
    taken={p.name for p in out}
    out+=tantra_bank(taken)
    return out

if __name__=="__main__":
    ps=all_presets(); print(len(ps))
    from collections import Counter
    print(Counter(p.cat for p in ps))
    nm=[sum(1 for n in range(1,NMATRIX+1) if p.p[f"X{n}_Dst"]) for p in ps]; print("routes min/avg/max",min(nm),sum(nm)/len(nm),max(nm)); print("mods avg",sum(len(p.used) for p in ps)/len(ps))
