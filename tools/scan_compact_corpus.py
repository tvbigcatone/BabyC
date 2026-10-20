import os, subprocess, re, json, pathlib, time
root=pathlib.Path(__file__).resolve().parents[1]; exe=root/'src/babyc'; tests=root/'src/tests'; counter=re.compile(r'\[babyc-bytecode-vm-prod\] attempts=(\d+) runs=(\d+)')
def norm(s): return '\n'.join(l for l in s.replace('\r\n','\n').split('\n') if '[babyc-ir]' not in l and '[babyc-irb]' not in l and '[babyc]' not in l and '[babyc-vm]' not in l and '[babyc-cov]' not in l and '[babyc-bytecode-vm]' not in l and '[babyc-exec-profile]' not in l and '[babyc-bytecode-vm-prod]' not in l).strip()
rows=[]; start=time.time()
for src in sorted(tests.glob('*.c')):
 exp=src.with_suffix('.expect')
 if not exp.exists(): continue
 env=os.environ.copy(); env.update(BABYC_EXEC='vm',BABYC_BYTECODE_VM='1',BABYC_VM_PROFILE='1')
 args=[str(exe)] + (['--analyze'] if 'analyze' in src.name else []) + [src.name]
 if src.name.startswith('032_args') or src.name.startswith('078_args_double_ptr'): args += ['-','arg1','arg2','arg3','arg4']
 try: p=subprocess.run(args,cwd=tests,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=20)
 except Exception as e: rows.append(dict(test=src.name,ok=False,fail=str(e))); continue
 actual=p.stdout.decode('latin1',errors='replace'); expected=exp.read_text(encoding='latin1'); m=counter.search(actual); a,r=map(int,m.groups()) if m else (0,0)
 rows.append(dict(test=src.name,ok=norm(actual)==norm(expected),rc=p.returncode,expected=norm(expected),actual=norm(actual),attempts=a,runs=r))
print(json.dumps({'count':len(rows),'pass':sum(x.get('ok',False) for x in rows),'fail':sum(not x.get('ok',False) for x in rows),'direct':sum(x.get('runs',0)>0 for x in rows),'attempts':sum(x.get('attempts',0)>0 for x in rows),'elapsed':time.time()-start,'failures':[x for x in rows if not x.get('ok',False)]},indent=2))
