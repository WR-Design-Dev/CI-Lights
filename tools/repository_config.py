"""Read the explicit publishing repository, including a fork's own CI repository."""
import os
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def repository_name():
    value = os.environ.get('GITHUB_REPOSITORY') or (ROOT / 'firmware_repository.txt').read_text(encoding='ascii').strip()
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', value):
        raise ValueError('Publishing repository must be owner/name')
    return value
