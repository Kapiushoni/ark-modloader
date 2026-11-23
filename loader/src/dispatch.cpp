#include "dispatch.h"

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine.h"
#include "hook.h"
#include "log.h"

namespace dispatch {
namespace {

using ue4::UObject;
using ProcessEventFn = void (*)(UObject*, UObject*, void*);

struct Hook {
    std::string pattern;
    bool suffix;
    ArkPreHook pre;
    ArkPostHook post;
    void* user;
    const void* owner;
};
using HookList = std::vector<Hook>;

struct Task {
    ArkTask fn;
    void* user;
    const void* owner;
};

ProcessEventFn gOrig = nullptr;
std::atomic<uint64_t> gCalls{0};

std::mutex gHookMu;
std::vector<Hook> gHooks;
std::atomic<int> gHookCount{0};
std::atomic<uint32_t> gHooksVersion{0};

// UFunction* -> matching hooks (nullptr = none). Reset on world change / hook changes.
std::mutex gCacheMu;
std::unordered_map<UObject*, std::shared_ptr<const HookList>> gCache;
UObject* gCacheWorld = nullptr;
uint32_t gCacheVersion = 0;

std::mutex gTaskMu;
std::vector<Task> gTasks;
std::atomic<bool> gHasTasks{false};
std::vector<Task> gTicks;
std::atomic<int> gTickCount{0};
uint64_t gLastFrame = 0;
thread_local bool tInWork = false;

bool Matches(const Hook& h, const std::string& path) {
    if (!h.suffix) return path == h.pattern;
    return path.size() > h.pattern.size() &&
           path.compare(path.size() - h.pattern.size(), std::string::npos, h.pattern) == 0 &&
           path[path.size() - h.pattern.size() - 1] == '.';
}

std::shared_ptr<const HookList> Lookup(UObject* func) {
    std::lock_guard<std::mutex> l(gCacheMu);
    UObject* world = engine::World();
    uint32_t ver = gHooksVersion.load();
    if (world != gCacheWorld || ver != gCacheVersion) {
        gCache.clear();
        gCacheWorld = world;
        gCacheVersion = ver;
    }
    auto it = gCache.find(func);
    if (it != gCache.end()) return it->second;

    std::string path = engine::Path(func);
    auto list = std::make_shared<HookList>();
    {
        std::lock_guard<std::mutex> hl(gHookMu);
        for (const Hook& h : gHooks)
            if (Matches(h, path)) list->push_back(h);
    }
    std::shared_ptr<const HookList> result = list->empty() ? nullptr : std::move(list);
    gCache.emplace(func, result);
    return result;
}

void RunGameThreadWork() {
    if (tInWork) return;
    tInWork = true;
    uint64_t frame = engine::FrameCounter();
    if (gTickCount.load(std::memory_order_relaxed) && frame != gLastFrame) {
        gLastFrame = frame;
        std::vector<Task> ticks;
        { std::lock_guard<std::mutex> l(gTaskMu); ticks = gTicks; }
        for (const Task& t : ticks) {
            // A previous tick in this frame may have unloaded this tick's owner.
            bool alive;
            { std::lock_guard<std::mutex> l(gTaskMu);
              alive = std::any_of(gTicks.begin(), gTicks.end(), [&](const Task& x) { return x.fn == t.fn && x.user == t.user; }); }
            if (alive) t.fn(t.user);
        }
    }
    if (gHasTasks.load(std::memory_order_acquire)) {  // one batch: tasks may re-post themselves
        std::vector<Task> tasks;
        {
            std::lock_guard<std::mutex> l(gTaskMu);
            tasks.swap(gTasks);
            gHasTasks = false;
        }
        for (const Task& t : tasks) t.fn(t.user);
    }
    tInWork = false;
}

void Hooked(UObject* self, UObject* func, void* params) {
    gCalls.fetch_add(1, std::memory_order_relaxed);

    if ((gTickCount.load(std::memory_order_relaxed) || gHasTasks.load(std::memory_order_relaxed)) && OnGameThread())
        RunGameThreadWork();

    if (gHookCount.load(std::memory_order_relaxed) == 0) return gOrig(self, func, params);

    std::shared_ptr<const HookList> hooks = Lookup(func);
    if (!hooks) return gOrig(self, func, params);

    bool callOriginal = true;
    for (const Hook& h : *hooks) {
        if (!h.pre) continue;
        ArkHookCtx ctx{reinterpret_cast<::UObject*>(self), reinterpret_cast<::UObject*>(func), params, h.user};
        if (h.pre(&ctx) == 0) callOriginal = false;
    }
    if (callOriginal) gOrig(self, func, params);
    for (const Hook& h : *hooks) {
        if (!h.post) continue;
        ArkHookCtx ctx{reinterpret_cast<::UObject*>(self), reinterpret_cast<::UObject*>(func), params, h.user};
        h.post(&ctx);
    }
}

}  // namespace

bool Install(void* processEvent) {
    const char* err = "?";
    gOrig = reinterpret_cast<ProcessEventFn>(hook::InlineHook(processEvent, reinterpret_cast<void*>(&Hooked), &err));
    if (!gOrig) LOGE("ProcessEvent hook failed: %s", err);
    return gOrig != nullptr;
}

void CallOriginal(UObject* obj, UObject* func, void* params) { gOrig(obj, func, params); }

bool OnGameThread() { return static_cast<uint32_t>(gettid()) == engine::GameThreadId(); }

int AddHook(const char* pattern, ArkPreHook pre, ArkPostHook post, void* user, const void* owner) {
    if (!pattern || !*pattern || (!pre && !post)) return -1;
    {
        std::lock_guard<std::mutex> l(gHookMu);
        gHooks.push_back({pattern, pattern[0] != '/', pre, post, user, owner});
        gHookCount = static_cast<int>(gHooks.size());
    }
    gHooksVersion.fetch_add(1);
    return 0;
}

void Post(ArkTask task, void* user, const void* owner) {
    std::lock_guard<std::mutex> l(gTaskMu);
    gTasks.push_back({task, user, owner});
    gHasTasks.store(true, std::memory_order_release);
}

void AddTick(ArkTask task, void* user, const void* owner) {
    std::lock_guard<std::mutex> l(gTaskMu);
    gTicks.push_back({task, user, owner});
    gTickCount = static_cast<int>(gTicks.size());
}

void RemoveOwner(const void* owner) {
    {
        std::lock_guard<std::mutex> l(gHookMu);
        gHooks.erase(std::remove_if(gHooks.begin(), gHooks.end(), [&](const Hook& h) { return h.owner == owner; }),
                     gHooks.end());
        gHookCount = static_cast<int>(gHooks.size());
    }
    gHooksVersion.fetch_add(1);
    std::lock_guard<std::mutex> l(gTaskMu);
    auto byOwner = [&](const Task& t) { return t.owner == owner; };
    gTicks.erase(std::remove_if(gTicks.begin(), gTicks.end(), byOwner), gTicks.end());
    gTasks.erase(std::remove_if(gTasks.begin(), gTasks.end(), byOwner), gTasks.end());
    gTickCount = static_cast<int>(gTicks.size());
}

uint64_t CallCount() { return gCalls.load(); }

}  // namespace dispatch
