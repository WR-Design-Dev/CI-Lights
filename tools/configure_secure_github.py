"""Verify a main-only secure signing environment before uploading only the active key."""
import argparse
import json
import subprocess
from pathlib import Path

from configure_github import api, api_list, gh
from prepare_secure_keys import DEFAULT_DIRECTORY, load_key, pem
from security_images import key_identity
from repository_config import repository_name

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--directory', type=Path, default=DEFAULT_DIRECTORY)
    parser.add_argument('--repository', default=repository_name())
    parser.add_argument('--store-reserves', action='store_true',
                        help='Explicitly also back up K1/K2 in a main-only environment requiring owner approval')
    args = parser.parse_args()
    if args.store_reserves and not args.apply:
        parser.error('--store-reserves requires --apply')
    repository = args.repository
    import re
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository):
        raise ValueError('Repository must be owner/name')
    user = api('user')
    repo = api('repos/' + repository)
    if user['login'].lower() != repository.split('/')[0].lower() or not repo.get('permissions', {}).get('admin') or repo['private'] or repo['default_branch'] != 'main':
        raise ValueError('Expected the authenticated owner and public production repository')
    writers = api_list(f'repos/{repository}/collaborators?affiliation=all&per_page=100')
    if any(person['login'] != user['login'] and person['permissions'].get('push') for person in writers):
        raise ValueError('Unexpected writer access; no secret uploaded and no permissions changed')
    if api_list(f'repos/{repository}/invitations?per_page=100') or any(not key['read_only'] for key in api_list(f'repos/{repository}/keys?per_page=100')):
        raise ValueError('Pending access or writable deploy keys must be resolved before secure signing')
    environment = f'repos/{repository}/environments/secure-firmware-signing'
    existing = api(environment, missing_ok=True)
    if existing is None:
        if not args.apply:
            print('Secure environment missing; use --apply after local package verification')
            return
        api(environment, 'PUT', {'deployment_branch_policy': {'protected_branches': False, 'custom_branch_policies': True}})
    else:
        policy = existing.get('deployment_branch_policy') or {}
        if policy.get('protected_branches') or not policy.get('custom_branch_policies'):
            raise ValueError('Secure environment must allow only the selected main branch')
    policies_url = environment + '/deployment-branch-policies'
    policies = api(policies_url + '?per_page=100')['branch_policies']
    if not policies and args.apply:
        api(policies_url, 'POST', {'name': 'main', 'type': 'branch'})
        policies = api(policies_url + '?per_page=100')['branch_policies']
    if len(policies) != 1 or policies[0]['name'] != 'main' or policies[0].get('type') != 'branch':
        raise ValueError('Only the main branch may use the secure signing environment')
    print('Secure signing environment verified: owner only, main only')
    if args.apply:
        registry = json.loads((ROOT / 'security/secure-keyring.json').read_text())
        index = registry['active_key']
        key = load_key(args.directory, index)
        if key_identity(key) != {name: registry['keys'][index][name] for name in ('spki_sha256', 'efuse_digest')}:
            raise ValueError('Active private key does not match public registry')
        gh('secret', 'set', 'SECURE_SIGNING_KEY_PEM', '--env', 'secure-firmware-signing',
           '--repo', repository, data=pem(key), sensitive=True)
        print(f'Active K{index} uploaded to the protected signing environment.')
        if args.store_reserves:
            # Load and validate both reserves before uploading either one.
            reserves = [(i, load_key(args.directory, i)) for i in (1, 2)]
            for i, reserve in reserves:
                if key_identity(reserve) != {name: registry['keys'][i][name] for name in ('spki_sha256', 'efuse_digest')}:
                    raise ValueError(f'Reserve K{i} does not match public trust')
            recovery = f'repos/{repository}/environments/secure-key-recovery'
            state = api(recovery, missing_ok=True)
            if state is None:
                api(recovery, 'PUT', {
                    'deployment_branch_policy': {'protected_branches': False, 'custom_branch_policies': True},
                    'reviewers': [{'type': 'User', 'id': user['id']}],
                    'prevent_self_review': False, 'can_admins_bypass': False})
            state = api(recovery)
            branch_policy = state.get('deployment_branch_policy') or {}
            rules = state.get('protection_rules', [])
            reviewers = next((rule.get('reviewers', []) for rule in rules if rule['type'] == 'required_reviewers'), [])
            if branch_policy.get('protected_branches') or not branch_policy.get('custom_branch_policies') or state.get('can_admins_bypass', True) or len(reviewers) != 1 or reviewers[0].get('reviewer', {}).get('id') != user['id']:
                raise ValueError('Recovery environment must require the owner approval, disallow admin bypass and restrict branches')
            rules_url = recovery + '/deployment-branch-policies'
            branches = api(rules_url + '?per_page=100')['branch_policies']
            if not branches:
                api(rules_url, 'POST', {'name': 'main', 'type': 'branch'})
                branches = api(rules_url + '?per_page=100')['branch_policies']
            if len(branches) != 1 or branches[0]['name'] != 'main' or branches[0].get('type') != 'branch':
                raise ValueError('Only main may request recovery secrets')
            for i, reserve in reserves:
                gh('secret', 'set', f'SECURE_RESERVE_K{i}_PEM', '--env', 'secure-key-recovery',
                   '--repo', repository, data=pem(reserve), sensitive=True)
            names = {secret['name'] for secret in api(recovery + '/secrets')['secrets']}
            if not {'SECURE_RESERVE_K1_PEM', 'SECURE_RESERVE_K2_PEM'} <= names:
                raise ValueError('GitHub did not confirm both reserve secret names')
            print('K1 and K2 stored in secure-key-recovery with required owner approval; regular builds do not reference them.')


if __name__ == '__main__':
    main()
