#!/usr/bin/env python3
"""roast6: list the files that really pass (plan matches, no failure, rc 0) and are not yet in OK_ROAST6 (Makefile).
usage: python3 test/newok6.py [--write]"""
import subprocess,glob,os,re,sys,concurrent.futures as cf
root=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
mk=open(root+'/Makefile').read()
m=re.search(r'OK_ROAST6\s*=(.*?)\n\n',mk,re.S)
cur=set(m.group(1).replace('\\\n',' ').split())
os.chdir(root+'/test/roast6')
files=sorted(glob.glob('**/*.t',recursive=True))
def run(f):
    try: r=subprocess.run([root+'/bin/p2','-6',f],capture_output=True,timeout=20,text=True,errors='replace',stdin=subprocess.DEVNULL)
    except subprocess.TimeoutExpired: return f,False
    out=r.stdout
    pm=re.search(r'^1\.\.(\d+)',out,re.M)
    if r.returncode or not pm or int(pm.group(1))==0 or re.search(r'^not ok',out,re.M): return f,False
    return f,int(pm.group(1))==len(re.findall(r'^ok\b',out,re.M))
with cf.ThreadPoolExecutor(8) as ex: res=list(ex.map(run,files))
subprocess.run(['git','clean','-fdq'])
new=[f for f,o in res if o and f not in cur]
print(' '.join(new))
if '--write' in sys.argv and new:
    mk=mk.replace('OK_ROAST6 =','OK_ROAST6 = '+' '.join(new)+' \\\n ',1)
    open(root+'/Makefile','w').write(mk)
