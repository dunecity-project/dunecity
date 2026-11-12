#!/usr/bin/env python3
"""All 66 vanilla campaign missions, using scenario-matrix.py's campaign category.

One fixed seed for each authored mission ID 1–22 of Atreides, Harkonnen and Ordos.
The player house has an idle human plus Brutal QuantBot AI support; enemies use
QuantBot at --enemy-difficulty (default Easy), never CampaignAIPlayer. Native
mission victory (including spice quotas) is required in --campaign-mode full
(default) and single. All scenario-matrix options are forwarded, including --list,
--shard K/N, --results DIR, --build-dir DIR, --jobs N and --report DIR [...].
Games must run through dunecity-queue submit on a committed revision.
"""
from pathlib import Path
import runpy
import sys


def main():
    driver = Path(__file__).with_name('scenario-matrix.py')
    sys.argv = [str(driver), '--tier', 'full', '--categories', 'campaign',
                '--cells', '^campaign/player-brutal-v-quantbot-[^/]+/vanilla$', '--check', *sys.argv[1:]]
    runpy.run_path(str(driver), run_name='__main__')


if __name__ == '__main__':
    main()
