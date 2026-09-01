"""
Translate CFC2's GUI markup into speech-ready text.

The corpus uses a small closed vocabulary -- 4 tag types (SIZE, ICON, CNTR,
RGB) and 132 ICON tokens -- so a complete table is finite and verifiable.

Two things make this worth care rather than a blanket strip:

1. Dropping <ICON> would silently destroy meaning. "PRESS <ICON DECIDE> TO
   START" becomes "PRESS TO START", and a move input becomes nothing at all.
   The achievement string ACHIEVEMENT_35_TIPS is literally
   <C_2><C_2><C_6><C_6><C_4><C_4><C_START> -- with icons stripped it conveys
   no combination whatsoever.

2. The attack icons are game-specific: C_A is "Vertical Slash" in Plasma Sword
   but "Any Attack" in Cybots. Rather than invent names, this module derives
   them from the game's OWN legends (the HELP_MOVES_* / HELP_EXMOVE_* strings,
   which pair each icon with Capcom's English name per game). Those are the
   authoritative source; see load_legends().
"""

import re

# Directional glyphs use numpad notation, confirmed from the corpus:
# MID_IGAI reads "Any Direction but <ICON C_8>" (up), and ACHIEVEMENT_35_TIPS
# spells a combination as C_2 C_2 C_6 C_6 C_4 C_4 C_START.
#
# Spoken as back/forward rather than left/right because that is how fighting
# game inputs are actually described, and it stays correct after a side switch.
DIRECTIONS = {
    "C_1": "down-back", "C_2": "down", "C_3": "down-forward",
    "C_4": "back", "C_5": "neutral", "C_6": "forward",
    "C_7": "up-back", "C_8": "up", "C_9": "up-forward",
    "C_4T": "charge back",
    "D_KEY": "d-pad", "KB_ARROWS": "arrow keys",
}

# Attack buttons whose meaning is stable across every game in the collection.
# Game-specific ones (C_A, C_B, C_P2, C_K2 ...) come from the legends instead.
ATTACKS = {
    "C_LP": "light punch", "C_MP": "medium punch", "C_HP": "heavy punch",
    "C_LK": "light kick", "C_MK": "medium kick", "C_HK": "heavy kick",
    "C_P": "punch", "C_K": "kick",
    "C_SP": "special move", "C_EX": "super",
    "C_START": "Start",
}

# UI affordances and hardware keys: what the player physically presses.
UI = {
    "DECIDE": "Confirm", "CANCEL": "Cancel", "KEY_SUP": "",
    "LSB": "left stick", "RSB": "right stick", "LSRS": "both sticks",
    "BTN_A": "A", "BTN_B": "B", "BTN_X": "X", "BTN_Y": "Y",
    "BTN_LB": "LB", "BTN_RB": "RB", "BTN_LT": "LT", "BTN_RT": "RT",
    "BTN_START": "Start", "BTN_SELECT": "Select",
    "CFG_A": "A", "CFG_B": "B", "CFG_X": "X", "CFG_Y": "Y",
    "CFG_L": "L", "CFG_R": "R",
    "JCN_A": "A", "JCN_B": "B", "JCN_X": "X", "JCN_Y": "Y",
    "JCN_L": "L", "JCN_R": "R", "JCN_LS": "left stick",
}

_KB_NAMED = {
    "KB_BACK": "Backspace", "KB_RETURN": "Enter", "KB_ESCAPE": "Escape",
    "KB_SPACE": "Space", "KB_TAB": "Tab", "KB_DELETE": "Delete",
    "KB_INSERT": "Insert", "KB_HOME": "Home", "KB_END": "End",
    "KB_PRIOR": "Page Up", "KB_NEXT": "Page Down",
    "KB_UP": "Up Arrow", "KB_DOWN": "Down Arrow",
    "KB_LEFT": "Left Arrow", "KB_RIGHT": "Right Arrow",
    "KB_LSHIFT": "Left Shift", "KB_RSHIFT": "Right Shift",
    "KB_LCONTROL": "Left Ctrl", "KB_RCONTROL": "Right Ctrl",
    "KB_LMENU": "Left Alt", "KB_RMENU": "Right Alt",
}


def _keyboard(token):
    """KB_A -> 'A', KB_F10 -> 'F10', KB_5 -> '5', plus the named keys above."""
    if token in _KB_NAMED:
        return _KB_NAMED[token]
    if token.startswith("KB_"):
        return token[3:]
    return None


def load_legends(strings):
    """
    Derive game-specific icon names from the game's own legend strings.

    HELP_MOVES_* and HELP_EXMOVE_* are legends: a run of <ICON X>Name pairs,
    e.g. '<ICON C_P2>Punch <ICON C_K2>Kick <ICON C_S2>Special'. Parsing them
    gives Capcom's own English name for each icon rather than a guess.

    `strings` is the {label: text} map from a GMD. Returns {token: name}.

    Where games disagree (C_A is "Any Attack" in Cybots, "Vertical Slash" in
    Plasma Sword) the more generic reading is preferred, because the mod does
    not always know which game's context a string belongs to. The per-game
    table is still returned by load_legends_per_game() for callers that do.
    """
    generic_first = ("6BTN", "CVS1", "MJUS")
    out = {}
    for label in sorted(strings, key=lambda k: (0 if any(g in k for g in generic_first) else 1, k)):
        if not (label.startswith("HELP_MOVES") or label.startswith("HELP_EXMOVE")
                or label == "HELP_SPMOVE"):
            continue
        text = re.sub(r"</?SIZE[^>]*>", "", strings[label])
        # Each <ICON X> is followed by its name, up to the next tag or EOL.
        for tok, name in re.findall(r"<ICON ([A-Za-z0-9_]+)>([^<\r\n]*)", text):
            name = name.strip()
            # A run like <ICON C_P><ICON C_K>Any Attack names BOTH icons; the
            # empty ones are filled by the trailing name via the second pass.
            if name and tok not in out:
                out[tok] = name
        # Second pass: attach the following name to icons that had none.
        pending = []
        for tok, name in re.findall(r"<ICON ([A-Za-z0-9_]+)>([^<\r\n]*)", text):
            name = name.strip()
            if not name:
                pending.append(tok)
            else:
                for p in pending:
                    out.setdefault(p, name)
                pending = []
    return out


def load_legends_per_game(strings):
    """Same as load_legends but keyed by the game suffix, e.g. {'STG2': {...}}."""
    games = {}
    for label, text in strings.items():
        m = re.match(r"HELP_(?:MOVES|EXMOVE)_([A-Z0-9]+)$", label)
        if not m:
            continue
        game = m.group(1)
        text = re.sub(r"</?SIZE[^>]*>", "", text)
        table = games.setdefault(game, {})
        pending = []
        for tok, name in re.findall(r"<ICON ([A-Za-z0-9_]+)>([^<\r\n]*)", text):
            name = name.strip()
            if not name:
                pending.append(tok)
                continue
            table[tok] = name
            for p in pending:
                table.setdefault(p, name)
            pending = []
    return games


class Translator:
    """Turns markup into speech text. Build once, reuse per string."""

    def __init__(self, legends=None, per_game=None):
        self.icons = {}
        self.icons.update(ATTACKS)
        self.icons.update(DIRECTIONS)
        self.icons.update(UI)
        self.per_game = per_game or {}
        # Legends fill in the game-specific attack icons, but never override
        # a direction or a hardware key -- those are unambiguous already.
        for tok, name in (legends or {}).items():
            if tok not in DIRECTIONS and tok not in UI:
                self.icons.setdefault(tok, name.lower() if name.isupper() else name)

    def icon(self, token, game=None):
        """
        Spoken form of one ICON token, or None if unknown.

        `game` (e.g. "STG2") selects that game's meaning where icons differ:
        C_A is "Vertical Slash" in Plasma Sword but "Any Attack" in Cybots,
        and C_EX is a different super name in all nine games that use it.
        Without a game the generic reading is used.
        """
        if game and game in self.per_game:
            name = self.per_game[game].get(token)
            if name and token not in DIRECTIONS and token not in UI:
                return name
        if token in self.icons:
            return self.icons[token]
        kb = _keyboard(token)
        if kb is not None:
            return kb
        return None

    def clean(self, text, legend=False, game=None):
        """
        Full markup -> speech. Icons become words; layout tags are dropped.

        Icons are comma-separated so an input sequence reads as discrete
        presses ("down, down, forward, forward, back, back, Start") rather
        than running together into one unintelligible word.

        `legend=True` is for the HELP_MOVES_*/HELP_EXMOVE_* strings, which are
        themselves icon->name legends. Speaking the icon AND its printed name
        stutters ("Punch, Punch Kick, Kick"), so there the icon is dropped and
        only the printed name is read -- the text already says what the button
        does, which is the whole point of a legend.
        """
        if not text:
            return ""

        if legend:
            text = re.sub(r"</?SIZE[^>]*>", "", text)
            text = re.sub(r"</?RGB[^>]*>", "", text)
            text = re.sub(r"<CNTR>", "", text)
            # Separate the run-together entries: "...Slash<ICON>Kick" -> ", ".
            text = re.sub(r"<ICON [A-Za-z0-9_]+>", "\x01", text)
            text = re.sub(r"\x01+", "\x01", text)
            parts = [p.strip() for p in text.split("\x01") if p.strip()]
            out = ", ".join(parts)
            out = out.replace("\r\n", ". ").replace("\r", " ").replace("\n", ". ")
            return re.sub(r"\s+", " ", out).strip()

        # Layout-only tags carry nothing audible.
        text = re.sub(r"</?SIZE[^>]*>", "", text)
        text = re.sub(r"</?RGB[^>]*>", "", text)
        text = re.sub(r"<CNTR>", "", text)
        # <ltr>/</ltr>: a left-to-right override, used once in the Arabic
        # corpus to stop a symbol being reflowed by bidirectional layout.
        # Purely visual, and irrelevant once the text is spoken.
        text = re.sub(r"</?ltr>", "", text, flags=re.IGNORECASE)

        def sub_icon(m):
            name = self.icon(m.group(1), game)
            if name is None:
                # Unknown token: keep it visible rather than silently dropping
                # meaning. Surfaced by the offline audit so it can be added.
                return " [%s] " % m.group(1)
            # Wrapped in markers (\x01) so the cleanup below can tell an icon
            # from the surrounding prose and see when two icons are adjacent.
            return "\x01" + name + "\x01" if name else " "

        text = re.sub(r"<ICON ([A-Za-z0-9_]+)>", sub_icon, text)

        text = text.replace("\r\n", ". ").replace("\r", " ").replace("\n", ". ")

        # Every icon needs a separator so it does not fuse with the next word
        # ("StartTraining"). It needs a COMMA only when another icon follows
        # immediately -- that is what makes an input sequence read as discrete
        # presses ("down, down, forward"). Before ordinary words a comma would
        # just chop the sentence up ("PRESS Confirm, TO START"), so use a
        # plain space there.
        # Icons arrive wrapped as \x01name\x01, so splitting yields alternating
        # prose / icon / prose pieces and odd indices are the icon names.
        # Two icons separated by nothing but whitespace get a COMMA, which is
        # what makes an input sequence read as discrete presses
        # ("down, down, forward"); everywhere else a plain space, so an icon
        # never fuses with a word ("StartTraining") and never chops a sentence
        # ("PRESS Confirm, TO START").
        pieces = text.split("\x01")
        out = [pieces[0]]
        for i in range(1, len(pieces), 2):
            name = pieces[i]
            gap = pieces[i + 1] if i + 1 < len(pieces) else ""
            nxt = pieces[i + 2] if i + 2 < len(pieces) else None
            if out[-1] and not out[-1].endswith((" ", "(", "/", "+")):
                out.append(" ")
            out.append(name)
            if nxt is not None and gap.strip() == "":
                # Another icon follows with only whitespace between them.
                out.append(",")
            out.append(gap)
            # The source often runs an icon straight into its label with no
            # space ("<ICON BTN_START>Training"); insert one so they do not
            # fuse into "StartTraining".
            if gap and not gap[:1].isspace() and not gap[:1] in ",.;:!?)/+":
                out.insert(len(out) - 1, " ")
            if not gap:
                out.append(" ")
        text = "".join(out)

        # Safety net: any layout tag not handled above is dropped rather than
        # spoken verbatim. The <ltr> tag was missed on the first pass because
        # the vocabulary was surveyed in English only and it occurs once, in
        # Arabic -- so assume the survey is incomplete rather than closed.
        # unknown_tags() reports these so they can be handled deliberately.
        text = re.sub(r"</?(?!ICON\b)[A-Za-z_][A-Za-z0-9_]*[^>]*>", "", text)

        text = re.sub(r"\s+", " ", text)
        text = re.sub(r",\s*([.,;:!?])", r"\1", text)
        text = re.sub(r"\s+([.,;:!?])", r"\1", text)
        text = re.sub(r"[,\s]+$", "", text.strip())
        return text.strip()


def unknown_tokens(strings, translator):
    """Every ICON token in `strings` the translator cannot name. Should be empty."""
    seen = set()
    for text in strings.values():
        seen.update(re.findall(r"<ICON ([A-Za-z0-9_]+)>", text))
    return sorted(t for t in seen if translator.icon(t) is None)


# Tags the translator deliberately understands. Anything else is dropped by
# the safety net in clean() and reported by unknown_tags().
KNOWN_TAGS = {"ICON", "SIZE", "/SIZE", "RGB", "/RGB", "CNTR", "ltr", "/ltr"}


def unknown_tags(strings):
    """
    Layout tags in `strings` that are not in KNOWN_TAGS.

    Run this across ALL languages, not just English: <ltr> occurs exactly once
    in the whole corpus and only in Arabic, and surveying English alone missed
    it. A tag being absent from one language proves nothing about the others.
    """
    seen = set()
    for text in strings.values():
        seen.update(re.findall(r"<(/?[A-Za-z_][A-Za-z0-9_]*)[^>]*>", text))
    return sorted(t for t in seen if t not in KNOWN_TAGS)
