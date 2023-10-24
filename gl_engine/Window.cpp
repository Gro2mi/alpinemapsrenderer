/*****************************************************************************
 * Alpine Terrain Renderer
 * Copyright (C) 2022 Adam Celarek
 * Copyright (C) 2023 Jakob Lindner
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

#include <array>

#include <QDebug>
#include <QImage>
#include <QMoveEvent>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLDebugLogger>
#include <QOpenGLExtraFunctions>

#include <QOpenGLVersionFunctionsFactory>

#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QPropertyAnimation>
#include <QRandomGenerator>
#include <QSequentialAnimationGroup>
#include <QTimer>
#include <glm/glm.hpp>
#include "DebugPainter.h"
#include "Framebuffer.h"
#include "ShaderManager.h"
#include "ShaderProgram.h"
#include "TileManager.h"
#include "Window.h"
#include "helpers.h"
#include "TimerManager.h"
#include "SSAO.h"
#include "ShadowMapping.h"

#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>

#include "UniformBufferObjects.h"

#ifndef __EMSCRIPTEN__
#include <QOpenGLFunctions_3_3_Core>
#endif

using gl_engine::Window;
using gl_engine::UniformBuffer;

Window::Window()
    : m_camera({ 1822577.0, 6141664.0 - 500, 171.28 + 500 }, { 1822577.0, 6141664.0, 171.28 }) // should point right at the stephansdom
{
    qDebug("Window::Window()");
    m_tile_manager = std::make_unique<TileManager>();
    QTimer::singleShot(1, [this]() { emit update_requested(); });
}

Window::~Window()
{
    qDebug("~Window::Window()");
}

void Window::initialise_gpu()
{
    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
    if (f->hasOpenGLFeature(QOpenGLExtraFunctions::OpenGLFeature::MultipleRenderTargets)) {
    }

    QOpenGLDebugLogger* logger = new QOpenGLDebugLogger(this);
    logger->initialize();
    connect(logger, &QOpenGLDebugLogger::messageLogged, [](const auto& message) {
        qDebug() << message;
    });
    logger->disableMessages(QList<GLuint>({ 131185 }));
    logger->startLogging(QOpenGLDebugLogger::SynchronousLogging);

    m_debug_painter = std::make_unique<DebugPainter>();
    m_shader_manager = std::make_unique<ShaderManager>();

    m_tile_manager->init();
    m_tile_manager->initilise_attribute_locations(m_shader_manager->tile_shader());
    m_screen_quad_geometry = gl_engine::helpers::create_screen_quad_geometry();
    m_gbuffer = std::make_unique<Framebuffer>(Framebuffer::DepthFormat::Float32,
                                              std::vector{
                                                  TextureDefinition{ Framebuffer::ColourFormat::RGB8   },       // Albedo
                                                  TextureDefinition{ Framebuffer::ColourFormat::RG16UI  },      // Octahedron Normals
                                                  TextureDefinition{ Framebuffer::ColourFormat::R32UI   }       // Discretized Encoded Depth for readback
                                                  //TextureDefinition{ Framebuffer::ColourFormat::RGBA32F}
                                              });

    m_atmospherebuffer = std::make_unique<Framebuffer>(Framebuffer::DepthFormat::None, std::vector{ TextureDefinition{Framebuffer::ColourFormat::RGBA8} });

    m_shared_config_ubo = std::make_shared<gl_engine::UniformBuffer<gl_engine::uboSharedConfig>>(0, "shared_config");
    m_shared_config_ubo->init();
    m_shared_config_ubo->bind_to_shader(m_shader_manager->all());

    m_camera_config_ubo = std::make_shared<gl_engine::UniformBuffer<gl_engine::uboCameraConfig>>(1, "camera_config");
    m_camera_config_ubo->init();
    m_camera_config_ubo->bind_to_shader(m_shader_manager->all());

    m_shadow_config_ubo = std::make_shared<gl_engine::UniformBuffer<gl_engine::uboShadowConfig>>(2, "shadow_config");
    m_shadow_config_ubo->init();
    m_shadow_config_ubo->bind_to_shader(m_shader_manager->all());

    m_ssao = std::make_unique<gl_engine::SSAO>(m_shader_manager->shared_ssao_program(), m_shader_manager->shared_ssao_blur_program());

    m_shadowmapping = std::make_unique<gl_engine::ShadowMapping>(m_shader_manager->shared_shadowmap_program(), m_shadow_config_ubo, m_shared_config_ubo);

    m_timer = std::make_unique<gl_engine::TimerManager>();

    m_timer->add_timer("ssao", gl_engine::TimerTypes::GPUAsync, "GPU");
    m_timer->add_timer("atmosphere", gl_engine::TimerTypes::GPUAsync, "GPU");
    m_timer->add_timer("tiles", gl_engine::TimerTypes::GPUAsync, "GPU");
    m_timer->add_timer("shadowmap", gl_engine::TimerTypes::GPUAsync, "GPU");
    m_timer->add_timer("compose", gl_engine::TimerTypes::GPUAsync, "GPU");
    m_timer->add_timer("cpu_total", gl_engine::TimerTypes::CPU, "TOTAL");
    m_timer->add_timer("gpu_total", gl_engine::TimerTypes::GPUAsync, "TOTAL");
    m_timer->add_timer("draw_list", gl_engine::TimerTypes::CPU, "CPU");
    m_timer->add_timer("all", gl_engine::TimerTypes::CPU, "TOTAL");


    emit gpu_ready_changed(true);
}

void Window::resize_framebuffer(int width, int height)
{
    if (width == 0 || height == 0)
        return;

    QOpenGLFunctions* f = QOpenGLContext::currentContext()->functions();
    if (f) {
        m_gbuffer->resize({ width, height });
        m_atmospherebuffer->resize({ 1, height });
        m_ssao->resize({width, height});

        f->glViewport(0, 0, width, height);
    }
}

gl_engine::ShaderProgram create_debug_shader(const char* fragmentShaderOverride = nullptr)
{
    static const char* const fragment_source = R"(
    out lowp vec4 out_Color;
    void main() {
        out_Color = vec4(0.2, 0.0, 1.0, 0.8);
    })";
    static const char* const vertex_source = R"(
out highp vec2 texcoords;
void main() {
    vec2 vertices[3]=vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(vertices[gl_VertexID], 0.0, 1.0);
    texcoords = 0.5 * gl_Position.xy + vec2(0.5);
})";
    gl_engine::ShaderProgram tmp(vertex_source, fragmentShaderOverride ? fragmentShaderOverride : fragment_source, gl_engine::ShaderCodeSource::PLAINTEXT);
    return tmp;
}

//static int paintCalls = 0;
void Window::paint(QOpenGLFramebufferObject* framebuffer)
{
    //if (paintCalls++ > 0) return;
    m_timer->start_timer("cpu_total");
    m_timer->start_timer("gpu_total");

    m_camera.set_viewport_size(m_gbuffer->size());

    QOpenGLExtraFunctions *f = QOpenGLContext::currentContext()->extraFunctions();

    // for wireframe mode
    #ifndef __EMSCRIPTEN__
    auto funcs = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>(QOpenGLContext::currentContext());
    #endif

    f->glEnable(GL_CULL_FACE);
    f->glCullFace(GL_BACK);

    // UPDATE CAMERA UNIFORM BUFFER
    // NOTE: Could also just be done on camera or viewport change!
    uboCameraConfig* cc = &m_camera_config_ubo->data;
    cc->position = glm::vec4(m_camera.position(), 1.0);
    cc->view_matrix = m_camera.local_view_matrix();
    cc->proj_matrix = m_camera.projection_matrix();
    cc->view_proj_matrix = cc->proj_matrix * cc->view_matrix;
    cc->inv_view_proj_matrix = glm::inverse(cc->view_proj_matrix);
    cc->inv_view_matrix = glm::inverse(cc->view_matrix);
    cc->inv_proj_matrix = glm::inverse(cc->proj_matrix);
    cc->viewport_size = m_gbuffer->size();
    m_camera_config_ubo->update_gpu_data();


    // DRAW ATMOSPHERIC BACKGROUND
    m_atmospherebuffer->bind();
    f->glClearColor(0.0, 0.0, 0.0, 1.0);
    f->glClear(GL_COLOR_BUFFER_BIT);
    f->glDisable(GL_DEPTH_TEST);
    f->glDepthFunc(GL_ALWAYS);
    f->glViewport(0, 0, 1, cc->viewport_size.y);
    auto p = m_shader_manager->atmosphere_bg_program();
    p->bind();
    m_timer->start_timer("atmosphere");
    m_screen_quad_geometry.draw();
    m_timer->stop_timer("atmosphere");
    p->release();
    f->glViewport(0, 0, cc->viewport_size.x, cc->viewport_size.y);

    // Generate Draw-List
    // Note: Could also just be done on camera change
    m_timer->start_timer("draw_list");
    const auto draw_tiles = m_tile_manager->generate_tilelist(m_camera);
    m_timer->stop_timer("draw_list");

    // DRAW SHADOWMAPS
    if (m_shared_config_ubo->data.m_csm_enabled) {
        m_timer->start_timer("shadowmap");
        m_shadowmapping->draw(m_tile_manager.get(), draw_tiles, m_camera);
        m_timer->stop_timer("shadowmap");
    }


    // DRAW GBUFFER
    m_gbuffer->bind();

    // Clear Albedo-Buffer
    const GLfloat clearAlbedoColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    f->glClearBufferfv(GL_COLOR, 0, clearAlbedoColor);
    // Clear Normals-Buffer
    const GLuint clearNormalColor[2] = {0u, 0u};
    f->glClearBufferuiv(GL_COLOR, 1, clearNormalColor);
    // Clear Encoded-Depth Buffer
    const GLuint clearEncDepthColor[1] = {0u};
    f->glClearBufferuiv(GL_COLOR, 2, clearEncDepthColor);
    // Clear Depth-Buffer
    //f->glClearDepthf(0.0f); // for reverse z
    f->glClear(GL_DEPTH_BUFFER_BIT);


    f->glEnable(GL_DEPTH_TEST);
    //f->glDepthFunc(GL_GREATER); // for reverse z
    f->glDepthFunc(GL_LESS);

    #ifndef __EMSCRIPTEN__
    if (funcs && m_shared_config_ubo->data.m_wireframe_mode > 0) funcs->glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    #endif

    m_shader_manager->tile_shader()->bind();
    m_timer->start_timer("tiles");
    m_tile_manager->draw(m_shader_manager->tile_shader(), m_camera, draw_tiles, m_sort_tiles, m_camera.position());
    m_timer->stop_timer("tiles");

    #ifndef __EMSCRIPTEN__
    if (funcs && m_shared_config_ubo->data.m_wireframe_mode > 0) funcs->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    #endif

    m_gbuffer->unbind();

    m_shader_manager->tile_shader()->release();


    if (m_shared_config_ubo->data.m_ssao_enabled) {
        m_timer->start_timer("ssao");
        m_ssao->draw(m_gbuffer.get(), &m_screen_quad_geometry, m_camera,
                     m_shared_config_ubo->data.m_ssao_kernel, m_shared_config_ubo->data.m_ssao_blur_kernel_size);
        m_timer->stop_timer("ssao");
    }

    /*
    static ShaderProgram p1 = create_debug_shader( R"(
            uniform sampler2D texin_albedo;             // 8vec3
            layout (location = 0) out lowp vec4 out_Color;
            in highp vec2 texcoords;
            void main() {
                highp vec4 texin = texture(texin_albedo, texcoords);
                //out_Color = vec4(texin.rgb, 1.0);
                out_Color = vec4(texcoords.x, texcoords.y, texin.x, 1.0);
            }
        )");

    if (framebuffer)
        framebuffer->bind();
    p1.bind();
    p1.set_uniform("texin_albedo", 0);
    m_gbuffer->bind_colour_texture(0, 0);
    m_screen_quad_geometry.draw();
    */

    if (framebuffer)
        framebuffer->bind();

    p = m_shader_manager->compose_program();

    p->bind();
    p->set_uniform("texin_depth", 0);
    m_gbuffer->bind_depth_texture(0);
    p->set_uniform("texin_albedo", 1);
    m_gbuffer->bind_colour_texture(0, 1);
    p->set_uniform("texin_normal", 2);
    m_gbuffer->bind_colour_texture(1, 2);
    p->set_uniform("texin_atmosphere", 3);
    m_atmospherebuffer->bind_colour_texture(0, 3);
    p->set_uniform("texin_ssao", 4);
    m_ssao->bind_ssao_texture(4);

    m_shadowmapping->bind_shadow_maps(p, 5);

    m_timer->start_timer("compose");
    m_screen_quad_geometry.draw();
    m_timer->stop_timer("compose");

    m_timer->stop_timer("cpu_total");
    m_timer->stop_timer("gpu_total");
    if (m_render_looped) {
        m_timer->stop_timer("all");
    }

    QList<gl_engine::qTimerReport> new_values = m_timer->fetch_results();
    if (new_values.size() > 0) {
        emit report_measurements(new_values);
    }

    if (m_render_looped) {
        m_timer->start_timer("all");
        emit update_requested();
    }

}

void Window::paintOverGL(QPainter* painter)
{
    /*
    const auto frame_duration_text = QString("draw indicator: ");

    const auto random_u32 = QRandomGenerator::global()->generate();

    painter->setFont(QFont("Helvetica", 12));
    painter->setPen(Qt::white);
    QRect text_bb = painter->boundingRect(10, 20, 1, 15, Qt::TextSingleLine, frame_duration_text);
    painter->drawText(10, 20, frame_duration_text);
    painter->drawText(10, 40, m_debug_scheduler_stats);
    painter->drawText(10, 60, m_debug_text);
    painter->setBrush(QBrush(QColor(random_u32)));
    painter->drawRect(int(text_bb.right()) + 5, 8, 12, 12);*/
}

void Window::shared_config_changed(gl_engine::uboSharedConfig ubo) {
    m_shared_config_ubo->data = ubo;
    m_shared_config_ubo->update_gpu_data();
    emit update_requested();
}

void Window::render_looped_changed(bool render_looped_flag) {
    m_render_looped = render_looped_flag;
}

void Window::key_press(const QKeyCombination& e) {
    QKeyEvent ev = QKeyEvent(QEvent::Type::KeyPress, e.key(), e.keyboardModifiers());
    this->keyPressEvent(&ev);
}

void Window::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key::Key_F5) {
        m_shader_manager->reload_shaders();
        qDebug("all shaders reloaded");
        // NOTE: UBOs need to be reattached to the programs!
        m_shared_config_ubo->bind_to_shader(m_shader_manager->all());
        m_camera_config_ubo->bind_to_shader(m_shader_manager->all());
        m_shadow_config_ubo->bind_to_shader(m_shader_manager->all());
        emit update_requested();
    }
    if (e->key() == Qt::Key::Key_F6) {
        if (this->m_render_looped) {
            this->m_render_looped = false;
            qDebug("Rendering loop exited");
        } else {
            this->m_render_looped = true;
            qDebug("Rendering loop started");
        }
        emit update_requested();
    }
    if (e->key() == Qt::Key::Key_F7) {
        if (this->m_sort_tiles) {
            this->m_sort_tiles = false;
            qDebug("Tile-Sorting deactivated");
        } else {
            this->m_sort_tiles = true;
            qDebug("Tile-Sorting active");
        }
        emit update_requested();
    }
    if (e->key() == Qt::Key::Key_F11
        || (e->key() == Qt::Key_P && e->modifiers() == Qt::ControlModifier)
        || (e->key() == Qt::Key_F5 && e->modifiers() == Qt::ControlModifier)) {
        e->ignore();
    }

    emit key_pressed(e->keyCombination());
}

void Window::keyReleaseEvent(QKeyEvent* e)
{
    emit key_released(e->keyCombination());
}

void Window::updateCameraEvent()
{
    emit update_camera_requested();
}

void Window::set_permissible_screen_space_error(float new_error)
{
    if (m_tile_manager)
        m_tile_manager->set_permissible_screen_space_error(new_error);
}

void Window::update_camera(const nucleus::camera::Definition& new_definition)
{
    //    qDebug("void Window::update_camera(const nucleus::camera::Definition& new_definition)");
    m_camera = new_definition;
    emit update_requested();
}

void Window::update_debug_scheduler_stats(const QString& stats)
{
    m_debug_scheduler_stats = stats;
    emit update_requested();
}

void Window::update_gpu_quads(const std::vector<nucleus::tile_scheduler::tile_types::GpuTileQuad>& new_quads, const std::vector<tile::Id>& deleted_quads)
{
    assert(m_tile_manager);
    m_tile_manager->update_gpu_quads(new_quads, deleted_quads);
}

float Window::depth(const glm::dvec2& normalised_device_coordinates)
{
    uint32_t fakeNormalizedDepth;
    m_gbuffer->read_colour_attachment_pixel(2, normalised_device_coordinates, &fakeNormalizedDepth);
    const auto depth = float(std::exp(double(fakeNormalizedDepth) / 4294967295.0 * 13.0));
    return depth;
}

glm::dvec3 Window::position(const glm::dvec2& normalised_device_coordinates)
{
    return m_camera.position() + m_camera.ray_direction(normalised_device_coordinates) * (double)depth(normalised_device_coordinates);
}

void Window::deinit_gpu()
{
    emit gpu_ready_changed(false);
    m_tile_manager.reset();
    m_debug_painter.reset();
    m_shader_manager.reset();
    m_gbuffer.reset();
    m_screen_quad_geometry = {};
}

void Window::set_aabb_decorator(const nucleus::tile_scheduler::utils::AabbDecoratorPtr& new_aabb_decorator)
{
    assert(m_tile_manager);
    m_tile_manager->set_aabb_decorator(new_aabb_decorator);
}

void Window::remove_tile(const tile::Id& id)
{
    assert(m_tile_manager);
    m_tile_manager->remove_tile(id);
}

nucleus::camera::AbstractDepthTester* Window::depth_tester()
{
    return this;
}
