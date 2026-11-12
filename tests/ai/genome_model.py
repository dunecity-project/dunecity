"""QuantBot harness genomes: finite typed genes and ordered contextual overrides.

Registry defaults describe the shipped default INI over the C++ constructor, not
an arbitrary user's/mod's configuration. Loading a registry audits the real INI
reader, including helper-expanded difficulty keys: a new boolean cannot be
forgotten silently. No game/default configuration is written by this module.

Levels are integers (including boolean 0/1) or exact strings, never Python/JSON
floats. Predicates are case-sensitive; absent/empty categorical lists match all.
Size bounds are inclusive. Matching expression rules apply in list order, with
the last assignment to a gene winning. Phase boundaries belong to the harness;
this module only consumes opening/mid/late, never wall time or simulation RNG.

A ``"scope": "game"`` gene is a whole-game [Game Options] rule, not a QuantBot
key: it is set once per game for every house, only in a genome's top-level
values, and never rendered into a per-house INI. ``requires`` on a QuantBot gene
names the game-rule levels without which its active level has no effect.
"""
import configparser
from decimal import Decimal
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
REGISTRY_PATH = Path(__file__).with_name('genome-registry.json')
DIFFICULTIES = ('defend', 'easy', 'medium', 'hard', 'brutal')
PHASES = ('opening', 'mid', 'late')
_DIMENSIONS = ('difficulty', 'mod', 'house', 'format', 'phase')
_CONTEXT_KEYS = set(_DIMENSIONS) | {'size'}
_GENE_ID = re.compile(r'[a-z][a-z0-9]*(?:_[a-z0-9]+)*\Z')
_SLUG = re.compile(r'[a-z0-9][a-z0-9_-]*\Z')
_DECIMAL = re.compile(r'(?:0|[1-9][0-9]*)(?:\.[0-9]+)?\Z')
_REQUIRED_GENE = {'id', 'name', 'section', 'key', 'kind', 'levels', 'default', 'owner_doc'}
_OPTIONAL_GENE = {'difficulty', 'scope', 'requires'}
GAME_SCOPE = 'game'
_GETTER = re.compile(
    r'iniFile\.get(Bool|Int|Double|String)Value\(\s*'
    r'("[^"\n]+"|section)\s*,\s*(prefix\s*\+\s*"[^"\n]+"|"[^"\n]+")'
    r'(?:\s*,\s*([A-Za-z_][A-Za-z0-9_.]*)\s*(?=\)))?')
_KINDS = {'Bool': 'bool', 'Int': 'int', 'Double': 'decimal', 'String': 'enum'}


def _fail(message):
    raise ValueError(message)


def _object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            _fail('duplicate JSON key: ' + key)
        result[key] = value
    return result


def _no_float(value):
    _fail('JSON floats/non-finite numbers are forbidden; use exact decimal strings: ' + value)


def _cpp_text(text):
    """Remove C++ comments without treating quoted text as a comment."""
    return re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/',
                  lambda match: match[0] if match[0].startswith('"') else '', text)


def _block(text, start):
    depth = 1
    for index in range(start, len(text)):
        if text[index] == '{':
            depth += 1
        elif text[index] == '}':
            depth -= 1
            if depth == 0:
                return text[start:index]
    _fail('unterminated C++ config block')


def _function(text, name):
    # An earlier invocation inside ``if (readFrom(...) && ...) {`` is not
    # the definition. Anchor the reader's void/bool helpers and constructors.
    match = re.search(r'(?m)^[ \t]*(?:(?:static[ \t]+)?(?:bool|void)[ \t]+)?'
                      r'(?:[A-Za-z_]\w*::)*' + re.escape(name)
                      + r'\s*\([^;{}]*\)\s*(?:const\s*)?\{', text)
    if not match:
        _fail('cannot inventory C++ config function: ' + name)
    return _block(text, match.end())


def _reader_catalog(source):
    """Expand genuine getters, not a second hard-coded list of boolean keys.

    This understands the reader's small helper convention, not arbitrary C++.
    Unrecognized scalar getters/helpers fail closed, prompting an inventory
    update if that convention changes instead of losing genes silently.
    """
    text = _cpp_text(source)
    reader = _function(text, 'readFrom')
    catalog = {}

    def collect(body, section=None, prefix=None, member=None, parameter='settings'):
        matches = list(_GETTER.finditer(body))
        scalar_calls = len(re.findall(r'iniFile\.get(?:Bool|Int|Double)Value\(', body))
        if sum(match[1] != 'String' for match in matches) != scalar_calls:
            _fail('unrecognized scalar config getter; update genome inventory')
        for match in matches:
            raw_kind, raw_section, raw_key, fallback = match.groups()
            actual_section = section if raw_section == 'section' else raw_section[1:-1]
            actual_key = (prefix + re.search(r'"([^"]+)"', raw_key)[1]
                          if raw_key.startswith('prefix') else raw_key[1:-1])
            if actual_section is None or actual_key is None:
                _fail('unresolved config helper section/key')
            if fallback and member and fallback.startswith(parameter + '.'):
                fallback = member + fallback[len(parameter):]
            catalog[(actual_section, actual_key)] = (_KINDS[raw_kind], fallback)

    collect(reader)
    helpers = {'loadDifficultySettings', 'loadUnitRatios', 'loadRecoverySettings', 'loadPriorities'}
    if set(re.findall(r'\b(load\w+)\(iniFile,', reader)) - helpers:
        _fail('unrecognized config reader helper; update genome inventory')
    for helper, parameter in (('loadDifficultySettings', 'settings'),
                              ('loadUnitRatios', 'ratios'),
                              ('loadRecoverySettings', 'settings')):
        body = _function(text, helper)
        if helper == 'loadRecoverySettings':
            calls = re.findall(helper + r'\(iniFile,\s*"([^"]+)",\s*([\w.]+)\)', reader)
            for section, member in calls:
                collect(body, section=section, member=member, parameter=parameter)
        else:
            calls = re.findall(helper + r'\(iniFile,\s*"([^"]+)",\s*"([^"]+)",\s*([\w.]+)\)', reader)
            for section, prefix, member in calls:
                collect(body, section=section, prefix=prefix, member=member, parameter=parameter)
        if not calls:
            _fail('unrecognized config helper calls: ' + helper)

    constructor = _function(text, 'QuantBotConfig')
    for category in ('Structure', 'Unit'):
        for name, build, target in re.findall(
                r'register' + category + r'Priority\([^,]+,\s*"([^"]+)",\s*(\d+),\s*(\d+)\)', constructor):
            catalog[(category + ' Priorities', name + '_Build')] = ('int', build)
            catalog[(category + ' Priorities', name + '_Target')] = ('int', target)
    houses = re.search(r'kTunedHouses\[6\]\s*=\s*\{([^}]+)\}', reader)
    if not houses:
        _fail('cannot inventory tuned-ratio config keys')
    for house in re.findall(r'"([^"]+)"', houses[1]):
        for band in range(10):
            key = house if band == 0 else house + '_Tech' + str(band)
            catalog[('Unit Ratios Tuned', key)] = ('ratios', None)
    for key in ('MixWeights', 'TempoWeights'):
        catalog[('Strategy', key)] = ('weights', None)
    return catalog


def _scalar(text, kind):
    text = text.strip()
    if kind == 'bool':
        if text.lower() in ('true', 'enabled', 'on', '1'):
            return 1
        if text.lower() in ('false', 'disabled', 'off', '0'):
            return 0
    elif kind == 'int':
        # INIFile::Key::getIntValue uses atol: the shipped Hard multiplier
        # "2.5" is actually 2. Never offer a fractional level for that int key.
        match = re.match(r'[+-]?[0-9]+', text)
        if match:
            return int(match[0])
    elif kind == 'decimal':
        text = text.removesuffix('f')
        if _DECIMAL.fullmatch(text):
            return text
    else:
        return text
    _fail('unrecognized ' + kind + ' config default: ' + text)


def _tuned_defaults(constructor, houses):
    defaults = {(house, band): '' for house in range(len(houses)) for band in range(10)}
    assignment = r'tunedRatios\[(\d+)\]\[(\d+)\]\s*=\s*\{([0-9,\s]+)\}'
    for house, band, values in re.findall(assignment, constructor):
        defaults[(int(house), int(band))] = ''.join(values.split())
    # Read the actual shared lower-band loop and its optional exclusion rather
    # than duplicate the constructor's numbers in a second default table.
    loops = re.finditer(r'for\s*\(const size_t house\s*:\s*\{([^}]+)\}\)\s*\{', constructor)
    for loop in loops:
        body = _block(constructor, loop.end())
        for house in map(int, re.findall(r'size_t\((\d+)\)', loop[1])):
            for excluded, band, values in re.findall(
                    r'(?:if\s*\(house\s*!=\s*(\d+)\)\s*)?tunedRatios\[house\]\[(\d+)\]\s*=\s*\{([0-9,\s]+)\}', body):
                if not excluded or house != int(excluded):
                    defaults[(house, int(band))] = ''.join(values.split())
    return {('Unit Ratios Tuned', house if band == 0 else house + '_Tech' + str(band)): defaults[(index, band)]
            for index, house in enumerate(houses) for band in range(10)}


@lru_cache(maxsize=1)
def _source_catalog():
    source = _cpp_text((ROOT / 'src/players/QuantBotConfig.cpp').read_text(encoding='utf-8'))
    strategy = _cpp_text((ROOT / 'src/players/QuantBotStrategy.cpp').read_text(encoding='utf-8'))
    catalog = _reader_catalog(source)
    constructor = _function(source, 'QuantBotConfig')
    assignments = dict(re.findall(r'^\s*([\w.]+)\s*=\s*([^;]+);', constructor, re.MULTILINE))
    defaults = {}
    for pair, (kind, member) in catalog.items():
        if kind in ('bool', 'int', 'decimal'):
            raw = assignments.get(member, member)
            if raw is None:
                _fail('cannot find compiled config default: ' + repr(pair))
            defaults[pair] = _scalar(raw, kind)
    defaults[('General Behavior', 'WaveFormation')] = 'None'
    settings = _function(strategy, 'Settings')
    mix = re.search(r'fixedMix\s*=\s*Mix::(\w+)', settings)
    tempo = re.search(r'fixedTempo\s*=\s*Tempo::(\w+)', settings)
    random = re.search(r'random\s*=\s*(true|false)', settings)
    if not (mix and tempo and random):
        _fail('cannot inventory strategy constructor defaults')
    defaults[('Strategy', 'Mode')] = 'Random' if random[1] == 'true' else 'Fixed:' + mix[1] + ',' + tempo[1]
    mix_names = re.findall(r'"([^"]+)"', re.search(r'kMixNames\[kMixCount\]\s*=\s*\{([^}]+)\}', strategy)[1])
    tempo_names = re.findall(r'"([^"]+)"', re.search(r'kTempoNames\[kTempoCount\]\s*=\s*\{([^}]+)\}', strategy)[1])
    for key, factor, names in (('MixWeights', 'Mix', mix_names), ('TempoWeights', 'Tempo', tempo_names)):
        weights = dict(re.findall(key[0].lower() + key[1:] +
                                 r'\[static_cast<size_t>\(' + factor + r'::(\w+)\)\]\s*=\s*(\d+)', settings))
        defaults[('Strategy', key)] = ','.join(name + ':' + weights[name] for name in names if name in weights)
    houses = re.findall(r'"([^"]+)"', re.search(r'kTunedHouses\[6\]\s*=\s*\{([^}]+)\}', source)[1])
    defaults.update(_tuned_defaults(constructor, houses))
    ini = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=(';', '#'))
    ini.optionxform = str
    ini.read(ROOT / 'config/QuantBot Config.ini.default', encoding='utf-8')
    for section in ini.sections():
        for key, value in ini.items(section):
            pair = (section, key)
            if pair not in catalog:
                _fail('default INI key is missing from the parser inventory: ' + repr(pair))
            defaults[pair] = _scalar(value, catalog[pair][0])
    return catalog, defaults, tuple(mix_names), tuple(tempo_names)


@lru_cache(maxsize=1)
def _game_option_catalog():
    """[Game Options] booleans of the requested-options reader and their compiled defaults.

    Keys come from ``applyGameOptionsFromConfig`` (the harness's requested-options
    path); defaults from GameOptionsClass in-class initializers. A rule without a
    compiled initializer has no auditable default and maps to None (fails closed).
    """
    reader = _function(_cpp_text((ROOT / 'src/globals.cpp').read_text(encoding='utf-8')),
                       'applyGameOptionsFromConfig')
    keys = dict(re.findall(r'\breadBool\(\s*"([^"\n]+)"\s*,\s*options\.(\w+)\s*\)', reader))
    header = _cpp_text((ROOT / 'include/DataTypes.h').read_text(encoding='utf-8'))
    declaration = re.search(r'\bclass\s+GameOptionsClass\s*\{', header)
    if not keys or not declaration:
        _fail('cannot inventory [Game Options] rules')
    members = _block(header, declaration.end())
    defaults = {member: _scalar(value, 'bool')
                for member, value in re.findall(r'\bbool\s+(\w+)\s*=\s*(true|false)\s*;', members)}
    return {key: defaults.get(member) for key, member in keys.items()}


def is_game_rule(gene):
    return gene.get('scope') == GAME_SCOPE


def _same_level(left, right):
    return type(left) is type(right) and left == right


def _level_valid(level, gene, mix_names, tempo_names):
    kind, key, section = gene['kind'], gene['key'], gene['section']
    if kind in ('bool', 'int'):
        return (type(level) is int and 0 <= level <= 2147483647
                and (kind != 'bool' or level in (0, 1))
                and (section != 'Force Concentration' or key != 'WaveReadinessPercent' or 10 <= level <= 300)
                and (section != 'Force Concentration' or key != 'LauncherKiteExtraDistance' or level <= 2))
    if type(level) is not str or any(char in level for char in '\r\n;#[]'):
        return False
    if kind == 'decimal':
        return bool(_DECIMAL.fullmatch(level))
    if kind == 'weights':
        names = mix_names if key == 'MixWeights' else tempo_names
        parsed = [entry.split(':') for entry in level.split(',')]
        return (all(len(entry) == 2 and entry[0] in names and re.fullmatch(r'[0-9]{1,6}', entry[1]) for entry in parsed)
                and len({entry[0] for entry in parsed}) == len(parsed)
                and sum(int(entry[1]) for entry in parsed) > 0)
    if kind == 'ratios':
        if level == '' or (level == 'Opening' and '_Tech' in key):
            return True
        parts = level.split(',')
        return (len(parts) == 10 and all(re.fullmatch(r'[0-9]{1,6}', part) for part in parts)
                and sum(map(int, parts)) > 0)
    if section == 'General Behavior' and key == 'WaveFormation':
        return level in ('None', 'Blob', 'Box', 'Pincer', 'Column', 'Staggered', 'Echelon')
    if section == 'Strategy' and key == 'Mode':
        return level == 'Random' or any(level == 'Fixed:' + mix + ',' + tempo
                                        for mix in mix_names for tempo in tempo_names)
    return False


def _validate_game_rule(gene):
    gene_id = gene['id']
    if gene['section'] != 'Game Options' or 'difficulty' in gene or 'requires' in gene:
        _fail(gene_id + ': a game rule is one [Game Options] key without difficulty or dependencies')
    catalog = _game_option_catalog()
    if gene['key'] not in catalog or gene['kind'] != 'bool':
        _fail(gene_id + ': unknown or non-boolean [Game Options] rule: ' + repr(gene['key']))
    if catalog[gene['key']] is None:
        _fail(gene_id + ': game rule has no compiled default to audit')
    if not all(type(level) is int and level in (0, 1) for level in gene['levels']):
        _fail(gene_id + ': level is not accepted unchanged by the game options reader')
    if not _same_level(gene['default'], catalog[gene['key']]):
        _fail(gene_id + ': default differs from the compiled game rule: ' + repr(catalog[gene['key']]))


def _validate_dependencies(registry):
    genes = {gene['id']: gene for gene in registry}
    for gene in registry:
        if 'requires' not in gene:
            continue
        requires = gene['requires']
        if not isinstance(requires, dict) or not requires:
            _fail(gene['id'] + ': requires must name game rules and levels')
        for rule_id, level in requires.items():
            rule = genes.get(rule_id)
            if rule is None or not is_game_rule(rule):
                _fail(gene['id'] + ': requires an unregistered game rule: ' + repr(rule_id))
            if not any(_same_level(level, candidate) for candidate in rule['levels']) or _same_level(level, rule['default']):
                _fail(gene['id'] + ': requires a registered non-default level of ' + rule_id)


def _validate_registry(registry, complete=False):
    if not isinstance(registry, list) or not registry:
        _fail('registry must be a nonempty list of genes')
    catalog, defaults, mixes, tempos = _source_catalog()
    ids, keys = set(), set()
    for gene in registry:
        if not isinstance(gene, dict) or not _REQUIRED_GENE <= gene.keys() or gene.keys() - (_REQUIRED_GENE | _OPTIONAL_GENE):
            _fail('registry gene has missing or unknown fields')
        gene_id = gene['id']
        if not isinstance(gene_id, str) or not _GENE_ID.fullmatch(gene_id) or gene_id in ids:
            _fail('invalid or duplicate registry gene id: ' + repr(gene_id))
        ids.add(gene_id)
        for field in ('name', 'section', 'key', 'kind', 'owner_doc'):
            if not isinstance(gene[field], str) or not gene[field].strip():
                _fail(gene_id + ': ' + field + ' must be a nonempty string')
        if 'scope' in gene and gene['scope'] != GAME_SCOPE:
            _fail(gene_id + ': scope must be "game" or absent (QuantBot INI)')
        pair = (gene['section'], gene['key'])
        if pair in keys:
            _fail(gene_id + ': duplicate INI section/key: ' + repr(pair))
        keys.add(pair)
        levels = gene['levels']
        if not isinstance(levels, list) or len(levels) < 2:
            _fail(gene_id + ': levels must contain at least two choices')
        for index, level in enumerate(levels):
            if any(_same_level(level, previous) for previous in levels[:index]):
                _fail(gene_id + ': duplicate level: ' + repr(level))
        if not any(_same_level(gene['default'], level) for level in levels):
            _fail(gene_id + ': default is not one of its levels')
        if is_game_rule(gene):
            _validate_game_rule(gene)
            continue
        if pair not in catalog:
            _fail(gene_id + ': unknown or duplicate INI section/key: ' + repr(pair))
        if gene['kind'] != catalog[pair][0]:
            _fail(gene_id + ': kind does not match the actual INI getter')
        for level in levels:
            if not _level_valid(level, gene, mixes, tempos):
                _fail(gene_id + ': level is not accepted unchanged by the INI parser: ' + repr(level))
        default = gene['default']
        expected = defaults.get(pair)
        equal = (Decimal(default) == Decimal(expected) if gene['kind'] == 'decimal'
                 else _same_level(default, expected))
        if not equal:
            _fail(gene_id + ': default differs from effective shipped configuration: ' + repr(expected))
        if 'difficulty' in gene:
            _categorical(gene['difficulty'], 'difficulty', gene_id)
    _validate_dependencies(registry)
    if complete:
        missing = sorted(pair for pair, (kind, _) in catalog.items() if kind == 'bool' and pair not in keys)
        if missing:
            _fail('registry is missing boolean config keys: ' + repr(missing))
        missing_enums = sorted(pair for pair, (kind, _) in catalog.items() if kind == 'enum' and pair not in keys)
        if missing_enums:
            _fail('registry is missing enum config keys: ' + repr(missing_enums))
    return registry


def load_registry(path=None):
    """Load and audit a full registry; duplicate JSON fields/floats are errors."""
    registry = json.loads(Path(path if path is not None else REGISTRY_PATH).read_text(encoding='utf-8'),
                          object_pairs_hook=_object, parse_float=_no_float, parse_constant=_no_float)
    return _validate_registry(registry, complete=True)


def _registry(registry):
    # Explicit lists may be useful subsets, but their keys/types/defaults must
    # still describe real parser inputs. Only file loads require full coverage.
    return load_registry() if registry is None else _validate_registry(registry)


def default_genome(registry=None):
    """Return a sparse baseline genome, resolving omitted genes to defaults.

    Sparse rendering preserves each mod's native baseline, while explicit
    assignments (even to the registry default) remain real overrides.
    """
    _registry(registry)
    return {'id': 'default', 'values': {}, 'expression': []}


def _categorical(values, dimension, location):
    if not isinstance(values, list) or any(type(value) is not str or not value or value != value.strip()
                                           for value in values):
        _fail(location + ': ' + dimension + ' must be a list of nonempty strings')
    allowed = DIFFICULTIES if dimension == 'difficulty' else PHASES if dimension == 'phase' else None
    if allowed and any(value not in allowed for value in values):
        _fail(location + ': unknown ' + dimension + ' value')
    if len(set(values)) != len(values):
        _fail(location + ': duplicate ' + dimension + ' predicate value')


def _values(values, location, genes=None):
    if not isinstance(values, dict):
        _fail(location + ': values must be an object')
    for gene_id, level in values.items():
        if not isinstance(gene_id, str) or not _GENE_ID.fullmatch(gene_id):
            _fail(location + ': invalid gene id: ' + repr(gene_id))
        if type(level) not in (int, str):
            _fail(location + ': levels must be integers or exact strings, not booleans/floats')
        if genes is not None:
            if gene_id not in genes:
                _fail(location + ': unknown gene: ' + gene_id)
            if not any(_same_level(level, candidate) for candidate in genes[gene_id]['levels']):
                _fail(location + ': unknown level for ' + gene_id + ': ' + repr(level))


def _required(genome, genes):
    """Game-rule levels needed by active dependent genes anywhere in a genome.

    Active means assigned (top-level or in any rule) to a non-default level.
    Returns rule id -> (level, dependent gene id).
    """
    needed = {}
    for values in [genome.get('values', {})] + [rule['values'] for rule in genome.get('expression', [])]:
        for gene_id, level in values.items():
            gene = genes[gene_id]
            if 'requires' not in gene or _same_level(level, gene['default']):
                continue
            for rule_id, required in gene['requires'].items():
                if rule_id in needed and not _same_level(needed[rule_id][0], required):
                    _fail('genome: ' + gene_id + ' and ' + needed[rule_id][1] + ' need different ' + rule_id + ' levels')
                needed.setdefault(rule_id, (required, gene_id))
    return needed


def _validate_shape(genome, genes=None, dependencies=True):
    if not isinstance(genome, dict) or genome.keys() - {'id', 'values', 'expression'}:
        _fail('genome must be an object with only id, values and expression')
    if not isinstance(genome.get('id'), str) or not _SLUG.fullmatch(genome['id']):
        _fail('genome id must be a lowercase safe slug')
    _values(genome.get('values', {}), 'genome', genes)
    rules = genome.get('expression', [])
    if not isinstance(rules, list):
        _fail('expression must be a list of rules')
    for index, rule in enumerate(rules):
        location = 'expression[' + str(index) + ']'
        if not isinstance(rule, dict) or 'values' not in rule or rule.keys() - {'when', 'values'}:
            _fail(location + ': rule must contain values and optional when only')
        _values(rule['values'], location, genes)
        if genes is not None and any(is_game_rule(genes[gene_id]) for gene_id in rule['values']):
            _fail(location + ': a game rule is one whole-game top-level value, not contextual expression')
        when = rule.get('when', {})
        if not isinstance(when, dict) or when.keys() - (set(_DIMENSIONS) | {'size_min', 'size_max'}):
            _fail(location + ': unknown or malformed when predicate')
        for dimension in _DIMENSIONS:
            if dimension in when:
                _categorical(when[dimension], dimension, location)
        for bound in ('size_min', 'size_max'):
            if bound in when and (type(when[bound]) is not int or when[bound] < 0):
                _fail(location + ': ' + bound + ' must be a nonnegative integer')
        if 'size_min' in when and 'size_max' in when and when['size_min'] > when['size_max']:
            _fail(location + ': size_min exceeds size_max')
    if genes is not None and dependencies:
        values = genome.get('values', {})
        for rule_id, (level, dependent) in sorted(_required(genome, genes).items()):
            if not _same_level(values.get(rule_id, genes[rule_id]['default']), level):
                _fail('genome: active ' + dependent + ' requires game rule ' + rule_id + '=' + str(level))


def validate_genome(genome, registry=None):
    """Reject unknown genes/levels/fields, contextual game rules and unmet dependencies."""
    _validate_shape(genome, {gene['id']: gene for gene in _registry(registry)})


def close_game_rules(genome, registry=None):
    """Copy a genome with exactly the game rules its active genes require.

    Search operators use this so that game rules follow their dependent genes
    instead of becoming independent loci; unrequired rules are dropped (sparse).
    """
    genes = {gene['id']: gene for gene in _registry(registry)}
    _validate_shape(genome, genes, dependencies=False)
    closed = json.loads(json.dumps(genome))
    values = closed.setdefault('values', {})
    needed = _required(closed, genes)
    for gene_id, gene in genes.items():
        if not is_game_rule(gene):
            continue
        if gene_id in needed:
            values[gene_id] = needed[gene_id][0]
        else:
            values.pop(gene_id, None)
    return closed


def game_rules(genomes, registry=None):
    """One explicit whole-game level for every registered game rule in a roster.

    Every arm of one game plays under the same rule. Explicit roster values must
    agree; an arm that does not set a rule adopts the game's level. All rules are
    returned (registry default when nobody sets one), so the harness requests and
    records the rule explicitly for both arms instead of relying on mod defaults.
    """
    genes = {gene['id']: gene for gene in _registry(registry)}
    chosen = {}
    for genome in genomes:
        _validate_shape(genome, genes)
        for gene_id, level in sorted(genome.get('values', {}).items()):
            if not is_game_rule(genes[gene_id]):
                continue
            if gene_id in chosen and not _same_level(chosen[gene_id][0], level):
                _fail('conflicting whole-game rule ' + gene_id + ': ' + chosen[gene_id][1] + ' versus ' + genome['id'])
            chosen.setdefault(gene_id, (level, genome['id']))
    return {gene_id: chosen[gene_id][0] if gene_id in chosen else gene['default']
            for gene_id, gene in sorted(genes.items()) if is_game_rule(gene)}


def _validate_context(context):
    if not isinstance(context, dict) or context.keys() != _CONTEXT_KEYS:
        _fail('context must contain difficulty, mod, house, format, size and phase only')
    for dimension in _DIMENSIONS:
        _categorical([context[dimension]], dimension, 'context')
    if type(context['size']) is not int or context['size'] <= 0:
        _fail('context size must be a positive integer')


def _matches(when, context):
    return (all(not when.get(dimension) or context[dimension] in when[dimension] for dimension in _DIMENSIONS)
            and context['size'] >= when.get('size_min', 0)
            and ('size_max' not in when or context['size'] <= when['size_max']))


def resolve(genome, context, registry=None):
    """Resolve defaults, sparse base values, then every matching ordered rule."""
    genes = _registry(registry)
    _validate_shape(genome, {gene['id']: gene for gene in genes})
    _validate_context(context)
    values = {gene['id']: gene['default'] for gene in genes}
    values.update(genome.get('values', {}))
    for rule in genome.get('expression', []):
        if _matches(rule.get('when', {}), context):
            values.update(rule['values'])
    return values


def render_ini(genome, context, registry=None):
    """Render explicit phase overrides for QuantBotConfig::applyOverrides.

    Unassigned genes leave the native mod baseline untouched. Each phase must
    start from a fresh baseline copy, so earlier-only assignments cannot leak
    into later phases. The engine hashes the actual resulting configuration.
    """
    genes = _registry(registry)
    values = resolve(genome, context, genes)
    explicit = set(genome.get('values', {}))
    for rule in genome.get('expression', []):
        if _matches(rule.get('when', {}), context):
            explicit.update(rule['values'])
    sections = {}
    for gene in genes:
        # Game rules are requested once per game by the harness, not per house.
        if gene['id'] in explicit and not is_game_rule(gene):
            sections.setdefault(gene['section'], {})[gene['key']] = str(values[gene['id']])
    return '\n\n'.join('[' + section + ']\n' + '\n'.join(key + '=' + value for key, value in sorted(entries.items()))
                        for section, entries in sorted(sections.items())) + ('\n' if sections else '')


def genome_id(genome):
    """SHA-256 of the genotype, excluding its human-readable id label.

    Mapping order, predicate-set order and omitted/empty optional fields are
    immaterial; expression rule order remains significant. This identity needs
    no implicit registry (and therefore works with explicit subset lists).
    It is a genotype digest, not the engine's effective configuration hash.
    """
    _validate_shape(genome)
    rules = []
    for rule in genome.get('expression', []):
        when = {key: sorted(value) if key in _DIMENSIONS else value
                for key, value in rule.get('when', {}).items() if key not in _DIMENSIONS or value}
        rules.append({'when': when, 'values': rule['values']})
    canonical = json.dumps({'values': genome.get('values', {}), 'expression': rules},
                           sort_keys=True, separators=(',', ':'), ensure_ascii=True)
    return hashlib.sha256(canonical.encode('utf-8')).hexdigest()
