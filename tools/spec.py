# MANTRA single-source parameter specification.
# Everything (C++ enum/table, UI script, factory presets, preset XML) is generated from this file.
import math

DIV_BEATS = [32,16,12,8,6,4,3,2,1.5,4/3,1,0.75,2/3,0.5,0.375,1/3,0.25,0.1875,1/6,0.125,1/12,0.0625]
DIV_NAMES = ["8 bars","4 bars","3 bars","2 bars","1.5 bars","1 bar","1/2.","1/2","1/4.","1/2T","1/4",
             "1/8.","1/4T","1/8","1/16.","1/8T","1/16","1/32.","1/16T","1/32","1/32T","1/64"]
assert len(DIV_BEATS)==len(DIV_NAMES)==22
DIV_QUARTER=10; DIV_BAR=5; DIV_EIGHTH=13; DIV_16=16

FILTER_TYPES = []
for fam,suf in (("Clean",("12","42")),("Dark",("12","42"))):
    for m in ("LP","BP","HP"):
        for s in suf:
            FILTER_TYPES.append(f"{fam} {m} {s}")
for m in ("LP","BP","HP"):
    for s in ("12","24"):
        FILTER_TYPES.append(f"Legacy {m} {s}")
FILTER_TYPES += ["Phaser 4p","Phaser 8p","RingMod","PitchShift","OctaveBand","Vowel"]
assert len(FILTER_TYPES)==24
DIST_TYPES = ["Soft","Hard","Fold","Tube"]
FLANGER_MODES = ["Flanger","Phaser"]
GLITCH_MODES = ["Run","Freeze"]
EFFECTS = ["Filter","Distortion","Delay","Lo-Fi","Flanger","Glitch"]
ROUTING = ["Serial","Parallel"]
MODE_NAMES = ["Steps","Curve"]
TREM_DEST = ["Off","Global","Layer A","Layer B"]
OFFON = ["Off","On"]

class P:
    def __init__(s,name,lo,hi,df,centre=None,step=0.0,labels=None,mod=False,ui=None,suffix=""):
        s.name=name;s.lo=lo;s.hi=hi;s.df=df;s.centre=centre;s.step=step
        s.labels=labels;s.mod=mod;s.ui=ui or name;s.suffix=suffix;s.group="";s.rel=None
    def skew(s):
        if s.centre is None: return 1.0
        n=(s.centre-s.lo)/(s.hi-s.lo)
        return math.log(0.5)/math.log(n)

def B(name,df=0,ui=None): return P(name,0,1,df,step=1,labels=OFFON,ui=ui)
def E(name,labels,df=0,ui=None,mod=False): return P(name,0,len(labels)-1,df,step=1,labels=labels,ui=ui,mod=mod)

# ---- layer-relative parameters (name, ui label) in order ---------------------------------
LAYER = [
 P("Level",0,1,0.8,centre=0.5,mod=True,ui="Level"),
 P("Pan",-1,1,0,mod=True,ui="Pan"),
 B("Bypass",0,"Bypass"),
]
for i in range(6):
    LAYER.append(E(f"Slot{i+1}",EFFECTS,i,ui=f"Slot {i+1}"))
LAYER += [
 # Filter
 P("FiMix",0,1,1,mod=True,ui="Mix"), B("FiByp",0,"Bypass"),
 E("FiType",FILTER_TYPES,0,"Type"),
 P("FiCut",20,20000,2000,centre=1000,mod=True,ui="Cutoff",suffix="Hz"),
 P("FiRes",0,1,0.25,mod=True,ui="Resonance"),
 P("FiLow",20,2000,20,centre=200,mod=True,ui="Low Cut",suffix="Hz"),
 P("FiHi",200,20000,20000,centre=4000,mod=True,ui="Hi Cut",suffix="Hz"),
 # Distortion
 P("DiMix",0,1,1,mod=True,ui="Mix"), B("DiByp",0,"Bypass"),
 P("DiDrive",0,1,0.4,mod=True,ui="Drive"),
 P("DiFuzz",0,1,0,mod=True,ui="Fuzz"),
 E("DiType",DIST_TYPES,0,"Type"),
 B("DiMono",0,"Mono"),
 P("DiTone",0,1,0.5,mod=True,ui="Tone"),
 P("DiOut",-24,12,0,mod=True,ui="Output",suffix="dB"),
 # Delay
 P("DeMix",0,1,0.4,mod=True,ui="Mix"), B("DeByp",0,"Bypass"),
 P("DeL",1,2000,375,centre=250,mod=True,ui="Speed L",suffix="ms"),
 P("DeR",1,2000,500,centre=250,mod=True,ui="Speed R",suffix="ms"),
 E("DeLS",DIV_NAMES,DIV_EIGHTH,"Sync L"),
 E("DeRS",DIV_NAMES,DIV_EIGHTH,"Sync R"),
 B("DeSync",1,"Sync"), B("DePing",0,"Ping-Pong"),
 P("DeFb",0,1,0.4,mod=True,ui="Feedback"),
 P("DeLP",200,20000,8000,centre=3000,mod=True,ui="LP",suffix="Hz"),
 P("DeBP",0,1,0,mod=True,ui="BP"),
 P("DeIn",0,1,1,mod=True,ui="IN"),
 # Lo-Fi
 P("LoMix",0,1,1,mod=True,ui="Mix"), B("LoByp",0,"Bypass"),
 P("LoBit",1,16,16,centre=8,mod=True,ui="Bit"),
 P("LoRate",0,1,0,mod=True,ui="Rate"),
 P("LoNoise",0,1,0,mod=True,ui="Noise"),
 P("LoTone",0,1,1,mod=True,ui="Tone"),
 # Flanger
 P("FlMix",0,1,0.5,mod=True,ui="Mix"), B("FlByp",0,"Bypass"),
 E("FlMode",FLANGER_MODES,0,"Mode"),
 P("FlDelay",0.1,10,2,centre=2,mod=True,ui="Delay",suffix="ms"),
 P("FlFb",-0.95,0.95,0.4,mod=True,ui="Feedback"),
 P("FlWidth",0,1,0.5,mod=True,ui="Width"),
 P("FlTone",0,1,0.7,mod=True,ui="Tone"),
 P("FlSpeed",0.02,20,0.4,centre=1,mod=True,ui="Speed",suffix="Hz"),
 E("FlDiv",DIV_NAMES,DIV_BAR,"Sync Speed"),
 B("FlSync",0,"Sync"),
 P("FlDepth",0,1,0.6,mod=True,ui="Depth"),
 # Glitch
 P("GlMix",0,1,1,mod=True,ui="Mix"), B("GlByp",0,"Bypass"),
 E("GlMode",GLITCH_MODES,0,"Mode"),
 P("GlSize",5,1000,120,centre=100,mod=True,ui="Buffer",suffix="ms"),
 P("GlScale",0.25,2,1,centre=1,mod=True,ui="Scale"),
 B("GlRev",0,"Reverse"),
 P("GlSmooth",0,1,0.3,mod=True,ui="Smooth"),
 P("GlStretch",0.25,2,1,centre=1,mod=True,ui="Stretch"),
]
LAYER_FX_RANGES = {  # relative offsets filled below
}
# group names per layer entry (for UI / randomize)
def group_of(n):
    for pre,g in (("Fi","Filter"),("Di","Distortion"),("De","Delay"),("Lo","Lo-Fi"),("Fl","Flanger"),("Gl","Glitch")):
        if n.startswith(pre): return g
    return "Layer"
for p in LAYER: p.group=group_of(p.name)

# ---- modulator parameters --------------------------------------------------------------
MOD = [
 E("Mode",MODE_NAMES,0,"Mode"),
 B("Sync",1,"Sync"),
 P("Speed",0.05,30,1,centre=2,ui="Speed",suffix="Hz"),
 E("Div",DIV_NAMES,DIV_BAR,"Sync Speed"),
 P("Steps",2,32,16,step=1,ui="Steps"),
 P("Gate",0,1,1,ui="Gate"),
 P("Smooth",0,1,0,ui="Smooth"),
 P("Shuffle",0,1,0,ui="Shuffle"),
 P("Tension",0,1,0.5,ui="Tension"),
 B("Invert",0,"Invert"),
 P("Shift",0,31,0,step=1,ui="Shift"),
 P("TremDepth",0,1,0,ui="Tremolo"),
 E("TremDest",TREM_DEST,0,"Trem Dest"),
 P("FlipDepth",0,1,0,ui="Pan Flip"),
 E("FlipDest",TREM_DEST,0,"Flip Dest"),
]
NMOD=8; NSTEP=32; NMATRIX=24

MATRIX = [
 P("Src",0,NMOD,0,step=1,ui="Source"),
 P("Dst",0,0,0,step=1,ui="Dest"),   # hi patched after dest list is known
 P("Depth",-1,1,0,ui="Depth"),
 B("Byp",0,"Bypass"),
]

# ---- globals -----------------------------------------------------------------------------
GLOBAL = [
 E("Routing",ROUTING,0,"Routing"),
 P("Speed",0.25,4,1,centre=1,ui="Playback Speed"),
 P("Bpm",20,300,120,centre=120,ui="BPM"),
 B("EqOn",1,"EQ"),
 P("EqBass",-12,12,0,mod=True,ui="Bass",suffix="dB"),
 P("EqMid",-12,12,0,mod=True,ui="Mid",suffix="dB"),
 P("EqFreq",200,8000,1000,centre=1000,mod=True,ui="Freq",suffix="Hz"),
 P("EqTreble",-12,12,0,mod=True,ui="Treble",suffix="dB"),
 B("RvOn",1,"Reverb"),
 P("RvSize",0,1,0.5,mod=True,ui="Size"),
 P("RvDecay",0,1,0.5,mod=True,ui="Decay"),
 P("RvMix",0,1,0.2,mod=True,ui="Mix"),
 P("RvBass",0,1,0.5,mod=True,ui="Bass"),
 P("RvTreble",0,1,0.5,mod=True,ui="Treble"),
 P("RvIn",0,1,1,ui="IN"),
 P("OutVol",-24,12,0,mod=True,ui="Volume",suffix="dB"),
 P("OutPan",-1,1,0,mod=True,ui="Pan"),
 P("OutMix",0,1,1,mod=True,ui="Mix"),
 B("OutClip",1,"Clipper"),
 B("MasterOn",1,"Master"),
 P("StepWrite",0,NMOD*NSTEP*1001+1001,0,step=1,ui="StepWrite"),
]
GLOBAL_GROUP = {"Routing":"Global","Speed":"Global","Bpm":"Global","StepWrite":"Global"}
for p in GLOBAL:
    n=p.name
    p.group = GLOBAL_GROUP.get(n, "EQ" if n.startswith("Eq") else "Reverb" if n.startswith("Rv") else "Output")

# ---- flatten -----------------------------------------------------------------------------
ALL=[]          # list of (absolute name, P, kind info)
class Entry: pass
def add(name,p,**kw):
    e=Entry(); e.name=name; e.p=p; e.idx=len(ALL); e.__dict__.update(kw); ALL.append(e); return e

LAYER_BASE=[]; LAYER_NAMES=["A","B"]
for l,ln in enumerate(LAYER_NAMES):
    LAYER_BASE.append(len(ALL))
    for r,p in enumerate(LAYER): add(f"{ln}_{p.name}",p,scope="layer",layer=l,rel=r)
GLOBAL_BASE=len(ALL)
for r,p in enumerate(GLOBAL): add(p.name if False else f"G_{p.name}",p,scope="global",rel=r)
MOD_BASE=[]
for m in range(NMOD):
    MOD_BASE.append(len(ALL))
    for r,p in enumerate(MOD): add(f"M{m+1}_{p.name}",p,scope="mod",mod_i=m,rel=r)
MX_BASE=len(ALL)
# destinations (modulatable params) -----------------------------------------------------
DEST=[None]  # 0 = none
for e in list(ALL):
    if e.p.mod: DEST.append(e)
for s in range(NMATRIX):
    for r,p in enumerate(MATRIX):
        q=P(p.name,p.lo,p.hi,p.df,step=p.step,labels=p.labels,ui=p.ui); q.group="Matrix"
        if p.name=="Dst": q.hi=len(DEST)-1
        add(f"X{s+1}_{p.name}",q,scope="matrix",slot=s,rel=r)
NP=len(ALL)
MX_SIZE=len(MATRIX); LAYER_SIZE=len(LAYER); MOD_SIZE=len(MOD)

def dest_name(e):
    if e is None: return "None"
    if e.scope=="layer":
        return f"{LAYER_NAMES[e.layer]} {e.p.group} {e.p.ui}" if e.p.group!="Layer" else f"{LAYER_NAMES[e.layer]} {e.p.ui}"
    return f"{e.p.group} {e.p.ui}"
DEST_NAMES=[dest_name(e) for e in DEST]

if __name__=="__main__":
    print("params",NP,"dests",len(DEST),"layer size",LAYER_SIZE)
    print(DEST_NAMES[:12])
