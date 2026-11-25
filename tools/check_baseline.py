#!/usr/bin/env python3
"""Refuse to migrate an older checkout while calling the reported P0 preserved."""
import argparse,json,pathlib,subprocess,sys
REQUIRED=[
 'headers/IsoTp.hpp','src/IsoTp.cpp','headers/Sha256.hpp','src/Sha256.cpp',
 'resources/schemas/event-envelope.v2.json','tests/test_envelopes.py','tests/test_adas_fusion.py',
 'tests/test_isotp_vectors.py','tests/test_project_ab.py','tests/test_performance.py']
def main():
    p=argparse.ArgumentParser();p.add_argument('--verify-p0',action='store_true');a=p.parse_args()
    missing=[x for x in REQUIRED if not pathlib.Path(x).is_file()]
    print(json.dumps({'required_P0_files_missing':missing,'meaning':'Presence only; genuine CMake/CTest and behavior checks are a separate gate.'},indent=2))
    return 1 if a.verify_p0 and missing else 0
if __name__=='__main__':sys.exit(main())
