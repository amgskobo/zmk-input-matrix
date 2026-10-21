#!/usr/bin/env python3
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
"""One complete-driver binary, named positive/counterexample cases, raw repeats."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('workspace',type=Path)
parser.add_argument('output',type=Path)
parser.add_argument('--iqs-root',type=Path,help='Explicit IQS owner checkout; add actual click-producer cases')
parser.add_argument('--iqs-only',action='store_true',help='Focused producer boundary run (requires --iqs-root)')
parser.add_argument('--regressions-only',action='store_true')
parser.add_argument('--strict-clicks',action='store_true',help='Additional click-duration/source-ownership and ON/ON/OFF robustness checks; known FAIL')
parser.add_argument('--negative-control',choices=['inactive-matrix','iqs-inline-first','iqs-drop-release'],
                    help='Explicit negative control; require named failures')
parser.add_argument('--core-revision',help='Explicit manifest-resolved core SHA from the integration entry')
args=parser.parse_args()
if args.strict_clicks and args.negative_control:
    parser.error('strict requirements and negative controls are separate selections')
if args.iqs_root and args.negative_control == 'inactive-matrix':
    parser.error('static route controls and the IQS producer are separate selections')
core=os.environ.get('ZMK_TEST_CORE','dya')
if core not in ('upstream','dya'): parser.error('ZMK_TEST_CORE must be upstream or dya')
if args.core_revision is not None and (len(args.core_revision)!=40 or
        any(c not in '0123456789abcdef' for c in args.core_revision)):
    parser.error('--core-revision must be a full lowercase commit SHA')
root=Path(__file__).resolve().parents[2]
workspace=args.workspace.resolve()
out=args.output
if not out.is_absolute(): parser.error('output must be absolute')
if out.exists() or out.is_symlink(): parser.error('output already exists: '+str(out))
out.mkdir(parents=True,exist_ok=False)
positive=('matrix-key-isolation','tap-no-hold','disable-held','reentrant-disable',
          'reentrant-retouch','forwarded-release','independent-button-release',
          'invalid-stream','long-walk','different-cells-real-hid','forwarded-release-real-input','separate-instances-control',
          'same-cell-real-hid','tap-over-hold-real-hid',
          'separate-instances-same-cell','shared-disable-instance','shared-layer-reset',
          'tap-during-press-callback','concurrent-output',
          'repeat-one','repeat-two','repeat-real-input','touch-toggle','touch-real-input',
          'repeat-passthrough-real-input','disabled-button-real-input','suppressed-pair-policy-edit',
          'repeat-frame-real-input','independent-mouse-owners','touch-button-alias-real-input')
negative={
 'consumer-pause':'consumer missed release while callbacks disabled',
 'consumer-replace':'old consumer retains held key after callback replacement',
}
if args.regressions_only:
    negative={}
positive=positive+('final-layer-no-repeat-control','final-no-layer-repeat-control',
    'final-forwarded-frame-control','final-layer-repeat','final-layer-suppressed-other-owner',
    'final-suppressed-frame','route-suppressed-release','route-touch-alias',
    'route-forwarded-pairs','route-cancels-hold','frame-empty-suppressed',
    'frame-suppressed-release','frame-suppressed-abs','frame-settings-reset','frame-layer-reset')

if args.negative_control == 'inactive-matrix':
    positive=()
    negative={'matrix-key-isolation':'mouse_sends==before && count==0'}



def identities():
    result={}
    for folder in ('src','include','dts','zephyr','tests/kernel','tests/deep-scan'):
        for p in sorted((root/folder).rglob('*')):
            if p.is_file(): result[p.relative_to(root).as_posix()]=hashlib.sha256(p.read_bytes()).hexdigest()
    for name in ('CMakeLists.txt','Kconfig','AGENTS.md','agent.md'):
        result[name]=hashlib.sha256((root/name).read_bytes()).hexdigest()
    return result
before=identities()
(out/'source-before.json').write_text(json.dumps(before,indent=2))
pins={'zmk':args.core_revision or {'upstream':'5b51501fead672c41b5cfb396f3dafe0894bf4e9','dya':'e5c9b6915b56801193e359dd9bad4a167ce0d1b8'}[core],
      'zephyr':'10ba6d0cb38bc3d258775d27982f707599320085'}
deps={}
for name,pin in pins.items():
    path=workspace/name
    rev=subprocess.check_output(['git','-C',str(path),'rev-parse','HEAD'],text=True).strip()
    status=subprocess.check_output(['git','-C',str(path),'status','--porcelain','--untracked-files=all'],text=True)
    if rev!=pin or status: raise SystemExit(f'unchanged selected dependency required: {path}')
    deps[name]=dict(path=str(path),revision=rev,status=status)
(out/'dependencies.json').write_text(json.dumps(deps,indent=2))
iqs_root=args.iqs_root.resolve() if args.iqs_root else None
iqs_cases=('iqs-pointer','iqs-matrix','iqs-pointer-to-matrix','iqs-matrix-to-pointer',
    'iqs-pointer-return','iqs-overlap','iqs-double-pointer','iqs-double-matrix',
    'iqs-triple-pointer','iqs-triple-matrix','iqs-replace-pointer','iqs-replace-matrix',
    'iqs-before-on-pointer-to-matrix','iqs-before-on-matrix-to-pointer',
    'iqs-double-pointer-to-matrix','iqs-double-matrix-to-pointer','iqs-triple-pointer-return')
if args.negative_control in ('iqs-inline-first','iqs-drop-release') and not iqs_root:
    parser.error('IQS negative control requires --iqs-root')
if args.iqs_only and not iqs_root: parser.error('--iqs-only requires --iqs-root')
def iqs_identity():
    files={}
    for name in ('src','Kconfig','AGENTS.md','tests/README.md','tests/maintenance.json'):
        p=iqs_root/name
        for f in sorted(p.rglob('*')) if p.is_dir() else [p]:
            if f.is_file(): files[f.relative_to(iqs_root).as_posix()]=hashlib.sha256(f.read_bytes()).hexdigest()
    return dict(path=str(iqs_root),revision=subprocess.check_output(
        ['git','-c','safe.directory='+str(iqs_root),'-C',str(iqs_root),'rev-parse','HEAD'],text=True).strip(),files=files)
iqs_before=None
if iqs_root:
    if not (iqs_root/'src/iqs7211e.c').is_file(): parser.error('IQS producer source missing')
    iqs_before=iqs_identity()
    (out/'iqs-before.json').write_text(json.dumps(iqs_before,indent=2))
    positive=iqs_cases if args.iqs_only else positive+iqs_cases
    if args.iqs_only: negative={}
    if args.negative_control=='iqs-inline-first':
        positive=()
        negative={name:'IQS first ON was emitted inline' for name in iqs_cases if 'before-on' in name}
    if args.negative_control=='iqs-drop-release':
        positive=()
        negative={'iqs-pointer':'IQS release left button held'}
if args.strict_clicks:
    positive=() if args.iqs_only else ('route-suppressed-release','route-touch-alias','route-forwarded-repeat','route-forwarded-pairs')
    if iqs_root: positive=positive+('iqs-overlap',)
    negative={}
(out/'selection.json').write_text(json.dumps(dict(
    positive_cases=positive,negative_controls=negative,
    acceptance='strict click duration/ownership and robustness' if args.strict_clicks else 'balanced source pairs: final release and recovery; early release permitted',
    iqs_producer='SELECTED' if iqs_root else 'NOT RUN: supply --iqs-root',
    scope='Gesture-to-click handoff through actual input/listener/HID; no I2C/split/host'),indent=2))
if not iqs_root: print('IQS producer: NOT RUN (requires explicit --iqs-root)',flush=True)
rows=[]
with tempfile.TemporaryDirectory(prefix='matrix-deep-scan-') as temp:
    build=Path(temp)/'build'
    command=['west','build','-p=always','-o=-j2','-s',str(workspace/'zmk/app'),
        '-d',str(build),'-b','native_sim//zmk_test_mock','--','-DCONFIG_ASSERT=y',
        '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DZMK_CONFIG='+str(root/'tests/kernel/config'),
        '-DEXTRA_DTC_OVERLAY_FILE='+str(root/'tests/deep-scan/extra.overlay')+
            (';'+str(root/'tests/deep-scan'/(args.negative_control+'.overlay')) if args.negative_control == 'inactive-matrix' else ''),
        '-DZMK_EXTRA_MODULES='+str(root)+';'+str(root/'tests/deep-scan')]
    if iqs_root:
        command+=['-DMATRIX_TEST_IQS_ROOT='+str(iqs_root),'-DEXTRA_CONF_FILE='+str(root/'tests/deep-scan/iqs.conf')+
                  (';'+str(root/'tests/deep-scan'/(args.negative_control+'.conf')) if args.negative_control in ('iqs-inline-first','iqs-drop-release') else '')]
    build_env=dict(os.environ,MATRIX_TEST_IQS_ROOT=str(iqs_root) if iqs_root else '')
    if iqs_root:
        # The fixture includes the selected producer once. Exclude another
        # module registration of it; keep all actual producer features enabled.
        modules=build_env.get('ZEPHYR_MODULES','').split(';')
        excluded=[m for m in modules if m and (Path(m)/'src/iqs7211e.c').is_file()]
        build_env['ZEPHYR_MODULES']=';'.join(m for m in modules if m not in excluded)
        (out/'producer-registration.json').write_text(json.dumps(dict(
            included=str(iqs_root),excluded_duplicate_modules=excluded),indent=2))
    (out/'build-command.json').write_text(json.dumps(dict(command=command,cwd=str(workspace),
        environment={k:v for k,v in build_env.items() if k in ('ZEPHYR_BASE','ZEPHYR_MODULES','Zephyr_DIR','MATRIX_TEST_IQS_ROOT')}),indent=2))
    with (out/'build.log').open('w') as log:
        result=subprocess.run(command,cwd=workspace,env=build_env,stdout=log,stderr=subprocess.STDOUT)
    (out/'build.exit').write_text(str(result.returncode))
    if result.returncode: raise SystemExit(result.returncode)
    for src,name in ((build/'zephyr/zmk.exe','zmk.exe'),(build/'zephyr/.config','config'),
        (build/'zephyr/zephyr.dts','zephyr.dts'),(build/'CMakeCache.txt','CMakeCache.txt'),
        (build/'compile_commands.json','compile_commands.json')):
        shutil.copy2(src,out/name)
    binary=out/'zmk.exe'
    (out/'binary.sha256').write_text(hashlib.sha256(binary.read_bytes()).hexdigest())
    failures=[]
    for case in (*positive,*negative):
        for repeat in range(1,4):
            command=[str(binary)]
            environment=dict(os.environ,MATRIX_REVIEW_CASE=case)
            environment.pop('MATRIX_STRICT_CLICKS',None)
            if args.strict_clicks: environment['MATRIX_STRICT_CLICKS']='1'
            try:
                result=subprocess.run(command,capture_output=True,env=environment,timeout=15)
                stdout,stderr,status=result.stdout,result.stderr,result.returncode
            except subprocess.TimeoutExpired as exc:
                stdout,stderr,status=exc.stdout or b'',exc.stderr or b'',124
            prefix=f'{case}-{repeat}'
            (out/(prefix+'.stdout')).write_bytes(stdout)
            (out/(prefix+'.stderr')).write_bytes(stderr)
            (out/(prefix+'.exit')).write_text(str(status))
            marker=('matrix deep-scan PASS case='+case) if case in positive else negative[case]
            matched=marker.encode() in stdout+stderr
            valid=matched and (status==0 if case in positive else status not in (0,124))
            rows.append(dict(case=case,repeat=repeat,command=command,
                environment={k:v for k,v in environment.items() if k in ('MATRIX_REVIEW_CASE','MATRIX_STRICT_CLICKS')},exit=status,
                expected='PASS' if case in positive else 'NAMED_ASSERTION_FAILURE',
                expected_marker=marker,matched=matched,expectation_verified=valid))
            print(case,repeat,'PASS' if status==0 else f'FAIL({status})',
                  'expected' if valid else 'UNEXPECTED',flush=True)
            if not valid: failures.append(prefix)
    after=identities()
    (out/'source-after.json').write_text(json.dumps(after,indent=2))
    (out/'results.json').write_text(json.dumps(rows,indent=2))
    if before!=after: failures.append('source drift')
    if iqs_root:
        iqs_after=iqs_identity()
        (out/'iqs-after.json').write_text(json.dumps(iqs_after,indent=2))
        if iqs_before!=iqs_after: failures.append('IQS source drift')
    if failures: raise SystemExit('Unexpected results: '+','.join(failures))
print(f'Matrix deep scan: positive{len(positive)}x3 PASS; counterexample{len(negative)}x3 named FAIL; remaining policy/interface observations stay explicit')
