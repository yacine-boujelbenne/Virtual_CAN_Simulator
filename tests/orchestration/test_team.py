"""Team roles and real Git handoffs; no provider process or account is invoked."""
import concurrent.futures
import json
import pathlib
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

PACK = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PACK / 'tools'))
from coordinate import Coordinator, run_git


class TeamTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name) / 'repo'
        self.root.mkdir()
        run_git(self.root, 'init', '-b', 'feature')
        run_git(self.root, 'config', 'user.email', 'fixture@example.invalid')
        run_git(self.root, 'config', 'user.name', 'Team Fixture')
        (self.root / 'initial.txt').write_text('preserved baseline\n')
        run_git(self.root, 'add', '.')
        run_git(self.root, 'commit', '-m', 'baseline')
        self.now = 1000.
        self.config = {
            'integration_branch': 'work/native-cpp', 'lease_seconds': 30,
            'check_timeout_seconds': 10, 'max_attempts_per_task': 4,
            'providers': {p: {
                'enabled': True, 'verified_local': True, 'mode': 'cli',
                'capabilities': ['cpp', 'runtime', 'control', 'resources', 'contracts',
                                 'docs', 'tests', 'review', 'ui', 'world'],
                # A team scheduler must assign inboxes rather than launch this.
                'command': [sys.executable, '-c', 'raise SystemExit(99)'],
            } for p in ('opencode', 'copilot', 'codex')},
        }
        self.config_path = pathlib.Path(self.tmp.name) / 'providers.json'
        self.config_path.write_text(json.dumps(self.config))
        self.c = Coordinator(self.root, self.config_path, lambda: self.now)
        self.addCleanup(self.c.close)
        self.board = {'tasks': [self.task('TEST-01')]}
        self.c.init(self.board)

    @staticmethod
    def task(identifier, capabilities=None, checks=None):
        return {
            'id': identifier, 'title': 'synthetic team workflow', 'depends_on': [],
            'capabilities': capabilities or ['cpp'], 'review_capabilities': ['review'],
            'preferred': ['opencode', 'codex', 'copilot'],
            'write_scopes': ['src/' + identifier.lower() + '.cpp'],
            'acceptance': ['fixture only'], 'priority': 1,
            'checks': checks if checks is not None else [[sys.executable, '-c',
                'from pathlib import Path; assert Path("initial.txt").read_text()=="preserved baseline\\n"']],
        }

    def join(self, provider='codex', instance='leader', role=None):
        return self.c.join(provider, instance, role=role)['identity']

    def standard_team(self):
        leader = self.join()
        backend = self.join('opencode', 'backend', 'backend')
        tester = self.join('codex', 'tester', 'tester')
        reviewer = self.join('copilot', 'reviewer', 'reviewer')
        return leader, backend, tester, reviewer

    def report(self, ticket, outcome, candidate=None):
        return {'task_id': ticket['task_id'], 'claim_token': ticket['claim_token'],
                'outcome': outcome, 'candidate_commit': candidate or ticket['candidate_commit'],
                'summary': 'synthetic team evidence'}

    def candidate(self, leader, worker, identifier='TEST-01'):
        ticket = self.c.assign(leader, worker, task_id=identifier, phase='implement')
        self.assertIsNotNone(ticket)
        sha = self.commit(ticket)
        result = self.c.submit(identifier, ticket['claim_token'],
                               self.report(ticket, 'ready_for_review', sha))
        self.assertEqual(result['status'], 'ready_test')
        return ticket, sha

    def commit(self, ticket, author=None, message=None):
        checkout = pathlib.Path(ticket['worktree'])
        path = checkout / ticket['task']['write_scopes'][0]
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('implementation\n')
        run_git(checkout, 'add', str(path.relative_to(checkout)))
        argv = ['commit', '-m', message or ticket['task_id']]
        if author:
            argv.append('--author=' + author)
        run_git(checkout, *argv)
        return run_git(checkout, 'rev-parse', 'HEAD')

    def test_first_agent_is_persistent_orchestrator_and_join_is_idempotent(self):
        first = self.c.join('opencode', 'first', role='backend')
        self.assertEqual(first['role'], 'orchestrator')
        self.assertEqual(first['identity'], 'opencode#first')
        self.assertEqual(self.c.join('opencode', 'first'), first)
        self.join('codex', 'later', 'tester')
        other = Coordinator(self.root, self.config_path, lambda: self.now)
        try:
            self.assertEqual(other.team()['leader'], first['identity'])
            self.assertEqual(len(other.team()['agents']), 2)
        finally:
            other.close()

    def test_concurrent_first_join_elects_exactly_one_orchestrator(self):
        def join_one(instance):
            coordinator = Coordinator(self.root, self.config_path, lambda: self.now)
            try:
                return coordinator.join('codex', instance)
            finally:
                coordinator.close()
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            agents = list(pool.map(join_one, ('one', 'two')))
        leaders = [a for a in agents if a['role'] == 'orchestrator']
        self.assertEqual(len(leaders), 1)
        self.assertEqual(self.c.team()['leader'], leaders[0]['identity'])

    def test_default_roles_cover_backend_testing_review_and_design(self):
        self.join()
        roles = [self.c.join('opencode', 'worker-' + str(i))['role'] for i in range(5)]
        self.assertEqual(roles, ['backend', 'tester', 'reviewer', 'design', 'implementer'])

    def test_late_agent_cannot_elect_itself_or_change_leadership(self):
        leader = self.join()
        with self.assertRaises(ValueError):
            self.join('opencode', 'second', 'orchestrator')
        self.assertEqual(self.c.team()['leader'], leader)

    def test_direct_claim_and_nonleader_assignment_are_rejected(self):
        leader, backend, _, _ = self.standard_team()
        with self.assertRaises(ValueError):
            self.c.claim('opencode', instance='backend')
        with self.assertRaises(ValueError):
            self.c.assign(backend, leader, task_id='TEST-01')
        self.assertEqual(self.c.task('TEST-01')['attempts'], 0)

    def test_orchestrator_does_not_take_worker_tasks(self):
        leader = self.join()
        with self.assertRaises(ValueError):
            self.c.assign(leader, leader, task_id='TEST-01')
        self.assertEqual(self.c.task('TEST-01')['attempts'], 0)

    def test_worker_inbox_waits_then_returns_only_its_assignment(self):
        leader, backend, tester, _ = self.standard_team()
        self.assertEqual(self.c.inbox(backend)['status'], 'waiting')
        ticket = self.c.assign(leader, backend, task_id='TEST-01')
        inbox = self.c.inbox(backend)
        self.assertEqual(inbox['status'], 'assigned')
        self.assertEqual(inbox['ticket']['claim_token'], ticket['claim_token'])
        self.assertEqual(self.c.inbox(tester)['status'], 'waiting')
        self.assertIsNone(self.c.assign(leader, backend, task_id='TEST-01'))

    def test_backend_and_design_roles_route_by_product_area(self):
        self.board['tasks'].append(self.task('UI-01', ['cpp', 'ui']))
        self.c.init(self.board)
        leader, backend, tester, reviewer = self.standard_team()
        designer = self.join('opencode', 'designer', 'design')
        self.assertIsNone(self.c.assign(leader, backend, task_id='UI-01'))
        self.assertIsNone(self.c.assign(leader, designer, task_id='TEST-01'))
        self.assertIsNone(self.c.assign(leader, tester, task_id='TEST-01', phase='implement'))
        self.assertIsNone(self.c.assign(leader, reviewer, task_id='TEST-01', phase='implement'))
        self.assertEqual(self.c.assign(leader, designer, task_id='UI-01')['task_id'], 'UI-01')
        self.assertEqual(self.c.assign(leader, backend, task_id='TEST-01')['task_id'], 'TEST-01')

    def test_test_phase_cannot_be_replaced_by_review_or_prose_claim(self):
        leader, backend, tester, reviewer = self.standard_team()
        _, sha = self.candidate(leader, backend)
        self.assertIsNone(self.c.assign(leader, reviewer, task_id='TEST-01', phase='review'))
        ticket = self.c.assign(leader, tester, task_id='TEST-01', phase='test')
        self.assertEqual(ticket['candidate_commit'], sha)
        self.assertEqual(self.c.task('TEST-01')['status'], 'testing')
        self.assertEqual(run_git(ticket['worktree'], 'branch', '--show-current'), '')
        for outcome in ('approve', 'pass'):
            with self.assertRaises(ValueError):
                self.c.submit('TEST-01', ticket['claim_token'], self.report(ticket, outcome))
        self.assertEqual(self.c.task('TEST-01')['status'], 'testing')

    def test_real_checks_review_then_integrate_reuses_exact_candidate_evidence(self):
        counter = pathlib.Path(self.tmp.name) / 'check-count.txt'
        command = ('from pathlib import Path; p=Path(' + repr(str(counter)) + '); '
                   'p.write_text(str(int(p.read_text())+1) if p.exists() else "1")')
        task = self.task('COUNT-01', checks=[[sys.executable, '-c', command]])
        self.board['tasks'].append(task)
        self.c.init(self.board)
        leader, backend, tester, reviewer = self.standard_team()
        _, sha = self.candidate(leader, backend, 'COUNT-01')
        ticket = self.c.assign(leader, tester, task_id='COUNT-01', phase='test')
        self.assertEqual(self.c.test_candidate('COUNT-01', ticket['claim_token'])['status'], 'ready_review')
        self.assertEqual(counter.read_text(), '1')
        review = self.c.assign(leader, reviewer, task_id='COUNT-01', phase='review')
        self.c.submit('COUNT-01', review['claim_token'], self.report(review, 'approve'))
        with self.assertRaises(ValueError):
            self.c.integrate('COUNT-01', leader=backend)
        self.assertEqual(self.c.integrate('COUNT-01', leader=leader)['status'], 'done')
        self.assertEqual(run_git(self.c.home / 'integration', 'rev-parse', 'HEAD'), sha)
        self.assertEqual(counter.read_text(), '1', 'Fresh checks should not run twice on unchanged candidate/base')

    def test_failed_checks_return_for_debugger_without_review_approval(self):
        task = self.task('FAIL-01', checks=[[sys.executable, '-c', 'raise SystemExit(3)']])
        self.board['tasks'].append(task)
        self.c.init(self.board)
        leader, backend, tester, reviewer = self.standard_team()
        self.candidate(leader, backend, 'FAIL-01')
        ticket = self.c.assign(leader, tester, task_id='FAIL-01', phase='test')
        self.assertEqual(self.c.test_candidate('FAIL-01', ticket['claim_token'])['status'], 'ready')
        self.assertIsNone(self.c.assign(leader, reviewer, task_id='FAIL-01', phase='review'))
        debugger = self.join('opencode', 'debugger', 'debugger')
        repair = self.c.assign(leader, debugger, task_id='FAIL-01', phase='implement')
        self.assertEqual(repair['previous_candidate'], ticket['candidate_commit'])
        self.assertTrue(pathlib.Path(ticket['worktree']).is_dir())

    def test_testing_rejects_source_changes_and_moved_head(self):
        leader, backend, tester, _ = self.standard_team()
        self.candidate(leader, backend)
        ticket = self.c.assign(leader, tester, task_id='TEST-01', phase='test')
        checkout = pathlib.Path(ticket['worktree'])
        (checkout / 'initial.txt').write_text('tampered\n')
        with self.assertRaises(ValueError):
            self.c.test_candidate('TEST-01', ticket['claim_token'])
        run_git(checkout, 'add', 'initial.txt')
        run_git(checkout, 'commit', '-m', 'unauthorized tester edit')
        with self.assertRaises(ValueError):
            self.c.test_candidate('TEST-01', ticket['claim_token'])
        self.assertEqual(self.c.task('TEST-01')['status'], 'testing')

    def test_same_provider_reviewer_cannot_approve_another_instance(self):
        leader, backend, tester, _ = self.standard_team()
        self.candidate(leader, backend)
        ticket = self.c.assign(leader, tester, task_id='TEST-01', phase='test')
        self.c.test_candidate('TEST-01', ticket['claim_token'])
        peer = self.join('opencode', 'review-peer', 'reviewer')
        self.assertIsNone(self.c.assign(leader, peer, task_id='TEST-01', phase='review'))

    def test_git_default_author_does_not_erase_actual_implementing_provider(self):
        leader, backend, tester, _ = self.standard_team()
        ticket = self.c.assign(leader, backend, task_id='TEST-01', phase='implement')
        sha = self.commit(ticket, author='Codex <codex@example.invalid>')
        self.c.submit('TEST-01', ticket['claim_token'], self.report(ticket, 'ready_for_review', sha))
        self.assertEqual(set(self.c.task('TEST-01')['author'].split(',')), {'codex', 'opencode'})
        testing = self.c.assign(leader, tester, task_id='TEST-01', phase='test')
        self.c.test_candidate('TEST-01', testing['claim_token'])
        for provider in ('codex', 'opencode'):
            peer = self.join(provider, 'review-peer', 'reviewer')
            self.assertIsNone(self.c.assign(leader, peer, task_id='TEST-01', phase='review'))

    def test_coauthor_cannot_become_independent_reviewer(self):
        ticket = self.c.claim('opencode')
        sha = self.commit(ticket, message='Implementation\n\nCo-authored-by: Codex <codex@example.invalid>')
        self.c.submit('TEST-01', ticket['claim_token'], self.report(ticket, 'ready_for_review', sha))
        self.assertEqual(set(self.c.task('TEST-01')['author'].split(',')), {'codex', 'opencode'})
        self.assertIsNone(self.c.claim('codex', phase='review'))
        self.assertIsNone(self.c.claim('opencode', phase='review'))

    def test_provider_mentions_and_review_credits_are_not_author_provenance(self):
        ticket = self.c.claim('opencode')
        sha = self.commit(ticket, message='Fix Codex adapter behavior\n\n'
                          'Reviewed-by: Codex <codex@example.invalid>\n'
                          'Keep Copilot configuration compatible.\n')
        self.c.submit('TEST-01', ticket['claim_token'], self.report(ticket, 'ready_for_review', sha))
        self.assertEqual(self.c.task('TEST-01')['author'], 'opencode')
        self.assertIsNotNone(self.c.claim('codex', phase='review'))

    def test_previous_implementer_remains_author_after_another_provider_fixes(self):
        first = self.c.claim('opencode')
        sha = self.commit(first)
        self.c.submit('TEST-01', first['claim_token'], self.report(first, 'ready_for_review', sha))
        review = self.c.claim('codex', phase='review')
        self.c.submit('TEST-01', review['claim_token'], self.report(review, 'reject'))
        fix = self.c.claim('copilot', phase='implement')
        fixed_sha = self.commit(fix)
        self.c.submit('TEST-01', fix['claim_token'], self.report(fix, 'ready_for_review', fixed_sha))
        self.assertEqual(set(self.c.task('TEST-01')['author'].split(',')), {'copilot', 'opencode'})
        self.assertIsNone(self.c.claim('opencode', phase='review'))
        self.assertIsNotNone(self.c.claim('codex', phase='review'))

    def test_empty_partial_attempt_does_not_disqualify_independent_reviewer(self):
        first = self.c.claim('opencode')
        self.c.submit('TEST-01', first['claim_token'], self.report(first, 'partial'))
        second = self.c.claim('codex', phase='implement')
        sha = self.commit(second)
        self.c.submit('TEST-01', second['claim_token'], self.report(second, 'ready_for_review', sha))
        self.assertEqual(self.c.task('TEST-01')['author'], 'codex')
        self.assertIsNotNone(self.c.claim('opencode', phase='review'))

    def test_dirty_partial_attempt_preserves_source_and_original_contributor(self):
        first = self.c.claim('opencode')
        partial = pathlib.Path(first['worktree']) / first['task']['write_scopes'][0]
        partial.parent.mkdir(parents=True, exist_ok=True)
        partial.write_text('unfinished implementation\n')
        self.c.submit('TEST-01', first['claim_token'], self.report(first, 'partial'))
        second = self.c.claim('codex', phase='implement')
        sha = self.commit(second)
        self.c.submit('TEST-01', second['claim_token'], self.report(second, 'ready_for_review', sha))
        self.assertEqual(set(self.c.task('TEST-01')['author'].split(',')), {'codex', 'opencode'})
        self.assertEqual(partial.read_text(), 'unfinished implementation\n')
        self.assertIsNone(self.c.claim('opencode', phase='review'))

    def test_stale_candidate_is_requeued_before_expensive_check_side_effects(self):
        marker = pathlib.Path(self.tmp.name) / 'must-not-run.txt'
        task = self.task('STALE-01', checks=[[sys.executable, '-c',
            'from pathlib import Path; Path(' + repr(str(marker)) + ').write_text("ran")']])
        self.board['tasks'].append(task)
        self.c.init(self.board)
        ticket = self.c.claim('opencode', task_id='STALE-01')
        sha = self.commit(ticket)
        self.c.submit('STALE-01', ticket['claim_token'], self.report(ticket, 'ready_for_review', sha))
        review = self.c.claim('codex', task_id='STALE-01', phase='review')
        self.c.submit('STALE-01', review['claim_token'], self.report(review, 'approve'))
        integration = self.c.home / 'integration'
        (integration / 'accepted-later.txt').write_text('separate accepted work\n')
        run_git(integration, 'add', 'accepted-later.txt')
        run_git(integration, 'commit', '-m', 'integration advanced')
        current = run_git(integration, 'rev-parse', 'HEAD')
        result = self.c.integrate('STALE-01')
        self.assertEqual(result['status'], 'ready')
        self.assertEqual(result['reason'], 'rebase_required')
        self.assertFalse(marker.exists(), 'Stale candidates must be detected before costly checks')
        self.assertEqual(run_git(integration, 'rev-parse', 'HEAD'), current)
        self.assertEqual(self.c.task('STALE-01')['candidate'], sha)

    def test_expired_testing_lease_is_fenced_and_requires_fresh_checkout(self):
        leader, backend, tester, _ = self.standard_team()
        self.candidate(leader, backend)
        old = self.c.assign(leader, tester, task_id='TEST-01', phase='test')
        self.now += 31
        self.c.expire()
        self.assertEqual(self.c.task('TEST-01')['status'], 'ready_test')
        self.assertEqual(self.c.inbox(tester)['status'], 'waiting')
        new = self.c.assign(leader, tester, task_id='TEST-01', phase='test')
        self.assertNotEqual(new['claim_token'], old['claim_token'])
        self.assertNotEqual(new['worktree'], old['worktree'])
        with self.assertRaises(ValueError):
            self.c.test_candidate('TEST-01', old['claim_token'])

    def test_handover_requires_leader_and_idle_registered_successor(self):
        leader, backend, tester, _ = self.standard_team()
        with self.assertRaises(ValueError):
            self.c.handover(backend, tester)
        self.c.assign(leader, backend, task_id='TEST-01')
        with self.assertRaises(ValueError):
            self.c.handover(leader, backend)
        self.c.handover(leader, tester)
        self.assertEqual(self.c.team()['leader'], tester)
        with self.assertRaises(ValueError):
            self.c.assign(leader, backend)

    def test_team_step_assigns_registered_worker_without_running_cli(self):
        leader, backend, _, _ = self.standard_team()
        with self.assertRaises(ValueError):
            self.c.step()
        result = self.c.step(leader=leader)
        self.assertEqual(result['status'], 'assigned')
        self.assertEqual(self.c.inbox(backend)['status'], 'assigned')
        self.assertEqual(list((self.c.home / 'logs').glob('*.log')), [])
        self.assertEqual(self.c.step(leader=leader)['status'], 'waiting')

    def test_stop_prevents_new_team_assignment(self):
        leader, backend, _, _ = self.standard_team()
        (self.c.home / 'STOP').write_text('operator stop\n')
        self.assertIsNone(self.c.assign(leader, backend, task_id='TEST-01'))
        self.assertEqual(self.c.step(leader=leader)['status'], 'stopped')
        self.assertEqual(self.c.task('TEST-01')['attempts'], 0)

    def test_long_running_check_renews_short_testing_lease(self):
        self.config['lease_seconds'] = .3
        self.config_path.write_text(json.dumps(self.config))
        self.c.clock = time.time
        task = self.task('SLOW-01', checks=[[sys.executable, '-c', 'import time; time.sleep(.7)']])
        self.board['tasks'].append(task)
        self.c.init(self.board)
        leader, backend, tester, _ = self.standard_team()
        self.candidate(leader, backend, 'SLOW-01')
        ticket = self.c.assign(leader, tester, task_id='SLOW-01', phase='test')
        self.assertEqual(self.c.test_candidate('SLOW-01', ticket['claim_token'])['status'], 'ready_review')

    def test_stop_interrupts_running_check_and_preserves_output(self):
        marker = pathlib.Path(self.tmp.name) / 'started.txt'
        command = ('from pathlib import Path; import time; '
                   'Path(' + repr(str(marker)) + ').write_text("started"); '
                   'print("CHECK_STARTED", flush=True); time.sleep(5)')
        task = self.task('STOP-01', checks=[[sys.executable, '-c', command]])
        self.board['tasks'].append(task)
        self.c.init(self.board)
        leader, backend, tester, _ = self.standard_team()
        self.candidate(leader, backend, 'STOP-01')
        ticket = self.c.assign(leader, tester, task_id='STOP-01', phase='test')

        def request_stop():
            deadline = time.monotonic() + 3
            while not marker.exists() and time.monotonic() < deadline:
                time.sleep(.01)
            (self.c.home / 'STOP').write_text('stop current test\n')

        stopper = threading.Thread(target=request_stop)
        stopper.start()
        started = time.monotonic()
        try:
            with self.assertRaisesRegex(ValueError, 'STOP'):
                self.c.test_candidate('STOP-01', ticket['claim_token'])
        finally:
            stopper.join(timeout=4)
        self.assertLess(time.monotonic() - started, 4)
        self.assertEqual(self.c.task('STOP-01')['status'], 'testing')
        logs = list((self.c.home / 'checks').glob('STOP-01-*.log'))
        self.assertTrue(logs)
        self.assertTrue(any('CHECK_STARTED' in log.read_text() for log in logs))

    def test_stop_blocks_integration_even_when_checks_are_cached(self):
        leader, backend, tester, reviewer = self.standard_team()
        self.candidate(leader, backend)
        ticket = self.c.assign(leader, tester, task_id='TEST-01', phase='test')
        self.c.test_candidate('TEST-01', ticket['claim_token'])
        review = self.c.assign(leader, reviewer, task_id='TEST-01', phase='review')
        self.c.submit('TEST-01', review['claim_token'], self.report(review, 'approve'))
        before = run_git(self.c.home / 'integration', 'rev-parse', 'HEAD')
        logs = set((self.c.home / 'checks').glob('*.log'))
        (self.c.home / 'STOP').write_text('operator stop before integration\n')
        with self.assertRaisesRegex(ValueError, 'STOP'):
            self.c.integrate('TEST-01', leader=leader)
        self.assertEqual(self.c.task('TEST-01')['status'], 'approved')
        self.assertEqual(run_git(self.c.home / 'integration', 'rev-parse', 'HEAD'), before)
        self.assertEqual(set((self.c.home / 'checks').glob('*.log')), logs)

    def test_leader_handover_during_integration_fences_previous_leader(self):
        leader, backend, tester, reviewer = self.standard_team()
        self.candidate(leader, backend)
        ticket = self.c.assign(leader, tester, task_id='TEST-01', phase='test')
        self.c.test_candidate('TEST-01', ticket['claim_token'])
        review = self.c.assign(leader, reviewer, task_id='TEST-01', phase='review')
        self.c.submit('TEST-01', review['claim_token'], self.report(review, 'approve'))
        before = run_git(self.c.home / 'integration', 'rev-parse', 'HEAD')
        original_check = self.c.clean_candidate
        handed_over = False

        def handover_after_check(task):
            nonlocal handed_over
            original_check(task)
            if not handed_over:
                self.c.handover(leader, tester)
                handed_over = True

        with mock.patch.object(self.c, 'clean_candidate', side_effect=handover_after_check):
            with self.assertRaises(ValueError):
                self.c.integrate('TEST-01', leader=leader)
        self.assertTrue(handed_over)
        self.assertEqual(self.c.team()['leader'], tester)
        self.assertEqual(self.c.task('TEST-01')['status'], 'approved')
        self.assertEqual(run_git(self.c.home / 'integration', 'rev-parse', 'HEAD'), before)
        self.assertEqual(self.c.integrate('TEST-01', leader=tester)['status'], 'done')


    def test_wait_returns_assignment_without_tight_polling(self):
        leader, backend, _, _ = self.standard_team()
        ticket = self.c.assign(leader, backend, task_id='TEST-01')
        result = self.c.wait_for_assignment(backend, poll_seconds=1, timeout_seconds=2)
        self.assertEqual(result['status'], 'assigned')
        self.assertEqual(result['ticket']['claim_token'], ticket['claim_token'])

    def test_wait_exits_on_stop(self):
        leader, backend, _, _ = self.standard_team()
        (self.c.home/'STOP').write_text('stop')
        self.assertEqual(self.c.wait_for_assignment(backend)['status'], 'stopped')

if __name__ == '__main__':
    unittest.main()
