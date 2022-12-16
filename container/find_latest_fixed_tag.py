#!/bin/env python3

import json
import os
import shutil
import sys
from argparse import ArgumentParser, ArgumentDefaultsHelpFormatter

argument_parser = ArgumentParser(formatter_class=ArgumentDefaultsHelpFormatter)
argument_parser.add_argument('--prefix', nargs='?', help='tag prefix to match')
argument_parser.add_argument('--suffix', nargs='?', help='tag suffix to match')
argument_parser.add_argument('--blacklist-contains', nargs='+', help='tags will be excluded from search if they contain this blacklisted string')
argument_parser.add_argument('--blacklist-exact', nargs='+', help='tags will be excluded from search if they match this exact string')
argument_parser.add_argument('--print-only-variable-part', action='store_true', help='if set, instead of the full tag name only the variable part is printed')
argument_parser.add_argument('--zero-padding', help='length to pad numeric parts of version tuples using zeros (increase in case of very long version number parts)', default=8)
argument_parser.add_argument('--debug', action='store_true', help='prints debug output')
argument_parser.add_argument('image', help='image to search tag for')
args = argument_parser.parse_args()

def debug(s):
    if args.debug:
        print(s)

def error(s):
    sys.stderr.write(s)
    sys.stderr.write('\n')
    sys.stderr.flush()

if not args.prefix is None and not args.prefix[-1] in '-_.':
    error('Warning: tag prefix does not appear to end in a separator; e.g. if tag schema is "focal-123" provide "focal-" as prefix argument, not just "focal"')

if not args.suffix is None and not args.suffix[0] in '-_.':
    error('Warning: tag suffix does not appear to start with a separator; e.g. if tag schema is "123-slim" provide "-slim" as suffix argument, not just "slim"')

def list_tags(image_name):
    tags = []
    try:
        skopeo_path = shutil.which('skopeo')
        if skopeo_path is None:
            print('Skopeo needs to be installed first! See: https://github.com/containers/skopeo')
            sys.exit(1)

        skopeo_cmd = '%s list-tags docker://%s' % (skopeo_path, image_name)
        skopeo_pipe = os.popen(skopeo_cmd, 'r')
        skopeo_jsons = '\n'.join(skopeo_pipe.readlines())
        skopeo_pipe.close()
        
        skopeo_json = json.loads(skopeo_jsons)
        
        tags = skopeo_json['Tags']
    except:
        error('Failed to load tags for %s' % image_name)
        sys.exit(1)

    if tags is None or len(tags) == 0:
        error('No tags found for %s' % image_name)
        sys.exit(1)
    
    return tags

def remove_prefix(prefix, s):
    if s.startswith(prefix):
        return s[len(prefix):]
    else:
        return s

def remove_suffix(suffix, s):
    if s.endswith(suffix):
        return s[:-len(suffix)]
    else:
        return s

def version_to_tuple(s):
    parts = []
    for part in s.split('.'):
        if part.isnumeric():
            part = part.zfill(args.zero_padding)
        parts.append(part)
    
    return tuple(parts)

def extract(tag):
    if not args.prefix is None:
        tag = remove_prefix(args.prefix, tag)

    if not args.suffix is None:
        tag = remove_suffix(args.suffix, tag)
    
    return tag

def is_blacklisted(tag):
    if not args.blacklist_exact is None:
        for unwanted in args.blacklist_exact:
            if unwanted == tag:
                return True

    if not args.blacklist_contains is None:
        for unwanted in args.blacklist_contains:
            if unwanted in tag:
                return True
    
    return False

def is_basic_match(tag):
    if not args.prefix is None and not tag.startswith(args.prefix):
        return False
    
    if not args.suffix is None and not tag.endswith(args.suffix):
        return False
    
    return True

latest_version = None
for tag in list_tags(args.image):
    if not is_basic_match(tag) or is_blacklisted(tag):
        continue

    tag_version = extract(tag)
    version_tuple = version_to_tuple(tag_version)

    if latest_version is None or version_tuple > latest_version[1]:
        latest_version = [tag, version_tuple]
        debug('New: %s' % (latest_version))
    else:
        debug('Old: %s' % ([tag, version_tuple]))

if latest_version is None:
    error('No latest version could be identified from tags.')
    sys.exit(1)

latest_tag = latest_version[0]
if args.print_only_variable_part:
    print(extract(latest_tag))
else:
    print(latest_tag)
