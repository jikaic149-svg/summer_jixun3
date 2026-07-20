# -*- coding: utf-8 -*-
import numpy as np
d = np.genfromtxt('vofa+.csv', delimiter=',', skip_header=1)
theta,tref,stref = d[:,0],d[:,1],d[:,2]
v,vref = d[:,6],d[:,7]
bref,aref,arpm = d[:,9],d[:,10],d[:,11]
fault,mode,trim = d[:,13],d[:,14],d[:,15]
DT=0.02; N=len(d); t=np.arange(N)*DT
print(f"rows={N} dur={N*DT:.1f}s")

mf=(mode==1)&(fault==0)
idx=np.where(mf)[0]
print(f"forward: t={t[idx[0]]:.2f}~{t[idx[-1]]:.2f}s ({mf.sum()*DT:.1f}s) fault rows={int((fault!=0).sum())}")

# stand段核对偏置
ms=(mode==0)&(fault==0)
print(f"[stand {ms.sum()*DT:.1f}s] theta={theta[ms].mean():+.3f} tref={tref[ms].mean():+.3f} bref={bref[ms].mean():+.1f} aref={aref[ms].mean():+.1f} arpm={arpm[ms].mean():+.1f} trim_end={trim[ms][-1] if ms.sum() else 0:+.3f}")
print(f"[forward全段] theta={theta[mf].mean():+.3f} tref={tref[mf].mean():+.3f} trim: {trim[idx[0]]:+.3f} -> {trim[idx[-1]]:+.3f}")

# drive窗口: forward中 vref>0.005
drv = mf & (vref>0.005)
lab=np.where(drv)[0]
# split contiguous
splits=np.where(np.diff(lab)>1)[0]
starts=np.r_[lab[0], lab[splits+1]]
ends=np.r_[lab[splits], lab[-1]]
print(f"\ndrive窗口数={len(starts)} 累计={drv.sum()*DT:.1f}s")
print(f"{'#':>2} {'t0':>7} {'dur':>5} | {'theta':>6} {'tref':>6} | {'bref':>6} {'base=aref-arpm-bref':>8} {'aref':>6} {'arpm':>6} | {'aref-arpm':>9} {'v_end':>6}")
for k,(s,e) in enumerate(zip(starts,ends)):
    sl=slice(s,e+1)
    base=aref[sl]-arpm[sl]-bref[sl]
    print(f"{k:>2} {t[s]:7.2f} {(e-s+1)*DT:5.2f} | {theta[sl].mean():+6.2f} {tref[sl].mean():+6.2f} | {bref[sl].mean():+6.1f} {base.mean():+8.1f} {aref[sl].mean():+6.1f} {arpm[sl].mean():+6.1f} | {(aref[sl]-arpm[sl]).mean():+9.1f} {v[e]:+6.3f}")

# 汇总
allsl=drv
base=aref[allsl]-arpm[allsl]-bref[allsl]
print(f"\n[drive汇总] theta={theta[allsl].mean():+.2f} tref={tref[allsl].mean():+.2f} bref={bref[allsl].mean():+.1f} base={base.mean():+.1f} aref={aref[allsl].mean():+.1f} arpm={arpm[allsl].mean():+.1f} net(aref-arpm)={(aref[allsl]-arpm[allsl]).mean():+.1f}")
# brake/hold段
oth = mf & ~drv
print(f"[非drive的forward段 {oth.sum()*DT:.1f}s] theta={theta[oth].mean():+.2f} bref={bref[oth].mean():+.1f} aref={aref[oth].mean():+.1f} arpm={arpm[oth].mean():+.1f}")
# theta随时间漂移
for a,b in [(0,10),(10,20),(20,30),(30,47)]:
    m2=mf&(t>=t[idx[0]]+a)&(t<t[idx[0]]+b)
    if m2.sum(): print(f"  forward+{a:>2}~{b:<2}s theta={theta[m2].mean():+.3f} tref={tref[m2].mean():+.3f} bref={bref[m2].mean():+.1f} trim={trim[m2].mean():+.3f}")
