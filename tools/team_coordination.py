"""Small persistent team roster. Provider identity and assigned job are separate."""
import json
import pathlib
import re
import time

ROLES = ('orchestrator', 'backend', 'tester', 'reviewer', 'design', 'implementer', 'debugger')
WORKER_ROLES = ('backend', 'tester', 'reviewer', 'design', 'implementer')


class TeamCoordination:
    def join(self, provider, instance, role=None):
        if provider not in ('codex', 'opencode', 'copilot'):
            raise ValueError('Unknown provider')
        if not re.fullmatch(r'[A-Za-z0-9_-]+', instance or ''):
            raise ValueError('A stable instance name is required')
        if role is not None and role not in ROLES:
            raise ValueError('Unknown role')
        identity = provider + '#' + instance
        with self.tx():
            old = self.team_agent(identity)
            if old:
                return old
            count = self.db.execute('SELECT count(*) FROM agents').fetchone()[0]
            assigned = 'orchestrator' if count == 0 else role or WORKER_ROLES[(count - 1) % len(WORKER_ROLES)]
            if count and assigned == 'orchestrator':
                raise ValueError('One orchestrator only; use explicit handover')
            if assigned == 'orchestrator' and self.db.execute(
                    "SELECT 1 FROM tasks WHERE owner=? AND status IN ('implementing','testing','reviewing')",
                    (identity,)).fetchone():
                raise ValueError('Finish the active task before becoming orchestrator')
            self.db.execute('INSERT INTO agents(identity,provider,instance,role,joined) VALUES(?,?,?,?,?)',
                            (identity, provider, instance, assigned, self.clock()))
            self.event('agent_joined', owner=identity, role=assigned)
        return self.team_agent(identity)

    def team_agent(self, identity):
        row = self.db.execute('SELECT * FROM agents WHERE identity=?', (identity,)).fetchone()
        return dict(row) if row else None

    def team(self):
        agents = [dict(r) for r in self.db.execute('SELECT * FROM agents ORDER BY seq')]
        leader = next((a['identity'] for a in agents if a['role'] == 'orchestrator'), None)
        return {'leader': leader, 'agents': agents}

    def require_leader(self, identity):
        if not identity or self.team()['leader'] != identity:
            raise ValueError('Only the first registered agent/orchestrator may assign or integrate; use its --leader identity')

    def set_role(self, leader, identity, role):
        with self.tx():
            self.require_leader(leader)
            if role not in ROLES or role == 'orchestrator' or identity == leader:
                raise ValueError('Use handover to change the orchestrator')
            if not self.team_agent(identity):
                raise ValueError('Agent must join first')
            if self.db.execute("SELECT 1 FROM tasks WHERE owner=? AND status IN ('implementing','testing','reviewing')", (identity,)).fetchone():
                raise ValueError('Finish the active task before changing role')
            self.db.execute('UPDATE agents SET role=? WHERE identity=?', (role, identity))
            self.event('agent_role', owner=identity, role=role, leader=leader)
        return self.team_agent(identity)

    def handover(self, leader, successor):
        with self.tx():
            self.require_leader(leader)
            agent = self.team_agent(successor)
            if not agent or successor == leader:
                raise ValueError('Choose a registered worker')
            if self.db.execute("SELECT 1 FROM tasks WHERE owner=? AND status IN ('implementing','testing','reviewing')", (successor,)).fetchone():
                raise ValueError('Successor has an active task')
            self.db.execute('UPDATE agents SET role=? WHERE identity=?', (agent['role'], leader))
            self.db.execute("UPDATE agents SET role='orchestrator' WHERE identity=?", (successor,))
            self.event('leader_handover', previous=leader, owner=successor)
        return self.team()

    @staticmethod
    def role_allows(role, phase, task):
        if phase == 'test':
            return role == 'tester'
        if phase == 'review':
            # A test/debug worker can review a different provider's candidate after tests.
            # submit() still enforces provider-level independence.
            return role in ('reviewer', 'tester')
        if phase != 'implement':
            return False
        if role in ('implementer', 'debugger'):
            return True
        graphics = bool(set(task['capabilities']) & {'ui', 'world'})
        return (role == 'design' and graphics) or (role == 'backend' and not graphics)

    def assign(self, leader, agent, task_id=None, phase=None):
        self.require_leader(leader)
        worker = self.team_agent(agent)
        if not worker:
            raise ValueError('Agent must join first')
        if worker['role'] == 'orchestrator':
            raise ValueError('The orchestrator assigns work; it does not claim worker tasks')
        return self.claim(worker['provider'], instance=worker['instance'],
                          task_id=task_id, phase=phase, _leader=leader)

    def inbox(self, identity):
        if not self.team_agent(identity):
            raise ValueError('Agent must join first')
        self.expire()
        row = self.db.execute("SELECT worktree FROM tasks WHERE owner=? AND status IN ('implementing','testing','reviewing')", (identity,)).fetchone()
        if row:
            ticket = json.loads((pathlib.Path(row['worktree']) / '.handoff/ticket.json').read_text())
            return {'status': 'assigned', 'ticket': ticket}
        return {'status': 'waiting', 'reason': 'Wait for an assignment; do not poll in a tight loop'}

    def wait_for_assignment(self, identity, poll_seconds=5, timeout_seconds=0):
        if poll_seconds < 1 or timeout_seconds < 0:
            raise ValueError('Use polling intervals >=1s and timeout >=0 (0 waits until STOP)')
        started=time.monotonic()
        while True:
            if (self.home/'STOP').exists():return {'status':'stopped'}
            result=self.inbox(identity)
            if result['status']=='assigned':return result
            if timeout_seconds and time.monotonic()-started>=timeout_seconds:return result
            time.sleep(min(poll_seconds,timeout_seconds-(time.monotonic()-started)) if timeout_seconds else poll_seconds)

    def team_step(self, leader):
        self.require_leader(leader)
        for phase in ('test', 'review', 'implement'):
            for agent in self.team()['agents']:
                if agent['role'] == 'orchestrator':
                    continue
                ticket = self.assign(leader, agent['identity'], phase=phase)
                if ticket:
                    self.export_progress()
                    return {'status': 'assigned', 'agent': agent['identity'], 'ticket': ticket}
        return {'status': 'waiting', 'reason': 'Workers are busy, dependencies/scopes block work, or a required role/provider is unavailable'}

    def brief(self):
        tasks = self.status()['tasks']
        counts = {}
        for task in tasks:
            counts[task['status']] = counts.get(task['status'], 0) + 1
        pending = []
        for task in tasks:
            if task['status'] == 'done':
                continue
            record = self.task(task['id'])
            deps = json.loads(record['payload'])['depends_on']
            pending.append({'id': task['id'], 'status': task['status'], 'owner': task['owner'],
                            'waiting_for': [d for d in deps if self.task(d)['status'] != 'done']})
        return {'team': self.team(), 'counts': counts, 'pending': pending,
                'providers': self.status()['providers']}
