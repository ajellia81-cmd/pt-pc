#pragma once
#include "engine/core/turkish_text.h"
#include "engine/core/chinese_text.h"
#include "engine/core/korean_text.h"
#include "engine/core/arabic_text.h"
#include "engine/core/russian_text.h"
#include "engine/core/ukrainian_text.h"
#include "engine/core/czech_text.h"
#include "engine/core/polish_text.h"
#include "engine/core/french_text.h"
#include "engine/core/german_text.h"
#include "engine/core/spanish_text.h"
#include "engine/core/japanese_text.h"
#include "engine/core/italian_text.h"
#include "engine/core/portuguese_text.h"
#include <string>
namespace pt::localized {
// The port's own texts in a language (1-6: the game's French, German, Spanish, Japanese, Italian and Portuguese, whose option
// screen and subtitles are the game's; 7-11: the added languages); empty for English or a key a table lacks
inline std::string_view Menu(std::string_view key, int language) {
 switch(language) {case 1:return french::Menu(key); case 2:return german::Menu(key); case 3:return spanish::Menu(key); case 4:return japanese::Menu(key); case 5:return italian::Menu(key); case 6:return portuguese::Menu(key); case 7:return turkish::Menu(key); case 8:return chinese::Menu(key); case 9:return arabic::Menu(key);case 10:return russian::Menu(key);case 11:return ukrainian::Menu(key);case 12:return czech::Menu(key);case 13:return polish::Menu(key);case 14:return korean::Menu(key);default:return {};}
}
inline std::string Characters(int language) {
 std::string out;for(int c=32;c<127;++c) out.push_back(char(c));
 auto add=[&](const auto& menu,const auto& subtitles){for(const auto& e:menu) out+=e.text;for(const auto& e:subtitles)for(auto line:e.lines) out+=line;};
 switch(language){case 7:add(turkish::kMenu,turkish::kSubtitles);break;case 8:add(chinese::kMenu,chinese::kSubtitles);break;case 9:add(arabic::kMenu,arabic::kSubtitles);break;case 10:add(russian::kMenu,russian::kSubtitles);break;case 11:add(ukrainian::kMenu,ukrainian::kSubtitles);break;case 12:add(czech::kMenu,czech::kSubtitles);break;case 13:add(polish::kMenu,polish::kSubtitles);break;case 14:add(korean::kMenu,korean::kSubtitles);break;}
 // The option screen adds the Korean language label to the original Chinese UI data.
 if (language == 8) out += "韩国语韓国語";
 // These fixed English licence lines are drawn in the localized credits font.  Include complete words here so
 // Uniscribe/HarfBuzz contributes ligatures such as the "fl" in Reflex to the generated atlas.
 if (language >= 7) out += "NVIDIA RTX NVIDIA Streamline, NVIDIA DLSS Frame Generation and NVIDIA Reflex "
     "NVIDIA, NVIDIA RTX, GeForce RTX, DLSS and Reflex are trademarks and/or registered trademarks of NVIDIA Corporation. "
     "AMD FidelityFX Super Resolution 3 (FidelityFX SDK 1.1.4) Copyright (C) 2024 Advanced Micro Devices, Inc. MIT License. "
     "Intel(R) Xe Super Sampling (XeSS) SDK 3.0.2 Copyright (C) 2025 Intel Corporation. Intel Simplified Software License. "
     "AMD and FidelityFX are trademarks of Advanced Micro Devices, Inc. Intel and XeSS are trademarks of Intel Corporation. "
     "whisper.cpp and ggml, Copyright (c) 2023-2026 The ggml authors. MIT License. Whisper, Copyright (c) 2022 OpenAI. "
     "Silero VAD, Copyright (c) 2020-present Silero Team. Noto Sans, Noto Sans SC, Noto Kufi Arabic and Noto Naskh Arabic: "
     "SIL Open Font License 1.1. Installer: LibOrbisPkg, GNU LGPL.";
 // the whole Russian and Ukrainian alphabet, for text that is not in the tables (a microphone's or a mod's name)
 if (language == 10 || language == 11) {
  auto utf8 = [&](uint32_t c) { out.push_back(char(0xC0 | (c >> 6))); out.push_back(char(0x80 | (c & 0x3F))); };
  for (uint32_t c = 0x0400; c <= 0x045F; ++c) utf8(c);
  utf8(0x0490); utf8(0x0491);
 }
 return out;
}
}
