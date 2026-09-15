"""Run committed upstream golden vectors against the native C++ codec runner.

Build on Linux: g++ -std=c++11 -Wall -Wextra -Werror -Isrc
  src/heater_protocol.cpp tests/protocol_runner.cpp -o /tmp/protocol_runner
Run: python tests/protocol_test.py /tmp/protocol_runner
"""
import json
import math
from pathlib import Path
import subprocess
import sys

vectors = json.loads(Path(__file__).with_name('protocol_vectors.json').read_text(encoding='utf-8'))
cases = vectors['cases']
result = subprocess.run([sys.argv[1]], input='\n'.join(c['input'] for c in cases)+'\n',
                        text=True, capture_output=True, check=True)
rows = result.stdout.splitlines()
assert len(rows) == len(cases), (len(rows), len(cases), result.stderr)
for case, row in zip(cases, rows):
    actual = row.split()
    expected = case['expected']
    assert len(actual) == len(expected), (case['input'], expected, actual)
    for a, e in zip(actual, expected):
        if isinstance(e, str):
            assert a == e, (case['input'], expected, actual)
        else:
            assert math.isclose(float(a), e, abs_tol=0.0001, rel_tol=0.00001), (case['input'], expected, actual)
print(f'{len(cases)} protocol vectors passed (8 profiles; upstream comparison and rejection cases).')
