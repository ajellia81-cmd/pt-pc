#include <cstdio>

#include "engine/core/korean_text.h"
#include "engine/core/japanese_text.h"
#include "engine/core/localized_text.h"
#include "engine/ui/ffnt.h"
#include "engine/ui/text_layout.h"

namespace {

bool Fits(std::string_view text, const pt::ui::TextStyle& style, glm::vec2 size) {
    const pt::ui::TextLayout layout = pt::ui::LayoutTextInBox(text, style, glm::vec2(0.0f), size, true);
    constexpr float kTolerance = 0.01f;
    for (const pt::ui::TextLine& line : layout.lines) {
        if (line.width > size.x + kTolerance) return false;
        for (const pt::ui::LaidGlyph& glyph : line.glyphs) {
            const glm::vec2 end = glyph.position + glyph.size;
            if (glyph.position.x < -kTolerance || glyph.position.y < -kTolerance || end.x > size.x + kTolerance || end.y > size.y + kTolerance) return false;
        }
    }
    return true;
}

bool HasAllGlyphs(const pt::ui::FfntFont& font, std::string_view text) {
    for (uint32_t code : pt::ui::DecodeUtf8(text)) {
        if (code >= 32 && code != pt::ui::kInlinePicture && !font.Find(code)) return false;
    }
    return true;
}

}

int main() {
    pt::ui::FfntFont font, subtitle_font;
    std::string error;
    if (!font.LoadUnicodeFont("Noto Sans KR", pt::localized::Characters(14), false, &error, 500)) {
        std::printf("FAIL: Korean font: %s\n", error.c_str());
        return 1;
    }
    if (!subtitle_font.LoadUnicodeFont("Noto Sans KR", pt::localized::Characters(14), false, &error, 300)) {
        std::printf("FAIL: Korean subtitle font: %s\n", error.c_str());
        return 1;
    }

    pt::ui::TextStyle pc;
    pc.font = &font;
    pc.font_width = pc.font_height = 20.0f;
    int failures = 0;
    // Japanese is the Korean localization's source language.  Every source
    // menu/transcript key must have a Korean value; this catches newly added
    // upstream PC-settings and Museum entries before a release is packaged.
    for (const pt::japanese::Text& entry : pt::japanese::kMenu) {
        if (pt::korean::Menu(entry.key).empty()) {
            std::printf("FAIL: missing Korean source-text key: %.*s\n", int(entry.key.size()), entry.key.data());
            ++failures;
        }
    }
    // Keep newly introduced upstream PC-settings rows from silently falling
    // back to English when the port is updated.
    for (const char* key : {"pc_photo_reset_settings", "pc_note_photo_reset_settings"}) {
        if (pt::korean::Menu(key).empty()) {
            std::printf("FAIL: missing Korean PC-settings text: %s\n", key);
            ++failures;
        }
    }
    for (const pt::korean::Text& entry : pt::korean::kMenu) {
        if (!HasAllGlyphs(font, entry.text)) {
            std::printf("FAIL: missing Korean glyphs in %.*s\n", int(entry.key.size()), entry.key.data());
            ++failures;
        }
        // The PC settings help area is 1100 by 86 virtual pixels. LayoutTextInBox is the same fit-to-box path the game uses.
        const bool help = entry.key.starts_with("pc_note_");
        const glm::vec2 box = help ? glm::vec2(1100.0f, 86.0f) : glm::vec2(300.0f, 30.0f);
        if (!Fits(entry.text, pc, box)) {
            std::printf("FAIL: Korean text escapes %s box: %.*s\n", help ? "help" : "label", int(entry.key.size()), entry.key.data());
            ++failures;
        }
    }

    pt::ui::TextStyle subtitle;
    subtitle.font = &subtitle_font;
    subtitle.font_width = subtitle.font_height = 32.0f;
    subtitle.text_space = -6.0f;
    subtitle.line_space = -4.0f;
    pt::ui::NormalizeSubtitleStyle(subtitle);
    for (const pt::korean::Subtitle& entry : pt::korean::kSubtitles) for (std::string_view line : entry.lines) {
        if (!HasAllGlyphs(subtitle_font, line) || !Fits(line, subtitle, glm::vec2(1024.0f, 160.0f))) {
            std::printf("FAIL: Korean subtitle layout: %08x\n", entry.key);
            ++failures;
        }
    }

    // Latin product names and credits can use every standard ligature in the
    // bundled Noto Sans KR face.  Every resulting shaped glyph must exist in
    // the Korean atlas, rather than only the individual ASCII characters.
    for (const auto& face : {&font, &subtitle_font}) {
        for (const char* text : {"NVIDIA Reflex", "Coffee with Lisa", "first", "office", "affluent"}) {
            for (const pt::ui::ShapedGlyph& glyph : face->ShapeLine(pt::ui::DecodeUtf8(text), 0.0f)) {
                if (!glyph.glyph) {
                    std::printf("FAIL: Korean ligature missing in %s\n", text);
                    ++failures;
                    break;
                }
            }
        }
    }

    std::printf("Korean layout: %zu menu entries, %zu subtitles, %zu glyphs, %d failures\n",
                std::size(pt::korean::kMenu), std::size(pt::korean::kSubtitles), font.Glyphs().size(), failures);
    return failures ? 1 : 0;
}
