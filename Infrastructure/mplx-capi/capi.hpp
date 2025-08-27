#pragma once
#include <cstdint>

#if defined(_WIN32)
  #define MPLX_API __declspec(dllexport)
#else
  #define MPLX_API __attribute__((visibility("default")))
#endif

extern "C" {
  // Р’РѕР·РІСЂР°С‰Р°РµС‚ 0 РїСЂРё СѓСЃРїРµС…Рµ. РџСЂРё РѕС€РёР±РєРµ: *out_error -> utf8 СЃС‚СЂРѕРєР° (РЅР°РґРѕ РІС‹Р·РІР°С‚СЊ mplx_free).
  MPLX_API int mplx_run_from_source(const char* source_utf8, const char* entry_utf8, long long* out_result, char** out_error);
  // JSON СЃ diagnostics (РјР°СЃСЃРёРІ СЃС‚СЂРѕРє). 0 РїСЂРё СѓСЃРїРµС…Рµ; *out_json -> utf8 (РЅСѓР¶РЅРѕ mplx_free). *out_error РїСЂРё СЃР±РѕРµ.
  MPLX_API int mplx_check_source(const char* source_utf8, char** out_json, char** out_error);
  // РћСЃРІРѕР±РѕР¶РґРµРЅРёРµ СЃС‚СЂРѕРє, РІС‹РґРµР»РµРЅРЅС‹С… РЅР°С‚РёРІРЅРѕР№ Р±РёР±Р»РёРѕС‚РµРєРѕР№.
  MPLX_API void mplx_free(char* ptr);
}