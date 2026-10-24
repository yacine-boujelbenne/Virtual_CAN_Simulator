#!/usr/bin/env python3
"""Local, cooperative three-provider coordinator. Python is development tooling.

No remote push, model API, credential handling or universal permission bypass.
Each attempt has an isolated Git worktree; SQLite transactions fence leases.
"""
import argparse,contextlib,hashlib,json,os,pathlib,re,shutil,signal,sqlite3,subprocess,sys,time,uuid

PROVIDERS=('opencode','copilot','codex')
ACTIVE=('implementing','reviewing')

def run_git(root,*args):
    p=subprocess.run(['git','-C',str(root),*args],text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    if p.returncode:raise RuntimeError(p.stderr.strip() or 'git command failed')
    return p.stdout.strip()

def read_json(path):return json.loads(pathlib.Path(path).read_text())
def redact(s):
    s=re.sub(r'(sk-|ghp_|github_pat_)[A-Za-z0-9_-]+','[REDACTED]',s)
    return re.sub(r'(?i)Bearer\s+\S+','Bearer [REDACTED]',s)

class Coordinator:
    def __init__(self,root,config=None,clock=time.time):
        self.root=pathlib.Path(root).resolve();self.clock=clock
        self.home=self.root/'.orchestrator';self.home.mkdir(exist_ok=True)
        self.config_path=pathlib.Path(config).resolve() if config else self.root/'coordination/providers.local.json'
        if not self.config_path.exists():self.config_path=self.root/'coordination/providers.example.json'
        self.db=sqlite3.connect(self.home/'state.sqlite3',timeout=30,isolation_level=None)
        self.db.row_factory=sqlite3.Row
        self.db.execute('PRAGMA journal_mode=WAL');self.db.execute('PRAGMA synchronous=FULL')
        self.db.executescript('''
          CREATE TABLE IF NOT EXISTS tasks(id TEXT PRIMARY KEY,payload TEXT NOT NULL,status TEXT NOT NULL,
            owner TEXT,token TEXT,expires REAL,author TEXT,candidate TEXT,base TEXT,worktree TEXT,
            attempts INTEGER NOT NULL DEFAULT 0,summary TEXT NOT NULL DEFAULT '');
          CREATE TABLE IF NOT EXISTS providers(name TEXT PRIMARY KEY,state TEXT NOT NULL DEFAULT 'ready',
            until_ts REAL NOT NULL DEFAULT 0,error TEXT NOT NULL DEFAULT '');
          CREATE TABLE IF NOT EXISTS events(seq INTEGER PRIMARY KEY AUTOINCREMENT,at REAL,kind TEXT,data TEXT);
        ''')
        for p in PROVIDERS:self.db.execute('INSERT OR IGNORE INTO providers(name) VALUES(?)',(p,))

    def close(self):self.db.close()
    def config(self):return read_json(self.config_path)
    @contextlib.contextmanager
    def tx(self):
        self.db.execute('BEGIN IMMEDIATE')
        try:yield;self.db.execute('COMMIT')
        except BaseException:self.db.execute('ROLLBACK');raise
    def event(self,kind,**data):
        self.db.execute('INSERT INTO events(at,kind,data) VALUES(?,?,?)',(self.clock(),kind,json.dumps(data,sort_keys=True)))
    def task(self,identifier):
        row=self.db.execute('SELECT * FROM tasks WHERE id=?',(identifier,)).fetchone()
        if row is None:raise ValueError('Unknown task: '+identifier)
        return dict(row)

    def init(self,board=None):
        cfg=self.config();board=board or read_json(self.root/'coordination/tasks.json')
        ids={x['id'] for x in board['tasks']}
        if len(ids)!=len(board['tasks']):raise ValueError('Duplicate task ID')
        graph={x['id']:x['depends_on'] for x in board['tasks']}
        def visit(k,trail):
            if k in trail:raise ValueError('Task dependency cycle')
            if k not in ids:raise ValueError('Unknown task dependency')
            for dep in graph[k]:visit(dep,trail|{k})
        for t in board['tasks']:
            visit(t['id'],set())
            if not re.fullmatch(r'[A-Z][A-Z0-9-]{1,40}',t['id']):raise ValueError('Unsafe task ID')
            if not t['write_scopes']:raise ValueError('Task needs explicit write scopes')
            for scope in t['write_scopes']:
                if scope.startswith('/') or '..' in pathlib.PurePosixPath(scope).parts or '\\' in scope or not scope:
                    raise ValueError('Unsafe write scope')
            if any(not isinstance(c,list) or not c or not all(isinstance(a,str) for a in c) for c in t['checks']):
                raise ValueError('Checks must be trusted argv arrays, never shell text')
        gitdir=pathlib.Path(run_git(self.root,'rev-parse','--absolute-git-dir'))
        exclude=gitdir/'info/exclude';exclude.parent.mkdir(exist_ok=True)
        existing=exclude.read_text() if exclude.exists() else ''
        patterns=['.orchestrator/','.handoff/','coordination/providers.local.json']
        exclude.write_text(existing+''.join('\n'+p for p in patterns if p not in existing.splitlines())+'\n')
        branch=cfg.get('integration_branch','work/native-cpp')
        if not branch.startswith('work/'):raise ValueError('Local integration branch must begin work/')
        refs=run_git(self.root,'for-each-ref','--format=%(refname:short)','refs/heads').splitlines()
        if branch not in refs:run_git(self.root,'branch',branch,'HEAD')
        integration=self.home/'integration'
        if not integration.exists():run_git(self.root,'worktree','add',str(integration),branch)
        if run_git(integration,'branch','--show-current')!=branch:raise RuntimeError('Wrong integration worktree')
        with self.tx():
            for t in board['tasks']:
                payload=json.dumps(t,sort_keys=True)
                old=self.db.execute('SELECT payload FROM tasks WHERE id=?',(t['id'],)).fetchone()
                if old and old['payload']!=payload:raise ValueError('Existing task is immutable; add a revised task ID: '+t['id'])
                self.db.execute('INSERT OR IGNORE INTO tasks(id,payload,status) VALUES(?,?,?)',(t['id'],payload,'ready'))
            self.event('initialized',integration_branch=branch,tasks=len(ids),base=run_git(integration,'rev-parse','HEAD'))
        self.export_progress();return {'tasks':len(ids),'integration_branch':branch,'integration_worktree':str(integration)}

    def expire(self):
        with self.tx():
            for row in self.db.execute("SELECT * FROM tasks WHERE status IN ('implementing','reviewing') AND expires<=?",(self.clock(),)).fetchall():
                status='ready' if row['status']=='implementing' else 'ready_review'
                self.event('lease_expired',task=row['id'],owner=row['owner'],quarantined_worktree=row['worktree'])
                self.db.execute('UPDATE tasks SET status=?,owner=NULL,token=NULL,expires=NULL,summary=? WHERE id=?',
                    (status,'Lease expired; old branch/worktree preserved; old token fenced',row['id']))

    def available(self,provider,automatic=False):
        cfg=self.config()['providers'].get(provider,{})
        p=dict(self.db.execute('SELECT * FROM providers WHERE name=?',(provider,)).fetchone())
        if not cfg.get('enabled',False):return False
        if automatic and (cfg.get('mode')!='cli' or not cfg.get('verified_local',False)):return False
        if p['state'] in ('paused','auth_blocked','missing'):return False
        return p['until_ts']<=self.clock()

    def claim(self,provider,automatic=False,phase=None,task_id=None):
        if provider not in PROVIDERS:raise ValueError('Unknown provider')
        self.expire()
        with self.tx():
            if not self.available(provider,automatic):return None
            if self.db.execute("SELECT 1 FROM tasks WHERE owner=? AND status IN ('implementing','reviewing')",(provider,)).fetchone():return None
            cfg=self.config();caps=set(cfg['providers'][provider].get('capabilities',[]))
            rows=[dict(x) for x in self.db.execute("SELECT * FROM tasks WHERE status IN ('ready_review','ready')")]
            rows.sort(key=lambda x:(0 if x['status']=='ready_review' else 1,json.loads(x['payload'])['priority'],
                                    json.loads(x['payload']).get('preferred',[]).index(provider) if provider in json.loads(x['payload']).get('preferred',[]) else 99,x['id']))
            for t in rows:
                task=json.loads(t['payload']);review=t['status']=='ready_review'
                if task_id and t['id']!=task_id:continue
                if phase and phase!=('review' if review else 'implement'):continue
                if not review and t['attempts']>=cfg.get('max_attempts_per_task',4):
                    self.db.execute("UPDATE tasks SET status='blocked',summary='Implementation attempt budget reached' WHERE id=?",(t['id'],))
                    self.event('task_budget_reached',task=t['id']);continue
                needed=set(task['review_capabilities'] if review else task['capabilities'])
                if not needed<=caps or (review and t['author']==provider):continue
                if any(self.task(dep)['status']!='done' for dep in task['depends_on']):continue
                if not review:
                    occupied=[]
                    for other in self.db.execute("SELECT payload FROM tasks WHERE status='implementing'"):
                        occupied+=json.loads(other['payload'])['write_scopes']
                    if any(a==b or (a.endswith('/') and b.startswith(a)) or (b.endswith('/') and a.startswith(b))
                           for a in task['write_scopes'] for b in occupied):continue
                token=uuid.uuid4().hex;phase='review' if review else 'implement'
                location=self.home/'worktrees'/f"{t['id']}-{phase}-{token[:10]}";location.parent.mkdir(exist_ok=True)
                base=t['base'] if review else run_git(self.home/'integration','rev-parse','HEAD')
                if review:run_git(self.root,'worktree','add','--detach',str(location),t['candidate'])
                else:run_git(self.root,'worktree','add','-b',f"work/{provider}/{t['id'].lower()}-{token[:10]}",str(location),base)
                expires=self.clock()+cfg.get('lease_seconds',180)
                self.db.execute('UPDATE tasks SET status=?,owner=?,token=?,expires=?,author=?,base=?,worktree=?,attempts=attempts+?,summary=? WHERE id=?',
                    ('reviewing' if review else 'implementing',provider,token,expires,t['author'] if review else provider,base,str(location),0 if review else 1,'Claimed '+phase,t['id']))
                ticket={'task_id':t['id'],'phase':phase,'provider':provider,'claim_token':token,'base_commit':base,
                    'candidate_commit':t['candidate'],'previous_candidate':t['candidate'] if not review else None,
                    'worktree':str(location),'task':task,'report_path':str(location/'.handoff/report.json'),
                    'control_root':str(self.root),'lease_expires_unix':expires}
                handoff=location/'.handoff';handoff.mkdir()
                (handoff/'ticket.json').write_text(json.dumps(ticket,indent=2)+'\n')
                (handoff/'team-status.md').write_text(self.status_text())
                self.event('claimed',task=t['id'],phase=phase,provider=provider,token=token,worktree=str(location),base=base)
                return ticket
        return None

    def leased(self,identifier,token):
        t=self.task(identifier)
        if t['status'] not in ACTIVE or t['token']!=token or t['expires']<=self.clock():raise ValueError('Stale/expired task token; do not resume this worktree')
        return t
    def heartbeat(self,identifier,token):
        with self.tx():
            self.leased(identifier,token)
            self.db.execute('UPDATE tasks SET expires=? WHERE id=?',(self.clock()+self.config().get('lease_seconds',180),identifier))

    def pause(self,provider,seconds=900,reason='operator pause',state='cooldown'):
        with self.tx():
            self.db.execute('UPDATE providers SET state=?,until_ts=?,error=? WHERE name=?',(state,self.clock()+seconds,redact(reason)[:500],provider))
            self.event('provider_paused',provider=provider,state=state,reason=redact(reason)[:500])
    def resume(self,provider):
        with self.tx():
            self.db.execute("UPDATE providers SET state='ready',until_ts=0,error='' WHERE name=?",(provider,));self.event('provider_resumed',provider=provider)

    def submit(self,identifier,token,report):
        with self.tx():
            t=self.leased(identifier,token);task=json.loads(t['payload'])
            if report.get('task_id')!=identifier or report.get('claim_token')!=token:raise ValueError('Report correlation mismatch')
            outcome=report.get('outcome');summary=str(report.get('summary',''))[:2500]
            if outcome in ('partial','capacity','blocked'):
                new='blocked' if outcome=='blocked' else ('ready_review' if t['status']=='reviewing' else 'ready')
                self.db.execute('UPDATE tasks SET status=?,owner=NULL,token=NULL,expires=NULL,summary=? WHERE id=?',(new,summary,identifier))
                if outcome=='capacity':self.db.execute("UPDATE providers SET state='cooldown',until_ts=?,error=? WHERE name=?",(self.clock()+900,summary[:500],t['owner']))
                self.event('handoff',task=identifier,provider=t['owner'],outcome=outcome,worktree=t['worktree'],summary=summary)
                return {'status':new}
            if t['status']=='reviewing':
                if report.get('candidate_commit')!=t['candidate']:raise ValueError('Review candidate mismatch')
                if t['owner']==t['author']:raise ValueError('Self-review forbidden')
                if run_git(t['worktree'],'rev-parse','HEAD')!=t['candidate']:raise ValueError('Reviewer moved away from the candidate')
                if run_git(t['worktree'],'status','--porcelain'):raise ValueError('Reviewer changed source; review must be read-only')
                if outcome not in ('approve','reject'):raise ValueError('Review must approve/reject the exact candidate')
                new='approved' if outcome=='approve' else 'ready'
                self.db.execute('UPDATE tasks SET status=?,owner=NULL,token=NULL,expires=NULL,summary=? WHERE id=?',(new,summary,identifier))
                self.event('reviewed',task=identifier,reviewer=t['owner'],author=t['author'],candidate=t['candidate'],outcome=outcome)
                return {'status':new}
            if outcome!='ready_for_review':raise ValueError('Implementation outcome must be ready_for_review')
            sha=str(report.get('candidate_commit',''))
            if not re.fullmatch(r'[a-f0-9]{40,64}',sha):raise ValueError('Commit SHA required')
            if run_git(t['worktree'],'rev-parse','HEAD')!=sha:raise ValueError('Candidate is not worktree HEAD')
            if run_git(t['worktree'],'rev-parse',sha+'^')!=t['base']:raise ValueError('Exactly one commit on the leased base is required')
            if run_git(t['worktree'],'status','--porcelain'):raise ValueError('Uncommitted work remains; checkpoint it first')
            paths=run_git(t['worktree'],'diff','--name-only',t['base'],sha).splitlines()
            if not paths:raise ValueError('Empty implementation commit')
            for path in paths:
                if not any(path==scope or (scope.endswith('/') and path.startswith(scope)) for scope in task['write_scopes']):
                    raise ValueError('Change outside leased scope: '+path)
            self.db.execute("UPDATE tasks SET status='ready_review',candidate=?,owner=NULL,token=NULL,expires=NULL,summary=? WHERE id=?",(sha,summary,identifier))
            self.event('submitted',task=identifier,author=t['owner'],candidate=sha,paths=paths,summary=summary)
            return {'status':'ready_review','candidate':sha}

    def integrate(self,identifier):
        # Expensive checks stay outside the DB lock so other leases can renew.
        t=self.task(identifier)
        if t['status']!='approved':raise ValueError('Independent approval required')
        if run_git(t['worktree'],'rev-parse','HEAD')!=t['candidate']:raise ValueError('Check worktree is not the approved candidate')
        if run_git(t['worktree'],'status','--porcelain'):raise ValueError('Candidate checkout is dirty')
        task=json.loads(t['payload']);evidence=[]
        for index,argv in enumerate(task['checks']):
            try:
                result=subprocess.run(argv,cwd=t['worktree'],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                                      timeout=self.config().get('check_timeout_seconds',300))
                code=result.returncode;output=result.stdout
            except (subprocess.TimeoutExpired,OSError) as exc:code=-1;output=str(exc)
            path=self.home/'checks'/f'{identifier}-{t["candidate"][:12]}-{index}.log';path.parent.mkdir(exist_ok=True)
            path.write_text(redact(output));evidence.append({'argv':argv,'returncode':code,'log':str(path)})
            if code:
                with self.tx():
                    current=self.task(identifier)
                    if current['status']!='approved' or current['candidate']!=t['candidate']:raise ValueError('Task changed during checks')
                    self.db.execute("UPDATE tasks SET status='blocked',summary='Trusted check failed; see coordinator evidence' WHERE id=?",(identifier,))
                    self.event('check_failed',task=identifier,evidence=evidence)
                return {'status':'blocked','checks':evidence}
        if run_git(t['worktree'],'status','--porcelain'):raise ValueError('Checks modified source; integration refused')
        # Only the final fast-forward and state transition hold the writer lock.
        with self.tx():
            current=self.task(identifier)
            if current['status']!='approved' or current['candidate']!=t['candidate']:raise ValueError('Task changed during checks')
            integration=self.home/'integration'
            if run_git(integration,'status','--porcelain'):raise ValueError('Integration checkout is dirty; preserved for inspection')
            if run_git(integration,'rev-parse','HEAD')!=t['base']:
                self.db.execute("UPDATE tasks SET status='ready',summary='Integration head advanced; recreate/review on fresh base' WHERE id=?",(identifier,))
                self.event('rebase_required',task=identifier,candidate=t['candidate']);return {'status':'ready','reason':'rebase_required'}
            run_git(integration,'merge','--ff-only',t['candidate'])
            self.db.execute("UPDATE tasks SET status='done',summary=? WHERE id=?",(t['summary'],identifier))
            self.event('integrated',task=identifier,candidate=t['candidate'],checks=evidence)
            return {'status':'done','commit':t['candidate'],'checks':evidence}

    def unblock(self,identifier,reason):
        with self.tx():
            t=self.task(identifier)
            if t['status']!='blocked':raise ValueError('Only blocked tasks may be unblocked')
            self.db.execute("UPDATE tasks SET status='ready',summary=? WHERE id=?",(reason,identifier));self.event('unblocked',task=identifier,reason=reason)

    def status(self):
        return {'tasks':[dict(x) for x in self.db.execute('SELECT id,status,owner,author,candidate,attempts,summary FROM tasks ORDER BY id')],
                'providers':[dict(x) for x in self.db.execute('SELECT * FROM providers ORDER BY name')]}
    def status_text(self):
        d=self.status();lines=['# Shared team status','',f'Generated at Unix time {self.clock():.3f}. SQLite is authoritative.','',
            '| Task | Status | Owner | Author | Attempts |','| --- | --- | --- | --- | --- |']
        for t in d['tasks']:lines.append(f"| {t['id']} | {t['status']} | {t['owner'] or '-'} | {t['author'] or '-'} | {t['attempts']} |")
        lines+=['','## Provider availability','']
        for p in d['providers']:lines.append(f"- {p['name']}: {p['state']}; cooldown until {p['until_ts']}; {p['error']}")
        return '\n'.join(lines)+'\n'
    def export_progress(self):
        folder=self.home/'progress';folder.mkdir(exist_ok=True)
        status=self.status_text();(self.home/'STATUS.md').write_text(status)
        for p in PROVIDERS:
            events=[]
            for e in self.db.execute('SELECT * FROM events ORDER BY seq DESC LIMIT 300'):
                data=json.loads(e['data'])
                if p in (data.get('provider'),data.get('owner'),data.get('author'),data.get('reviewer')):
                    events.append(f"- event {e['seq']} / {e['kind']}: {json.dumps(data,sort_keys=True)}")
            (folder/(p.upper()+'.md')).write_text(f'# {p.upper()} live progress\n\n'+status+'\n## Recent handoffs/evidence\n\n'+'\n'.join(events[:30])+'\n')

    def failure(self,ticket,reason,kind='cooldown'):
        with self.tx():
            try:t=self.leased(ticket['task_id'],ticket['claim_token'])
            except ValueError:return
            exhausted=ticket['phase']=='implement' and t['attempts']>=self.config().get('max_attempts_per_task',4)
            new='blocked' if exhausted else ('ready_review' if ticket['phase']=='review' else 'ready')
            self.db.execute('UPDATE tasks SET status=?,owner=NULL,token=NULL,expires=NULL,summary=? WHERE id=?',(new,redact(reason)[:2000],t['id']))
            self.event('attempt_interrupted',task=t['id'],provider=t['owner'],reason=redact(reason)[:500],worktree=t['worktree'],status=new)
        self.pause(ticket['provider'],self.config().get('provider_cooldown_seconds',900),reason,state=kind)

    def dispatch(self,ticket,max_seconds=None):
        cfg=self.config()['providers'][ticket['provider']]
        role={'opencode':'OPENCODE.md','copilot':'COPILOT.md','codex':'CODEX.md'}[ticket['provider']]
        prompt=(f"Read LAW.md, {role}, .handoff/ticket.json and .handoff/team-status.md. "
                f"Do only task {ticket['task_id']} phase {ticket['phase']}. "
                "For implementation make exactly one scoped commit on the leased base. For review do not modify source. "
                "Write .handoff/report.json with task_id, claim_token, outcome, summary, candidate_commit and tests. "
                "Implementation outcome: ready_for_review/partial/blocked/capacity. Review: approve/reject/blocked/capacity. "
                "Do not self-integrate, push, edit policy, or infer tests passed. Preserve partial work before stopping.")
        command=cfg.get('review_command' if ticket['phase']=='review' else 'command') or cfg.get('command')
        if not command:raise ValueError('No configured command')
        argv=[x.replace('{prompt}',prompt) for x in command]
        if not shutil.which(argv[0]):self.failure(ticket,'Provider executable missing','missing');return {'status':'provider_unavailable'}
        log=self.home/'logs'/f"{ticket['task_id']}-{ticket['claim_token']}.log";log.parent.mkdir(exist_ok=True)
        timeout=cfg.get('timeout_seconds',600)
        if max_seconds is not None:timeout=min(timeout,max_seconds)
        started=time.monotonic();stop_reason=None
        with log.open('w') as output:
            proc=subprocess.Popen(argv,cwd=ticket['worktree'],stdin=subprocess.DEVNULL,stdout=output,stderr=subprocess.STDOUT,
                                  start_new_session=(os.name!='nt'))
            while proc.poll() is None:
                if (self.home/'STOP').exists():stop_reason='STOP file requested'
                if time.monotonic()-started>timeout:stop_reason='Provider attempt timed out'
                if output.tell()>self.config().get('max_log_bytes',5000000):stop_reason='Provider output budget reached'
                if stop_reason:
                    if os.name!='nt':os.killpg(proc.pid,signal.SIGTERM)
                    else:proc.terminate()
                    try:proc.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        if os.name!='nt':os.killpg(proc.pid,signal.SIGKILL)
                        else:proc.kill()
                        proc.wait()
                    break
                self.heartbeat(ticket['task_id'],ticket['claim_token']);time.sleep(.2)
        raw=log.read_text(errors='replace');log.write_text(redact(raw))
        if stop_reason:self.failure(ticket,stop_reason);return {'status':'interrupted','reason':stop_reason}
        if proc.returncode:
            reason=redact(raw[-2000:]) or f'Provider exit {proc.returncode}'
            kind='auth_blocked' if re.search(r'(?i)unauthori[sz]ed|authentication|permission denied|forbidden',reason) else 'cooldown'
            self.failure(ticket,reason,kind);return {'status':'provider_failed','kind':kind}
        report=pathlib.Path(ticket['report_path'])
        if not report.exists():self.failure(ticket,'Provider exited without structured report');return {'status':'missing_report'}
        try:return self.submit(ticket['task_id'],ticket['claim_token'],read_json(report))
        except (ValueError,RuntimeError,json.JSONDecodeError) as exc:
            self.failure(ticket,'Report rejected: '+str(exc));return {'status':'report_rejected','error':str(exc)}

    def step(self,deadline=None):
        if (self.home/'STOP').exists():return {'status':'stopped'}
        self.expire()
        for t in self.status()['tasks']:
            if t['status']=='approved':
                result=self.integrate(t['id']);self.export_progress();return {'task':t['id'],**result}
        order=self.config().get('dispatch_order',list(PROVIDERS));ticket=None
        for provider in order:
            ticket=self.claim(provider,automatic=True,phase='review')
            if ticket:break
        if not ticket:
            rows=[dict(x) for x in self.db.execute("SELECT * FROM tasks WHERE status='ready'")]
            rows.sort(key=lambda x:(json.loads(x['payload'])['priority'],x['id']))
            for row in rows:
                preferred=json.loads(row['payload']).get('preferred',[])
                for provider in dict.fromkeys(preferred+order):
                    ticket=self.claim(provider,automatic=True,phase='implement',task_id=row['id'])
                    if ticket:break
                if ticket:break
        if ticket:
            try:result=self.dispatch(ticket,None if deadline is None else max(.01,deadline-time.monotonic()))
            except (OSError,RuntimeError,ValueError) as exc:
                self.failure(ticket,str(exc));result={'status':'attempt_failed','error':str(exc)}
            self.export_progress();return {'task':ticket['task_id'],'provider':ticket['provider'],'phase':ticket['phase'],**result}
        self.export_progress();return {'status':'waiting','reason':'No enabled compatible CLI provider / ready task / independent reviewer'}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',default='.');parser.add_argument('--config')
    subs=parser.add_subparsers(dest='action',required=True)
    for name in ('init','status','doctor','step','export'):subs.add_parser(name)
    p=subs.add_parser('claim');p.add_argument('--provider',required=True,choices=PROVIDERS)
    for name in ('heartbeat','submit'):
        p=subs.add_parser(name);p.add_argument('--task',required=True);p.add_argument('--token',required=True)
        if name=='submit':p.add_argument('--report',required=True)
    p=subs.add_parser('integrate');p.add_argument('--task',required=True)
    p=subs.add_parser('unblock');p.add_argument('--task',required=True);p.add_argument('--reason',required=True)
    p=subs.add_parser('pause');p.add_argument('--provider',choices=PROVIDERS,required=True);p.add_argument('--seconds',type=int,default=900)
    p=subs.add_parser('resume');p.add_argument('--provider',choices=PROVIDERS,required=True)
    p=subs.add_parser('run');p.add_argument('--max-calls',type=int,default=6);p.add_argument('--max-wall-seconds',type=int,default=1800)
    p.add_argument('--watch',action='store_true');p.add_argument('--idle-seconds',type=float,default=10)
    a=parser.parse_args()
    if a.action=='doctor':
        print(json.dumps({'tools':{x:shutil.which(x) for x in ('git','python3','cmake','g++','docker','node','code','opencode','copilot','codex')},
            'authentication':'Not probed. Validate each installed CLI/account/version separately.','live_provider_execution':'Not established by tool presence.'},indent=2));return
    c=Coordinator(a.root,a.config)
    try:
        if a.action=='init':result=c.init()
        elif a.action=='status':result=c.status()
        elif a.action=='export':c.export_progress();result={'status':'exported'}
        elif a.action=='claim':result=c.claim(a.provider)
        elif a.action=='heartbeat':c.heartbeat(a.task,a.token);result={'status':'renewed'}
        elif a.action=='submit':result=c.submit(a.task,a.token,read_json(a.report));c.export_progress()
        elif a.action=='integrate':result=c.integrate(a.task);c.export_progress()
        elif a.action=='pause':c.pause(a.provider,a.seconds);result={'status':'cooldown'}
        elif a.action=='resume':c.resume(a.provider);result={'status':'ready'}
        elif a.action=='unblock':c.unblock(a.task,a.reason);result={'status':'ready'}
        elif a.action=='step':result=c.step()
        else:
            if a.max_calls<1 or a.max_wall_seconds<1 or a.idle_seconds<.2:raise ValueError('Positive bounded run settings required')
            calls=0;iterations=0;started=time.monotonic();result={'status':'budget_reached'}
            while calls<a.max_calls and time.monotonic()-started<a.max_wall_seconds:
                step=c.step(deadline=started+a.max_wall_seconds);iterations+=1;print(json.dumps(step),flush=True)
                if 'provider' in step:calls+=1
                if step['status']=='stopped':result=step;break
                if step['status']=='waiting':
                    if not a.watch:result=step;break
                    time.sleep(min(a.idle_seconds,max(0,a.max_wall_seconds-(time.monotonic()-started))))
            result={**result,'provider_calls':calls,'iterations':iterations}
        print(json.dumps(result,indent=2))
    finally:c.close()

if __name__=='__main__':
    try:main()
    except (ValueError,RuntimeError,OSError) as exc:
        print(json.dumps({'error':str(exc)}),file=sys.stderr);sys.exit(1)
