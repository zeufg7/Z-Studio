import mido, random
random.seed(7)
TPQ=480
mid=mido.MidiFile(ticks_per_beat=TPQ)
def track(name, notes):
    t=mido.MidiTrack(); mid.tracks.append(t)
    t.append(mido.MetaMessage('track_name', name=name, time=0))
    ev=[]
    for (p,s,l,v) in notes:
        ev.append((s,'on',p,v)); ev.append((s+l,'off',p,0))
    ev.sort(key=lambda e:(e[0], 0 if e[1]=='off' else 1))
    last=0
    for (s,k,p,v) in ev:
        tick=max(0,int(round(s*TPQ)))
        t.append(mido.Message('note_on' if k=='on' else 'note_off', note=p, velocity=v, time=tick-last)); last=tick
def human(s,l,legato=True, stacc=False):
    js=random.gauss(0,0.035)
    if stacc: ll=l*random.uniform(0.25,0.4)
    elif legato: ll=l+random.uniform(0.02,0.12)
    else: ll=l*random.uniform(0.8,0.95)
    return s+js, max(0.03,ll)
RH=[]; LH=[]
# compasso 1-2: melodia em colcheias e semicolcheias (Ré maior)
mel=[(74,0,1),(76,1,.5),(78,1.5,.5),(79,2,1.5),(78,3.5,.5),
     (76,4,.5),(74,4.5,.25),(76,4.75,.25),(78,5,1),(81,6,2)]
# compasso 3: tercinas
mel+=[(79,8,1/3),(78,8+1/3,1/3),(76,8+2/3,1/3),(74,9,1),(73,10,1/3),(74,10+1/3,1/3),(76,10+2/3,1/3),(78,11,1)]
# compasso 4: sincopa
mel+=[(76,12,.5),(78,12.5,1),(79,13.5,.5),(81,14,2)]
# compasso 5-6: staccatos
for i,p in enumerate([81,79,78,76,74,76,78,79]):
    mel.append((p,16+i*0.5,.5,'s'))
mel+=[(78,20,1),(76,21,1),(74,22,2)]
for m in mel:
    p,s,l=m[0],m[1],m[2]; st=len(m)>3
    ss,ll=human(s,l,legato=not st, stacc=st)
    RH.append((p,ss,ll,random.randint(70,100) if not st else random.randint(85,110)))
# acordes na mao direita (compasso 7-8)
for (s,ch) in [(24,[66,69,74]),(26,[67,71,74]),(28,[66,69,73]),(30,[62,66,69,74])]:
    for p in ch:
        ss,ll=human(s,2); RH.append((p,ss+random.uniform(0,0.02),ll,random.randint(60,85)))
# mao esquerda: baixo + arpejo
prog=[(50,[57,62]),(55,[59,62]),(47,[54,59]),(52,[55,59]),(50,[57,62]),(55,[59,64]),(50,[57,62]),(45,[52,57])]
for bar,(b,ar) in enumerate(prog):
    s0=bar*4
    ss,ll=human(s0,4); LH.append((b-12 if bar%2==0 else b-12,ss,ll,random.randint(60,80)))
    for k in range(4):
        p=ar[k%2]
        ss,ll=human(s0+1+k*0.75 if False else s0+0.5+k*0.875,0.5)
    for k,off in enumerate([1,1.5,2,2.5,3,3.5]):
        ss,ll=human(s0+off,.5); LH.append((ar[k%2]-12+12,ss,ll,random.randint(50,70)))
# nota fantasma (esbarrao)
RH.append((77,9.02,0.03,20))
track('Piano', RH+LH)
mid.save('humano.mid'); print('ok', len(RH), len(LH))
