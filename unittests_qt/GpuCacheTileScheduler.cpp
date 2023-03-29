/*****************************************************************************
 * Alpine Terrain Builder
 * Copyright (C) 2022 alpinemaps.org
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

#include "nucleus/tile_scheduler/GpuCacheTileScheduler.h"
#include "nucleus/tile_scheduler/utils.h"
#include "unittests_qt/TileScheduler.h"

#include <unordered_set>

#include <QSignalSpy>
#include <QTest>
#include <glm/glm.hpp>

#include "nucleus/camera/Definition.h"
#include "nucleus/camera/stored_positions.h"
#include "sherpa/TileHeights.h"

using nucleus::tile_scheduler::GpuCacheTileScheduler;

class TestGpuCacheTileScheduler : public TestTileScheduler {
    Q_OBJECT
private:
    std::unique_ptr<TileScheduler> makeScheduler() const override
    {
        auto sch = std::make_unique<GpuCacheTileScheduler>();
        TileHeights h;
        h.emplace({ 0, { 0, 0 } }, { 100, 200 });
        sch->set_aabb_decorator(nucleus::tile_scheduler::AabbDecorator::make(std::move(h)));
        sch->set_max_n_simultaneous_requests(400);
        return sch;
    }

private slots:
    //  void initTestCase() {}    // implementing these functions will override TestTileScheduler and break the tests.
    //  void init() {}            // so call the TestTileScheduler::init and initTestCase somehow, then it should be good again.
    void init()
    {
        TestTileScheduler::init();
        dynamic_cast<GpuCacheTileScheduler*>(m_scheduler.get())->set_gpu_cache_size(0);
    }
    void loadCandidates()
    {
        const auto tile_list = dynamic_cast<GpuCacheTileScheduler*>(m_scheduler.get())->load_candidates(test_cam, m_scheduler->aabb_decorator());
        QVERIFY(!tile_list.empty());
    }

    void no_crash_with_0_cache_size()
    {
        dynamic_cast<GpuCacheTileScheduler*>(m_scheduler.get())->set_gpu_cache_size(0);
        QVERIFY(m_scheduler->gpu_tiles().empty());
        connect(m_scheduler.get(), &TileScheduler::tile_requested, this, &TestTileScheduler::giveTiles);
        connect(this, &TestTileScheduler::orthoTileReady, m_scheduler.get(), &TileScheduler::receive_ortho_tile);
        connect(this, &TestTileScheduler::heightTileReady, m_scheduler.get(), &TileScheduler::receive_height_tile);
        m_scheduler->update_camera(test_cam);
        QTest::qWait(10);
        // tiles are on the gpu
        const auto gpu_tiles = m_scheduler->gpu_tiles();

        QSignalSpy spy(m_scheduler.get(), &TileScheduler::tile_expired);
        nucleus::camera::Definition replacement_cam = nucleus::camera::stored_positions::westl_hochgrubach_spitze();
        replacement_cam.set_viewport_size({ 2560, 1440 });
        m_scheduler->update_camera(replacement_cam);
    }

    void expiresOldGpuTiles()
    {
        dynamic_cast<GpuCacheTileScheduler*>(m_scheduler.get())->set_gpu_cache_size(400);
        QVERIFY(m_scheduler->gpu_tiles().empty());
        connect(m_scheduler.get(), &TileScheduler::tile_requested, this, &TestTileScheduler::giveTiles);
        connect(this, &TestTileScheduler::orthoTileReady, m_scheduler.get(), &TileScheduler::receive_ortho_tile);
        connect(this, &TestTileScheduler::heightTileReady, m_scheduler.get(), &TileScheduler::receive_height_tile);
        m_scheduler->update_camera(test_cam);
        QTest::qWait(50);
        // tiles are on the gpu
        const auto gpu_tiles = m_scheduler->gpu_tiles();

        QSignalSpy spy(m_scheduler.get(), &TileScheduler::tile_expired);
        nucleus::camera::Definition replacement_cam = nucleus::camera::stored_positions::westl_hochgrubach_spitze();
        replacement_cam.set_viewport_size({ 2560, 1440 });
        m_scheduler->update_camera(replacement_cam);
        spy.wait(100);
        QCOMPARE(m_scheduler->gpu_tiles().size(), 400); // 400 cached tiles should remain
        for (const auto& tileExpireSignal : spy) {
            const tile::Id tile = tileExpireSignal.at(0).value<tile::Id>();
            QVERIFY(gpu_tiles.contains(tile));
        }
    }
};

QTEST_MAIN(TestGpuCacheTileScheduler)
#include "GpuCacheTileScheduler.moc"
