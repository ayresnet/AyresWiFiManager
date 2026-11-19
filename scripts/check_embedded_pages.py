"""Verify the firmware's compressed pages match the files in data/."""
from pathlib import Path
import gzip
import re
import sys

root = Path(__file__).resolve().parent.parent
header = (root / 'src/AWM_html_gz.h').read_text(encoding='utf-8')
for name in ('INDEX', 'SUCCESS', 'ERROR'):
    match = re.search(rf'{name}_HTML_GZ\[\][^=]*=\s*\{{([^}}]*)\}}', header, re.S)
    if not match:
        sys.exit(f'Missing embedded {name} page')
    compressed = bytes(int(value, 16) for value in re.findall(r'0x([0-9a-fA-F]{2})', match.group(1)))
    source = (root / 'data' / f'{name.lower()}.html').read_bytes()
    if gzip.decompress(compressed) != source:
        sys.exit(f'Embedded {name} page does not match its source')
    print(f'{name}: OK')
