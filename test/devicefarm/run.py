#!/usr/bin/env python3

from datetime import datetime
import json
import re
import subprocess
import sys
import tempfile

def run_test():
    cmdline = [
        'gcloud',
        'firebase',
        'test',
        'android',
        'run',
        '--flags-file', 'flags.yaml',
    ] + sys.argv[1:]

    print('Stand by...\n')
    proc = subprocess.run(
        cmdline,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        encoding='utf-8'
    )
    if proc.returncode != 0:
        print(proc.stderr)
        exit(proc.returncode)

    return proc.stdout, proc.stderr


def display_test_results(stdout, stderr):
    result = json.loads(stdout)
    if len(result) == 0:
        return
    fields = ['axis_value', 'outcome', 'test_details']
    max_width = {f: max(len(res[f]) for res in result) for f in fields}
    for line in result:
        for field in fields:
            print('{:<{w}}'.format(line[field], w=max_width[field]), end='  ')
        print()
    print()


def get_test_info(stderr):
    pattern = (
        r'^.*GCS bucket at \[(https.*?)\]'
        + r'.*Test \[(matrix-.*?)\]'
        + r'.*streamed to \[(https.*?)\]'
    )
    re_matches = re.match(pattern, stderr, flags=re.DOTALL)
    return {
        'url_storage': re_matches.group(1),
        'matrix_id': re_matches.group(2),
        'url_info': re_matches.group(3),
    }


def display_test_info(test_info):
    print('GCS:    {}'.format(test_info['url_storage']))
    print('Info:   {}'.format(test_info['url_info']))
    print('Matrix: {}\n'.format(test_info['matrix_id']))


def download_cloud_artifacts(test_info, file_pattern):
    pattern = r'^.*storage\/browser\/(.*)'
    re_match = re.match(pattern, test_info['url_storage'])
    gs_dir = re_match.group(1)

    cmdline = ['gsutil', 'ls', 'gs://{}**/{}'.format(gs_dir, file_pattern)]
    proc = subprocess.run(
        cmdline,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        encoding='utf-8'
    )
    if proc.returncode != 0:
        print(proc.stderr)
        exit(proc.returncode)

    tmpdir = tempfile.mkdtemp(
        prefix=datetime.now().strftime('%Y%m%d-%H%M%S-'),
        dir='.'
    )
    for line in proc.stdout.splitlines():
        name_suffix = line[5 + len(gs_dir):]
        cmdline = [
            'gsutil',
            'cp',
            line,
            '{}/{}'.format(tmpdir, name_suffix.replace('/', '_'))
        ]
        proc = subprocess.run(cmdline)


if __name__ == '__main__':
    stdout, stderr = run_test()
    display_test_results(stdout, stderr)
    test_info = get_test_info(stderr)
    display_test_info(test_info)
    download_cloud_artifacts(test_info, 'logcat')
