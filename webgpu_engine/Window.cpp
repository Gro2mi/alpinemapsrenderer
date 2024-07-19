/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2024 Patrick Komon
 * Copyright (C) 2024 Gerald Kimmersdorfer
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

#include "Window.h"
#include <webgpu/raii/RenderPassEncoder.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif
#include <webgpu/webgpu.h>
#include <webgpu/webgpu_interface.hpp>

#include <glm/gtx/string_cast.hpp>

#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
#include "imgui.h"
#endif

namespace webgpu_engine {

Window::Window()
    : m_tile_manager { std::make_unique<TileManager>() }
{
}

Window::~Window()
{
  // Destructor cleanup logic here
}

void Window::set_wgpu_context(WGPUInstance instance, WGPUDevice device, WGPUAdapter adapter, WGPUSurface surface, WGPUQueue queue)
{
    m_instance = instance;
    m_device = device;
    m_adapter = adapter;
    m_surface = surface;
    m_queue = queue;
}

void Window::initialise_gpu()
{
    assert(m_device != nullptr); // just make sure that wgpu context is set

    create_buffers();

    m_shader_manager = std::make_unique<ShaderModuleManager>(m_device);
    m_shader_manager->create_shader_modules();
    m_pipeline_manager = std::make_unique<PipelineManager>(m_device, *m_shader_manager);
    m_pipeline_manager->create_pipelines();
    create_bind_groups();

    m_compute_graph = std::make_unique<compute::nodes::NodeGraph>();
    m_compute_graph->init_test_node_graph(*m_pipeline_manager, m_device);
    connect(m_compute_graph.get(), &compute::nodes::NodeGraph::run_finished, this, &Window::request_redraw);

    m_tile_manager->init(m_device, m_queue, *m_pipeline_manager, *m_compute_graph);

    qInfo() << "gpu_ready_changed";
    emit gpu_ready_changed(true);
}

void Window::resize_framebuffer(int w, int h)
{
    m_swapchain_size = glm::vec2(w, h);

    m_gbuffer_format = webgpu::FramebufferFormat(m_pipeline_manager->tile_pipeline().framebuffer_format());
    m_gbuffer_format.size = glm::uvec2 { w, h };
    m_gbuffer = std::make_unique<webgpu::Framebuffer>(m_device, m_gbuffer_format);

    webgpu::FramebufferFormat atmosphere_framebuffer_format(m_pipeline_manager->atmosphere_pipeline().framebuffer_format());
    atmosphere_framebuffer_format.size = glm::uvec2(1, h);
    m_atmosphere_framebuffer = std::make_unique<webgpu::Framebuffer>(m_device, atmosphere_framebuffer_format);

    m_compose_bind_group = std::make_unique<webgpu::raii::BindGroup>(m_device, m_pipeline_manager->compose_bind_group_layout(),
        std::initializer_list<WGPUBindGroupEntry> {
            m_gbuffer->color_texture_view(0).create_bind_group_entry(0), // albedo texture
            m_gbuffer->color_texture_view(1).create_bind_group_entry(1), // position texture
            m_gbuffer->color_texture_view(2).create_bind_group_entry(2), // normal texture
            m_atmosphere_framebuffer->color_texture_view(0).create_bind_group_entry(3), // atmosphere texture
        });
}

std::unique_ptr<webgpu::raii::RenderPassEncoder> begin_render_pass(
    WGPUCommandEncoder encoder, WGPUTextureView color_attachment, WGPUTextureView depth_attachment)
{
    return std::make_unique<webgpu::raii::RenderPassEncoder>(encoder, color_attachment, depth_attachment);
}

void Window::paint(webgpu::Framebuffer* framebuffer, WGPUCommandEncoder encoder)
{
    // Painting logic here, using the optional framebuffer parameter which is currently unused

    // ONLY ON CAMERA CHANGE!
    // update_camera(m_camera);
    // emit update_camera_requested();

    // TODO remove, debugging
    // uboSharedConfig* sc = &m_shared_config_ubo->data;
    // sc->m_sun_light = QVector4D(0.0f, 1.0f, 1.0f, 1.0f);
    // sc->m_sun_light_dir = QVector4D(elapsed, 1.0f, 1.0f, 1.0f);
    // ToDo only update on change?
    m_shared_config_ubo->update_gpu_data(m_queue);

    // render atmosphere to color buffer
    {
        std::unique_ptr<webgpu::raii::RenderPassEncoder> render_pass = m_atmosphere_framebuffer->begin_render_pass(encoder);
        wgpuRenderPassEncoderSetBindGroup(render_pass->handle(), 0, m_camera_bind_group->handle(), 0, nullptr);
        wgpuRenderPassEncoderSetPipeline(render_pass->handle(), m_pipeline_manager->atmosphere_pipeline().pipeline().handle());
        wgpuRenderPassEncoderDraw(render_pass->handle(), 3, 1, 0, 0);
    }

    // render tiles to geometry buffers
    {
        std::unique_ptr<webgpu::raii::RenderPassEncoder> render_pass = m_gbuffer->begin_render_pass(encoder);
        wgpuRenderPassEncoderSetBindGroup(render_pass->handle(), 0, m_shared_config_bind_group->handle(), 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(render_pass->handle(), 1, m_camera_bind_group->handle(), 0, nullptr);

        const auto tile_set = m_tile_manager->generate_tilelist(m_camera);
        m_tile_manager->draw(render_pass->handle(), m_camera, tile_set, true, m_camera.position());
    }

    // render geometry buffers to target framebuffer
    {
        std::unique_ptr<webgpu::raii::RenderPassEncoder> render_pass = framebuffer->begin_render_pass(encoder);
        wgpuRenderPassEncoderSetPipeline(render_pass->handle(), m_pipeline_manager->compose_pipeline().pipeline().handle());
        wgpuRenderPassEncoderSetBindGroup(render_pass->handle(), 0, m_shared_config_bind_group->handle(), 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(render_pass->handle(), 1, m_camera_bind_group->handle(), 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(render_pass->handle(), 2, m_compose_bind_group->handle(), 0, nullptr);
        wgpuRenderPassEncoderDraw(render_pass->handle(), 3, 1, 0, 0);
    }

    m_needs_redraw = false;
}

void Window::paint_gui()
{
#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
    if (ImGui::Combo("Normal Mode", (int*)&m_shared_config_ubo->data.m_normal_mode, "None\0Flat\0Smooth\0\0")) {
        m_needs_redraw = true;
    }
    {
        static int currentItem = m_shared_config_ubo->data.m_overlay_mode;
        static const std::vector<std::pair<std::string, int>> overlays
            = { { "None", 0 }, { "Normals", 1 }, { "Tiles", 2 }, { "Zoomlevel", 3 }, { "Vertex-ID", 4 }, { "Vertex Height-Sample", 5 },
                  { "Compute Output", 99 }, { "Decoded Normals", 100 }, { "Steepness", 101 }, { "SSAO Buffer", 102 }, { "Shadow Cascades", 103 } };
        const char* currentItemLabel = overlays[currentItem].first.c_str();
        if (ImGui::BeginCombo("Overlay", currentItemLabel)) {
            for (size_t i = 0; i < overlays.size(); i++) {
                bool isSelected = ((size_t)currentItem == i);
                if (ImGui::Selectable(overlays[i].first.c_str(), isSelected)) {
                    currentItem = i;
                    m_needs_redraw = true;
                }
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        m_shared_config_ubo->data.m_overlay_mode = overlays[currentItem].second;
        if (m_shared_config_ubo->data.m_overlay_mode > 0) {
            if (ImGui::SliderFloat("Overlay Strength", &m_shared_config_ubo->data.m_overlay_strength, 0.0f, 1.0f)) {
                m_needs_redraw = true;
            }
        }
        if (m_shared_config_ubo->data.m_overlay_mode >= 100) {
            if (ImGui::Checkbox("Overlay Post Shading", (bool*)&m_shared_config_ubo->data.m_overlay_postshading_enabled)) {
                m_needs_redraw = true;
            }
        }
    }

    if (ImGui::Checkbox("Phong Shading", (bool*)&m_shared_config_ubo->data.m_phong_enabled)) {
        m_needs_redraw = true;
    }

    if (ImGui::CollapsingHeader("Compute pipeline")) {
        if (ImGui::Button("Run pipeline", ImVec2(280, 20))) {
            m_compute_graph->run();
        }
    }
#endif
}

glm::vec4 Window::synchronous_position_readback(const glm::dvec2& ndc) {
    if (!m_position_readback_done) return {};

    // A little bit silly, but we have to transform it back to device coordinates
    glm::uvec2 device_coordinates = {
        (ndc.x + 1) * 0.5 * m_swapchain_size.x,
        (1 - (ndc.y + 1) * 0.5) * m_swapchain_size.y
    };

    // clamp device coordinates to the swapchain size
    device_coordinates = glm::clamp(device_coordinates, glm::uvec2(0), glm::uvec2(m_swapchain_size - glm::vec2(1.0)));

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_device, {});

    const auto& src_texture = m_gbuffer->color_texture(1);
    // Define Source Texture
    WGPUImageCopyTexture image_copy_texture_source = {};
    image_copy_texture_source.texture = src_texture.handle();
    image_copy_texture_source.mipLevel = 0;
    image_copy_texture_source.origin = { device_coordinates.x, device_coordinates.y, 0 };
    image_copy_texture_source.aspect = {};
    // Define destination buffer
    WGPUTextureDataLayout texture_data_layout = {
        .nextInChain = nullptr,
        .offset      = 0,
        .bytesPerRow = 256, // multiple of 256
        .rowsPerImage = 1,
    };
    WGPUImageCopyBuffer image_copy_buffer_destination = {};
    image_copy_buffer_destination.layout = texture_data_layout;
    image_copy_buffer_destination.buffer = m_position_readback_buffer->handle();
    WGPUExtent3D image_copy_extent = {
        .width = 1,
        .height = 1,
        .depthOrArrayLayers = 1,
    };
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &image_copy_texture_source, &image_copy_buffer_destination, &image_copy_extent);

    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, {});
    wgpuCommandEncoderRelease(encoder);
    wgpuQueueSubmit(m_queue, 1, &command);
    wgpuCommandBufferRelease(command);

    m_position_readback_done = false;
    wgpuQueueOnSubmittedWorkDone(m_queue, []([[maybe_unused]]WGPUQueueWorkDoneStatus status, void* pUserData) {
            auto onBufferMapped = [](WGPUBufferMapAsyncStatus status, void* pUserData) {
                Window* _this = reinterpret_cast<Window*>(pUserData);

                if (status != WGPUBufferMapAsyncStatus_Success) {
                    qWarning() << "Error in buffer mapping";
                    _this->m_position_readback_done = true;
                    return;
                };

                glm::vec4* bufferData = (glm::vec4*)wgpuBufferGetConstMappedRange(_this->m_position_readback_buffer->handle(), 0, sizeof(glm::vec4));
                _this->m_position_readback_result = bufferData[0];
                wgpuBufferUnmap(_this->m_position_readback_buffer->handle());
                _this->m_position_readback_done = true;
            };
            Window* _this = reinterpret_cast<Window*>(pUserData);
            if (status != WGPUQueueWorkDoneStatus_Success) {
                qWarning() << "Error in queue work";
                _this->m_position_readback_done = true;
                return;
            }
            wgpuBufferMapAsync(_this->m_position_readback_buffer->handle(), WGPUMapMode_Read, 0, sizeof(glm::vec4), onBufferMapped, pUserData);
        }, this);

    webgpu::waitForFlag(m_device, &m_position_readback_done);

    //std::cout << "Position: " << glm::to_string(m_position_readback_result) << std::endl;
    return m_position_readback_result;
}


float Window::depth([[maybe_unused]] const glm::dvec2& normalised_device_coordinates)
{
    auto position = synchronous_position_readback(normalised_device_coordinates);
    return position.z;
}

glm::dvec3 Window::position([[maybe_unused]] const glm::dvec2& normalised_device_coordinates)
{
    // If we read position directly no reconstruction is necessary
    //glm::dvec3 reconstructed = m_camera.position() + m_camera.ray_direction(normalised_device_coordinates) * (double)depth(normalised_device_coordinates);
    auto position = synchronous_position_readback(normalised_device_coordinates);
    return m_camera.position() + glm::dvec3(position.x, position.y, position.z);
}

void Window::deinit_gpu()
{
    m_pipeline_manager->release_pipelines();
    m_shader_manager->release_shader_modules();
    emit gpu_ready_changed(false);
}

void Window::set_aabb_decorator(const nucleus::tile_scheduler::utils::AabbDecoratorPtr& aabb_decorator) { m_tile_manager->set_aabb_decorator(aabb_decorator); }

void Window::set_quad_limit(unsigned int new_limit) { m_tile_manager->set_quad_limit(new_limit); }

nucleus::camera::AbstractDepthTester* Window::depth_tester()
{
    // Return this object as the depth tester
    return this;
}

nucleus::utils::ColourTexture::Format Window::ortho_tile_compression_algorithm() const
{
    // TODO use compressed textures in the future
    return nucleus::utils::ColourTexture::Format::Uncompressed_RGBA;
}

void Window::set_permissible_screen_space_error([[maybe_unused]] float new_error)
{
  // Logic for setting permissible screen space error, parameter currently unused
}

void Window::update_camera([[maybe_unused]] const nucleus::camera::Definition& new_definition)
{
    // NOTE: Could also just be done on camera or viewport change!
    uboCameraConfig* cc = &m_camera_config_ubo->data;
    cc->position = glm::vec4(new_definition.position(), 1.0);
    cc->view_matrix = new_definition.local_view_matrix();
    cc->proj_matrix = new_definition.projection_matrix();
    cc->view_proj_matrix = cc->proj_matrix * cc->view_matrix;
    cc->inv_view_proj_matrix = glm::inverse(cc->view_proj_matrix);
    cc->inv_view_matrix = glm::inverse(cc->view_matrix);
    cc->inv_proj_matrix = glm::inverse(cc->proj_matrix);
    cc->viewport_size = new_definition.viewport_size();
    cc->distance_scaling_factor = new_definition.distance_scale_factor();
    m_camera_config_ubo->update_gpu_data(m_queue);
    m_camera = new_definition;

    m_needs_redraw = true;
}

void Window::update_debug_scheduler_stats([[maybe_unused]] const QString& stats)
{
  // Logic for updating debug scheduler stats, parameter currently unused
}

void Window::update_gpu_quads([[maybe_unused]] const std::vector<nucleus::tile_scheduler::tile_types::GpuTileQuad>& new_quads,
    [[maybe_unused]] const std::vector<tile::Id>& deleted_quads)
{
    // std::cout << "received " << new_quads.size() << " new quads, should delete " << deleted_quads.size() << " quads" << std::endl;
    m_tile_manager->update_gpu_quads(new_quads, deleted_quads);
    m_needs_redraw = true;
}

void Window::request_redraw() { m_needs_redraw = true; }

void Window::create_buffers()
{
    m_shared_config_ubo = std::make_unique<Buffer<uboSharedConfig>>(m_device, WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform);
    m_camera_config_ubo = std::make_unique<Buffer<uboCameraConfig>>(m_device, WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform);
    m_position_readback_buffer = std::make_unique<webgpu::raii::RawBuffer<glm::vec4>>(
        m_device, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead, 256 / sizeof(glm::vec4), "position readback buffer");
}

void Window::create_bind_groups()
{
    m_shared_config_bind_group = std::make_unique<webgpu::raii::BindGroup>(m_device, m_pipeline_manager->shared_config_bind_group_layout(),
        std::initializer_list<WGPUBindGroupEntry> { m_shared_config_ubo->raw_buffer().create_bind_group_entry(0) });

    m_camera_bind_group = std::make_unique<webgpu::raii::BindGroup>(m_device, m_pipeline_manager->camera_bind_group_layout(),
        std::initializer_list<WGPUBindGroupEntry> { m_camera_config_ubo->raw_buffer().create_bind_group_entry(0) });
}

void Window::update_required_gpu_limits(WGPULimits& limits, const WGPULimits& supported_limits)
{
    if (supported_limits.maxColorAttachmentBytesPerSample < 32u) {
        qFatal("Minimum supported maxColorAttachmentBytesPerSample needs to be >=32");
    }
    if (supported_limits.maxTextureArrayLayers < 1024u) {
        qWarning() << "Minimum supported maxTextureArrayLayers is " << supported_limits.maxTextureArrayLayers << " (1024 recommended)!";
    }
    limits.maxColorAttachmentBytesPerSample = std::max(limits.maxColorAttachmentBytesPerSample, 32u);
    limits.maxTextureArrayLayers = std::min(std::max(limits.maxTextureArrayLayers, 1024u), supported_limits.maxTextureArrayLayers);
}

} // namespace webgpu_engine
