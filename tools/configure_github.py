"""Configure GitHub OTA hosting without displaying or passing private keys as arguments."""

import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parent.parent


def gh(*arguments, data=None, sensitive=False):
    result = subprocess.run(['gh', *arguments], input=data, capture_output=True)
    if result.returncode:
        if sensitive:
            raise RuntimeError('GitHub rejected the secret upload; private contents were not printed')
        raise RuntimeError(result.stderr.decode('utf-8', errors='replace').strip())
    return result.stdout


def api(endpoint, method='GET', body=None, missing_ok=False):
    arguments = ['gh', 'api', '--method', method, endpoint]
    data = None
    if body is not None:
        arguments += ['--input', '-']
        data = json.dumps(body).encode('utf-8')
    result = subprocess.run(arguments, input=data, capture_output=True)
    if result.returncode:
        error = result.stderr.decode('utf-8', errors='replace')
        if missing_ok and 'HTTP 404' in error:
            return None
        raise RuntimeError(error.strip())
    return json.loads(result.stdout) if result.stdout.strip() else None


def api_list(endpoint):
    pages = json.loads(gh('api', '--paginate', '--slurp', endpoint))
    return [item for page in pages for item in page]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', default='WR-Design-Dev/CI-Lights')
    parser.add_argument('--owner', default='WR-Design-Dev', help='The only account allowed to write source code')
    parser.add_argument('--apply', action='store_true', help='Restrict writers, upload the signing secret, and enable Pages')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', args.repository):
        parser.error('Repository must be owner/name')
    if shutil.which('gh') is None:
        parser.error('Install GitHub CLI first, then run gh auth login')
    try:
        account = api('user')
        if account['login'].lower() != args.owner.lower():
            raise RuntimeError(f'Authenticate as {args.owner}; refusing to configure access as another account')
        base = f'repos/{args.repository}'
        repo = api(base)
        if repo['owner']['type'] != 'User' or repo['owner']['login'].lower() != args.owner.lower():
            raise RuntimeError('This helper supports a personal repository owned by the sole writer')
        if repo['private'] or repo['default_branch'] != 'main' or not repo.get('permissions', {}).get('admin'):
            raise RuntimeError('Expected a public repository with main as default branch and administrator access')
        collaborators = api_list(base + '/collaborators?affiliation=all&per_page=100')
        other_writers = [person for person in collaborators
                         if person['login'].lower() != args.owner.lower() and person['permissions'].get('push')]
        invitations = api_list(base + '/invitations?per_page=100')
        write_keys = [key for key in api_list(base + '/keys?per_page=100') if not key['read_only']]
        print(f'Authenticated owner: {account["login"]}')
        print('Other accounts with write access: ' + (', '.join(person['login'] for person in other_writers) or 'none'))
        print(f'Pending collaborator invitations: {len(invitations)}; write-enabled deploy keys: {len(write_keys)}')
        if not args.apply:
            print('Read-only check complete. Use --apply to configure access and firmware hosting.')
            return
        key_path = ROOT / 'secrets/ota_signing_key.pem'
        if not key_path.is_file():
            raise RuntimeError('Local signing key is missing. Restore the original key before configuring GitHub.')
        # Validate against the tracked public fingerprint before any upload.
        subprocess.run([sys.executable, str(ROOT / 'tools/prepare_ota_key.py'), '--generate'], check=True)
        env_path = base + '/environments/firmware-signing'
        environment = api(env_path, missing_ok=True)
        if environment is None:
            api(env_path, 'PUT', {'deployment_branch_policy': {
                'protected_branches': False, 'custom_branch_policies': True}})
        else:
            policy = environment.get('deployment_branch_policy') or {}
            if policy.get('protected_branches') or not policy.get('custom_branch_policies'):
                raise RuntimeError('Set firmware-signing to Selected branches and tags, allowing only branch main')
        policies_path = env_path + '/deployment-branch-policies'
        policies = api(policies_path + '?per_page=100')['branch_policies']
        if len(policies) > 1 or any(rule['name'] != 'main' or rule.get('type') != 'branch' for rule in policies):
            raise RuntimeError('Remove other firmware-signing branch/tag rules; only branch main may access the secret')
        if not policies:
            api(policies_path, 'POST', {'name': 'main', 'type': 'branch'})
        verified = api(policies_path + '?per_page=100')['branch_policies']
        if len(verified) != 1 or verified[0]['name'] != 'main' or verified[0].get('type') != 'branch':
            raise RuntimeError('Could not verify the main-only signing environment')
        for person in other_writers:
            api(base + '/collaborators/' + person['login'], 'DELETE')
        for invitation in invitations:
            api(base + '/invitations/' + str(invitation['id']), 'DELETE')
        for key in write_keys:
            api(base + '/keys/' + str(key['id']), 'DELETE')
        remaining = api_list(base + '/collaborators?affiliation=all&per_page=100')
        if any(person['login'].lower() != args.owner.lower() and person['permissions'].get('push') for person in remaining):
            raise RuntimeError('Other writers still have access; the signing key has not been uploaded')
        # stdin keeps the PEM out of the process arguments and shell history.
        gh('secret', 'set', 'OTA_SIGNING_KEY_PEM', '--env', 'firmware-signing',
           '--repo', args.repository, data=key_path.read_bytes(), sensitive=True)
        # A broad repository secret would defeat the environment restriction.
        repository_secrets = api(base + '/actions/secrets')['secrets']
        if any(secret['name'] == 'OTA_SIGNING_KEY_PEM' for secret in repository_secrets):
            api(base + '/actions/secrets/OTA_SIGNING_KEY_PEM', 'DELETE')
        pages = api(base + '/pages', missing_ok=True)
        if pages is None:
            api(base + '/pages', 'POST', {'build_type': 'workflow'})
        elif pages.get('build_type') != 'workflow':
            api(base + '/pages', 'PUT', {'build_type': 'workflow'})
        print(f'Only {args.owner} has human write access. GitHub Actions retains workflow-scoped release publishing.')
        print('Production signing secret uploaded to the verified main-only environment.')
        print(f'Pages configured: https://{repo["owner"]["login"].lower()}.github.io/{repo["name"]}/')
        print('Commit and push the reviewed firmware files to main to start the first release.')
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'GitHub setup failed: {error}\n')


if __name__ == '__main__':
    main()
