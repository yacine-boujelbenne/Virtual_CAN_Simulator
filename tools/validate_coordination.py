#!/usr/bin/env python3
"""Validate the coordination pack, not the vehicle application."""
import json,pathlib,sys
def main():
    root=pathlib.Path(__file__).resolve().parents[1]
    board=json.loads((root/'coordination/tasks.json').read_text());tasks={t['id']:t for t in board['tasks']}
    assert len(tasks)==len(board['tasks']),'Duplicate task IDs'
    def visit(key,trail):
        assert key in tasks,'Missing dependency '+key
        assert key not in trail,'Dependency cycle'
        t=tasks[key]
        assert t['write_scopes'] and t['acceptance'] and t['checks'],'Incomplete task '+key
        for command in t['checks']:assert isinstance(command,list) and command and all(isinstance(x,str) for x in command)
        for dep in t['depends_on']:visit(dep,trail|{key})
    for key in tasks:visit(key,set())
    providers=json.loads((root/'coordination/providers.example.json').read_text())['providers']
    assert set(providers)=={'opencode','copilot','codex'}
    assert all(not p['enabled'] for p in providers.values()),'Example must not start paid/account execution'
    for name in ('LAW.md','OPENCODE.md','COPILOT.md','CODEX.md','AGENTS.md','coordination/CURRENT_STATE.md'):
        assert (root/name).is_file(),name
    print(json.dumps({'coordination_pack':'valid','tasks':len(tasks),'provider_execution':'disabled until configured','vehicle_validation':'not run'}))
if __name__=='__main__':main()
