#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "graphics/animated_texture_atlas/animated_texture_atlas.hpp"
#include "graphics/draw_info/draw_info.hpp"
#include "graphics/vertex_geometry/vertex_geometry.hpp"
#include "graphics/shader_standard/shader_standard.hpp"
#include "graphics/texture_packer/texture_packer.hpp"
#include "graphics/shader_cache/shader_cache.hpp"
#include "graphics/batcher/generated/batcher.hpp"
#include "graphics/fps_camera/fps_camera.hpp"
#include "graphics/window/window.hpp"

#include "utility/glfw_lambda_callback_manager/glfw_lambda_callback_manager.hpp"
#include "utility/texture_packer_model_loading/texture_packer_model_loading.hpp"
#include "utility/glm_printing/glm_printing.hpp"
#include "utility/print_utils/print_utils.hpp"
#include "utility/unique_id_generator/unique_id_generator.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

#include <cstdio>
#include <cstdlib>

unsigned int SCREEN_WIDTH = 640;
unsigned int SCREEN_HEIGHT = 480;

static void error_callback(int error, const char *description) { fprintf(stderr, "Error: %s\n", description); }

// Wrapper that automatically creates a lambda for member functions
template <typename T, typename R, typename... Args> auto wrap_member_function(T &obj, R (T::*f)(Args...)) {
    // Return a std::function that wraps the member function in a lambda
    return std::function<R(Args...)>{[&obj, f](Args &&...args) { return (obj.*f)(std::forward<Args>(args)...); }};
}

/**
 * @brief given an orthanormal basis A , and another orthanormal basis B specified by right up and forward, then
 * there exists a transformation T that maps the elements of one basis to the other, we construct it here:
 */
glm::mat4 compute_transform_to_rotate_basis_to_new_basis(glm::vec3 right, glm::vec3 up, glm::vec3 forward) {
    glm::mat4 rotation_matrix = glm::mat4(1.0f);
    rotation_matrix[0] = glm::vec4(right, 0.0f);
    rotation_matrix[1] = glm::vec4(up, 0.0f);
    rotation_matrix[2] = glm::vec4(-forward, 0.0f); // We negate the direction for correct facing
    return rotation_matrix;
}

int main() {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::debug);

    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("mwe_shader_cache_logs.txt", true);
    file_sink->set_level(spdlog::level::info);

    std::vector<spdlog::sink_ptr> sinks = {console_sink, file_sink};

    GLFWwindow *window =
        initialize_glfw_glad_and_return_window(SCREEN_WIDTH, SCREEN_HEIGHT, "glfw window", false, true, false);

    std::vector<ShaderType> requested_shaders = {ShaderType::TEXTURE_PACKER_CWL_V_TRANSFORMATION_UBOS_1024};
    ShaderCache shader_cache(requested_shaders, sinks);
    Batcher batcher(shader_cache);

    const auto textures_directory = std::filesystem::path("assets");
    std::filesystem::path output_dir = std::filesystem::path("assets") / "packed_textures";
    int container_side_length = 1024;

    TexturePacker texture_packer(textures_directory, output_dir, container_side_length);
    shader_cache.set_uniform(ShaderType::TEXTURE_PACKER_CWL_V_TRANSFORMATION_UBOS_1024,
                             ShaderUniformVariable::PACKED_TEXTURE_BOUNDING_BOXES, 1);

    FPSCamera camera(glm::vec3(0, 0, 3), 50, SCREEN_WIDTH, SCREEN_HEIGHT, 90, 0.1, 50);
    std::function<void(unsigned int)> char_callback = [](unsigned int _) {};
    std::function<void(int, int, int, int)> key_callback = [](int _, int _1, int _2, int _3) {};
    std::function<void(double, double)> mouse_pos_callback = wrap_member_function(camera, &FPSCamera::mouse_callback);
    std::function<void(int, int, int)> mouse_button_callback = [](int _, int _1, int _2) {};
    GLFWLambdaCallbackManager glcm(window, char_callback, key_callback, mouse_pos_callback, mouse_button_callback);

    std::vector<draw_info::IVPTextured> lighter = model_loading::parse_model_into_ivpts(
        "assets/models/lighter.obj",
        batcher.texture_packer_cwl_v_transformation_ubos_1024_shader_batcher.object_id_generator, false);
    std::vector<draw_info::IVPTexturePacked> packed_lighter =
        texture_packer_model_loading::convert_ivpt_to_ivptp(lighter, texture_packer);

    AnimatedTextureAtlas animated_texture_atlas("assets/spritesheets/flame.json", "assets/spritesheets/flame.png", 30.0,
                                                true, texture_packer);

    auto flame_st = texture_packer.get_packed_texture_sub_texture("assets/spritesheets/flame.png");
    float flame_height = 4;
    float flame_width = 1.7;
    Transform flame;
    flame.position = glm::vec3(0, flame_height / 2.0, 0);
    auto dummy = vertex_geometry::generate_rectangle_texture_coordinates();
    draw_info::IVPTexturePacked packed_flame(
        vertex_geometry::generate_rectangle_indices(),
        vertex_geometry::generate_rectangle_vertices(0, 0, flame_width, flame_height), dummy, dummy,
        flame_st.packed_texture_index, flame_st.packed_texture_bounding_box_index, "",
        batcher.texture_packer_cwl_v_transformation_ubos_1024_shader_batcher.object_id_generator.get_id());

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glfwSwapInterval(0);

    int width, height;

    double previous_time = glfwGetTime();

    // temporary
    int animated_flame_ltw_mat_index = 1;

    std::vector<glm::vec2> packed_tex_coords_last_tick;
    int curr_obj_id = 0;
    while (!glfwWindowShouldClose(window)) {
        double current_time = glfwGetTime();
        double delta_time = current_time - previous_time;
        previous_time = current_time;

        glfwGetFramebufferSize(window, &width, &height);

        glViewport(0, 0, width, height);

        glClearColor(0.1f, 0.2f, 0.4f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        camera.process_input(window, delta_time);

        glm::mat4 projection = camera.get_projection_matrix();
        glm::mat4 view = camera.get_view_matrix();

        for (auto &ivptp : packed_lighter) {
            std::vector<unsigned int> ltw_indices(ivptp.xyz_positions.size(), 0);
            std::vector<int> ptis(ivptp.xyz_positions.size(), ivptp.packed_texture_index);
            std::vector<int> ptbbis(ivptp.xyz_positions.size(), ivptp.packed_texture_bounding_box_index);
            batcher.texture_packer_cwl_v_transformation_ubos_1024_shader_batcher.queue_draw(
                ivptp.id, ivptp.indices, ltw_indices, ptis, ivptp.packed_texture_coordinates, ptbbis,
                ivptp.xyz_positions);
        }

        shader_cache.set_uniform(ShaderType::TEXTURE_PACKER_CWL_V_TRANSFORMATION_UBOS_1024,
                                 ShaderUniformVariable::CAMERA_TO_CLIP, projection);
        shader_cache.set_uniform(ShaderType::TEXTURE_PACKER_CWL_V_TRANSFORMATION_UBOS_1024,
                                 ShaderUniformVariable::WORLD_TO_CAMERA, view);

        // use animated texture atlas
        // --------------------------
        // TODO: was going to compare with the observatory to figure out why its upside down...
        double ms_curr_time = glfwGetTime() * 1000.0;
        std::vector<glm::vec2> flame_ptcs =
            animated_texture_atlas.get_texture_coordinates_of_current_animation_frame(ms_curr_time);

        bool new_coords = false;
        if (flame_ptcs != packed_tex_coords_last_tick) {
            new_coords = true;
            curr_obj_id += 1;
        }
        packed_tex_coords_last_tick = flame_ptcs;

        glm::mat4 billboard_transform = compute_transform_to_rotate_basis_to_new_basis(
            camera.transform.compute_right_vector(), glm::vec3(0, 1, 0), camera.transform.compute_forward_vector());

        batcher.texture_packer_cwl_v_transformation_ubos_1024_shader_batcher
            .ltw_matrices[animated_flame_ltw_mat_index] = flame.get_transform_matrix() * billboard_transform;

        std::vector<unsigned int> flame_ltw_mat_idxs(4, animated_flame_ltw_mat_index);
        std::vector<int> flame_ptis(4, packed_flame.packed_texture_index);
        std::vector<int> flame_ptbbis(4, packed_flame.packed_texture_bounding_box_index);

        // have to replace to put in the new texture coords
        batcher.texture_packer_cwl_v_transformation_ubos_1024_shader_batcher.queue_draw(
            packed_flame.id, packed_flame.indices, flame_ltw_mat_idxs, flame_ptis, flame_ptcs, flame_ptbbis,
            packed_flame.xyz_positions, new_coords);

        batcher.texture_packer_cwl_v_transformation_ubos_1024_shader_batcher.upload_ltw_matrices();
        batcher.texture_packer_cwl_v_transformation_ubos_1024_shader_batcher.draw_everything();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);

    glfwTerminate();
    exit(EXIT_SUCCESS);
}
