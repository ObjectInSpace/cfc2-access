"""Offline reference for PWS2-R02 v1; no process access or hook installation.

The injected reader is a test boundary, NOT a native lifetime guard. Successful
brackets cannot exclude ABA or torn records. No event authorizes audio.
"""
import math
import struct

ROOT_SLOT = 0x14262F0B0
CONTEXT_SLOT = 0x142C79B40
CALLER = 0x140A5DF81
CALLBACKS = (0x140AFAB70, 0x140AF9A90)
MAX_RESOURCES = 8
MAX_READS = 10
MAX_BYTES = 904
MAX_COMMANDS = 64
MAX_COMMAND_BYTES = 65536
U64_MAX = (1 << 64) - 1


def checked_range(address, size):
    if not 0 < address <= U64_MAX or not 0 < size <= U64_MAX - address:
        raise ValueError('address_range')
    return address


class BudgetReader:
    def __init__(self, read):
        self.read = read
        self.calls = self.bytes = 0

    def take(self, address, size):
        checked_range(address, size)
        if self.calls + 1 > MAX_READS or self.bytes + size > MAX_BYTES:
            raise ValueError('read_budget')
        # Charge attempts, including short reads/faults. Never retry.
        self.calls += 1
        self.bytes += size
        raw = self.read(address, size)
        if not isinstance(raw, bytes) or len(raw) != size:
            raise OSError('short_read')
        return raw


def at(raw, offset, fmt):
    return struct.unpack_from('<' + fmt, raw, offset)[0]


def capture_matrix_load(read, *, root, object_address, resource_address,
                        matrix_address, caller, invocation, revision):
    """One pre-copy event under an external invocation/revision admission gate.

    root must come from the separately checked PWS2 identity bracket. The
    returned addresses are opaque. No selected-slot value is substituted for
    the resource matrix about to be copied, and no ownership is cached.
    """
    result = dict(schema=1, stage='list8_before_matrix_copy',
                  evidence_kind='offline_reference', audio_eligible=False,
                  invocation=invocation, revision=revision,
                  object=object_address, resource=resource_address,
                  records={}, issues=[])
    reader = BudgetReader(read)

    def take(name, address, size):
        raw = reader.take(address, size)
        result['records'][name] = raw.hex()
        return raw

    try:
        if caller != CALLER or invocation <= 0 or revision < 0:
            raise ValueError('stage_rejected')
        checked_range(root, 0x8CD40)
        checked_range(object_address, 0xE0 + MAX_RESOURCES * 8)
        checked_range(resource_address, 0xA8)
        if matrix_address != resource_address + 0x34:
            raise ValueError('matrix_argument')
        delta = resource_address - (root + 0x55010)
        if delta < 0 or delta >= 640 * 0xA8 or delta % 0xA8:
            raise ValueError('resource_pool')
        before_root = take('root_before', ROOT_SLOT, 8)
        if at(before_root, 0, 'Q') != root:
            raise ValueError('root_changed')
        before_context = take('context_before', CONTEXT_SLOT, 8)
        context = at(before_context, 0, 'Q')
        checked_range(context, 0x1F81AC)
        obj = take('object', object_address, 0xE0)
        count = at(obj, 0xD8, 'i')
        if not 1 <= count <= MAX_RESOURCES:
            raise ValueError('resource_count')
        pointers = take('association', object_address + 0xE0, count * 8)
        matches = [i for i in range(count)
                   if at(pointers, i * 8, 'Q') == resource_address]
        if len(matches) != 1:
            raise ValueError('association_ambiguous')
        result['ordinal'] = matches[0]
        owner = at(obj, 2, 'b')
        result.update(owner_raw=owner, layout_raw=obj[1],
                      callback=at(obj, 8, 'Q'), state_raw=at(obj, 0x28, 'i'),
                      object_word=at(obj, 0x38, 'H'))
        if obj[0] != 8 or result['callback'] not in CALLBACKS:
            result['issues'].append('unreviewed_object')
        if owner not in range(4):
            result['issues'].append('invalid_owner')
        if not obj[0xC0]:
            result['issues'].append('disabled_object')
        res = take('resource', resource_address, 0xA8)
        if not res[0]:
            result['issues'].append('disabled_resource')
        if res[1] != 0:
            result['issues'].append('unreviewed_resource_type')
        packet = at(res, 8, 'Q')
        header = take('packet_header', packet, 0x18)
        if at(header, 0, 'i') < 0:
            result['issues'].append('negative_packet_header')
        slots = take('selector_and_slots_before', context + 0x1F80A4, 0x108)
        result['selected_slot'] = at(slots, 0, 'i')
        if result['selected_slot'] != 1:
            result['issues'].append('wrong_selected_slot')
        globals_raw = take('viewport_and_template', 0x141C00210, 0x80)
        floats = (struct.unpack_from('<16f', res, 0x34) +
                  struct.unpack_from('<64f', slots, 8) +
                  struct.unpack('<32f', globals_raw))
        if not all(math.isfinite(f) for f in floats):
            result['issues'].append('nonfinite_matrix')
        after_root = take('root_after', ROOT_SLOT, 8)
        after_context = take('context_after', CONTEXT_SLOT, 8)
        if before_root != after_root or before_context != after_context:
            result['issues'].append('identity_changed')
        result['status'] = 'observed_with_issues' if result['issues'] else 'matching_brackets_only'
    except (OSError, ValueError) as exc:
        result['status'] = 'rejected'
        result['issues'].append(str(exc))
    result.update(read_attempts=reader.calls, requested_bytes=reader.bytes)
    return result


def command7_rows(payload):
    """Copied command-7 bytes -> three transposed rows, preserving float bits."""
    if len(payload) != 64:
        raise ValueError('command7_size')
    return b''.join(payload[(column * 4 + row) * 4:(column * 4 + row + 1) * 4]
                    for row in range(3) for column in range(4))


def decode_commands(blob, count):
    """Strict bounded snapshot decoder, NOT an emulation of native trust rules.

    Require a complete caller-supplied byte window. Unknown payloads stay raw;
    no native pointer is followed. Matrix commands have exact reviewed sizes.
    """
    if not isinstance(blob, bytes) or len(blob) > MAX_COMMAND_BYTES:
        raise ValueError('command_byte_budget')
    if not 0 <= count <= MAX_COMMANDS:
        raise ValueError('command_count')
    records = []
    offset = 0
    for _ in range(count):
        if len(blob) - offset < 24:
            raise ValueError('short_header')
        command, size, items, a, b, c = struct.unpack_from('<6i', blob, offset)
        if size <= 0 or size > len(blob) - offset - 24:
            raise ValueError('payload_bounds')
        payload = blob[offset + 24:offset + 24 + size]
        if command in (4, 7) and (size != {4: 128, 7: 64}[command] or items != 1):
            raise ValueError('matrix_command_shape')
        record = dict(offset=offset, command=command, size=size, count=items,
                      metadata=(a, b, c), payload=payload.hex())
        if command == 7:
            record['uploaded_rows'] = command7_rows(payload).hex()
        records.append(record)
        offset += 24 + size
    if offset != len(blob):
        raise ValueError('trailing_bytes')
    return records
