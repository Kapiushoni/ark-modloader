#include "events.h"

#include <algorithm>
#include <mutex>
#include <vector>

#include "dispatch.h"
#include "engine.h"
#include "log.h"

namespace events {
namespace {

using ue4::UObject;

constexpr int32_t RF_ArchetypeObject = 0x20;

struct Sub { UObject* cls; NewObjectFn fn; void* user; const void* owner; };
struct Pending { UObject* obj; int32_t index; };

std::mutex gMu;
std::vector<Sub> gSubs;
std::vector<Pending> gPending;

// Same layout as the engine's FUObjectArray::FUObjectCreateListener (Itanium ABI):
// vtable = { ~D1, ~D0, NotifyUObjectCreated }.
class CreateListener {
public:
    virtual ~CreateListener() {}
    virtual void NotifyUObjectCreated(const UObject* object, int32_t index) {
        if (!object || (object->ObjectFlags & (ue4::RF_ClassDefaultObject | RF_ArchetypeObject))) return;
        std::lock_guard<std::mutex> l(gMu);
        if (gSubs.empty()) return;
        auto* o = const_cast<UObject*>(object);
        for (const Sub& s : gSubs)
            if (engine::IsA(o, s.cls)) { gPending.push_back({o, index}); return; }
    }
};
CreateListener gListener;

void Deliver(void*) {
    std::vector<Pending> todo;
    {
        std::lock_guard<std::mutex> l(gMu);
        if (gPending.empty()) return;
        todo.swap(gPending);
    }
    auto& arr = engine::Objects()->ObjObjects;
    for (const Pending& p : todo) {
        // Still the same live object? (it may have been destroyed and its slot reused)
        if (p.index < 0 || p.index >= arr.NumElements || arr.Objects[p.index].Object != p.obj) continue;
        std::vector<Sub> subs;
        { std::lock_guard<std::mutex> l(gMu); subs = gSubs; }
        for (const Sub& s : subs) {
            bool alive;  // an earlier callback may have unloaded this one's mod
            { std::lock_guard<std::mutex> l(gMu);
              alive = std::any_of(gSubs.begin(), gSubs.end(), [&](const Sub& x) { return x.fn == s.fn && x.user == s.user; }); }
            if (alive && engine::IsA(p.obj, s.cls)) s.fn(s.user, p.obj);
        }
    }
}

}  // namespace

bool Init() {
    using AddFn = void (*)(ue4::FUObjectArray*, CreateListener*);
    auto add = reinterpret_cast<AddFn>(engine::Sym("_ZN13FUObjectArray24AddUObjectCreateListenerEPNS_22FUObjectCreateListenerE"));
    if (!add) { LOGE("events: AddUObjectCreateListener not found"); return false; }
    add(engine::Objects(), &gListener);
    dispatch::AddTick(Deliver, nullptr, reinterpret_cast<const void*>(&Init));
    return true;
}

void OnNewObject(UObject* cls, NewObjectFn fn, void* user, const void* owner) {
    if (!cls || !fn) return;
    std::lock_guard<std::mutex> l(gMu);
    gSubs.push_back({cls, fn, user, owner});
}

void RemoveOwner(const void* owner) {
    std::lock_guard<std::mutex> l(gMu);
    gSubs.erase(std::remove_if(gSubs.begin(), gSubs.end(), [&](const Sub& s) { return s.owner == owner; }), gSubs.end());
}

}  // namespace events
