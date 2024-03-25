/*****************************************************************************
 * Alpine Terrain Renderer
 * Copyright (C) 2024 Lucas Dworschak
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/

#pragma once

#include <stb_slim/stb_truetype.h>

#include <QImage>
#include <unordered_map>
#include <vector>

#include <nucleus/Raster.h>
#include <nucleus/map_label/MapLabelData.h>

namespace nucleus::maplabel {
class LabelFactory {
public:
    const LabelMeta create_label_meta();

    const std::vector<VertexData> create_labels(const std::unordered_set<std::shared_ptr<nucleus::vectortile::FeatureTXT>>& features);
    void create_label(const QString text, const glm::vec3 position, const float importance, std::vector<VertexData>& vertex_data);

    static const inline std::vector<unsigned int> indices = { 0, 1, 2, 0, 2, 3 };

private:
    constexpr static float font_size = 48.0f;
    constexpr static glm::vec2 icon_size = glm::vec2(50.0f);

    Raster<uint8_t> make_font_raster();
    Raster<glm::u8vec2> make_outline(const Raster<uint8_t>& font_bitmap);

    std::vector<float> inline create_text_meta(std::u16string* safe_chars, float* text_width);

    // list of all characters that will be available (will be rendered to the font_atlas)
    // list will be generated by vectortile extractor
    static constexpr char16_t all_char_list[] = { 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 45, 46, 47, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56,
        57, 58, 59, 60, 61, 62, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95,
        96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126,
        167, 176, 180, 196, 214, 220, 223, 225, 226, 228, 233, 236, 237, 243, 246, 250, 252, 253, 268, 269, 283, 328, 337, 345, 352, 353, 381, 382, 8364 };

    // static constexpr glm::ivec2 m_font_outline = glm::ivec2(3, 3);
    static constexpr float m_font_outline = 7.2f;
    static constexpr glm::ivec2 m_font_padding = glm::ivec2(2, 2);
    static constexpr QSize m_font_atlas_size = QSize(1024, 1024);
    static constexpr float uv_width_norm = 1.0f / m_font_atlas_size.width();

    std::unordered_map<char16_t, const CharData> m_char_data;
    stbtt_fontinfo m_fontinfo;

    QByteArray m_font_file;
};
} // namespace nucleus::maplabel
