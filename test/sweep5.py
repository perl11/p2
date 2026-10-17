#!/usr/bin/env python3
"""roast5 sweep: run every test/roast5/**/*.t through bin/p2 and bucket the failures.

usage: python3 test/sweep5.py [-j N] [-t SECONDS] [-n TOP]
Prints the number of files that pass (exit 0, no "not ok") and the most common failure
messages with one example file each. test/roast5 is cleaned of the files the tests leave."""
import subprocess, glob, re, os, sys, collections, concurrent.futures as cf
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
jobs, tmo, top = 8, 15, 25
a = sys.argv[1:]
while a:
    o = a.pop(0)
    if o == '-j': jobs = int(a.pop(0))
    elif o == '-t': tmo = int(a.pop(0))
    elif o == '-n': top = int(a.pop(0))
os.chdir(root+'/test/roast5')
if not os.path.exists('test.pl'): os.symlink('../p5/test.pl','test.pl')
if not os.path.exists('TEST'): os.symlink('../p5/test.pl','TEST')  # cmd/mod.t reads t/TEST
def run(f):
    try:
        r=subprocess.run([root+'/bin/p2',f],capture_output=True,timeout=tmo,text=True,errors='replace')
        return f,r.returncode,r.stdout,r.stderr
    except subprocess.TimeoutExpired: return f,'TO','',''
files=sorted(glob.glob('**/*.t',recursive=True))
res=[]
with cf.ThreadPoolExecutor(jobs) as ex: res=list(ex.map(run,files))
os.remove('test.pl'); os.remove('TEST')
subprocess.run(['git','clean','-fdq'],cwd=root+'/test/roast5')
ok=0;b=collections.Counter();ex={}
for f,rc,out,err in res:
    nok=len(re.findall(r'^not ok',out,re.M))
    if rc==0 and nok==0: ok+=1; continue
    line=(err.strip().splitlines() or [''])[0] if rc!=0 else 'not ok'
    k=re.sub(r'at \S+ line \d+','',line); k=re.sub(r'"[^"]*"','"…"',k); k=re.sub(r'\d+','N',k)[:90]
    if rc=='TO': k='TIMEOUT'
    b[k]+=1; ex.setdefault(k,f)
print('pass',ok,'of',len(files))
for k,n in b.most_common(top): print(n,k,'|',ex[k])
