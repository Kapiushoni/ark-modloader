// Loader UI language: English or Russian. Chosen by ArkMods/loader.ini "language = auto|en|ru"
// (auto = the device language).
#pragma once
#include <string>

namespace i18n {

void Init(const std::string& modsDir);  // reads/creates loader.ini, resolves the language
const std::string& Lang();               // "en" or "ru"

// Translation of an English UI string (returned unchanged when there is none or in English).
std::string Tr(const std::string& en);

}  // namespace i18n
