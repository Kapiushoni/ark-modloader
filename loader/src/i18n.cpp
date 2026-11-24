#include "i18n.h"

#include <sys/system_properties.h>

#include <cctype>
#include <cstdio>
#include <map>

#include "log.h"

namespace i18n {
namespace {

std::string gLang = "en";

const std::map<std::string, std::string>& Russian() {
    static const std::map<std::string, std::string> m = {
        {"MODS", "МОДЫ"},
        {"ON", "ВКЛ"},
        {"OFF", "ВЫКЛ"},
        {"OPEN >", "ОТКРЫТЬ >"},
        {"SETTINGS", "НАСТРОЙКИ"},
        {"DUMP SDK", "ДАМП SDK"},
        {"all classes, fields and functions -> ArkMods/sdk/sdk.txt", "все классы, поля и функции -> ArkMods/sdk/sdk.txt"},
        {"RUN >", "ПУСК >"},
        {"DONE", "ГОТОВО"},
        {"FAILED", "ОШИБКА"},
        {"No mods in the ArkMods folder", "В папке ArkMods нет модов"},
        {"No settings", "Нет настроек"},
        {"(text settings: edit the mod's .ini in ArkMods)", "(текстовые настройки меняются в .ini мода в ArkMods)"},
        {"(empty)", "(пусто)"},
        {"restart the game to apply", "применится после перезапуска игры"},
        {"failed to load - see loader.log", "не загрузился - см. loader.log"},
        {"script error - see loader.log", "ошибка в скрипте - см. loader.log"},
        {"script errors - see loader.log", "ошибки в скрипте - см. loader.log"},
        {"copy failed", "не удалось скопировать"},
        {"not a mod (no ArkMod_Init)", "не мод (нет ArkMod_Init)"},
    };
    return m;
}

std::string Prop(const char* name) {
    char v[PROP_VALUE_MAX] = {};
    __system_property_get(name, v);
    return v;
}

}  // namespace

void Init(const std::string& modsDir) {
    std::string path = modsDir + "/loader.ini", setting = "auto";
    bool found = false;
    if (FILE* f = fopen(path.c_str(), "r")) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            std::string l = line;
            size_t eq = l.find('=');
            if (l.empty() || l[0] == '#' || eq == std::string::npos) continue;
            std::string k = l.substr(0, eq), v = l.substr(eq + 1);
            auto trim = [](std::string s) {
                while (!s.empty() && isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
                while (!s.empty() && isspace(static_cast<unsigned char>(s.front()))) s.erase(0, 1);
                return s;
            };
            if (trim(k) == "language") { setting = trim(v); found = true; }
        }
        fclose(f);
    }
    if (!found) {
        if (FILE* f = fopen(path.c_str(), "a")) {
            fprintf(f, "# ArkModLoader settings\n# language = auto (device language), en or ru\nlanguage = auto\n");
            fclose(f);
        }
    }
    std::string lang = setting;
    if (lang == "auto" || lang.empty()) {
        lang = Prop("persist.sys.locale");
        if (lang.empty()) lang = Prop("ro.product.locale");
        if (lang.empty()) lang = Prop("persist.sys.language");
    }
    gLang = lang.compare(0, 2, "ru") == 0 ? "ru" : "en";
    LOG("ui language: %s (setting %s)", gLang.c_str(), setting.c_str());
}

const std::string& Lang() { return gLang; }

std::string Tr(const std::string& en) {
    if (gLang != "ru") return en;
    const auto& m = Russian();
    auto it = m.find(en);
    if (it != m.end()) return it->second;
    if (en.compare(0, 13, "init failed (") == 0) return "ошибка инициализации (" + en.substr(13);
    return en;
}

}  // namespace i18n
