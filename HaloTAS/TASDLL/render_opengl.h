#pragma once

#include <GL/gl3w.h>

#include <dwmapi.h>
#include <cstdio>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>

void glfw_error_callback(int error, const char* description);

GLuint LoadShaders(std::string vertex_file_path, std::string fragment_file_path);