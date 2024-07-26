/*****************************************************************************
 * Alpine Terrain Renderer
 * Copyright (C) 2023 Adam Celarek
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

#include <QTimer>
#include <QtTest/QSignalSpy>
#include <catch2/catch_test_macros.hpp>

#include "test_helpers.h"
#include <nucleus/utils/image_loader.h>

#ifdef NDEBUG
constexpr bool asserts_are_enabled = false;
#else
constexpr bool asserts_are_enabled = true;
#endif

TEST_CASE("nucleus/bits_and_pieces: check that asserts are enabled")
{
    CHECK(asserts_are_enabled);
}

TEST_CASE("nucleus/bits_and_pieces: check that NaNs are enabled (-ffast-math removes support, "
          "-fno-finite-math-only puts it back in)")
{
    CHECK(std::isnan(std::numeric_limits<float>::quiet_NaN()
                     * float(std::chrono::system_clock::now().time_since_epoch().count())));
    CHECK(std::isnan(double(std::numeric_limits<float>::quiet_NaN()
                            * float(std::chrono::system_clock::now().time_since_epoch().count()))));
}

TEST_CASE("nucleus/bits_and_pieces: qt signals and slots")
{
    QTimer t;
    t.setSingleShot(true);
    t.setInterval(1);
    QSignalSpy spy(&t, &QTimer::timeout);
    t.start();
    spy.wait(20);
    CHECK(spy.size() == 1);
}

TEST_CASE("nucleus/bits_and_pieces: image loading")
{
    const auto white = nucleus::utils::image_loader::rgba8(test_helpers::white_jpeg_tile(4));
    REQUIRE(white.width() == 4);
    REQUIRE(white.height() == 4);
    for (const auto p : white.buffer()) {
        CHECK(p == glm::u8vec4(255, 255, 255, 255));
    }

    const auto black = nucleus::utils::image_loader::rgba8(test_helpers::black_png_tile(8));
    REQUIRE(black.width() == 8);
    REQUIRE(black.height() == 8);
    for (const auto p : black.buffer()) {
        CHECK(p == glm::u8vec4(0, 0, 0, 255));
    }
}
