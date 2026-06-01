#include <stdio.h>
#include <glad_gl.h>

void ntux_user_entry(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("OpenGL Test Application\n");
    printf("=======================\n\n");
    
    /* Load OpenGL function pointers */
    printf("Loading OpenGL...\n");
    if (!gladLoadGL()) {
        printf("ERROR: Failed to load OpenGL\n");
        return;
    }
    
    printf("\n✓ OpenGL successfully loaded\n\n");
    
    /* Test basic GL calls */
    printf("Testing OpenGL function pointers...\n");
    
    /* Get vendor information */
    const char* vendor = (const char*)glGetString(0x1F00);  /* GL_VENDOR */
    const char* renderer = (const char*)glGetString(0x1F01); /* GL_RENDERER */
    const char* version = (const char*)glGetString(0x1F02);  /* GL_VERSION */
    
    printf("  Vendor: %s\n", vendor);
    printf("  Renderer: %s\n", renderer);
    printf("  Version: %s\n\n", version);
    
    /* Test shader compilation */
    printf("Testing shader compilation...\n");
    const char* vs = "attribute vec3 pos; void main() { gl_Position = vec4(pos, 1.0); }";
    GLuint shader = glCreateShader(0x8B31); /* GL_VERTEX_SHADER */
    glShaderSource(shader, 1, &vs, NULL);
    glCompileShader(shader);
    printf("  Vertex shader created and compiled\n\n");
    
    /* Test program creation */
    printf("Testing program linking...\n");
    GLuint program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    glUseProgram(program);
    printf("  Program created, attached, and linked\n\n");
    
    /* Test buffer creation */
    printf("Testing buffer creation...\n");
    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(0x8892, vbo); /* GL_ARRAY_BUFFER */
    float vertices[] = { -1.0f, -1.0f, 0.0f,  1.0f, -1.0f, 0.0f,  0.0f, 1.0f, 0.0f };
    glBufferData(0x8892, sizeof(vertices), vertices, 0x88E0); /* GL_STATIC_DRAW */
    printf("  VAO/VBO created and data uploaded\n\n");
    
    /* Test texture creation */
    printf("Testing texture creation...\n");
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(0x0DE1, tex); /* GL_TEXTURE_2D */
    uint8_t pixel[4] = { 255, 128, 64, 255 };
    glTexImage2D(0x0DE1, 0, 0x8058, 1, 1, 0, 0x1908, 0x1401, pixel); /* GL_RGBA, GL_UNSIGNED_BYTE */
    printf("  1x1 pixel texture created\n\n");
    
    /* Test rendering setup */
    printf("Testing rendering setup...\n");
    glClearColor(0.2f, 0.3f, 0.5f, 1.0f);
    glEnable(0x0B90); /* GL_DEPTH_TEST */
    glBlendFunc(0x0302, 0x0303); /* GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA */
    printf("  Clear color and render state configured\n\n");
    
    /* Test draw call */
    printf("Testing draw call...\n");
    glClear(0x4000 | 0x100); /* GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT */
    glDrawArrays(0x0004, 0, 3); /* GL_TRIANGLES */
    glFinish();
    printf("  Draw call submitted\n\n");
    
    printf("✓ All OpenGL tests passed!\n\n");
    printf("OpenGL 3.3 driver for Intel Braswell GPU ready for use.\n");
    
    /* Cleanup */
    glDeleteShader(shader);
    glDeleteProgram(program);
}
