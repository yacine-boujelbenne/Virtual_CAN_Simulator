import concurrent.futures,copy,json,pathlib,subprocess,sys,tempfile,time,unittest

PACK=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(PACK/'tools'))
from coordinate import Coordinator,run_git

class Clock:
    def __init__(self):self.now=1000.
    def __call__(self):return self.now

FAKE='''import json,pathlib,subprocess
p=pathlib.Path('.handoff/ticket.json');t=json.loads(p.read_text())
if t['phase']=='implement':
 f=pathlib.Path(t['task']['write_scopes'][0]);f.parent.mkdir(parents=True,exist_ok=True);f.write_text('implemented '+t['task_id']+'\\n')
 subprocess.run(['git','add',str(f)],check=True)
 subprocess.run(['git','commit','-m',t['task_id']],check=True,stdout=subprocess.DEVNULL)
 sha=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();outcome='ready_for_review'
else:sha=t['candidate_commit'];outcome='approve'
pathlib.Path(t['report_path']).write_text(json.dumps({'task_id':t['task_id'],'claim_token':t['claim_token'],'outcome':outcome,'candidate_commit':sha,'summary':'Synthetic provider test only','tests':[]}))
'''

class CoordinatorTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=pathlib.Path(self.tmp.name)/'repo';self.root.mkdir();self.clock=Clock()
        run_git(self.root,'init','-b','feature');run_git(self.root,'config','user.email','test@example.invalid');run_git(self.root,'config','user.name','Coordinator Test')
        (self.root/'initial.txt').write_text('baseline\n');run_git(self.root,'add','.');run_git(self.root,'commit','-m','baseline')
        self.config={'integration_branch':'work/native-cpp','lease_seconds':30,'provider_cooldown_seconds':60,'check_timeout_seconds':10,'max_attempts_per_task':4,
          'dispatch_order':['opencode','codex','copilot'],'providers':{p:{'enabled':True,'verified_local':True,'mode':'cli','capabilities':['cpp','review'],
          'command':[sys.executable,'-c',FAKE],'timeout_seconds':2} for p in ('opencode','copilot','codex')}}
        self.config_path=pathlib.Path(self.tmp.name)/'providers.json';self.save_config()
        self.c=Coordinator(self.root,self.config_path,self.clock);self.addCleanup(self.c.close)
        self.board={'tasks':[self.task('TEST-01','src/one.cpp')]};self.c.init(self.board)

    def save_config(self):self.config_path.write_text(json.dumps(self.config))
    def task(self,id,path,deps=None,checks=None):
        return {'id':id,'title':'synthetic test','depends_on':deps or [],'capabilities':['cpp'],'review_capabilities':['review'],
            'preferred':['opencode','codex','copilot'],'write_scopes':[path],'acceptance':['fixture only'],
            'checks':checks if checks is not None else [[sys.executable,'-c','from pathlib import Path; assert Path("initial.txt").read_text()=="baseline\\n"']], 'priority':1}
    def report(self,ticket,outcome,sha=None):
        return {'task_id':ticket['task_id'],'claim_token':ticket['claim_token'],'outcome':outcome,'candidate_commit':sha or ticket['candidate_commit'],'summary':'fixture evidence'}
    def commit(self,ticket,path=None):
        w=pathlib.Path(ticket['worktree']);p=w/(path or ticket['task']['write_scopes'][0]);p.parent.mkdir(parents=True,exist_ok=True);p.write_text('change\n')
        run_git(w,'add',str(p.relative_to(w)));run_git(w,'commit','-m',ticket['task_id']);return run_git(w,'rev-parse','HEAD')
    def submit_candidate(self):
        t=self.c.claim('opencode');sha=self.commit(t);self.c.submit(t['task_id'],t['claim_token'],self.report(t,'ready_for_review',sha));return t,sha
    def approved(self):
        t,sha=self.submit_candidate();r=self.c.claim('codex');self.c.submit(r['task_id'],r['claim_token'],self.report(r,'approve'));return t,sha

    def test_independent_review_checks_and_fast_forward(self):
        t,sha=self.approved();result=self.c.integrate(t['task_id'])
        self.assertEqual(result['status'],'done');self.assertEqual(run_git(self.c.home/'integration','rev-parse','HEAD'),sha)
        self.assertEqual(run_git(self.root,'branch','--show-current'),'feature')

    def test_author_cannot_claim_own_review(self):
        self.submit_candidate();self.assertIsNone(self.c.claim('opencode'))

    def test_unmet_dependency_waits(self):
        self.board['tasks'].append(self.task('TEST-02','src/two.cpp',['TEST-01']));self.c.init(self.board)
        first=self.c.claim('opencode');self.assertEqual(first['task_id'],'TEST-01');self.assertIsNone(self.c.claim('codex'))

    def test_scope_overlap_waits_but_independent_work_proceeds(self):
        self.board['tasks'] += [self.task('TEST-02','src/one.cpp'),self.task('TEST-03','ui/panel.cpp')];self.c.init(self.board)
        self.c.claim('opencode');second=self.c.claim('codex');self.assertEqual(second['task_id'],'TEST-03')

    def test_expired_token_fenced_and_worktree_preserved(self):
        old=self.c.claim('opencode');self.clock.now+=31
        new=self.c.claim('codex');self.assertNotEqual(old['claim_token'],new['claim_token'])
        self.assertNotEqual(old['worktree'],new['worktree']);self.assertTrue(pathlib.Path(old['worktree']).exists())
        with self.assertRaisesRegex(ValueError,'Stale'):self.c.submit(old['task_id'],old['claim_token'],self.report(old,'partial'))

    def test_heartbeat_preserves_lease(self):
        t=self.c.claim('opencode');self.clock.now+=20;self.c.heartbeat(t['task_id'],t['claim_token']);self.clock.now+=20
        self.c.leased(t['task_id'],t['claim_token'])

    def test_out_of_scope_commit_rejected(self):
        t=self.c.claim('opencode');sha=self.commit(t,'LAW.md')
        with self.assertRaisesRegex(ValueError,'outside'):self.c.submit(t['task_id'],t['claim_token'],self.report(t,'ready_for_review',sha))

    def test_dirty_tree_cannot_be_submitted(self):
        t=self.c.claim('opencode');sha=self.commit(t);(pathlib.Path(t['worktree'])/'uncommitted.txt').write_text('preserve')
        with self.assertRaisesRegex(ValueError,'Uncommitted'):self.c.submit(t['task_id'],t['claim_token'],self.report(t,'ready_for_review',sha))

    def test_multiple_commits_require_bounded_reslice(self):
        t=self.c.claim('opencode');self.commit(t);w=pathlib.Path(t['worktree']);(w/'src/one.cpp').write_text('another\n')
        run_git(w,'add','.');run_git(w,'commit','-m','extra');sha=run_git(w,'rev-parse','HEAD')
        with self.assertRaisesRegex(ValueError,'Exactly one'):self.c.submit(t['task_id'],t['claim_token'],self.report(t,'ready_for_review',sha))

    def test_reviewer_moving_head_is_rejected(self):
        self.submit_candidate();r=self.c.claim('codex');self.commit(r,'reviewer_change.txt')
        with self.assertRaisesRegex(ValueError,'moved'):self.c.submit(r['task_id'],r['claim_token'],self.report(r,'approve'))

    def test_trusted_failure_does_not_move_integration(self):
        task=self.task('FAIL-01','src/fail.cpp',checks=[[sys.executable,'-c','raise SystemExit(3)']])
        self.c.init({'tasks':self.board['tasks']+[task]});t=self.c.claim('opencode',task_id='FAIL-01');sha=self.commit(t)
        self.c.submit(t['task_id'],t['claim_token'],self.report(t,'ready_for_review',sha));r=self.c.claim('codex',phase='review')
        self.c.submit(r['task_id'],r['claim_token'],self.report(r,'approve'));before=run_git(self.c.home/'integration','rev-parse','HEAD')
        self.assertEqual(self.c.integrate(t['task_id'])['status'],'blocked');self.assertEqual(before,run_git(self.c.home/'integration','rev-parse','HEAD'))

    def test_advanced_integration_requires_new_base_and_review(self):
        t,sha=self.approved();w=self.c.home/'integration';(w/'other.txt').write_text('independent')
        run_git(w,'add','.');run_git(w,'commit','-m','other accepted work')
        r=self.c.integrate(t['task_id']);self.assertEqual(r['reason'],'rebase_required');self.assertEqual(self.c.task(t['task_id'])['candidate'],sha)

    def test_quota_failure_fails_over_and_other_provider_reviews(self):
        self.config['providers']['opencode']['command']=[sys.executable,'-c','import sys; print("429 quota exhausted"); sys.exit(7)'];self.save_config()
        self.assertEqual(self.c.step()['status'],'provider_failed')
        self.assertEqual(self.c.step()['provider'],'codex')
        review=self.c.step();self.assertEqual(review['provider'],'copilot');self.assertEqual(review['status'],'approved')
        self.assertEqual(self.c.step()['status'],'done')

    def test_missing_executable_is_not_a_code_failure(self):
        self.config['providers']['opencode']['command']=['no-such-agent-ecu-lab'];self.save_config()
        self.assertEqual(self.c.step()['status'],'provider_unavailable');self.assertEqual(self.c.status()['providers'][2]['name'],'opencode')
        self.assertEqual(self.c.step()['provider'],'codex')

    def test_provider_timeout_preserves_attempt_and_continues(self):
        self.config['providers']['opencode'].update(command=[sys.executable,'-c','import time; time.sleep(4)'],timeout_seconds=.1);self.save_config()
        first=self.c.step();self.assertEqual(first['status'],'interrupted')
        self.assertEqual(self.c.step()['provider'],'codex')

    def test_only_author_available_waits_for_independent_review(self):
        self.submit_candidate()
        self.c.pause('codex',100);self.c.pause('copilot',100)
        self.assertEqual(self.c.step()['status'],'waiting')

    def test_stop_file_prevents_new_call(self):
        (self.c.home/'STOP').write_text('stop')
        self.assertEqual(self.c.step()['status'],'stopped');self.assertEqual(self.c.task('TEST-01')['attempts'],0)

    def test_progress_survives_new_coordinator_instance(self):
        t=self.c.claim('opencode');self.c.export_progress()
        other=Coordinator(self.root,self.config_path,self.clock)
        try:self.assertEqual(other.task('TEST-01')['token'],t['claim_token'])
        finally:other.close()
        self.assertTrue((self.c.home/'progress/OPENCODE.md').exists())

    def test_pause_expiry_and_auth_resume(self):
        self.c.pause('opencode',10);self.assertIsNone(self.c.claim('opencode'));self.clock.now+=11
        self.assertIsNotNone(self.c.claim('opencode'))
        self.c.pause('codex',1,state='auth_blocked');self.clock.now+=2;self.assertFalse(self.c.available('codex'))
        self.c.resume('codex');self.assertTrue(self.c.available('codex'))

    def test_unverified_or_manual_provider_not_automatically_launched(self):
        for p in self.config['providers'].values():p['verified_local']=False
        self.save_config();self.assertEqual(self.c.step()['status'],'waiting')
        self.config['providers']['copilot'].update(verified_local=True,mode='manual');self.save_config()
        self.assertEqual(self.c.step()['status'],'waiting');self.assertIsNotNone(self.c.claim('copilot'))

    def test_dependency_cycles_and_mutated_task_contracts_rejected(self):
        altered=copy.deepcopy(self.board);altered['tasks'][0]['title']='Changed under an existing ID'
        with self.assertRaisesRegex(ValueError,'immutable'):self.c.init(altered)
        cycle={'tasks':[self.task('CYCLE-01','a',['CYCLE-02']),self.task('CYCLE-02','b',['CYCLE-01'])]}
        with self.assertRaisesRegex(ValueError,'cycle'):self.c.init(cycle)

    def test_concurrent_claims_do_not_own_same_scope(self):
        def claim(p):
            c=Coordinator(self.root,self.config_path,self.clock)
            try:return c.claim(p)
            finally:c.close()
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:rows=list(pool.map(claim,['opencode','codex']))
        self.assertEqual(sum(r is not None for r in rows),1)

if __name__=='__main__':unittest.main()
