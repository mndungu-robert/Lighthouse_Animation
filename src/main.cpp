#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cmath>

// --- Interactive Camera & Presentation Controls ---
float camYaw = 28.0f;
float camPitch = 14.0f;
float camDistance = 17.0f;

bool wireframeMode = false;
bool lightingEnabled = true;
bool wKeyLast = false;
bool lKeyLast = false;

struct Mesh
{
    unsigned int VAO = 0, VBO = 0, EBO = 0;
    unsigned int indexCount = 0;
};

// --- Helper: Read Shader File ---
std::string readShaderFile(const std::string &filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
        file.open("../" + filename);
    if (!file.is_open())
    {
        std::cerr << "ERROR: Could not open shader file: " << filename << std::endl;
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// --- Helper: Compile & Link Shader Program ---
unsigned int createShaderProgram(const std::string &vertPath, const std::string &fragPath)
{
    std::string vertCode = readShaderFile(vertPath);
    std::string fragCode = readShaderFile(fragPath);
    const char *vShaderCode = vertCode.c_str();
    const char *fShaderCode = fragCode.c_str();

    unsigned int vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vShaderCode, NULL);
    glCompileShader(vertex);

    int success;
    char infoLog[512];
    glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(vertex, 512, NULL, infoLog);
        std::cerr << "VERTEX SHADER ERROR (" << vertPath << "):\n"
                  << infoLog << std::endl;
    }

    unsigned int fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fShaderCode, NULL);
    glCompileShader(fragment);
    glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(fragment, 512, NULL, infoLog);
        std::cerr << "FRAGMENT SHADER ERROR (" << fragPath << "):\n"
                  << infoLog << std::endl;
    }

    unsigned int program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success)
    {
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        std::cerr << "SHADER LINK ERROR:\n"
                  << infoLog << std::endl;
    }

    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

// --- Helper: Upload Vertex Data (Pos[3], Color[3], Normal[3] = 9 floats per vertex) ---
Mesh uploadMesh(const std::vector<float> &vertices, const std::vector<unsigned int> &indices)
{
    Mesh m;
    m.indexCount = static_cast<unsigned int>(indices.size());

    glGenVertexArrays(1, &m.VAO);
    glGenBuffers(1, &m.VBO);
    glGenBuffers(1, &m.EBO);

    glBindVertexArray(m.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    // Position (location = 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    // Color (location = 1)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // Normal (location = 2)
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void *)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
    return m;
}

// --- Skybox Cube (for Sunset Gradient) ---
Mesh createSkyCube()
{
    float s = 1.0f;
    std::vector<float> v = {
        -s, -s, -s, 0, 0, 0, 0, 0, 0, s, -s, -s, 0, 0, 0, 0, 0, 0, s, s, -s, 0, 0, 0, 0, 0, 0, -s, s, -s, 0, 0, 0, 0, 0, 0,
        -s, -s, s, 0, 0, 0, 0, 0, 0, s, -s, s, 0, 0, 0, 0, 0, 0, s, s, s, 0, 0, 0, 0, 0, 0, -s, s, s, 0, 0, 0, 0, 0, 0};
    std::vector<unsigned int> idx = {
        0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4, 0, 4, 7, 7, 3, 0,
        1, 5, 6, 6, 2, 1, 3, 2, 6, 6, 7, 3, 0, 1, 5, 5, 4, 0};
    return uploadMesh(v, idx);
}

// --- Ocean Grid (Subdivided for Smooth Waves) ---
Mesh createOceanMesh(float size, int divisions)
{
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    float step = size / divisions;
    float half = size * 0.5f;

    for (int z = 0; z <= divisions; ++z)
    {
        for (int x = 0; x <= divisions; ++x)
        {
            float px = -half + x * step;
            float pz = -half + z * step;
            vertices.insert(vertices.end(), {px, 0.0f, pz, 0.05f, 0.25f, 0.45f, 0.0f, 1.0f, 0.0f});
        }
    }

    for (int z = 0; z < divisions; ++z)
    {
        for (int x = 0; x < divisions; ++x)
        {
            unsigned int r1 = z * (divisions + 1);
            unsigned int r2 = (z + 1) * (divisions + 1);
            indices.insert(indices.end(), {r1 + x, r2 + x, r1 + x + 1, r1 + x + 1, r2 + x, r2 + x + 1});
        }
    }
    return uploadMesh(vertices, indices);
}

// --- Island Mesh with Analytical Surface Normals ---
float getIslandHeight(float x, float z, float radius)
{
    float r = sqrt(x * x + z * z);
    float t = glm::clamp(r / radius, 0.0f, 1.0f);
    float baseHeight = 1.65f * cos(t * glm::half_pi<float>());
    float bump = 0.16f * sin(x * 1.7f) * cos(z * 1.7f) * (1.0f - t);
    return baseHeight + bump - 0.12f;
}

Mesh createIslandMesh(float radius, int rings, int sectors)
{
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    float eps = 0.05f;

    for (int r = 0; r <= rings; ++r)
    {
        float t = static_cast<float>(r) / rings;
        float curRadius = t * radius;

        for (int s = 0; s <= sectors; ++s)
        {
            float theta = (static_cast<float>(s) / sectors) * 2.0f * glm::pi<float>();
            float x = curRadius * cos(theta);
            float z = curRadius * sin(theta);
            float y = getIslandHeight(x, z, radius);

            // Finite-difference normal for smooth terrain shading
            float hL = getIslandHeight(x - eps, z, radius);
            float hR = getIslandHeight(x + eps, z, radius);
            float hD = getIslandHeight(x, z - eps, radius);
            float hU = getIslandHeight(x, z + eps, radius);
            glm::vec3 normal = glm::normalize(glm::vec3(hL - hR, 2.0f * eps, hD - hU));

            glm::vec3 sandColor(0.62f, 0.52f, 0.36f);
            glm::vec3 grassColor(0.18f, 0.36f, 0.16f);
            glm::vec3 col = glm::mix(grassColor, sandColor, glm::clamp(t * 1.25f, 0.0f, 1.0f));

            vertices.insert(vertices.end(), {x, y, z, col.r, col.g, col.b, normal.x, normal.y, normal.z});
        }
    }

    for (int r = 0; r < rings; ++r)
    {
        for (int s = 0; s < sectors; ++s)
        {
            unsigned int cur = r * (sectors + 1) + s;
            unsigned int next = (r + 1) * (sectors + 1) + s;
            indices.insert(indices.end(), {cur, next, cur + 1, cur + 1, next, next + 1});
        }
    }
    return uploadMesh(vertices, indices);
}

// --- Tapered Cylinder with Outward Normals ---
Mesh createTaperedCylinder(float bottomRadius, float topRadius, float height, int sectors, int stacks, bool striped, glm::vec3 solidColor)
{
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    float slope = (bottomRadius - topRadius) / height;

    for (int i = 0; i <= stacks; ++i)
    {
        float t = static_cast<float>(i) / stacks;
        float y = t * height;
        float r = glm::mix(bottomRadius, topRadius, t);

        glm::vec3 color = solidColor;
        if (striped)
        {
            color = (i % 2 == 0) ? glm::vec3(0.85f, 0.14f, 0.14f) : glm::vec3(0.95f, 0.94f, 0.90f);
        }

        for (int j = 0; j <= sectors; ++j)
        {
            float theta = (static_cast<float>(j) / sectors) * 2.0f * glm::pi<float>();
            float cx = cos(theta);
            float cz = sin(theta);
            glm::vec3 normal = glm::normalize(glm::vec3(cx, slope, cz));

            vertices.insert(vertices.end(), {r * cx, y, r * cz, color.r, color.g, color.b, normal.x, normal.y, normal.z});
        }
    }

    for (int i = 0; i < stacks; ++i)
    {
        for (int j = 0; j < sectors; ++j)
        {
            unsigned int cur = i * (sectors + 1) + j;
            unsigned int next = (i + 1) * (sectors + 1) + j;
            indices.insert(indices.end(), {cur, next, cur + 1, cur + 1, next, next + 1});
        }
    }
    return uploadMesh(vertices, indices);
}

// --- Box Mesh (for Boat Hull & Cabin) ---
Mesh createBoxMesh(float w, float h, float d, glm::vec3 color)
{
    float hw = w * 0.5f, hh = h * 0.5f, hd = d * 0.5f;
    std::vector<float> v = {
        // Front (+Z)
        -hw, -hh, hd, color.r, color.g, color.b, 0, 0, 1, hw, -hh, hd, color.r, color.g, color.b, 0, 0, 1,
        hw, hh, hd, color.r, color.g, color.b, 0, 0, 1, -hw, hh, hd, color.r, color.g, color.b, 0, 0, 1,
        // Back (-Z)
        hw, -hh, -hd, color.r, color.g, color.b, 0, 0, -1, -hw, -hh, -hd, color.r, color.g, color.b, 0, 0, -1,
        -hw, hh, -hd, color.r, color.g, color.b, 0, 0, -1, hw, hh, -hd, color.r, color.g, color.b, 0, 0, -1,
        // Left (-X)
        -hw, -hh, -hd, color.r, color.g, color.b, -1, 0, 0, -hw, -hh, hd, color.r, color.g, color.b, -1, 0, 0,
        -hw, hh, hd, color.r, color.g, color.b, -1, 0, 0, -hw, hh, -hd, color.r, color.g, color.b, -1, 0, 0,
        // Right (+X)
        hw, -hh, hd, color.r, color.g, color.b, 1, 0, 0, hw, -hh, -hd, color.r, color.g, color.b, 1, 0, 0,
        hw, hh, -hd, color.r, color.g, color.b, 1, 0, 0, hw, hh, hd, color.r, color.g, color.b, 1, 0, 0,
        // Top (+Y)
        -hw, hh, hd, color.r, color.g, color.b, 0, 1, 0, hw, hh, hd, color.r, color.g, color.b, 0, 1, 0,
        hw, hh, -hd, color.r, color.g, color.b, 0, 1, 0, -hw, hh, -hd, color.r, color.g, color.b, 0, 1, 0,

            // Bottom (-Y)
            - hw,
        -hh, -hd, color.r, color.g, color.b, 0, -1, 0, hw, -hh, -hd, color.r, color.g, color.b, 0, -1, 0,
        hw, -hh, hd, color.r, color.g, color.b, 0, -1, 0, -hw, -hh, hd, color.r, color.g, color.b, 0, -1, 0};
    std::vector<unsigned int> idx;
    for (unsigned int f = 0; f < 6; ++f)
    {
        unsigned int b = f * 4;
        idx.insert(idx.end(), {b, b + 1, b + 2, b + 2, b + 3, b});
    }
    return uploadMesh(v, idx);
}
// --- UV Sphere (for Person's Head) ---
Mesh createSphereMesh(float radius, int stacks, int sectors, glm::vec3 color)
{
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    for (int i = 0; i <= stacks; ++i)
    {
        float phi = glm::pi<float>() * static_cast<float>(i) / stacks;
        for (int j = 0; j <= sectors; ++j)
        {
            float theta = 2.0f * glm::pi<float>() * static_cast<float>(j) / sectors;
            float x = sin(phi) * cos(theta);
            float y = cos(phi);
            float z = sin(phi) * sin(theta);
            vertices.insert(vertices.end(), {radius * x, radius * y, radius * z, color.r, color.g, color.b, x, y, z});
        }
    }
    for (int i = 0; i < stacks; ++i)
    {
        for (int j = 0; j < sectors; ++j)
        {
            unsigned int cur = i * (sectors + 1) + j;
            unsigned int next = (i + 1) * (sectors + 1) + j;
            indices.insert(indices.end(), {cur, next, cur + 1, cur + 1, next, next + 1});
        }
    }
    return uploadMesh(vertices, indices);
}
// --- Keyboard Controls for Live Presentation ---
void processInput(GLFWwindow *window, float deltaTime)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
    float rotSpeed = 55.0f * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
        camYaw -= rotSpeed;
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
        camYaw += rotSpeed;
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
        camPitch = glm::min(camPitch + rotSpeed, 80.0f);
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
        camPitch = glm::max(camPitch - rotSpeed, 4.0f);
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
        camDistance = glm::max(camDistance - 10.0f * deltaTime, 6.0f);
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
        camDistance = glm::min(camDistance + 10.0f * deltaTime, 42.0f);
    // Toggle Wireframe (W)
    bool wNow = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
    if (wNow && !wKeyLast)
    {
        wireframeMode = !wireframeMode;
        glPolygonMode(GL_FRONT_AND_BACK, wireframeMode ? GL_LINE : GL_FILL);
    }
    wKeyLast = wNow;
    // Toggle Blinn-Phong Lighting On/Off (L) for Presentation Comparison
    bool lNow = glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS;
    if (lNow && !lKeyLast)
    {
        lightingEnabled = !lightingEnabled;
    }
    lKeyLast = lNow;
}
int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *window = glfwCreateWindow(1100, 750, "Lighthouse - Stage B2 (W=Wireframe, L=Toggle Lighting, Arrows=Orbit)", NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        return -1;
    glEnable(GL_DEPTH_TEST);
    unsigned int mainShader = createShaderProgram("shaders/basic.vert", "shaders/basic.frag");
    unsigned int skyShader = createShaderProgram("shaders/sky.vert", "shaders/sky.frag");
    // Create Meshes
    Mesh skyCube = createSkyCube();
    Mesh ocean = createOceanMesh(110.0f, 120);
    Mesh island = createIslandMesh(5.5f, 20, 40);
    Mesh tower = createTaperedCylinder(1.1f, 0.75f, 5.5f, 28, 6, true, glm::vec3(1.0f));
    Mesh gallery = createTaperedCylinder(1.05f, 1.05f, 0.25f, 28, 1, false, glm::vec3(0.18f, 0.18f, 0.22f));
    Mesh lantern = createTaperedCylinder(0.65f, 0.65f, 1.0f, 28, 1, false, glm::vec3(1.0f, 0.85f, 0.35f));
    Mesh roof = createTaperedCylinder(0.85f, 0.0f, 0.9f, 28, 1, false, glm::vec3(0.75f, 0.12f, 0.12f));
    // Boat & Person Hierarchy Meshes
    Mesh boatHull = createBoxMesh(1.4f, 0.45f, 2.8f, glm::vec3(0.45f, 0.24f, 0.10f));
    Mesh boatBow = createTaperedCylinder(0.7f, 0.0f, 1.1f, 4, 1, false, glm::vec3(0.45f, 0.24f, 0.10f));
    Mesh personBody = createTaperedCylinder(0.16f, 0.13f, 0.65f, 16, 1, false, glm::vec3(0.85f, 0.35f, 0.15f));
    Mesh personHead = createSphereMesh(0.14f, 12, 16, glm::vec3(0.92f, 0.75f, 0.60f));
    // Golden-Hour Sun Parameters
    glm::vec3 sunDir = glm::normalize(glm::vec3(-0.65f, 0.22f, -0.72f));
    glm::vec3 sunColor = glm::vec3(1.0f, 0.58f, 0.22f);
    glm::vec3 fogColor = glm::vec3(0.88f, 0.45f, 0.18f);
    float lastFrame = 0.0f;
    while (!glfwWindowShouldClose(window))
    {
        float time = static_cast<float>(glfwGetTime());
        float deltaTime = time - lastFrame;
        lastFrame = time;
        processInput(window, deltaTime);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        float aspect = (height > 0) ? static_cast<float>(width) / height : 1.0f;
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 150.0f);
        float radYaw = glm::radians(camYaw);
        float radPitch = glm::radians(camPitch);
        glm::vec3 camPos(
            camDistance * cos(radPitch) * sin(radYaw),
            camDistance * sin(radPitch),
            camDistance * cos(radPitch) * cos(radYaw));
        glm::mat4 view = glm::lookAt(camPos, glm::vec3(0.0f, 2.5f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        // 1. Draw Sky Gradient Background First
        glDepthFunc(GL_LEQUAL);
        glUseProgram(skyShader);
        glUniformMatrix4fv(glGetUniformLocation(skyShader, "uView"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(skyShader, "uProjection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3fv(glGetUniformLocation(skyShader, "uSunDir"), 1, glm::value_ptr(sunDir));
        glBindVertexArray(skyCube.VAO);
        glDrawElements(GL_TRIANGLES, skyCube.indexCount, GL_UNSIGNED_INT, 0);
        glDepthFunc(GL_LESS);
        // 2. Draw Lit 3D Scene
        glUseProgram(mainShader);
        glUniformMatrix4fv(glGetUniformLocation(mainShader, "uView"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(mainShader, "uProjection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3fv(glGetUniformLocation(mainShader, "uViewPos"), 1, glm::value_ptr(camPos));
        glUniform3fv(glGetUniformLocation(mainShader, "uSunDir"), 1, glm::value_ptr(sunDir));
        glUniform3fv(glGetUniformLocation(mainShader, "uSunColor"), 1, glm::value_ptr(sunColor));
        glUniform3fv(glGetUniformLocation(mainShader, "uFogColor"), 1, glm::value_ptr(fogColor));
        glUniform1f(glGetUniformLocation(mainShader, "uTime"), time);
        glUniform1i(glGetUniformLocation(mainShader, "uLightingEnabled"), lightingEnabled ? 1 : 0);
        int modelLoc = glGetUniformLocation(mainShader, "uModel");
        int isWaterLoc = glGetUniformLocation(mainShader, "uIsWater");
        // --- Ocean (with wave displacement & sky reflection) ---
        glUniform1i(isWaterLoc, 1);
        glm::mat4 model = glm::mat4(1.0f);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glBindVertexArray(ocean.VAO);
        glDrawElements(GL_TRIANGLES, ocean.indexCount, GL_UNSIGNED_INT, 0);
        // --- Island & Lighthouse (solid objects) ---
        glUniform1i(isWaterLoc, 0);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glBindVertexArray(island.VAO);
        glDrawElements(GL_TRIANGLES, island.indexCount, GL_UNSIGNED_INT, 0);
        float baseY = 1.4f;
        model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, baseY, 0.0f));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glBindVertexArray(tower.VAO);
        glDrawElements(GL_TRIANGLES, tower.indexCount, GL_UNSIGNED_INT, 0);
        model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, baseY + 5.5f, 0.0f));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glBindVertexArray(gallery.VAO);
        glDrawElements(GL_TRIANGLES, gallery.indexCount, GL_UNSIGNED_INT, 0);
        model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, baseY + 5.75f, 0.0f));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glBindVertexArray(lantern.VAO);
        glDrawElements(GL_TRIANGLES, lantern.indexCount, GL_UNSIGNED_INT, 0);
        model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, baseY + 6.75f, 0.0f));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glBindVertexArray(roof.VAO);
        glDrawElements(GL_TRIANGLES, roof.indexCount, GL_UNSIGNED_INT, 0);
        // --- HIERARCHICAL TRANSFORMATION: Boat (Parent) + Person (Child) ---
        float bobY = sin(time * 2.0f) * 0.08f + 0.12f;
        float pitchRock = sin(time * 1.7f) * 0.06f;
        float rollRock = cos(time * 2.1f) * 0.07f;
        // 1. Parent Matrix: Boat position & wave rocking in world space
        glm::mat4 boatTransform = glm::translate(glm::mat4(1.0f), glm::vec3(6.8f, bobY, 4.2f));
        boatTransform = glm::rotate(boatTransform, glm::radians(-35.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        boatTransform = glm::rotate(boatTransform, pitchRock, glm::vec3(1.0f, 0.0f, 0.0f));
        boatTransform = glm::rotate(boatTransform, rollRock, glm::vec3(0.0f, 0.0f, 1.0f));
        // Draw Boat Hull
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(boatTransform));
        glBindVertexArray(boatHull.VAO);
        glDrawElements(GL_TRIANGLES, boatHull.indexCount, GL_UNSIGNED_INT, 0);
        // Draw Boat Bow (Child of boatTransform)
        glm::mat4 bowTransform = glm::translate(boatTransform, glm::vec3(0.0f, 0.0f, 1.4f));
        bowTransform = glm::rotate(bowTransform, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        bowTransform = glm::rotate(bowTransform, glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(bowTransform));
        glBindVertexArray(boatBow.VAO);
        glDrawElements(GL_TRIANGLES, boatBow.indexCount, GL_UNSIGNED_INT, 0);
        // Draw Person Torso (Child of boatTransform -> inherits all boat motion!)
        glm::mat4 personBodyTransform = glm::translate(boatTransform, glm::vec3(0.0f, 0.22f, -0.2f));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(personBodyTransform));
        glBindVertexArray(personBody.VAO);
        glDrawElements(GL_TRIANGLES, personBody.indexCount, GL_UNSIGNED_INT, 0);
        // Draw Person Head (Grandchild: child of personBodyTransform)
        glm::mat4 personHeadTransform = glm::translate(personBodyTransform, glm::vec3(0.0f, 0.78f, 0.0f));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(personHeadTransform));
        glBindVertexArray(personHead.VAO);
        glDrawElements(GL_TRIANGLES, personHead.indexCount, GL_UNSIGNED_INT, 0);
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    glfwTerminate();
    return 0;
}