#include "app/AppState.h"
#include "ui/Ui.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <cstdio>

namespace {

GLFWwindow* g_window = nullptr;

void glfwErrorCb(int err, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", err, desc);
}

void wakeFn() {
    if (g_window) glfwPostEmptyEvent();
}

}

int main(int /*argc*/, char** /*argv*/) {
    glfwSetErrorCallback(glfwErrorCb);
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    g_window = glfwCreateWindow(1400, 900, "Plotter Power Tools", nullptr, nullptr);
    if (!g_window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(g_window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    serialpowertools::AppState app;
    app.setWakeFn(&wakeFn);
    app.pushLog("Plotter Power Tools — ready.");

    bool firstFrame = true;
    while (!glfwWindowShouldClose(g_window)) {
        glfwWaitEventsTimeout(1.0 / 60.0);

        // ESC closes (parity with the Qt shortcut).
        if (glfwGetKey(g_window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(g_window, GLFW_TRUE);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(
            0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
        if (firstFrame) {
            serialpowertools::ui::applyDefaultLayout(static_cast<unsigned>(dockspaceId));
            firstFrame = false;
        }

        serialpowertools::ui::drawControls(app);
        serialpowertools::ui::drawOutput(app);

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(g_window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(g_window);
    }

    app.disconnect();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(g_window);
    glfwTerminate();
    return 0;
}
