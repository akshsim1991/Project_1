// CommandUtil.h - small helpers shared by the command files.
#pragma once
#include <functional>
#include <vector>

#include "Engine.h"

constexpr size_t kMaxPreview = 2000;  // objects shown while moving or rotating

inline Prompt P(const wchar_t* text, unsigned accept, std::vector<Keyword> keywords = {}, std::wstring def = L"") {
    Prompt p;
    p.text = text;
    p.accept = accept | (keywords.empty() ? 0u : (unsigned)kAcceptKeyword);
    p.keywords = std::move(keywords);
    p.defaultText = std::move(def);
    return p;
}

// A point prompt measured from `base` (rubber band, ortho, direct distance).
inline Prompt From(Prompt p, Vec2 base) {
    p.hasBase = true;
    p.base = base;
    return p;
}

// A prompt that picks one object with the pick box (no object snaps).
// A prompt for text that may contain spaces.
inline Prompt TextPrompt(const wchar_t* text, unsigned accept = 0, std::wstring def = L"") {
    Prompt p = P(text, accept | kAcceptEmpty, {}, std::move(def));
    p.spaces = true;
    return p;
}
inline Prompt Pick(const wchar_t* text, unsigned accept = 0, std::vector<Keyword> keywords = {},
                   std::wstring def = L"") {
    Prompt p = P(text, kAcceptPoint | accept, std::move(keywords), std::move(def));
    p.pickObject = true;
    return p;
}

inline std::vector<uint64_t> SelectionIds(Engine* E) { return {E->Selection().begin(), E->Selection().end()}; }

inline void PreviewTransformed(Engine* E, const std::vector<uint64_t>& ids, Preview& out,
                               const std::function<void(Entity&)>& transform) {
    for (size_t i = 0; i < ids.size() && i < kMaxPreview; ++i) {
        const Entity* e = E->Doc().Find(ids[i]);
        if (!e) continue;
        Entity copy = *e;
        transform(copy);
        out.entities.push_back(std::move(copy));
    }
}

inline bool Positive(Engine* E, double v) {
    if (v > 0) return true;
    E->Msg(L"The value must be positive and not zero.");
    return false;
}

// The steps a command has made, so its own Undo option can take back the
// last one: the objects as they were before, and the ones it added.
struct StepLog {
    struct Step {
        std::vector<Entity> before;
        std::vector<uint64_t> added;
    };
    std::vector<Step> steps;
    Step& Begin() {
        steps.emplace_back();
        return steps.back();
    }
    bool Undo(Document& doc) {
        if (steps.empty()) return false;
        Step s = std::move(steps.back());
        steps.pop_back();
        for (uint64_t id : s.added) doc.Erase(id);
        for (const Entity& e : s.before) doc.Restore(e);
        return true;
    }
};

// Objects added as one undo step, on the current layer in the current
// colour, keeping the line weights and types they were made with (thick
// outlines, thin construction, chain lines).
inline void AddKeepingStyle(Engine* E, const wchar_t* step, const std::vector<Entity>& ents) {
    Document& d = E->Doc();
    const CurrentStyle cur = d.Current();
    d.BeginChange(step);
    for (Entity e : ents) {
        if (e.layer < 0) {
            e.layer = cur.layer;
            if (e.color == kByLayer) e.color = cur.color;
            if (e.linetype == kLtByLayer) e.linetype = (int16_t)cur.linetype;
            if (e.lineweight == kLwByLayer) e.lineweight = (int16_t)cur.lineweight;
        }
        d.Add(e);
    }
    d.EndChange();
}

std::unique_ptr<Command> CreateModifyCommand(const std::wstring& name);
std::unique_ptr<Command> CreateDrawCommand(const std::wstring& name);
std::unique_ptr<Command> CreateDimCommand(const std::wstring& name);
std::unique_ptr<Command> CreateAnnotCommand(const std::wstring& name);
std::unique_ptr<Command> CreateSheetCommand(const std::wstring& name);
std::unique_ptr<Command> CreateRelateCommand(const std::wstring& name);
std::unique_ptr<Command> CreateEgCommand(const std::wstring& name);
std::unique_ptr<Command> CreateCurvesCommand(const std::wstring& name);
