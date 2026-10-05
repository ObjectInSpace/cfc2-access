"""Retain actor/display ownership evidence from the pinned offline dump."""
import argparse
import hashlib
import json
from pathlib import Path

from collection_lifecycle_audit import CODE_SHA256, DEFAULT_DUMP, Dump


SPANS = (
    ('display-publication-and-layout-routing', 0x140AFA010, 0x140AFA45C),
    ('indexed-gauge-publication', 0x140AFB400, 0x140AFB695),
    ('indexed-gauge-update', 0x140AFAB70, 0x140AFB3F8),
    ('special-gauge-update', 0x140AF9A90, 0x140AFA006),
    ('display-allocation-wrapper', 0x140AEFC30, 0x140AEFC98),
    ('general-pool-list-publication', 0x140A4BC00, 0x140A4BCFB),
    ('included-actor-setup-loop', 0x140A667F0, 0x140A6683E),
    ('actor-resource-setup', 0x140A66020, 0x140A667E9),
    ('actor-reset-through-gauge-initialization-prefix', 0x140A669B0, 0x140A66AA8),
    ('display-outer-state-dispatch-prefix', 0x140A6ED60, 0x140A6EE80),
    ('display-publisher-state-machine', 0x140A6E250, 0x140A6E7D7),
    ('display-resource-allocation', 0x140A63CA0, 0x140A63D25),
    ('display-resource-pool-initialize', 0x140A643D0, 0x140A64498),
    ('display-resource-release', 0x140A64D20, 0x140A64D8E),
    ('display-object-release', 0x140A4BA80, 0x140A4BBF9),
    ('display-resource-binding', 0x140A60AA0, 0x140A60B01),
    ('list-eight-update-before-matrix-build', 0x140A4C710, 0x140A4C7D6),
    ('list-resource-matrix-build', 0x140A7C600, 0x140A7C8FF),
    ('list-eight-draw', 0x140A5DE70, 0x140A5E05D),
    ('later-list-projection-setup', 0x140A5D821, 0x140A5D86B),
    ('resource-conditional-geometry-update', 0x140AEE2E0, 0x140AEE5A5),
    ('component-rotation-a', 0x140F33250, 0x140F332D5),
    ('component-rotation-b', 0x140F332E0, 0x140F33365),
    ('component-rotation-c', 0x140F33370, 0x140F333F5),
    ('component-scale-premultiply', 0x140F33450, 0x140F334AE),
    ('component-translation-replace', 0x140F334B0, 0x140F334C9),
    ('component-translation-premultiply', 0x140F33520, 0x140F3357E),
    ('fixed-actor-value-reset-call-loop', 0x140A6DFD0, 0x140A6DFF5),
    ('actor-value-zero-or-one-writer', 0x140AD44F0, 0x140AD4577),
    ('render-submit-default-scalar', 0x140F35EF0, 0x140F35F15),
    ('render-submit-explicit-scalar', 0x140F36410, 0x140F36435),
    ('render-packet-segment-submission', 0x140F34CF0, 0x140F34E34),
    ('render-queue-build', 0x140F2F390, 0x140F2FB39),
    ('render-queue-drain', 0x140F2E990, 0x140F2ED7E),
    ('render-command-copy', 0x140048810, 0x1400488AD),
    ('queued-render-matrix-entry-prefix', 0x140F340B0, 0x140F34106),
    ('queued-render-vertex-submission-tail', 0x140F34A4B, 0x140F34AE5),
    ('matrix-copy-entry-abi', 0x140F32440, 0x140F32487),
    ('indexed-actor-subtraction-caller', 0x140AD1296, 0x140AD12D9),
    ('descriptor-driven-actor-subtraction', 0x140AD17B0, 0x140AD1B9E),
    ('render-queue-sort-value', 0x140F2EE80, 0x140F2EF2D),
    ('vertex-command-session-gate', 0x1400487C0, 0x14004880D),
    ('vertex-command-buffer-copy', 0x14004CA10, 0x14004CAB3),
    ('command-buffer-reset', 0x140048E90, 0x140048ECB),
    ('command-consumer-entry', 0x140073AC0, 0x140073B38),
    ('command-consumer-walk', 0x14007421D, 0x1400742DC),
    ('command-consumer-loop-back', 0x14007433D, 0x140074379),
    ('command-consumer-dispatch-through-four', 0x140074785, 0x140074950),
    ('command-seven-transpose-upload', 0x140074C1C, 0x140074DFA),
    ('command-four-setup-helper', 0x1402E1B90, 0x1402E3C66),
)

# Pin numerical operands outside .text independently. These are rendering
# operands, not an assertion of screen coordinates or an allowed health range.
DATA = (
    ('gauge-divisor', 0x141017210, '00007a44'),
    ('gauge-multiplier', 0x1410DDF80, '00008e42'),
    ('gauge-percent-divisor', 0x140FE5400, '0000c842'),
    ('gauge-offset', 0x1410A40F8, 'e17a943e'),
    ('layout-step', 0x1410DDF84, '00001f43'),
    ('layout-origin', 0x1410712D4, '00008c43'),
    ('layout-sixth-origin', 0x14106E870, '00007043'),
    ('special-gauge-multiplier', 0x1410DDF7C, '5c8f623f'),
    ('special-gauge-offset', 0x1410DDF78, '1f85eb3d'),
    ('publisher-state-targets', 0x140A6E7D8,
     '88e2a600f8e3a60055e5a600e7e6a60049e7a60089e7a600'),
    ('publisher-outer-state-targets', 0x140A6F458,
     '58eea60062eea6006ceea60076eea60080eea6008aeea60094eea600'
     '93f0a6009df0a600a7f0a600dff1a600'),
    ('resource-type-zero-initializer', 0x141745700, 'a03ca64001000000'),
    ('resource-type-zero-release', 0x14172AF48, '204da64001000000'),
    ('resource-default-scale', 0x1410A8378, '0000803f0000803f0000803f'),
    ('render-queue-type-targets', 0x140F2FB3C,
     '06f4f20006f4f2009bf6f2009bf6f2009bf6f2009bf6f20006f4f20006f4f200'
     '33f8f2003af8f20033f8f2004ff8f20071f8f20092f8f20092f8f20092f8f200'
     '92f8f20092f8f20092f8f200e2f8f200e2f8f20033f8f200e2f8f200f9f8f20076f9f200'),
    ('render-queue-drain-targets', 0x140F2EDDC,
     '29ebf20029ebf20029ebf20029ebf20061ebf20061ebf20099ebf20099ebf200'
     'ceebf200ceebf200d9ebf200d9ebf200e4ebf200eeebf200eeebf200f8ebf200'
     'f8ebf20002ecf20002ecf2000cecf20016ecf20020ecf2002aecf20034ecf2003eecf200'),
    ('render-unit-scalar', 0x140FDE828, '0000803f'),
    ('actor-subtraction-mode-multiplier', 0x14105BC24, '9a99193f'),
    ('actor-subtraction-flag-multiplier', 0x140FE4244, '0000003f'),
    ('actor-subtraction-table-threshold', 0x140FE4788, '00004040'),
    ('actor-subtraction-context-multiplier', 0x141018AAC, '0000803e'),
    ('queue-sort-special-threshold', 0x1410DD838, '00c0da45'),
    ('queue-sort-special-result', 0x14105BC40, '00401c46'),
    ('queue-sort-sign-mask', 0x140FE42C0, '00000080000000800000008000000080'),
)


def produce(directory=DEFAULT_DUMP):
    dump = Dump(directory)
    data = []
    for label, address, expected in DATA:
        raw = dump.read(address, len(bytes.fromhex(expected)))
        if raw.hex() != expected:
            raise ValueError(f'HUD operand differs at {address:#x}')
        data.append(dict(label=label, address=hex(address), bytes=raw.hex(),
                         sha256=hashlib.sha256(raw).hexdigest()))
    return dict(schema=1, code_sha256=CODE_SHA256,
                evidence='offline pinned dump', audio_eligible=False,
                spans=[dump.span(*span) for span in SPANS], data=data)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dump', type=Path, default=DEFAULT_DUMP)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = produce(args.dump)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f"Retained {len(result['spans'])} spans and {len(result['data'])} data ranges")
