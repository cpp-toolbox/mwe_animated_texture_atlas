#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "graphics/animated_texture_atlas/animated_texture_atlas.hpp"
#include "graphics/shader_standard/shader_standard.hpp"
#include "graphics/window/window.hpp"
#include "graphics/shader_cache/shader_cache.hpp"
#include "graphics/batcher/generated/batcher.hpp"
#include "graphics/texture_packer/texture_packer.hpp"
#include "graphics/fps_camera/fps_camera.hpp"
#include "graphics/vertex_geometry/vertex_geometry.hpp"
#include "utility/glfw_lambda_callback_manager/glfw_lambda_callback_manager.hpp"
#include "utility/texture_packer_model_loading/texture_packer_model_loading.hpp"

#define STB_IMAGE_IMPLEMENTATION

#include <stb_image.h>

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

    float flame_height = 4;
    float flame_width = 1.7;
    auto flame_vertices = generate_rectangle_vertices(0, 0, flame_width, flame_height);
    auto flame_indices = generate_rectangle_indices();
    Transform flame;
    flame.position = glm::vec3(0, flame_height / 2.0, 0);

    GLFWwindow *window =
        initialize_glfw_glad_and_return_window(SCREEN_WIDTH, SCREEN_HEIGHT, "glfw window", false, true, false);

    std::vector<ShaderType> requested_shaders = {ShaderType::TEXTURE_PACKER_CWL_V_TRANSFORMATION_UBOS_1024};
    ShaderCache shader_cache(requested_shaders, sinks);
    Batcher batcher(shader_cache);
    TexturePacker texture_packer(
        "assets/packed_textures/packed_texture.json",
        {"assets/packed_textures/packed_texture_0.png", "assets/packed_textures/packed_texture_1.png"});

    FPSCamera camera(glm::vec3(0, 0, 3), 50, SCREEN_WIDTH, SCREEN_HEIGHT, 90, 0.1, 50);
    std::function<void(unsigned int)> char_callback = [](unsigned int _) {};
    std::function<void(int, int, int, int)> key_callback = [](int _, int _1, int _2, int _3) {};
    std::function<void(double, double)> mouse_pos_callback = wrap_member_function(camera, &FPSCamera::mouse_callback);
    std::function<void(int, int, int)> mouse_button_callback = [](int _, int _1, int _2) {};
    GLFWLambdaCallbackManager glcm(window, char_callback, key_callback, mouse_pos_callback, mouse_button_callback);

    std::vector<IVPTextured> lighter = parse_model_into_ivpts("assets/models/lighter.obj", true);
    std::vector<IVPTexturePacked> packed_lighter = convert_ivpt_to_ivptp(lighter, texture_packer);

    AnimatedTextureAtlas animated_texture_atlas("assets/spritesheets/flame.json", "assets/spritesheets/flame.png",
                                                30.0);

    GLuint ltw_matrices_gl_name;
    glm::mat4 ltw_matrices[1024];

    // initialize all matrices to identity matrices
    for (int i = 0; i < 1024; ++i) {
        ltw_matrices[i] = glm::mat4(1.0f);
    }

    glGenBuffers(1, &ltw_matrices_gl_name);
    glBindBuffer(GL_UNIFORM_BUFFER, ltw_matrices_gl_name);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(ltw_matrices), ltw_matrices, GL_STATIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ltw_matrices_gl_name);

    std::vector<unsigned int> ltw_mat_idxs(4, 0);

    auto pt_idx = texture_packer.get_packed_texture_index_of_texture("assets/spritesheets/flame.png");
    std::vector<int> pt_idxs(4, pt_idx); // 4 because square

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

        unsigned int count = 0;
        for (auto &ivptp : packed_lighter) {
            std::vector<unsigned int> ltw_indices(ivptp.xyz_positions.size(), 0);
            std::vector<int> ptis(ivptp.xyz_positions.size(), ivptp.packed_texture_index);
            batcher.texture_packer_cwl_v_transformation_ubos_1024_shader_batcher.queue_draw(
                count, ivptp.indices, ivptp.xyz_positions, ltw_indices, ptis, ivptp.packed_texture_coordinates);
            count++;
        }

        shader_cache.set_uniform(ShaderType::TEXTURE_PACKER_CWL_V_TRANSFORMATION_UBOS_1024,
                                 ShaderUniformVariable::CAMERA_TO_CLIP, projection);
        shader_cache.set_uniform(ShaderType::TEXTURE_PACKER_CWL_V_TRANSFORMATION_UBOS_1024,
                                 ShaderUniformVariable::WORLD_TO_CAMERA, view);

        // use animated texture atlas
        // --------------------------
        double ms_curr_time = glfwGetTime() * 1000.0;
        std::vector<glm::vec2> atlas_texture_coordinates =
            animated_texture_atlas.get_texture_coordinates_of_sprite(ms_curr_time);
        auto packed_tex_coords =
            texture_packer.get_packed_texture_coordinates("assets/spritesheets/flame.png", atlas_texture_coordinates);
        bool new_coords = false;
        if (packed_tex_coords != packed_tex_coords_last_tick) {
            new_coords = true;
            curr_obj_id += 1;
        }
        packed_tex_coords_last_tick = packed_tex_coords;

        glm::mat4 billboard_transform = compute_transform_to_rotate_basis_to_new_basis(
            camera.transform.compute_right_vector(), glm::vec3(0, 1, 0), camera.transform.compute_forward_vector());

        ltw_matrices[animated_flame_ltw_mat_index] = flame.get_transform_matrix() * billboard_transform;

        // load in the matrices
        glBindBuffer(GL_UNIFORM_BUFFER, ltw_matrices_gl_name);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ltw_matrices), ltw_matrices);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        // --------------------------
        //
        //
        std::vector<unsigned int> flame_ltw_mat_idxs(4, 1);

        batcher.texture_packer_cwl_v_transformation_ubos_1024_shader_batcher.queue_draw(
            count + curr_obj_id, flame_indices, flame_vertices, flame_ltw_mat_idxs, pt_idxs, packed_tex_coords);

        batcher.texture_packer_cwl_v_transformation_ubos_1024_shader_batcher.draw_everything();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);

    glfwTerminate();
    exit(EXIT_SUCCESS);
}
