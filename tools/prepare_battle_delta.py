"""Copy one original guest function and instrument its time output stores.

Technique from Budokai-NX by Unknown (MIT). Does not modify the codegen output
(app/generated/dbz3_recomp.*.cpp); writes app/generated/b3_battle_delta.cpp.
The added calls change only the effective clock output; all original PPC
instructions and speed inputs remain.
"""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]
GENERATED = ROOT / "app/generated"
MARK = "DEFINE_REX_FUNC(sub_820FC260) {"
RENAMED = 'extern "C" void DeltaBattle_sub_820FC260(PPCContext& ctx,uint8_t* base)'


def find_function():
    hits = []
    for path in sorted(GENERATED.glob("dbz3_recomp.*.cpp")):
        source = path.read_text(encoding="utf-8-sig")
        if MARK in source:
            hits.append((path, source))
    assert len(hits) == 1, f"expected exactly one file defining sub_820FC260, found {len(hits)}"
    path, source = hits[0]
    start = source.index(MARK)
    end = source.find("\nDEFINE_REX_FUNC(", start + 1)
    if end < 0:
        end = len(source)
    return path, source[start:end].rstrip()


def main():
    path, original = find_function()
    digest = hashlib.sha256(original.encode()).hexdigest()
    store = "REX_STORE_U32(ctx.r31.u32 + ctx.r29.u32, temp.u32);"
    assert original.count(store) == 1
    call = "b3::timing::PublishBattleEffectiveDelta(ctx.r31.u32,temp.u32,base);"
    body = original.replace("DEFINE_REX_FUNC(sub_820FC260)", RENAMED, 1)
    body = body.replace(store, store + "\n\t" + call)
    calls = [call]
    for offset in (8, 12):
        effect_store = f"REX_STORE_U32(ctx.r11.u32 + {offset}, temp.u32);"
        assert original.count(effect_store) == 1
        effect_call = f"b3::timing::PublishBattleEffectsDelta(ctx.r11.u32+{offset},temp.u32,base);"
        body = body.replace(effect_store, effect_store + "\n\t" + effect_call)
        calls.append(effect_call)
    restored = body
    for injected in calls:
        restored = restored.replace("\n\t" + injected, "")
    restored = restored.replace(RENAMED, "DEFINE_REX_FUNC(sub_820FC260)", 1)
    assert restored == original, "reversibility check failed"
    prefix = '#include "dbz3_pch.h"\n#include "dbz3_init.h"\n#include "b3_battle_delta_bridge.h"\n'
    text = prefix + body + "\n"
    out = GENERATED / "b3_battle_delta.cpp"
    if not out.exists() or out.read_text(encoding="utf-8") != text:
        out.write_text(text, encoding="utf-8")
    print(json.dumps({
        "source": path.name,
        "original_lines": original.count("\n") + 1,
        "generated_lines": text.count("\n"),
        "original_body_sha256": digest,
        "injected_calls": len(calls),
    }))


if __name__ == "__main__":
    main()
