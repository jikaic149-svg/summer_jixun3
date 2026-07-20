# -*- coding: utf-8 -*-
import numpy as np
d = np.genfromtxt('vofa+.csv', delimiter=',', skip_header=1)
theta,tref,stref,pos = d[:,0],d[:,1],d[:,2],d[:,3]
perr = d[:,5]
v,vref,verr,bref = d[:,6],d[:,7],d[:,8],d[:,9]
aref,arpm,cmd = d[:,10],d[:,11],d[:,12]
fault,mode,integ = d[:,13],d[:,14],d[:,15]
DT=0.02; N=len(d); t=np.arange(N)*DT
print(f"rows={N} dur={N*DT:.1f}s")
print("mode:",{int(m):int((mode==m).sum()) for m in np.unique(mode)})
print("fault:",{int(m):int((fault==m).sum()) for m in np.unique(fault)})

# segments by (mode,fault)
print("\n[segments >1s]")
cur=(mode[0],fault[0]); s=0
for i in range(1,N+1):
    key=(mode[i],fault[i]) if i<N else None
    if key!=cur:
        if (i-s)*DT>1.0:
            sl=slice(s,i)
            print(f"  {t[s]:6.1f}-{t[i-1]:6.1f}s mode={int(cur[0])} fault={int(cur[1])} "
                  f"th={theta[sl].mean():+.2f} tref={tref[sl].mean():+.2f} "
                  f"v={v[sl].mean():+.3f}(max|{np.abs(v[sl]).max():.2f}) vref={vref[sl].mean():+.3f} "
                  f"integ={integ[sl].mean():+.2f}(end {integ[i-1]:+.2f}) pos_move={np.sum(v[sl])*DT:+.2f}m")
        cur=key; s=i

# forward-mode detail
mf=(mode==1)&(fault==0)
if mf.sum()>0:
    print(f"\n[forward rows={mf.sum()} ({mf.sum()*DT:.1f}s)]")
    print(f"  v: mean={v[mf].mean():+.3f} std={v[mf].std():.3f} min={v[mf].min():+.2f} max={v[mf].max():+.2f}")
    print(f"  vref: mean={vref[mf].mean():+.3f} max={vref[mf].max():+.2f} (=0.10?)")
    print(f"  |v-vref|>0.1 比例: {np.mean(np.abs(v[mf]-vref[mf])>0.1)*100:.0f}%")
    print(f"  integ: mean={integ[mf].mean():+.2f} max={integ[mf].max():+.2f} min={integ[mf].min():+.2f} 顶1.5比例={np.mean(np.abs(integ[mf])>=1.45)*100:.0f}%")
    print(f"  theta: mean={theta[mf].mean():+.2f}  tref: mean={tref[mf].mean():+.2f} 顶3比例={np.mean(np.abs(tref[mf])>=2.95)*100:.0f}%")
    print(f"  stref mean={stref[mf].mean():+.2f}")
    # guard visible? vref jumps to 0 while mode=1
    guard = mf & (np.abs(vref)<0.005) & (np.abs(v)>0.15)
    print(f"  疑似守卫激活行(vref=0且|v|>0.15): {guard.sum()} ({guard.sum()*DT:.1f}s)")
# stand check for new offset
ms=(mode==0)&(fault==0)&(np.abs(v)<0.05)
if ms.sum()>50:
    print(f"\n[stand静立 {ms.sum()*DT:.1f}s] theta mean={theta[ms].mean():+.3f} (新offset应≈0)")
# parked rows theta center (theta0 check with new offset)
mp=(fault==0)&(np.abs(v)<0.03)&(np.abs(arpm)<5)&(np.abs(cmd)<0.06)
print(f"[parked {mp.sum()*DT:.1f}s] theta median={np.median(theta[mp]):+.2f} p10={np.percentile(theta[mp],10):+.2f} p90={np.percentile(theta[mp],90):+.2f}")

# oscillation check: v in forward, zero crossings & dominant period
if mf.sum()>200:
    vv=v[mf]-v[mf].mean()
    zc=np.sum(np.diff(np.sign(vv))!=0)
    print(f"\n[forward v 振荡] 过零次数={zc} 时长={mf.sum()*DT:.1f}s -> 主周期约 {2*mf.sum()*DT/max(zc,1):.1f}s")
    # integral vs v phase
    c=np.corrcoef(integ[mf],v[mf])[0,1]
    print(f"  corr(integ, v)={c:+.2f}   corr(integ, 前移1s的v)={np.corrcoef(integ[mf][:-50],v[mf][50:])[0,1]:+.2f}")
# fault snapshots
ft=np.where((fault==3)&(np.roll(fault,1)!=3))[0]
for k in ft[:5]:
    s=max(0,k-25)
    print(f"[fault@{t[k]:.1f}s] theta {theta[s]:+.1f}->{theta[k-1]:+.1f} v {v[s]:+.2f}->{v[k-1]:+.2f} integ {integ[k-1]:+.2f} tref {tref[k-1]:+.2f} bref {bref[k-1]:+.0f} mode={int(mode[k-1])}")
