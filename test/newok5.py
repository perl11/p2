#!/usr/bin/env python3
"""roast5: list the files that really pass (plan > 0, all ok, no skip-all) and are not yet in OK_ROAST (Makefile).
usage: python3 test/newok5.py [--write]   (--write appends them to OK_ROAST)"""
import subprocess,glob,os,re,sys,concurrent.futures as cf
root=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
mk=open(root+'/Makefile').read()
m=re.search(r'OK_ROAST\s*=(.*?)\n\n',mk,re.S)
cur=set(m.group(1).replace('\\\n',' ').split())
os.chdir(root+'/test/roast5')
for n in ('test.pl','TEST'):
    if not os.path.exists(n): os.symlink('../p5/test.pl',n)
files=sorted(glob.glob('**/*.t',recursive=True))
def run(f):
    try: r=subprocess.run([root+'/bin/p2',f],capture_output=True,timeout=20,text=True,errors='replace',stdin=subprocess.DEVNULL)
    except subprocess.TimeoutExpired: return f,False
    out=r.stdout
    pm=re.search(r'^1\.\.(\d+)(.*)',out,re.M)
    if r.returncode or not pm or int(pm.group(1))==0 or re.search(r'^not ok',out,re.M): return f,False
    if re.search(r'^ok \d+ # skip',out,re.M) and len(re.findall(r'^ok \d+ # skip',out,re.M))>=int(pm.group(1)): return f,False
    return f,int(pm.group(1))==len(re.findall(r'^ok\b',out,re.M))
with cf.ThreadPoolExecutor(8) as ex: res=list(ex.map(run,files))
os.remove('test.pl'); os.remove('TEST')
subprocess.run(['git','clean','-fdq'])
new=[f for f,o in res if o and f not in cur]
print(' '.join(new))
if '--write' in sys.argv and new:
    mk=mk.replace('  op/die_except.t','  '+' '.join(new)+' \\\n  op/die_except.t',1)
    open(root+'/Makefile','w').write(mk)
