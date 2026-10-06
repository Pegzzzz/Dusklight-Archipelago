"""Port of generator/logic/requirement.cpp: the randomizer's logic expression language.

ParseRequirementString is ported line by line (string surgery included) so that every
expression in the data parses to exactly the tree the C++ generator builds. The tree is then
constant-folded (setting comparisons are resolved at parse time, just like in C++) and turned
into Python source so evaluation during Archipelago's fill is a plain function call.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Callable

if TYPE_CHECKING:
    from .world_graph import LogicWorld

# Requirement types (requirement.hpp)
NOTHING = "NOTHING"
IMPOSSIBLE = "IMPOSSIBLE"
OR = "OR"
AND = "AND"
ITEM = "ITEM"
COUNT = "COUNT"
EVENT = "EVENT"
MACRO = "MACRO"
DAY = "DAY"
NIGHT = "NIGHT"
HUMAN_LINK = "HUMAN_LINK"
WOLF_LINK = "WOLF_LINK"
TWILIGHT_WOLF = "TWILIGHT_WOLF"
TWILIGHT_HUMAN = "TWILIGHT_HUMAN"
GOLDEN_BUGS = "GOLDEN_BUGS"
HEARTS = "HEARTS"
DUNGEONS_COMPLETED = "DUNGEONS_COMPLETED"


class FormTime:
    NONE = 0b0000
    HUMAN_DAY = 0b0001
    HUMAN_NIGHT = 0b0010
    WOLF_DAY = 0b0100
    WOLF_NIGHT = 0b1000
    HUMAN = HUMAN_DAY | HUMAN_NIGHT
    WOLF = WOLF_DAY | WOLF_NIGHT
    DAY = HUMAN_DAY | WOLF_DAY
    NIGHT = HUMAN_NIGHT | WOLF_NIGHT
    ALL = 0b1111
    TWILIGHT_WOLF = 0b10000
    TWILIGHT_HUMAN = 0b100000

    ALL_FORM_TIMES = (HUMAN_DAY, HUMAN_NIGHT, WOLF_DAY, WOLF_NIGHT)
    ALL_FORM_TIMES_AND_TWILIGHT = (HUMAN_DAY, HUMAN_NIGHT, WOLF_DAY, WOLF_NIGHT, TWILIGHT_WOLF, TWILIGHT_HUMAN)


DUNGEON_COMPLETION_EVENTS = (
    "Can Complete Forest Temple",
    "Can Complete Goron Mines",
    "Can Complete Lakebed Temple",
    "Can Complete Arbiters Grounds",
    "Can Complete Snowpeak Ruins",
    "Can Complete Temple of Time",
    "Can Complete City in the Sky",
    "Can Complete Palace of Twilight",
)


class LogicError(Exception):
    pass


@dataclass(eq=False)
class Req:
    type: str
    args: list = field(default_factory=list)

    def __repr__(self) -> str:
        if self.type in (AND, OR):
            return "(" + f" {self.type.lower()} ".join(map(repr, self.args)) + ")"
        if self.args:
            return f"{self.type}{tuple(self.args)}"
        return self.type


NO_REQUIREMENT = Req(NOTHING)
IMPOSSIBLE_REQUIREMENT = Req(IMPOSSIBLE)


def _contains_any(s: str, *subs: str) -> bool:
    return any(sub in s for sub in subs)


def parse_requirement(req_str: str, world: "LogicWorld") -> Req:
    """Line-by-line port of ParseRequirementString (forceLogic is always true here: the
    Archipelago world always uses logic)."""
    logic = list(req_str)
    nesting = 1
    delimiter = "+"
    for i, ch in enumerate(logic):
        if ch == "(":
            nesting += 1
        elif ch == ")":
            nesting -= 1
        if nesting == 1 and ch == " ":
            logic[i] = delimiter
    if nesting != 1:
        raise LogicError(f'Extra or missing parenthesis within expression: "{req_str}"')

    logic_str = "".join(logic)
    split: list[str] = []
    while True:
        pos = logic_str.find(delimiter)
        if pos == -1:
            break
        before = logic_str[pos - 1] if pos > 0 else "\0"
        after = logic_str[pos + 1] if pos + 1 < len(logic_str) else "\0"
        if before not in "!=><" and after not in "!=><":
            split.append(logic_str[:pos])
            logic_str = logic_str[pos + 1:]
        else:
            logic_str = logic_str[:pos] + logic_str[pos + 1:]
    split.append(logic_str)

    if len(split) == 1:
        arg = split[0].replace("_", " ")
        if arg == "Nothing":
            return Req(NOTHING)
        if arg == "Human Link":
            return Req(HUMAN_LINK)
        if arg == "Wolf Link":
            return Req(WOLF_LINK)
        if arg == "Twilight Wolf":
            return Req(TWILIGHT_WOLF)
        if arg == "Twilight Human":
            return Req(TWILIGHT_HUMAN)
        if arg[:1] == "'":
            return Req(EVENT, [world.get_event_index(arg[1:-1])])
        # Macros must be checked before items (some macros share an item's name)
        macro_index = world.get_macro_index(arg)
        if macro_index != -1:
            return Req(MACRO, [macro_index])
        item = world.find_item(arg)
        if item is not None:
            return Req(ITEM, [item])
        if _contains_any(arg, "!=", "==", ">=", "<="):
            comp = arg.rfind("=")
            option = arg[comp + 1:]
            setting = arg[:comp - 1]
            if "==" in arg:
                result = world.setting_index(setting) == world.option_index(setting, option)
            elif "!=" in arg:
                result = world.setting_index(setting) != world.option_index(setting, option)
            elif ">=" in arg:
                result = world.setting_index(setting) >= world.option_index(setting, option)
            else:
                result = world.setting_index(setting) <= world.option_index(setting, option)
            return Req(NOTHING) if result else Req(IMPOSSIBLE)
        if "count" in arg:
            inner = arg[arg.find("(") + 1:-1]
            parts = inner.split(", ")
            count_str = parts[1]
            if world.has_setting(count_str):
                count_str = world.setting(count_str)
            item = world.get_item(parts[0])
            return Req(COUNT, [int(count_str), item])
        if arg == "Day":
            return Req(DAY)
        if arg == "Night":
            return Req(NIGHT)
        if "hearts" in arg:
            num = arg[arg.find("(") + 1:-1]
            if world.has_setting(num):
                num = world.setting(num)
            return Req(HEARTS, [int(num)])
        if arg == "Impossible":
            return Req(IMPOSSIBLE)
        if "golden bugs" in arg:
            return Req(GOLDEN_BUGS, [int(arg[arg.find("(") + 1:-1])])
        if "dungeons completed" in arg:
            num = arg[arg.find("(") + 1:-1]
            if world.has_setting(num):
                num = world.setting(num)
            return Req(DUNGEONS_COMPLETED, [int(num)])
        raise LogicError(f'Unrecognized logic symbol: "{req_str}"')

    if len(split) == 2:
        raise LogicError(f"Unrecognized 2 part expression: {req_str}")

    is_and = "and" in split
    is_or = "or" in split
    if is_and and is_or:
        raise LogicError(f'"and" & "or" in same nesting level when parsing "{req_str}"')
    if not (is_and or is_or):
        raise LogicError(f'Could not determine logical operator type from expression: "{req_str}"')
    req = Req(AND if is_and else OR)
    for sub in split:
        if sub in ("and", "or"):
            continue
        if sub[:1] == "(":
            sub = sub[1:-1]
        req.args.append(parse_requirement(sub, world))
    return req


# --------------------------------------------------------------------------------------------
# Simplification and code generation


def simplify(req: Req, world: "LogicWorld") -> Req:
    """Constant-fold a requirement. Macros are folded when their body is constant."""
    t = req.type
    if t in (AND, OR):
        args = []
        for arg in req.args:
            s = simplify(arg, world)
            if t == AND:
                if s.type == IMPOSSIBLE:
                    return IMPOSSIBLE_REQUIREMENT
                if s.type == NOTHING:
                    continue
            else:
                if s.type == NOTHING:
                    return NO_REQUIREMENT
                if s.type == IMPOSSIBLE:
                    continue
            if s.type == t:
                args.extend(s.args)
            else:
                args.append(s)
        if not args:
            return NO_REQUIREMENT if t == AND else IMPOSSIBLE_REQUIREMENT
        if len(args) == 1:
            return args[0]
        return Req(t, args)
    if t == MACRO:
        body = world.simplified_macro(req.args[0])
        if body.type in (NOTHING, IMPOSSIBLE):
            return body
        return req
    if t == COUNT and req.args[0] <= 0:
        return NO_REQUIREMENT
    return req


class CodeGen:
    """Turns requirement trees into Python expressions over (c, e, f):
    c = item counts list, e = owned-event bytearray, f = form/time bitset."""

    INLINE_LIMIT = 160

    def __init__(self, world: "LogicWorld") -> None:
        self.world = world
        self.macro_src: dict[int, str] = {}

    def expr(self, req: Req) -> str:
        t = req.type
        if t == NOTHING:
            return "True"
        if t == IMPOSSIBLE:
            return "False"
        if t == AND:
            return "(" + " and ".join(self.expr(a) for a in req.args) + ")"
        if t == OR:
            return "(" + " or ".join(self.expr(a) for a in req.args) + ")"
        if t == ITEM:
            return f"c[{req.args[0].index}]"
        if t == COUNT:
            return f"c[{req.args[1].index}]>={req.args[0]}"
        if t == EVENT:
            return f"e[{req.args[0]}]"
        if t == MACRO:
            index = req.args[0]
            src = self.macro_expr(index)
            if len(src) <= self.INLINE_LIMIT:
                return src
            return f"M[{index}](c,e,f)"
        if t == DAY:
            return f"f&{FormTime.DAY}"
        if t == NIGHT:
            return f"f&{FormTime.NIGHT}"
        if t == HUMAN_LINK:
            return f"f&{FormTime.HUMAN}"
        if t == WOLF_LINK:
            return f"f&{FormTime.WOLF}"
        if t == TWILIGHT_WOLF:
            return f"f&{FormTime.TWILIGHT_WOLF}"
        if t == TWILIGHT_HUMAN:
            return f"f&{FormTime.TWILIGHT_HUMAN}"
        if t == GOLDEN_BUGS:
            bugs = "+".join(f"c[{i.index}]" for i in self.world.golden_bugs)
            return f"({bugs})>={req.args[0]}"
        if t == HEARTS:
            poh = self.world.get_item("Piece of Heart").index
            hc = self.world.get_item("Heart Container").index
            return f"c[{poh}]+(c[{hc}]+3)*5>={req.args[0] * 5}"
        if t == DUNGEONS_COMPLETED:
            evs = "+".join(f"e[{self.world.get_event_index(n, add_if_missing=False)}]"
                           for n in DUNGEON_COMPLETION_EVENTS
                           if self.world.get_event_index(n, add_if_missing=False) != -1)
            return f"({evs or '0'})>={req.args[0]}"
        raise LogicError(f"cannot generate code for {t}")

    def macro_expr(self, index: int) -> str:
        if index not in self.macro_src:
            self.macro_src[index] = "(" + self.expr(self.world.simplified_macro(index)) + ")"
        return self.macro_src[index]

    def compile(self, req: Req, name: str = "<req>") -> Callable:
        src = self.expr(req)
        code = compile(f"lambda c,e,f: bool({src})", name, "eval")
        return eval(code, {"M": self.world.macro_functions})


def items_in(req: Req, world: "LogicWorld", seen_macros: set | None = None) -> set:
    """All items a (simplified) requirement can depend on, including through macros."""
    if seen_macros is None:
        seen_macros = set()
    out = set()
    t = req.type
    if t in (AND, OR):
        for a in req.args:
            out |= items_in(a, world, seen_macros)
    elif t == ITEM:
        out.add(req.args[0])
    elif t == COUNT:
        out.add(req.args[1])
    elif t == MACRO:
        index = req.args[0]
        if index not in seen_macros:
            seen_macros.add(index)
            out |= items_in(world.simplified_macro(index), world, seen_macros)
    elif t == GOLDEN_BUGS:
        out |= set(world.golden_bugs)
    elif t == HEARTS:
        out.add(world.get_item("Piece of Heart"))
        out.add(world.get_item("Heart Container"))
    return out
