#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#define _TO_STR(x) #x
#define TO_STR(x) _TO_STR(x)

#define MAX_POINTS 20
#define POINT_RADIUS 20

#define ZOOM_COEFF 1.1

#define DEFAULT_WINDOW_WIDTH  640
#define DEFAULT_WINDOW_HEIGHT 480

#define TRANSFORM_MATRIX_OFFSET 0
#define WINDOW_SIZE_OFFSET      48
#define SELECTED_POINT_OFFSET   56
#define POINTS_COUNT_OFFSET     60
#define POINTS_ARRAY_OFFSET     64

const char* VEX_SHADER_SRC =
  "#version 330 core\n"
  "layout(location = 0) in vec2 vex;"
  "void main() {"
    "gl_Position = vec4(vex.xy, 0, 1);"
  "}";

const char* FRAG_SHADER_SRC =
  "#version 330 core\n"
  "#define MAX_ITERS 100\n"
  "#define EPSILON_SQ 0.0001\n"
  "layout(std140) uniform SharedState {"
    "mat3 transform_mat;"                      // 0
    "vec2 window_size;"                        // 48
    "int selected_point;"                      // 56
    "int points_count;"                        // 60
    "vec2 points_arr[" TO_STR(MAX_POINTS) "];" // 64
  "};"
  "void main() {"
    "float radius_sq = " TO_STR(POINT_RADIUS) " * " TO_STR(POINT_RADIUS) " * abs(determinant(transform_mat));"
    "vec2 frag_coord = (transform_mat * vec3(gl_FragCoord.xy - window_size / 2.0, 1)).xy;"
    "for (int i = 0; i < points_count; ++i) {"
      "vec2 frag_dist = frag_coord - points_arr[i];"
      "if (dot(frag_dist, frag_dist) <= radius_sq) {"
        "gl_FragColor = (i == selected_point) ? vec4(0.8, 0.8, 0.8, 1) : vec4(1, 1, 1, 1);"
        "return;"
      "}"
    "}"
    "for (int i = 0; i < MAX_ITERS; ++i) {"
      "vec2 reciprocal_sum = vec2(0, 0);"
      "for (int j = 0; j < points_count; ++j) {"
        "vec2 frag_diff = frag_coord - points_arr[j];"
        "float length_sq = dot(frag_diff, frag_diff);"
        "if (length_sq <= EPSILON_SQ) {"
          "gl_FragColor = vec4(0, 0, (j + 1.0) / float(points_count), 1);"
          "return;"
        "}"
        "reciprocal_sum += frag_diff / length_sq;"
      "}"
      "frag_coord -= reciprocal_sum / dot(reciprocal_sum, reciprocal_sum);"
    "}"
    "gl_FragColor = vec4(0, 0, 0, 1);"
  "}";

const float QUAD_VERTICES[] = {
  -1, 1,
  1, 1,
  -1, -1,
  1, -1
};

const float IDENTITY_MATRIX[] = {
  1, 0, 0, 0,
  0, 1, 0, 0,
  0, 0, 1, 0
};

struct State {
  GLFWwindow* window;

  int points_count;
  int selected_point;
  float points[4 * MAX_POINTS];
  float transform_mat[12];
  float prev_mouse_pos[2];
};

void state_init(struct State* state, GLFWwindow* window) {
  memset(state, 0, sizeof(struct State));

  state->window = window;

  state->selected_point = -1;

  memcpy(state->transform_mat, IDENTITY_MATRIX, sizeof(IDENTITY_MATRIX));

  float dims[] = {DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT};

  glBufferData(GL_UNIFORM_BUFFER, POINTS_ARRAY_OFFSET + 4 * sizeof(float) * MAX_POINTS, NULL, GL_DYNAMIC_DRAW);

  glBufferSubData(GL_UNIFORM_BUFFER, TRANSFORM_MATRIX_OFFSET, 12 * sizeof(float), state->transform_mat);
  glBufferSubData(GL_UNIFORM_BUFFER, WINDOW_SIZE_OFFSET, sizeof(dims), dims);
  glBufferSubData(GL_UNIFORM_BUFFER, SELECTED_POINT_OFFSET, 4, &state->selected_point);
  glBufferSubData(GL_UNIFORM_BUFFER, POINTS_COUNT_OFFSET, 4, &state->points_count);
  glBufferSubData(GL_UNIFORM_BUFFER, POINTS_ARRAY_OFFSET, 4 * sizeof(float) * state->points_count, state->points);

  glfwSetWindowUserPointer(window, state);
}

void state_adjust_transform_matrix(struct State* state,
                             float a1, float a2, float a3,
                             float a4, float a5, float a6,
                             float a7, float a8, float a9) {
  float result[12];
  float* t = state->transform_mat;

  //               [ a1 a2 a3 ]
  //               [ a4 a5 a6 ]
  //               [ a7 a8 a9 ]
  // [ t0 t4 t8  ]
  // [ t1 t5 t9  ]
  // [ t2 t6 t10 ]

  result[0] =  t[0] * a1 + t[4] * a4 + t[8]  * a7;
  result[1] =  t[1] * a1 + t[5] * a4 + t[9]  * a7;
  result[2] =  t[2] * a1 + t[6] * a4 + t[10] * a7;
  result[4] =  t[0] * a2 + t[4] * a5 + t[8]  * a8;
  result[5] =  t[1] * a2 + t[5] * a5 + t[9]  * a8;
  result[6] =  t[2] * a2 + t[6] * a5 + t[10] * a8;
  result[8] =  t[0] * a3 + t[4] * a6 + t[8]  * a9;
  result[9] =  t[1] * a3 + t[5] * a6 + t[9]  * a9;
  result[10] = t[2] * a3 + t[6] * a6 + t[10] * a9;

  memcpy(t, result, sizeof(result));

  glBufferSubData(GL_UNIFORM_BUFFER, TRANSFORM_MATRIX_OFFSET, sizeof(result), t);
}

float state_get_transform_matrix_det(struct State* state) {
  float* t = state->transform_mat;

  // [ t0 t4 t8  ]
  // [ t1 t5 t9  ]
  // [ t2 t6 t10 ]

  return
    t[0] * t[5] * t[10] +
    t[4] * t[9] * t[2]  +
    t[8] * t[1] * t[6]  -
    t[8] * t[5] * t[2]  -
    t[4] * t[1] * t[10] -
    t[0] * t[9] * t[6];
}

void state_map_point(struct State* state, float x, float y, float z, float* out) {
  float* t = state->transform_mat;

  //              [ x ]
  //              [ y ]
  //              [ z ]
  // [ t0 t4 t8  ]
  // [ t1 t5 t9  ]
  // [ t2 t6 t10 ]

  out[0] = t[0] * x + t[4] * y + t[8]  * z;
  out[1] = t[1] * x + t[5] * y + t[9]  * z;
  out[2] = t[2] * x + t[6] * y + t[10] * z;
}

int state_push_point(struct State* state) {
  if (state->points_count >= MAX_POINTS)
    return 1;

  state->points[4 * state->points_count] = 0;
  state->points[4 * state->points_count + 1] = 0;

  state->points_count++;

  glBufferSubData(GL_UNIFORM_BUFFER, POINTS_ARRAY_OFFSET, 4 * sizeof(float) * state->points_count, state->points);
  glBufferSubData(GL_UNIFORM_BUFFER, POINTS_COUNT_OFFSET, 4, &state->points_count);

  return 0;
}

int state_pop_point(struct State* state) {
  if (state->points_count <= 0)
    return 1;

  if (state->selected_point >= state->points_count - 1) {
    state->selected_point = -1;
    glBufferSubData(GL_UNIFORM_BUFFER, SELECTED_POINT_OFFSET, 4, &state->selected_point);
  }

  state->points_count--;

  glBufferSubData(GL_UNIFORM_BUFFER, POINTS_COUNT_OFFSET, 4, &state->points_count);

  return 0;
}

void state_move_point(struct State* state, size_t idx, float dx, float dy) {
  float adj_pos[3];
  state_map_point(state, dx, dy, 0, adj_pos);

  state->points[4 * idx] += adj_pos[0];
  state->points[4 * idx + 1] += adj_pos[1];

  glBufferSubData(GL_UNIFORM_BUFFER, POINTS_ARRAY_OFFSET, 4 * sizeof(float) * state->points_count, state->points);
}

void state_recompute_selected_point(struct State* state, float x, float y) {
  int width;
  int height;
  glfwGetFramebufferSize(state->window, &width, &height);

  float adj_pos[3];
  state_map_point(state, x - width / 2.0, height / 2.0 - y, 1, adj_pos);

  state->selected_point = -1;

  float radius_sq = POINT_RADIUS * POINT_RADIUS * fabs(state_get_transform_matrix_det(state));

  for (int i = 0; i < state->points_count; ++i) {
    float point_dx = state->points[4 * i] - adj_pos[0];
    float point_dy = state->points[4 * i + 1] - adj_pos[1];

    if (point_dx * point_dx + point_dy * point_dy <= radius_sq) {
      state->selected_point = i;
      break;
    }
  }

  glBufferSubData(GL_UNIFORM_BUFFER, SELECTED_POINT_OFFSET, 4, &state->selected_point);
}

void report_glfw_error(const char* msg) {
  const char* error;
  int error_code = glfwGetError(&error);

  printf("ERROR: %s. Error code: %X\n", msg, error_code);
  printf("ERROR: %s\n", error);
}

GLuint build_shader(GLenum shader_type, const char* src) {
  GLuint shader = glCreateShader(shader_type);
  glShaderSource(shader, 1, &src, NULL);
  glCompileShader(shader);

  GLint compile_status;
  GLint info_log_len;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);

  if (compile_status != GL_TRUE) {
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_log_len);
    char* info_log = malloc(info_log_len);
    glGetShaderInfoLog(shader, info_log_len, NULL, info_log);
    printf("ERROR: Unable to compile shader.\n");
    printf("ERROR: %s", info_log);
    free(info_log);
    glDeleteShader(shader);
    return 0;
  }

  return shader;
}

GLuint build_program(GLuint vertex_shader, GLuint fragment_shader) {
  GLuint program = glCreateProgram();
  glAttachShader(program, vertex_shader);
  glAttachShader(program, fragment_shader);

  glLinkProgram(program);

  GLint link_status;
  GLint info_log_len;
  glGetProgramiv(program, GL_LINK_STATUS, &link_status);

  if (link_status != GL_TRUE) {
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_log_len);
    char* info_log = malloc(info_log_len);
    glGetProgramInfoLog(program, info_log_len, NULL, info_log);
    printf("ERROR: Unable to link shaders.\n");
    printf("ERROR: %s", info_log);
    free(info_log);
    glDeleteProgram(program);
    return 0;
  }

  return program;
}

void on_window_resize(GLFWwindow* window, int width, int height) {
  (void)(window);
  
  glViewport(0, 0, width, height);

  float dims[] = {width, height};
  glBufferSubData(GL_UNIFORM_BUFFER, WINDOW_SIZE_OFFSET, sizeof(dims), dims);
}

void on_keyboard_input(GLFWwindow* window, int key, int scancode, int action, int mods) {
  (void)(scancode);
  (void)(mods);

  if (action == GLFW_PRESS) {
    struct State* state = glfwGetWindowUserPointer(window);

    switch (key) {
    case GLFW_KEY_EQUAL:
      state_push_point(state);
      break;
    case GLFW_KEY_MINUS:
      state_pop_point(state);
      break;
    }
  }
}

void on_mouse_move(GLFWwindow* window, double x, double y) {
  struct State* state = glfwGetWindowUserPointer(window);

  if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
    float dx = state->prev_mouse_pos[0] - x;
    float dy = y - state->prev_mouse_pos[1];

    if (state->selected_point != -1) {
      state_move_point(state, (size_t) state->selected_point, -dx, -dy);
    } else {
      state_adjust_transform_matrix(state,
                                    1, 0, dx,
                                    0, 1, dy,
                                    0, 0, 1);
    }
  } else {
    state_recompute_selected_point(state, x, y);
  }

  state->prev_mouse_pos[0] = x;
  state->prev_mouse_pos[1] = y;
}

void on_mouse_scroll(GLFWwindow* window, double x, double y) {
  (void)(x);

  struct State* state = glfwGetWindowUserPointer(window);

  float k = pow(ZOOM_COEFF, y);

  state_adjust_transform_matrix(state,
                                k, 0, 0,
                                0, k, 0,
                                0, 0, 1);
}

int main() {
  if (!glfwInit()) {
    report_glfw_error("Can't initialize GLFW");
    return 1;
  }

  glfwDefaultWindowHints();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  GLFWwindow* window = glfwCreateWindow(DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT, "NFractal", NULL, NULL);

  if (window == NULL) {
    report_glfw_error("Can't create window");
    glfwTerminate();
    return 2;
  }

  glfwMakeContextCurrent(window);

  if (!gladLoadGL(glfwGetProcAddress)) {
    printf("ERROR: Can't load OpenGL libraries.\n");
    glfwTerminate();
    return 3;
  }

  GLuint vex_shader = build_shader(GL_VERTEX_SHADER, VEX_SHADER_SRC);
  GLuint frag_shader = build_shader(GL_FRAGMENT_SHADER, FRAG_SHADER_SRC);

  if (vex_shader == 0 || frag_shader == 0) {
    glDeleteShader(vex_shader);
    glDeleteShader(frag_shader);
    glfwTerminate();
    return 4;
  }

  GLuint program = build_program(vex_shader, frag_shader);

  if (program == 0) {
    glDeleteShader(vex_shader);
    glDeleteShader(frag_shader);
    glfwTerminate();
    return 4;
  }

  GLuint vex_buffer;
  GLuint vex_array;
  glGenBuffers(1, &vex_buffer);
  glGenVertexArrays(1, &vex_array);

  glBindVertexArray(vex_array);
  glBindBuffer(GL_ARRAY_BUFFER, vex_buffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD_VERTICES), QUAD_VERTICES, GL_STATIC_DRAW);

  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
  glEnableVertexAttribArray(0);

  glUseProgram(program);

  GLuint uniform_block = glGetUniformBlockIndex(program, "SharedState");
  glUniformBlockBinding(program, uniform_block, 1);

  GLuint uniform_buffer;
  glGenBuffers(1, &uniform_buffer);
  glBindBufferBase(GL_UNIFORM_BUFFER, 1, uniform_buffer);

  struct State state;
  state_init(&state, window);

  glfwSetFramebufferSizeCallback(window, on_window_resize);
  glfwSetKeyCallback(window, on_keyboard_input);
  glfwSetCursorPosCallback(window, on_mouse_move);
  glfwSetScrollCallback(window, on_mouse_scroll);

  glfwSwapInterval(1);

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glfwSwapBuffers(window);
  }

  glDeleteShader(vex_shader);
  glDeleteShader(frag_shader);
  glDeleteProgram(program);

  glDeleteVertexArrays(1, &vex_array);
  glDeleteBuffers(1, &vex_buffer);
  glDeleteBuffers(1, &uniform_buffer);

  glfwTerminate();
  return 0;
}
